#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <ucontext.h>

#include "xed/xed-interface.h"

#include "dsm_sync.h"
#include "dsm_msg.h"
#include "dsm_util.h"
#include "dsm_inet.h"

/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/


// Length of the UD2 instruction for isa: x86-64.
#define UD2_SIZE	2


/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/


// Intel XED machine state.
xed_state_t xed_machine_state;

// Instruction buffer.
unsigned char inst_buf[UD2_SIZE];

// UD2 instruction opcodes for isa: x86-64.
unsigned char ud2_opcodes[UD2_SIZE] = {0x0f, 0x0b};

// Pointer to memory address at which fault occurred. 
void *fault_addr;


/*
 *******************************************************************************
 *                        Private Function Definitions                         *
 *******************************************************************************
*/


// Performs ILD on given address for decoder state. Returns instruction length.
static xed_uint_t getInstLength (void *addr, xed_state_t *decoderState) {
	static xed_decoded_inst_t xedd;
	xed_error_enum_t err;

	// Configure decoder for specified machine state.
	xed_decoded_inst_zero_set_mode(&xedd, decoderState);

	// Perform instruction-length-decoding.
	if ((err = xed_ild_decode(&xedd, addr, XED_MAX_INSTRUCTION_BYTES))
		!= XED_ERROR_NONE) {
		dsm_panic(xed_error_enum_t2str(err));
	}

	// Return length.
	return xed_decoded_inst_get_length(&xedd);
}

// Prepares to write: Messages the arbiter, waits for an acknowledgement.
static void takeAccess (void) {
	dsm_msg msg;

	// Seize the I/O semaphore.
	dsm_down(&(smap->sem_io));

	// Configure message, and send to arbiter.
	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_SYNC_REQ;
	
	printf("[%d] Sent write request!\n", getpid());
	dsm_sendall(sock_arbiter, &msg, sizeof(msg));

	// Wait for acknowledgement.
	dsm_recvall(sock_arbiter, &msg, sizeof(msg));
	printf("[%d] Received go-ahead!\n", getpid());

	// Verify acknowledgement.
	if (msg.type != MSG_WRITE_OKAY) {
		dsm_cpanic("takeAccess", "Unknown message received!");
	}
}

// Releases access: Messages the arbiter, then suspends itself until continued.
static void dropAccess (void) {
	dsm_msg msg;
	
	// Release the I/O semaphore.
	dsm_up(&(smap->sem_io));

	// Configure synchronization information message.
	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_SYNC_INFO;
	msg.payload.sync.offset = fault_addr - ((void *)smap + smap->data_off);
	msg.payload.sync.size = 4;
	memcpy(msg.payload.sync.buf, fault_addr, 4);

	// Send synchronization information to arbiter.
	dsm_sendall(sock_arbiter, &msg, sizeof(msg));
	printf("[%d] Sent sync info!\n", getpid());

	// Schedule a suspend signal
	if (kill(getpid(), SIGTSTP) == -1) {
		dsm_panic("Couldn't suspend process!\n");
	}
}


/*
 *******************************************************************************
 *                         Public Function Definitions                         *
 *******************************************************************************
*/


// Initializes the decoder tables necessary for use in the sync handlers.
void dsm_sync_init (void) {

	// Initialize decoder table.
	xed_tables_init();

	// Setup machine state.
	xed_state_init2(&xed_machine_state, XED_MACHINE_MODE_LONG_64,
		XED_ADDRESS_WIDTH_64b);
}

// Handler: Synchronization action for SIGSEGV.
void dsm_sync_sigsegv (int signal, siginfo_t *info, void *ucontext) {
	ucontext_t *context = (ucontext_t *)ucontext;
	void *prgm_counter = (void *)context->uc_mcontext.gregs[REG_RIP];
	xed_uint_t len;

	// Grab semaphore and request write access.
	takeAccess();

	// Get instruction length.
	len = getInstLength(prgm_counter, &xed_machine_state);

	// Compute start of next instruction.
	void *nextInst = prgm_counter + len;

	// Copy out UD2_SIZE bytes for fault substitution.
	memcpy(inst_buf, nextInst, UD2_SIZE);

	// Assign full access permissions to program text page.
	off_t offset = (uintptr_t)nextInst % (uintptr_t)DSM_PAGESIZE;
	void *pageStart = nextInst - offset;
	dsm_mprotect(pageStart, DSM_PAGESIZE, PROT_READ|PROT_WRITE|PROT_EXEC);

	// Copy in the UD2 instruction.
	memcpy(nextInst, ud2_opcodes, UD2_SIZE);

	// Set fault address.
	fault_addr = info->si_addr;

	// Give protected portion of shared page read-write access.
	void *page = (void *)smap + smap->data_off;
	dsm_mprotect(page, DSM_PAGESIZE, PROT_WRITE);
}

// Handler: Synchronization action for SIGILL.
void dsm_sync_sigill (int signal, siginfo_t *info, void *ucontext) {
	ucontext_t *context = (ucontext_t *)ucontext;
	void *prgm_counter = (void *)context->uc_mcontext.gregs[REG_RIP];

	// Restore origin instruction.
	memcpy(prgm_counter, inst_buf, UD2_SIZE);

	// Protect shared page again.
	void *page = (void *)smap + smap->data_off;
	dsm_mprotect(page, DSM_PAGESIZE, PROT_READ);

	// Release lock and send sychronization information.
	dropAccess();
}

// [DEBUG] Handler: Synchronization action for SIGCONT.
void dsm_sync_sigcont (int signal, siginfo_t *info, void *ucontext) {
	printf("[%d] SIGCONT!\n", getpid());
}

// [DEBUG] Handler: Synchronization action for SIGTSTP.
void dsm_sync_sigtstp (int signal, siginfo_t *info, void *ucontext) {
	printf("[%d] SIGTSTP!\n", getpid());
}
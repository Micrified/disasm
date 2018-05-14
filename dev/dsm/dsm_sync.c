#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include "dsm_util.h"
#include "dsm_table.h"
#include "dsm_sync.h"
#include "dsm_signal.h"
#include "xed/xed-interface.h"


/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/


// Length of the UD2 instruction for the x86-64 isa.
#define UD2_SZ		2


/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/


// Intel XED machine state.
xed_state_t xed_machine_state;

// Instruction buffer.
unsigned char inst_buf[UD2_SZ];

// UD2 instruction opcodes for x86-64 isa.
unsigned char ud2_opc[UD2_SZ] = {0x0f, 0x0b};

// Pointer to the memory address at which a fault occurred.
void *fault_addr;


/*
 *******************************************************************************
 *                        Private Function Definitions                         *
 *******************************************************************************
*/


// Performs ILD on given address with decoder state. Returns instruction length.
static xed_uint_t getInstLength (void *address, xed_state_t *decoderState) {
	static xed_decoded_inst_t xedd;
	xed_error_enum_t err;

	// Configure decoder for specified machine state (inst, address width).
	xed_decoded_inst_zero_set_mode(&xedd, decoderState);

	// Decode.
	if ((err = xed_ild_decode(&xedd, address, XED_MAX_INSTRUCTION_BYTES)) 
		!= XED_ERROR_NONE) {
		dsm_panic(xed_error_enum_t2str(err));
	}

	// Return length.
	return xed_decoded_inst_get_length(&xedd);
}

// Prepares for write: Acquires lock, freezes all processes in group.
static void seizeAccess (void) {

	// Acquire shared object semaphore to ensure atomicity.
	dsm_down(&(shared_obj->sem_lock));

	// Freeze all other processes in the group with SIGTSTP.
	dsm_killpg(SIGTSTP);

}

// Releases after write: Unfreezes all processes, then releases lock.
static void releaseAccess (void) {
	
	// Unfreeze all other processes in the group with SIGCONT
	dsm_killpg(SIGCONT);

	// Release shared object semaphore.
	dsm_up(&(shared_obj->sem_lock));

}

// Copies shared memory from private_obj with shared_obj to update it.
static void updateSharedObject (void) {

	// Compute start of destination address.
	void *dest = shared_obj + shared_obj->data_off;

	// Compute state of origin address.
	void *src = private_obj + private_obj->data_off;

	// Compute size of free data region.
	size_t size = shared_obj->obj_size - shared_obj->data_off;

	// Copy over memory.
	memcpy(dest, src, size);
}

// Copies shared memory from shared_obj to private_obj to update it.
static void updatePrivateObject (void) {

	// Set write access to private page.
	dsm_mprotect(private_obj, PAGESIZE, PROT_WRITE);

	// Compute start of destination address.
	void *dest = private_obj + private_obj->data_off;

	// Compute start of origin address.
	void *src = shared_obj + shared_obj->data_off;

	// Compute size of free data region.
	size_t size = private_obj->obj_size - private_obj->data_off;

	// Copy over memory.
	memcpy(dest, src, size);

	// Set read-only access to private page.
	dsm_mprotect(private_obj, PAGESIZE, PROT_READ);
}


/*
 *******************************************************************************
 *                         Public Function Definitions                         *
 *******************************************************************************
*/


// Initializes the decoder tables necessary for use in the sync handlers.
void dsm_sync_init (void) {

	// Initialize decoder tables.
	xed_tables_init();

	// Setup machine state.
	xed_state_init2(&xed_machine_state, XED_MACHINE_MODE_LONG_64,
		XED_ADDRESS_WIDTH_64b);

	printf("[%d] [%d] Decoder initialized!\n", getpid(), getpgid(0));

}

// Handler: Sychronization action for SIGSEGV.
void dsm_sync_sigsegv (int signal, siginfo_t *info, void *ucontext) {
	ucontext_t *context = (ucontext_t *)ucontext;
	void *prgm_counter = (void *)context->uc_mcontext.gregs[REG_RIP];
	xed_uint_t len;

	printf("[%d] [%d] SIGSEGV!\n", getpid(), getpgid(0));
	
	// Seize write access to shared memory.
	seizeAccess();

	// Get instruction length.
	len = getInstLength(prgm_counter, &xed_machine_state);

	// Compute start of next instruction.
	void *nextInst = prgm_counter + len;

	// Copy out UD2_SZ bytes for substitution of fault.
	memcpy(inst_buf, nextInst, UD2_SZ);

	// Assign read, write, and execute access to process text page.
	off_t offset = (uintptr_t)nextInst % (uintptr_t)PAGESIZE;
	void *pagestart = nextInst - offset;
	dsm_mprotect(pagestart, PAGESIZE, PROT_READ|PROT_WRITE|PROT_EXEC);

	// Copy in UD2 instruction.
	memcpy(nextInst, ud2_opc, UD2_SZ);

	// [CHANGE] Set fault address.
	fault_addr = info->si_addr;

	// Allow writing to protected page.
	dsm_mprotect(private_obj, private_obj->obj_size, PROT_WRITE);

}

// Handler: Sychronization action for SIGILL
void dsm_sync_sigill (int signal, siginfo_t *info, void *ucontext) {
	ucontext_t *context = (ucontext_t *)ucontext;
	void *prgm_counter = (void *)context->uc_mcontext.gregs[REG_RIP];

	printf("[%d] [%d] SIGILL!\n", getpid(), getpgid(0));

	// Sychronize shared pages.
	updateSharedObject();

	// Release object lock and unfreeze other processes.
	releaseAccess();

	// Restore original instruction.
	memcpy(prgm_counter, inst_buf, UD2_SZ);

	// Protect private page.
	dsm_mprotect(private_obj, private_obj->obj_size, PROT_READ);
	
}

// Handler: Sychronization action for SIGCONT.
void dsm_sync_sigcont (int signal, siginfo_t *info, void *ucontext) {
	
	// Copy shared page to private.
	updatePrivateObject();

	printf("[%d] [%d] Updated!\n", getpid(), getpgid(0));

}

// [DEBUG] Handler: Prints on SIGTSTP.
void dsm_sync_sigtstp (int signal, siginfo_t *info, void *ucontext) {
	printf("[%d] [%d] Paused!\n", getpid(), getpgid(0));
}
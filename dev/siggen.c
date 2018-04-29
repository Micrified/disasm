#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
#include "xed/xed-interface.h"

/*
 *******************************************************************************
 *                    Global Variables & Symbolic Constants                    *
 *******************************************************************************
*/

#define READ				PROT_READ
#define WRITE				PROT_WRITE
#define NONE				PROT_NONE

// Page size.
unsigned long pagesize;

// Page pointer.
void *page;

// Machine state. Used with decoder.
xed_state_t machine_state;


/*
 *******************************************************************************
 *                            Function Declarations                            *
 *******************************************************************************
*/


// Installs a handler for the given signal. Exits program on error.
void setAction (int signal, void (*f)(int, siginfo_t *, void *));

// Applies given protections to given memory page. Exits program on error.
void setProtection (void *address, size_t size, int flags);

// Performs ILD on given address with decode state. Returns instruction length. 
unsigned int getInstructionLength (void *address, xed_state_t *decoderState);


/*
 *******************************************************************************
 *                            Function Definitions                             *
 *******************************************************************************
*/


// Installs a handler for the given signal. Exits program on error.
void setAction (int signal, void (*f)(int, siginfo_t *, void *)) {
	struct sigaction sa;
	
	sa.sa_flags = SA_SIGINFO;	// Configure to receive three arguments. 
	sigemptyset(&sa.sa_mask);	// Zero the mask to block no signals in handler.
	sa.sa_sigaction = f;		// Set the handler function. Must be non-NULL.

	// Install action. Verify success.
	if (sigaction(signal, &sa, NULL) == -1) {
		fprintf(stderr, "Error: Sigaction setup failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

// Applies given protections to given memory page. Exits program on error.
void setProtection (void *address, size_t size, int flags) {
	if (mprotect(address, size, flags) == -1) {
		fprintf(stderr, "Error: mprotect failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

// Performs ILD on given address with decode state. Returns instruction length. 
unsigned int getInstructionLength (void *address, xed_state_t *decoderState) {
	static xed_decoded_inst_t xedd;
	xed_error_enum_t err;

	// Configure decoder for specified machine state (isa, address width).
	xed_decoded_inst_zero_set_mode(&xedd, decoderState);

	// Decode.
	if ((err = xed_ild_decode(&xedd, address, XED_MAX_INSTRUCTION_BYTES))
		!= XED_ERROR_NONE) {
		fprintf(stderr, "Error: %s\n", xed_error_enum_t2str(err));
		exit(EXIT_FAILURE);
	}

	// Return length.
	return xed_decoded_inst_get_length(&xedd);
}


/*
 *******************************************************************************
 *                               Signal Handlers                               *
 *******************************************************************************
*/


// Signal Handler.
void handler (int signal, siginfo_t *info, void *ucontext) {
	ucontext_t *context = (ucontext_t *)ucontext;
	void *prgm_counter = (void *)context->uc_mcontext.gregs[REG_RIP];
	unsigned int len;

	// Output context information.
	printf("==================== Signal Handler ====================\n");
	printf("si_addr:\t\t%p\n", info->si_addr);
	printf("program counter:\t%p\n", prgm_counter);

	// Extract and show length of current instruction.
	len = getInstructionLength(prgm_counter, &machine_state);
	printf("\ncurrent instruction:\t%u bytes\n", len);
	
	// Extract and show length of next instruction.
	len = getInstructionLength(prgm_counter + len, &machine_state);
	printf("next instruction:\t%u bytes\n", len);

	printf("=======================================================\n");
	
	// Restore protections.
	setProtection(info->si_addr, pagesize, WRITE);
}


/*
 *******************************************************************************
 *                                    Main                                     *
 *******************************************************************************
*/


int main (void) {
	
	// Initialized decoder tables. 
	xed_tables_init();
	
	// Setup machine state.
	xed_state_init2(&machine_state, XED_MACHINE_MODE_LONG_64, 
		XED_ADDRESS_WIDTH_64b);

	// Set the page size.
	pagesize = sysconf(_SC_PAGE_SIZE);

	// Set the handler.
	setAction(SIGSEGV, handler);

	// Allocate memory.
	if (posix_memalign(&page, pagesize, pagesize) != 0) {
		fprintf(stderr, "Error: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Protect the page.
	setProtection(page, pagesize, READ);

	// Do something bad.
	int *p = (int *)page;
	*p = 0;

	// Free memory.
	free(page);
	
	return 0;
}
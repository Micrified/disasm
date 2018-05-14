#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <semaphore.h>
#include <sys/mman.h>
#include "dsm_util.h"


/*
 *******************************************************************************
 *                            Function Definitions                             *
 *******************************************************************************
*/


// Exits fatally with given error message. Also outputs errno.
void dsm_panic (const char *msg) {
	const char *fmt = "[%d] Fatal Error: \"%s\". Errno: \"%s\"\n";
	fprintf(stderr, fmt, getpid(), msg, strerror(errno));
	exit(EXIT_FAILURE);
}

// Increments a semaphore. Panics on error.
void dsm_up (sem_t *sp) {
	if (sem_post(sp) == -1) {
		dsm_panic("Couldn't \"up\" semaphore!");
	}
}

// Decrements a semaphore. Panics on error.
void dsm_down (sem_t *sp) {
	if (sem_wait(sp) == -1) {
		dsm_panic("Couldn't \"down\" semaphore!");
	}
}

// Returns a semaphore's value. Panics on error.
int dsm_getSemValue (sem_t *sp) {
	int v = -1;
	
	if (sem_getvalue(sp, &v) == -1) {
		dsm_panic("Couldn't get semaphore value!");
	}

	return v;
}

// Sets the given protections to a memory page. Exits fatally on error.
void dsm_mprotect (void *address, size_t size, int flags) {

	// Exit fatally if protection fails.
	if (mprotect(address, size, flags) == -1) {
		dsm_panic("Couldn't protect specified page!");
	}

}

// Allocates a page-aligned slice of memory. Exits fatally on error.
void *dsm_pageAlloc (void *address, size_t size) {
	size_t alignment = sysconf(_SC_PAGE_SIZE);

	if (posix_memalign(&address, alignment, size) == -1) {
		dsm_panic("Couldn't allocate aligned page!");
	}

	return address;
}

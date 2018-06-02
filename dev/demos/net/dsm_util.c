#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <stdarg.h>
#include "dsm_util.h"


/*
 *******************************************************************************
 *                          I/O Function Definitions                           *
 *******************************************************************************
*/


// Exits fatally with given error message. Also outputs errno.
void dsm_panic (const char *msg) {
	const char *fmt = "[%d] Fatal Error: \"%s\". Errno (%d): \"%s\"\n";
	fprintf(stderr, fmt, getpid(), msg, errno, strerror(errno));
	exit(EXIT_FAILURE);
}

// Exits fatally with given error message. Outputs custom errno.
void dsm_cpanic (const char *msg, const char *reason) {
	const char *fmt = "[%d] Fatal Error: \"%s\". Reason: \"%s\"\n";
	fprintf(stderr, fmt, getpid(), msg, reason);
	exit(EXIT_FAILURE);
}

// Exits fatally with formatted error. Supports tokens: {%s, %d, %f, %u}.
void dsm_panicf (const char *fmt, ...) {
	va_list ap;
	const char *p, *sval;
	int ival;
	unsigned int uval;
	double dval;

	// Print error start.
	fprintf(stderr, "[%d] Fatal Error: \"", getpid());

	// Initialize argument list.
	va_start(ap, fmt);

	// Parse format string.
	for (p = fmt; *p; p++) {

		// Ignore non-tokens.
		if (*p != '%') {
			putc(*p, stderr);
			continue;
		}

		switch (*(++p)) {
			case 'd': {
				ival = va_arg(ap, int);
				fprintf(stderr, "%d", ival);
				break;
			}
			case 'f': {
				dval = va_arg(ap, double);
				fprintf(stderr, "%f", dval);
				break;
			}
			case 's': {
				for (sval = va_arg(ap, char *); *sval != '\0'; sval++) {
					putc(*sval, stderr);
				}
				break;
			}
			case 'u': {
				uval = va_arg(ap, unsigned);
				fprintf(stderr, "%u", uval);
			}
		}
	}

	// Print error end.
	fprintf(stderr, "\".\n"); 

	// Clean up.
	va_end(ap);

	// Exit.
	exit(EXIT_FAILURE);
}

// Outputs warning to stderr.
void dsm_warning (const char *msg) {
	const char *fmt = "[%d] Warning: \"%s\".\n";
	fprintf(stderr, fmt, getpid(), msg);
}

/*
 *******************************************************************************
 *                       Semaphore Function Definitions                        *
 *******************************************************************************
*/


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

// Unlinks a named semaphore. Exits fatally on error.
void dsm_unlinkNamedSem (const char *name) {
	if (sem_unlink(name) == -1) {
		dsm_panicf("Couldn't unlink named semaphore: \"%s\"!", name);
	}
}


/*
 *******************************************************************************
 *                         Memory Function Definitions                         *
 *******************************************************************************
*/

// Allocates a zeroed block of memory. Exits fatally on error.
void *dsm_zalloc (size_t size) {
	void *p;

	// Verify allocation success.
	if ((p = malloc(size)) == NULL) {
		dsm_cpanic("dsm_zalloc", "Allocation failed!");
	}

	return memset(p, 0, size);
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

// Unlinks a shared memory file. Exits fatally on error.
void dsm_unlinkSharedFile (const char *name) {
	if (shm_unlink(name) == -1) {
		dsm_panicf("Couldn't unlink shared file: \"%s\"!", name);
	}
}

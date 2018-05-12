#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <semaphore.h>
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


#if !defined(DSM_UTIL_H)
#define DSM_UTIL_H

#include <stdlib.h>
#include <semaphore.h>

/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/


#define MAX(a,b)				((a) > (b) ? (a) : (b))

#define MIN(a,b)				((a) < (b) ? (a) : (b))

#define PAGESIZE				sysconf(_SC_PAGESIZE)


/*
 *******************************************************************************
 *                            Function Declarations                            *
 *******************************************************************************
*/


// Exits fatally with given error message. Also outputs errno.
void dsm_panic (const char *msg);

// Increments a semaphore. Panics on error.
void dsm_up (sem_t *sp);

// Decrements a semaphore. Panics on error.
void dsm_down (sem_t *sp);

// Returns a semaphore's value. Panics on error.
int dsm_getSemValue (sem_t *sp);

// Sets the given protections to a memory page. Exits fatally on error.
void dsm_mprotect (void *address, size_t size, int flags);

// Allocates a page-aligned slice of memory. Exits fatally on error.
void *dsm_pageAlloc (void *address, size_t size);

#endif
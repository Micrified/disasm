#if !defined(DSM_UTIL_H)
#define DSM_UTIL_H

#include <stdlib.h>
#include <sys/poll.h>
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
 *                          I/O Function Declarations                          *
 *******************************************************************************
*/


// Exits fatally with given error message. Also outputs errno.
void dsm_panic (const char *msg);

// Exits fatally with given error message. Outputs custom errno.
void dsm_cpanic (const char *msg, const char *reason);

// Exits fatally with formatted error. Supports tokens: {%s, %d, %f, %u}.
void dsm_panicf (const char *fmt, ...);

// Outputs warning to stderr.
void dsm_warning (const char *msg);


/*
 *******************************************************************************
 *                       Semaphore Function Declarations                       *
 *******************************************************************************
*/


// Increments a semaphore. Panics on error.
void dsm_up (sem_t *sp);

// Decrements a semaphore. Panics on error.
void dsm_down (sem_t *sp);

// Returns a semaphore's value. Panics on error.
int dsm_getSemValue (sem_t *sp);


/*
 *******************************************************************************
 *                        Memory Function Declarations                         *
 *******************************************************************************
*/

// Allocates a zeroed block of memory. Exits fatally on error.
void *dsm_zalloc (size_t size);

// Sets the given protections to a memory page. Exits fatally on error.
void dsm_mprotect (void *address, size_t size, int flags);

// Allocates a page-aligned slice of memory. Exits fatally on error.
void *dsm_pageAlloc (void *address, size_t size);


#endif
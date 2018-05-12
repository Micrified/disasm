#if !defined(DSM_UTIL_H)
#define DSM_UTIL_H


/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/


#define MAX(a,b)				((a) > (b) ? (a) : (b))

#define MIN(a,b)				((a) < (b) ? (a) : (b))


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


#endif
#if !defined(DSM_MANAGER_H)
#define DSM_MANAGER_H


/*
 *******************************************************************************
 *                            Function Declarations                            *
 *******************************************************************************
*/


/* 
 * Creates or opens the shared file, maps it to memory, then waits to begin.
 * This function should be called only once per process. A process that has
 * invoked this function should never fork.
 *
 * nproc: The number of processes expected to use the shared memory. 
*/
void dsm_init (unsigned int nproc);


#endif
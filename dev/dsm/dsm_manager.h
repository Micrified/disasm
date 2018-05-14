#if !defined(DSM_MANAGER_H)
#define DSM_MANAGER_H


/*
 *******************************************************************************
 *                            Function Declarations                            *
 *******************************************************************************
*/


/* 
 * The initializer function performs the following setup tasks:
 * 1. Creates or opens the shared file.
 * 2. Creates or opens the shared semaphores.
 * 3. Maps the shared file to memory.
 * 4. If creator of shared file, then initializes control structures.
 * 5. Forces last process checking in to destroy shared data.
 * 6. Initializes and moves processes to a new process group.
 * 7. Allocates a private read-only copy of the shared object.
 * 8. Installs SIGSEGV and SIGILL sychronization handlers.
 *
 * BEHAVIOR: The initializer should be called once per process. Forks of 
 * a process that has invoked the initializer SHOULD be safe to use.
 * Actual segmentation faults and illegal instructions will produce
 * undefined results as of this time.
 *
 * ARGUMENTS:
 *	nproc: The number of expected participant processes. If < nproc
 *		processes fail to check in, the following objects must be
 *		manually removes from /dev/shm on Linux systems:
 *		(dsm_object, sem.dsm_tally, sem.dsm_barrier).
*/
void dsm_init (unsigned int nproc);

/*
 * Unmaps the shared file from memory, deallocates the private page,
 * and changes the process to it's own group.
*/
void dsm_exit (void);




#endif
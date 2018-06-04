#if !defined (DSM_INTERFACE_H)
#define DSM_INTERFACE_H


/*
 *******************************************************************************
 *                            Function Declarations                            *
 *******************************************************************************
*/


/* Initializes the arbiter; connects to session daemon; starts session server.
 * - sid: The session identifer.
 * - addr: The address of the session daemon.
 * - port: The port of the session daemon.
 * - nproc: The number of expected processes.
 * This function blocks the caller until all nproc processes have connected.
*/
void dsm_init (const char *sid, const char *addr, const char *port, 
	unsigned int nproc);

/* Returns the process global identifier. Must be called after initialization. */
int dsm_getgid (void);

/* Suspends process until all registered processes reach the barrier. */
void dsm_barrier (void);

/* Disconnects from the arbiter; unmaps shared object. */
void dsm_exit (void);


#endif
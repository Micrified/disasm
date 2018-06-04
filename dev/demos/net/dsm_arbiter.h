#if !defined(DSM_ARBITER_H)
#define DSM_ARBITER_H


/*
 *******************************************************************************
 *                            Function Declarations                            *
 *******************************************************************************
*/


/* Runs the arbiter routine. This function does not return. 
 * - sid: The session identifier
 * - nproc: The total number of expected participant processes (across network).
 * - addr: The address of the session-daemon.
 * - port: The port of the session-daemon.
*/
void arbiter (const char *sid, unsigned int nproc, const char *addr,
	const char *port);


#endif
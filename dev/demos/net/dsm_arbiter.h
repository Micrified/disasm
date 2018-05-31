#if !defined(DSM_ARBITER_H)
#define DSM_ARBITER_H


/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/


// Minimum size of the process table (corresponds to number of open files).
#define DSM_MIN_NPROC			64


/*
 *******************************************************************************
 *                              Type Definitions                               *
 *******************************************************************************
*/


// Bit-field enumerating possible process states.
typedef struct dsm_pstate {
	unsigned int is_stopped;						// Process stopped.
	unsigned int is_waiting;						// Process at a barrier.
	unsigned int is_queued;							// Process in writer-queue.
} dsm_pstate;

// Structure describing process entry.
typedef struct dsm_proc {
	int fd;											// Process socket.
	int pid;										// Process ID.
	dsm_pstate flags;								// Process state.
} dsm_proc;

// Structure describing process table.
typedef struct dsm_ptab {
	unsigned int length;							// Process table length.
	dsm_proc *processes;							// Array of pstates.
} dsm_ptab;


#endif
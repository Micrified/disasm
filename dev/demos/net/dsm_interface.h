#if !defined(DSM_INTERFACE_H)
#define DSM_INTERFACE_H


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
	int writer;										// Writing process.
} dsm_ptab;


/*
 *******************************************************************************
 *                            Function Declarations                            *
 *******************************************************************************
*/


// Initialize shared memory session for nproc processes using daemon at port.
void dsm_init (const char *sid, unsigned int nproc, unsigned int port); 

// Exit shared memory session.
void dsm_exit (void);


#endif
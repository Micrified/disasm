#if !defined(DSM_TYPES_H)
#define DSM_TYPES_H

#include <semaphore.h>

/*
 *******************************************************************************
 *                         Arbiter Symbolic Constants                          *
 *******************************************************************************
*/


// Default port to which the listener socket is bound.
#define DSM_DEF_ARB_PORT		"4800"

// Minimum size of the process table (corresponds to number of open files).
#define DSM_MIN_NPROC			64


/*
 *******************************************************************************
 *                        Interface Symbolic Constants                         *
 *******************************************************************************
*/


// The name of the shared initialization semaphore.
#define DSM_SEM_INIT_NAME			"dsm_start"

// The name of the shared file.
#define DSM_SHM_FILE_NAME			"dsm_file"

// The minimum size of a shared memory file.
#define DSM_SHM_FILE_SIZE			(2 * DSM_PAGESIZE)


/*
 *******************************************************************************
 *                          Arbiter Type Definitions                           *
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
	int gid;										// Global process ID.
	int pid;										// Process ID.
	dsm_pstate flags;								// Process state.
} dsm_proc;

// Structure describing process table.
typedef struct dsm_ptab {
	unsigned int length;							// Process table length.
	dsm_proc *processes;							// Array of pstates.
} dsm_ptab;


/*
 *******************************************************************************
 *                         Interface Type Definitions                          *
 *******************************************************************************
*/


// Type describing a shared memory instance.
typedef struct dsm_smap { 
	sem_t sem_io;			// The I/O semaphore.
	sem_t sem_barrier;		// The barrier semaphore.
	off_t data_off;			// Offset to usable memory space.
	size_t size;			// Size of shared memory. 
} dsm_smap;


#endif
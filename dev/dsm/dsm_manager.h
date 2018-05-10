#if !defined(DSM_MANAGER_H)
#define DSM_MANAGER_H


/*
 *******************************************************************************
 *                   Role: Distributed Shared Memory Manager                   *
 *******************************************************************************
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <semaphore.h>

/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/

// [DELETE AFTER] Minimum macro.
#define MIN(a,b)	((a) < (b) ? (a) : (b))

// [DELETE AFTER] Maximum macro.
#define MAX(a,b)	((a) > (b) ? (a) : (b))

// The minimum size of a shared memory object.
#define DSM_MIN_OBJ_SIZE			sysconf(_SC_PAGESIZE)


// The default name given to a shared memory object.
#define DSM_DEFAULT_OBJ_NAME		"dsm_shared"


/*
 *******************************************************************************
 *                            Function Declarations                            *
 *******************************************************************************
*/


// Initializes the shared memory object and table. Exits fatally on error.
void dsm_init (const char *name);

// Unmaps shared object from memory. Destroys if only owner.
void dsm_destroy (const char *name);


#endif
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

#include "dsm_util.h"
#include "dsm_table.h"
#include "dsm_signal.h"
#include "dsm_sync.h"
#include "dsm_manager.h"

/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/


#define DSM_MIN_OBJ_SIZE		sysconf(_SC_PAGESIZE)

#define DSM_DEF_OBJ_NAME		"dsm_object"

#define DSM_SEM_BAR_NAME		"dsm_barrier"

#define DSM_SEM_TAL_NAME		"dsm_tally"


/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/


// Pointer to shared object in process memory.
dsm_table *shared_obj;

// Pointer to private object in process memory.
dsm_table *private_obj;

// Barrier semaphore. Processes wait on this until arbiter releases them.
sem_t *sem_barrier;

// Tally semaphore.  Processes increment this when they check in.
sem_t *sem_tally;


/*
 *******************************************************************************
 *                        Private Function Definitions                         *
 *******************************************************************************
*/


// Opens semaphore or creates with initial value val. Returns semaphore pointer.
static sem_t *getSem (const char *name, unsigned int val) {
	sem_t *sp;

	// Try creating exclusive semaphore. Defer exists error.
	if ((sp = sem_open(name, O_CREAT|O_EXCL|O_RDWR, S_IRUSR|S_IWUSR, val)) 
		== SEM_FAILED && errno != EEXIST) {
		dsm_panic("Couldn't create semaphore!");
	}

	// Try opening existing semaphore. Panic on error.
	if (sp == SEM_FAILED && (sp = sem_open(name, O_RDWR)) == SEM_FAILED) {
		dsm_panic("Couldn't open semaphore!");
	}

	return sp;
}

// Sets the size of a shared file. Returns size. Panics on error.
static off_t setSharedFileSize (int fd, off_t size) {
	if (ftruncate(fd, size) == -1) {
		dsm_panic("Couldn't resize shared file!");
	}
	
	return size;
}

// Gets the size of a shared file. Panics on error.
static off_t getSharedFileSize (int fd) {
	struct stat sb;
	
	if (fstat(fd, &sb) == -1) {
		dsm_panic("Couldn't get size of shared file!");
	}

	return sb.st_size;
}

// Maps shared file with given size and protections to memory. Panics on error.
static void *mapSharedFile (int fd, size_t size, int prot) {
	void *p;

	// Let operating system choose starting address.
	if ((p = mmap(NULL, size, prot, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		dsm_panic("Couldn't map shared file to memory!");
	}

	return p;
}

// Creates or opens shared file. Sets owner flag and returns file descriptor.
static int getSharedFile (const char *name, int *owner_p) {
	int fd, mode = S_IRUSR|S_IWUSR, owner = 0;
	
	// Try creating exclusive file. Defer exists error.
	if ((fd = shm_open(name, O_CREAT|O_EXCL|O_RDWR, mode)) == -1
		&& errno != EEXIST) {
		dsm_panic("Couldn't create shared file!");
	}
	
	// Set owner flag.
	if (fd != -1) {
		owner = 1;
	}

	// Try opening existing file. Panic on error.
	if (!owner && (fd = shm_open(name, O_RDWR, mode)) == -1) {
		dsm_panic("Couldn't open shared file!");
	}

	// Set owner pointer.
	if (owner_p != NULL) {
		*owner_p = owner;
	}

	return fd;
}

// Unlinks the shared object and shared semaphores.
static void destroySharedData (void) {

		// Unlink the barrier semaphore (do not destroy).
		if (sem_unlink(DSM_SEM_BAR_NAME) == -1) {
			dsm_panic("Couldn't unlink barrier semaphore!");
		}
	
		// Unlink the tally semaphore (do not destroy).
		if (sem_unlink(DSM_SEM_TAL_NAME) == -1) {
			dsm_panic("Couldn't unlink tally semaphore!");
		}

		// Unlink the shared object.
		if (shm_unlink(DSM_DEF_OBJ_NAME) == -1) {
			dsm_panic("Couldn't unlink shared object!");
		}
}

// Sets the process group ID (pgid). Releases table access if arbiter.
static void setPGID (int arbiter) {

	// If arbiter, change group ID and release table lock.
	if (arbiter) {
		
		if (setpgid(0, 0) == -1) {
			dsm_panic("Couldn't establish process group!");
		}

		dsm_up(&(shared_obj->sem_lock));
	} else {

		// Set process group to that of the arbiter.
		if (setpgid(0, dsm_getTablePGID(shared_obj)) == -1) {
			dsm_panic("Couldn't set process group!");
		}

	}
}


/*
 *******************************************************************************
 *                         Public Function Definitions                         *
 *******************************************************************************
*/


/* 
 * Creates or opens the shared file, maps it to memory, then waits to begin.
 * This function should be called only once per process. A process that has
 * invoked this function should never fork.
 *
 * nproc: The number of processes expected to use the shared memory. 
*/
void dsm_init (unsigned int nproc) {
	int fd;				// File descriptor of shared file.
	int arbiter;		// He who creates the shared file becomes arbiter.
	off_t size = 0;		// The size of the file to be mapped into memory.

	// Open or create the barrier semaphore.
	sem_barrier = getSem(DSM_SEM_BAR_NAME, 0);

	// Open or create the tally semaphore.
	sem_tally = getSem(DSM_SEM_TAL_NAME, 0);

	// Open or create the shared file.
	fd = getSharedFile(DSM_DEF_OBJ_NAME, &arbiter);

	// If shared-file creator, set size. Else get size.
	if (arbiter) {
		size = setSharedFileSize(fd, DSM_MIN_OBJ_SIZE);
	} else {
		size = getSharedFileSize(fd);
	}

	// Map shared file into memory. Set the global pointer.
	shared_obj = (dsm_table *)mapSharedFile(fd, (size_t)size, 
		PROT_READ|PROT_WRITE);

	// If arbiter, set group, install the table, then lower the barrier.
	if (arbiter) {

		// Zero out space before.
		memset(shared_obj, 0, (size_t)size);
		
		// Initialize the table, then lock it down to sync pgid after.
		dsm_initTable(shared_obj, (size_t)size);
		
		dsm_down(&(shared_obj->sem_lock));

		printf("[%d] [%d] (ARBITER) Releasing!\n", getpid(), getpgid(0));
		for (int i = nproc; i > 1; i--) {
			dsm_up(sem_barrier);
		}
	} else {
		printf("[%d] [%d] (REGULAR) Waiting...\n", getpid(), getpgid(0));
		dsm_down(sem_barrier);
	}

	// Increment the tally semaphore.
	dsm_up(sem_tally);
	
	// If last to check in, unlink shared object and semaphore.
	if (dsm_getSemValue(sem_tally) == nproc) {
		destroySharedData();
	}

	// Set PGID. If arbiter, release table lock.
	setPGID(arbiter);

	// Allocate private object, copy shared into it, then read protect it.
	private_obj = (dsm_table *)dsm_pageAlloc(private_obj, size);
	memcpy(private_obj, shared_obj, size);
	dsm_mprotect(private_obj, size, PROT_READ);

	// Initialize sychronization data structures.
	dsm_sync_init();

	// Install sychronization signal handlers.
	dsm_sigaction(SIGSEGV, dsm_sync_sigsegv);
	dsm_sigaction(SIGILL, dsm_sync_sigill);
	dsm_sigaction(SIGCONT, dsm_sync_sigcont);

	// Close shared file descriptor.
	close(fd);
}

/*
 * Unmaps the shared file from memory, deallocates the private page,
 * and changes the process to it's own group.
*/
void dsm_exit (void) {

	// Deallocate private page.
	if (private_obj != NULL) {
		free(private_obj);
	}

	// Unmap shared object.
	if (shared_obj != NULL && munmap(shared_obj, shared_obj->obj_size) == -1) {
		dsm_panic("Couldn't unmap shared object!");
	}

	// Exit process group.
	if (setpgid(0, 0) == -1) {
		dsm_panic("Couldn't exit process group!");
	}

	// Reset SIGSEGV and SIGILL handlers to default.
	dsm_sigdefault(SIGSEGV);
	dsm_sigdefault(SIGILL);
	dsm_sigdefault(SIGCONT);
}


/*
 *******************************************************************************
 *                              Testing Functions                              *
 *******************************************************************************
*/

// The program each fork runs. Exits when completed.
void childProgram (int nproc) {

	// Run the initializer.
	dsm_init(nproc);

	// Acquire a table address to write to.
	int *x = (int *)(private_obj + DSM_TAB_SIZE);

	// Increment it.
	(*x)++;

	// Print that you're done.
	printf("[%d] [%d] Done (x = %d)!\n", getpid(), getpgid(0), *x);

	// Run the destructor.
	dsm_exit();

	exit(EXIT_SUCCESS);
}


// Main just forks some children. Then it waits for them to die.
int main (void) {
	int children = 3;	// The number of forks to perform.

	// Fork away!
	printf("[%d] [%d] Master: Go!\n", getpid(), getpgid(0));
	for (int i = 0; i < children; i++) {
		if (fork() == 0) {
			childProgram(children);
		}
	}

	// Wait on children.
	for (int i = 0; i < children; i++) {
		wait(NULL);
	}

	printf("[%d [%d] Master: All children have finished!\n", getpid(), getpgid(0));

	return EXIT_SUCCESS;
}
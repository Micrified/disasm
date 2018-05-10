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

#define OBJ_SIZE		sizeof(struct sharedObject)

#define OBJ_NAME		"semaphore"


/*
 *******************************************************************************
 *                                 Data Types                                  *
 *******************************************************************************
*/

struct sharedObject {
	sem_t sem_a;		// The first child process's semaphore.
	sem_t sem_b;		// The second child process's semaphore.
};

/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/


// Shared object pointer.
struct sharedObject *shared;


/*
 *******************************************************************************
 *                              Utility Routines                               *
 *******************************************************************************
*/

// Exits with an error message from src.
void panic (const char *src, const char *msg) {
	fprintf(stderr, "Fatal [%d]: \"%s\". Reason: \"%s\"\n", getpid(), src, msg);
	exit(EXIT_FAILURE);
}

// Maps a shared object named "name" into process memory.
void mapObject (const char *name) {
	int fd;

	// Open the shared page.
	if ((fd = shm_open(name, O_RDWR, S_IRUSR | S_IWUSR)) == -1) {
		panic("Couldn't open shared page!", strerror(errno));
	}

	// Map object into memory.
	if ((shared = mmap(NULL, OBJ_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0))
		== MAP_FAILED) {
		panic("Couldn't map object into memory!", strerror(errno));
	}

	// Close the file descriptor.
	close(fd);
}

// Creates a zero-initialized sharedObject instance with name: "name".
void createObject (const char *name) {
	int fd;

	// Create the shared page.
	if ((fd = shm_open(name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) == -1) {
		panic("Couldn't create shared page!", strerror(errno));
	}

	// Set page size.
	ftruncate(fd, OBJ_SIZE);

	// Map object into memory.
	if ((shared = mmap(NULL, OBJ_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0))
		== MAP_FAILED) {
		panic("Couldn't map object to memory!", strerror(errno));
	}

	// Initialize both semaphores to zero.
	sem_init(&(shared->sem_a), 1, 0); 
	sem_init(&(shared->sem_b), 1, 0);

	// Unmap object from memory.
	if (munmap(shared, OBJ_SIZE) == -1) {
		panic("Couldn't unmap object from memory!", strerror(errno));
	}

	// Close file descriptor.
	close(fd);
}


/*
 *******************************************************************************
 *                               Child Routines                                *
 *******************************************************************************
*/

// Pingpong routine run by child processes.
void pingpong (int child) {
	
	for (int i = 0; i < 5; i++) {

		// DOWN: Acquire personal semaphore.
		if (child == 0 && (i % 2) == 1) {
			if (sem_wait(&(shared->sem_a)) == -1) {
				panic("Couldn't decrement semaphore!", strerror(errno));
			}
		}
		if (child == 1 && (i % 2) == 0) {
			if (sem_wait(&(shared->sem_b)) == -1) {
				panic("Couldn't decrement semaphore!", strerror(errno));
			}
		}

		// Print.
		if (child == 0) {
			printf("Ping! ...\n");
		} else {
			printf("... Pong!\n");
		}

		// UP: Release counterparts semaphore.
		if (child == 0) {
			if (sem_post(&(shared->sem_b)) == -1) {
				panic("Failed to release semaphore!", strerror(errno));
			}		
		} else {
			if (sem_post(&(shared->sem_a)) == -1) {
				panic("Failed to release semaphore!", strerror(errno));
			}
		}

	}

	// Exit nicely. Shared object is automatically unmapped.
	exit(EXIT_SUCCESS);
}



/*
 *******************************************************************************
 *                                    Main                                     *
 *******************************************************************************
*/


int main (void) {

	// Create a shared object (zeroed out).
	createObject(OBJ_NAME);

	// Fork two childen.
	for (int i = 0; i < 2; i++) {
		if (fork() == 0) {
			mapObject(OBJ_NAME);
			pingpong(i);
		}
	}

	// Wait for the children.
	for (int i = 0; i < 2; i++) {
		wait(NULL);
	}

	// Destroy the shared page.
	if (shm_unlink(OBJ_NAME) == -1) {
		panic("Couldn't destroy shared page!", strerror(errno));
	}

	return 0;
}
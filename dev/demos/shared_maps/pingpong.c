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

#define OBJ_SIZE		512

#define OBJ_NAME		"pingpong"

/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/


// Shared turn variable. 
void *turn;


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
	if ((turn = mmap(NULL, OBJ_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0))
		== MAP_FAILED) {
		panic("Couldn't map object into memory!", strerror(errno));
	}
}

// Creates a shared object of name "name", initially sets all values to zero.
void createObject (const char *name) {
	int fd;

	// Create the shared page.
	if ((fd = shm_open(name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) == -1) {
		panic("Couldn't create shared page!", strerror(errno));
	}

	// Set page contents.
	ftruncate(fd, OBJ_SIZE);

	// Map object into memory.
	if ((turn = mmap(NULL, OBJ_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0))
		== MAP_FAILED) {
		panic("Couldn't map object to memory!", strerror(errno));
	}

	// Zero memory map.
	memset(turn, 0, OBJ_SIZE);

	// Unmap object from memory.
	if (munmap(turn, OBJ_SIZE) == -1) {
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


// The child process ping-pong routine.
void pingpong (int child) {

	// Perform action 5 times.
	for (int i = 0; i < 5; i++) {
		
		// Busy wait on turn.
		while (*((int *)turn) != child)
			;

		// Print.
		if (child == 0) {
			printf("Ping! ...\n");
		} else {
			printf("... Pong!\n");
		}

		// Reset turn.
		*((int *)turn) = (1 - child);
	}

	// Exit nicely: Mapped object is automatically removed from memory.
	exit(EXIT_SUCCESS);
}


/*
 *******************************************************************************
 *                                    Main                                     *
 *******************************************************************************
*/


int main (void) {

	// Create a zeroed shared object.
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
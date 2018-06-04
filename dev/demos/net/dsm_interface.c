#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <semaphore.h>

#include "dsm_interface.h"
#include "dsm_types.h"
#include "dsm_inet.h"
#include "dsm_msg.h"
#include "dsm_arbiter.h"
#include "dsm_util.h"
#include "dsm_signal.h"
#include "dsm_sync.h"

/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/


// Initialization semaphore. Processes wait on this during initialization.
sem_t *sem_start;

// Shared memory pointer. Manifests as a pointer to type: dsm_shm
dsm_smap *smap;

// Process global identifier.
static int gid = -1;

// Socket for IPC to arbiter.
int sock_arbiter = -1;

// [DEBUG] Temporary holder of STDOUT fd.
int stdout_fd;

/*
 *******************************************************************************
 *                        Internal Function Definitions                        *
 *******************************************************************************
*/


// Initializes a dsm_shm map at the given aligned-address. Returns pointer.
static dsm_smap *initSharedMapAt (dsm_smap *addr, size_t size) {

	// Initialize the semaphores.
	if (sem_init(&(addr->sem_io), 1, 1) == -1 ||
		sem_init(&(addr->sem_barrier), 1, 1) == -1) {
		dsm_panic("Couldn't initialize semaphores!");
	}

	// Extra-check: Size of dsm_shm doesn't exceed the size of one page.
	if (sizeof(dsm_smap) > DSM_PAGESIZE) {
		dsm_cpanic("dsm_smap", "sizeof(dsm_smap) exceeds pagesize!");
	}

	// Set the offset: Exactly one memory page from aligned address 'addr'.
	addr->data_off = DSM_PAGESIZE;

	// Set the size: Should be > one page.
	if (size < DSM_PAGESIZE) {
		dsm_cpanic("initSharedMapAt", "Shared map size must be >= pagesize!");
	} else {
		addr->size = size;
	}

	return addr;
}


// Opens or creates semaphore with initial value 'val'. Returns semaphore ptr.
static sem_t *getSem (const char *name, unsigned int val) {
	sem_t *sp;

	// Try creating exclusive semaphore. Defer EEXIST error.
	if ((sp = sem_open(name, O_CREAT|O_EXCL|O_RDWR, S_IRUSR|S_IWUSR, val))
		== SEM_FAILED && errno != EEXIST) {
		dsm_panicf("Couldn't create named-semaphore: \"%s\"!", name);
	}

	// Try opening existing semaphore. Panic on error.
	if (sp == SEM_FAILED && (sp = sem_open(name, O_RDWR)) == SEM_FAILED) {
		dsm_panicf("Couldn't open named-semaphore: \"%s\"!", name);
	}

	return sp;
}

// Sets size of shared file. Returns given size on success. Panics on error.
static off_t setSharedFileSize (int fd, off_t size) {
	if (ftruncate(fd, size) == -1) {
		dsm_panicf("Couldn't resize shared file (fd = %d)!", fd);
	}
	return size;
}

// Returns the size of a shared file. Panics on error.
static off_t getSharedFileSize (int fd) {
	struct stat sb;

	if (fstat(fd, &sb) == -1) {
		dsm_panicf("Couldn't get size of shared file (fd = %d)!", fd);
	}

	return sb.st_size;
}

// Maps shared file of given size to memory with protections. Panics on error.
static void *mapSharedFile (int fd, size_t size, int prot) {
	void *map;

	// Let operating system select starting address: PAGE ALIGNED
	if ((map = mmap(NULL, size, prot, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		dsm_panicf("Couldn't map shared file to memory (fd = %d)!", fd);
	}

	// Zero the memory.
	memset(map, 0, size);

	return map;
}

// Creates or opens a shared file. Sets owner flag, returns file-descriptor.
static int getSharedFile (const char *name, int *is_owner) {
	int fd, mode = S_IRUSR|S_IWUSR, owner = 0;

	// Try creating exclusive file. Defer EEXIST error.
	if ((fd = shm_open(name, O_CREAT|O_EXCL|O_RDWR, mode)) == -1 &&
		errno != EEXIST) {
		dsm_panicf("Couldn't created shared file \"%s\"!", name);
	}

	// Set owner flag.
	owner = (fd != -1);

	// Try opening existing file. Panic on error.
	if (!owner && (fd = shm_open(name, O_RDWR, mode)) == -1) {
		dsm_panicf("Couldn't open shared file \"%s\"!", name);
	}

	// Set owner pointer.
	if (is_owner != NULL) {
		*is_owner = owner;	
	}

	return fd;
}


/*
 *******************************************************************************
 *                              Message Functions                              *
 *******************************************************************************
*/


// Sends a registration message to the given socket.
static void send_addProc (void) {
	dsm_msg msg;

	// Configure message.
	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_ADD_PROC;
	msg.payload.proc.pid = getpid();
	msg.payload.proc.gid = -1;

	// Send message.
	dsm_sendall(sock_arbiter, &msg, sizeof(msg));
}

// Reads a reply from the arbiter, and returns the message GID.
static int recv_gid (void) {
	dsm_msg msg;

	// Receive message.
	if (dsm_recvall(sock_arbiter, &msg, sizeof(msg)) != 0) {
		dsm_cpanic("recv_gid", "Lost connection to arbiter!");
	}

	// Verify message.
	if (msg.type != MSG_SET_GID || msg.payload.proc.pid != getpid()) {
		dsm_cpanic("recv_gid", "Bad message!");
	}

	// Return the global identifier.
	return msg.payload.proc.gid;
}

// Reads a start message from the arbiter.
static void recv_waitDone (void) {
	dsm_msg msg;

	// Receive message.
	if (dsm_recvall(sock_arbiter, &msg, sizeof(msg)) != 0) {
		dsm_cpanic("recv_waitDone", "Lost connection to arbiter!");
	}

	// Verify message.
	if (msg.type != MSG_WAIT_DONE) {
		dsm_cpanic("recv_waitDone", "Bad message!");
	}
}

// Informs arbiter that process is waiting on a barrier.
static void send_waitBarr (void) {
	dsm_msg msg;

	// Configure message.
	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_WAIT_BARR;

	// Send message.
	dsm_sendall(sock_arbiter, &msg, sizeof(msg));
}

// Sends an exit message to the arbiter.
static void send_prgmDone (void) {
	dsm_msg msg;

	// Configure message.
	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_PRGM_DONE;
	msg.payload.done.nproc = 1;
	
	// Send message.
	dsm_sendall(sock_arbiter, &msg, sizeof(msg));
}


/*
 *******************************************************************************
 *                        External Function Definitions                        *
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
	unsigned int nproc) {
	int fd, first;
	off_t size = 0;

	// Verify state.
	if (sock_arbiter != -1 || smap != NULL) {
		dsm_cpanic("dsm_init", "Initializer called twice without destructor!");
	}

	// Create or open the init-semaphore.
	sem_start = getSem(DSM_SEM_INIT_NAME, 0);

	printf("[%d] sem_start created!\n", getpid()); fflush(stdout);

	// Create or open the shared file.
	fd = getSharedFile(DSM_SHM_FILE_NAME, &first);

	printf("[%d] shared file created!\n", getpid()); fflush(stdout);

	// Set or get file size.
	if (first) {
		size = setSharedFileSize(fd, DSM_SHM_FILE_SIZE);
	} else {
		size = getSharedFileSize(fd);
	}

	// Map shared file to memory.
	smap = (dsm_smap *)mapSharedFile(fd, size, PROT_READ|PROT_WRITE);

	printf("[%d] memory map created!\n", getpid()); fflush(stdout);

	// If first: Setup dsm_smap and protect shared page. Then fork arbiter.
	if (first) {
		initSharedMapAt(smap, size);
		if (fork() == 0) {
			close(STDOUT_FILENO);
			dup(stdout_fd);
			arbiter(sid, nproc, addr, port);
		}
	}

	printf("[%d] Waiting...\n", getpid()); fflush(stdout);

	// Wait on initialization-semaphore for release by arbiter.
	dsm_down(sem_start);

	// Connect to arbiter.
	sock_arbiter = dsm_getConnectedSocket(DSM_LOOPBACK_ADDR, 
		DSM_DEF_ARB_PORT);
	
	// Send registration-message. 
	send_addProc();

	// Read global identifier.
	gid = recv_gid();

	printf("[%d] Ready to go! (GID = %d)\n", getpid(), gid); fflush(stdout);

	// Initialize decoder.
	dsm_sync_init();

	// Install the signal handlers.
	dsm_sigaction(SIGSEGV, dsm_sync_sigsegv);
	dsm_sigaction(SIGILL, dsm_sync_sigill);
	//dsm_sigaction(SIGCONT, dsm_sync_sigcont);
	//dsm_sigaction(SIGTSTP, dsm_sync_sigtstp);

	// Protect the shared page.
	void *page = (void *)smap + smap->data_off;
	dsm_mprotect(page, DSM_PAGESIZE, PROT_READ);

	// Block until start message is received.
	recv_waitDone();
}

/* Returns the process global identifier. Must be called after initialization. */
int dsm_getgid (void) {
	if (gid == -1) {
		dsm_cpanic("dsm_getgid", "dsm_init must be called first!");
	}
	return gid;
}

/* Suspends process until all registered processes reach the barrier. */
void dsm_barrier (void) {
	send_waitBarr();
	if (kill(getpid(), SIGTSTP) == -1) {
		dsm_panic("Couldn't suspend process!");
	}
}

/* Returns pointer to shared page. Returns NULL on error. */
void *dsm_getSharedPage (void) {

	// Verify state.
	if (sock_arbiter == -1 || smap == NULL) {
		return NULL;
	}

	// Return pointer to shared page.
	return ((void *)smap + smap->data_off);
}

/* Disconnects from the arbiter; unmaps shared object. */
void dsm_exit (void) {

	// Verify state.
	if (sock_arbiter == -1 || smap == NULL) {
		dsm_cpanic("dsm_exit", "Routine called twice or no initialization!");
	}

	// Send exit message to arbiter.
	send_prgmDone();

	// Close the socket.
	close(sock_arbiter);

	// Reset the socket.
	sock_arbiter = -1;

	// Unmap the shared file.
	//if (munmap((void *)smap, smap->size) == -1) {
	//	dsm_panic("Couldn't unmap shared file!");
	//}
	
	// Reset global pointer.
	smap = NULL;

	// Reset the signal handlers.
	dsm_sigdefault(SIGSEGV);
	dsm_sigdefault(SIGILL);;
}


/*
 *******************************************************************************
 *                                    Main                                     *
 *******************************************************************************
*/


int main (void) {
	void *page;

	// Get identity.
	int whoami = (fork() == 0) ? 1 : 0;

	// Redirect output to a new terminal.
	//stdout_fd = dsm_redirXterm();

	// Call initializer.
	dsm_init("arethusa", "127.0.0.1", "4200", 2);

	// Get shared page.
	page = dsm_getSharedPage();
	int *turn = (int *)page;

	for (int i = 0; i < 5; i++) {

		while (*turn != whoami);
		if (whoami == 0) {
			printf("Ping ...\n"); fflush(stdout);
		} else {
			printf("... Pong\n"); fflush(stdout);
		}
		sleep(1);
		*turn = (1 - *turn);
	}

	sleep(5);

	// Call destructor.
	dsm_exit();

	return 0;
}



#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsm_util.h"
#include "dsm_interface.h"


/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/


// Default listener socket backlog
#define DSM_DEF_BACKLOG			32

// Minimum number of concurrent pollable connections.
#define DSM_MIN_POLLABLE		32

// Loopback address.
#define DSM_LOOPBACK_ADDR		"127.0.0.1"


/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/

// [A] Boolean flag indicating if program should continue polling.
int alive = 1;

// [A] Process table. Indexed by file-descriptor.
dsm_ptab ptab = (dsm_ptab){.length = 0, .processes = NULL, .writer = -1};

// [A] Pollable file-descriptors.
pollset *pollableSet;

// [A] The operation queue.
dsm_opqueue *opqueue;

// [A] Function map.
dsm_msg_func fmap[MAX_MSG_VALUE];

// [A] Server-socket. Connects to session-server.
int sock_server;

// [A] Listener-socket. Handles local processes.
int sock_listen;


/*
 *******************************************************************************
 *                     Process Table Function Definitions                      *
 *******************************************************************************
*/


// [A] Initializes the global process table.
static void initProcessTable (unsigned int length) {
	
	// Allocate and zero out process array.
	ptab.processes = (dsm_proc *)dsm_zalloc(length * sizeof(dsm_proc));

	// Update length.
	ptab.length = length;
}

// [A] Resize the process table (at least minLength).
static void resizeProcessTable (unsigned int minLength) {
	void *new_processes;
	unsigned int new_length;

	// Compute new length.
	new_length = MAX(minLength, 2 * ptab.length);
	
	// Allocate and zero out new process array.
	new_processes = dsm_zalloc(new_length * sizeof(dsm_proc));

	// Copy over the old array.
	memcpy(new_processes, ptab.processes, ptab.length * sizeof(dsm_proc));

	// Free old array.
	free(ptab.processes);

	// Assign new array, and update the length.
	ptab.processes = new_processes;
	ptab.length = new_length;
}

// [A] Deallocates the process table.
static void freeProcessTable (void) {
	free(ptab.processes);
}

// [A] Registers a new file-descriptor in the process table.
static void registerProcess (int fd, int pid) {

	// Reallocate if necessary.
	if (fd >= ptab.length) {
		resizeProcessTable(fd + 1);
	}

	// Verify entry is not occupied (should not happen).
	if (ptab.processes[fd].pid != 0) {
		dsm_cpanic("registerProcess", "Current slot occupied!");
	}
	
	// Insert new entry.
	ptab.processes[fd] = (dsm_proc) {
		.fd = fd,
		.pid = pid,
		.flags = (dsm_pstate){.is_stopped = 0, .is_waiting = 0}
	};
}

// [A] Unregisters a given file-descriptor from the process table.
static void unregisterProcess (int fd) {

	// Verify location is accessible.
	if (fd < 0 || fd > ptab.length || ptab.processes == NULL) {
		dsm_cpanic("unregisterProcess", "Location inaccessible!");
	}

	memset(ptab.processes + fd, 0, sizeof(dsm_proc));
}

// [A] Prints the process table.
static void showProcessTable (void) {
	int n = 0;
	printf("------------------------- Process Table -----------------------\n");
	printf("\tFD\t\tPID\t\tSTOP\t\tWAIT\n");
	for (int i = 0; i < ptab.length; i++) {
		if (ptab.processes[i].pid == 0) {
			continue;
		}
		n++;
		dsm_proc p = ptab.processes[i];
		char s = (p.flags.is_stopped ? 'Y' : 'N');
		char w = (p.flags.is_waiting ? 'Y' : 'N');
		printf("\t%d\t\t%d\t\t%c\t\t%c\n", i, p.pid, s, w);
	}
	printf(" [%d/%d slots occupied]\n", n, ptab.length);
	printf("---------------------------------------------------------------\n");
}


/*
 *******************************************************************************
 *                         Message Dispatch Functions                          *
 *******************************************************************************
*/


// Informs session-server that all processes on this machine have finished.
static void send_programDone (int fd, unsigned int nproc) {
	dsm_msg msg;

	// Configure message.
	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_PRGM_DONE;
	msg.payload.done.nproc = nproc;

	// Send the message.
	dsm_sendall(fd, &msg, sizeof(msg));
}

// Informs session-server that all processes on this machine are stopped.
static void send_stopDone (int fd, unsigned int nproc) {
	dsm_msg msg;

	// Configure message.
	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_STOP_DONE;
	msg.payload.done.nproc = nproc;

	// Send the message.
	dsm_sendall(fd, &msg, sizeof(msg));
}

/*
 *******************************************************************************
 *                          Message Handler Functions                          *
 *******************************************************************************
*/

// Message requesting arbiter stop all processes and send ack.
static void msg_stopAll (int fd, dsm_msg *mp) {

	// If a writer is set, all processes are already paused.
	if (ptab.writer != -1) {
		send_stopDone(fd, nproc);
		return;
	}

	// Otherwise, pause all processes.
	signalProcess(-1, SIGTSTP);

	// Send response.
	send_stopDone(fd, nproc);
}

// Message requesting arbiter continue all stopped processes.
static void msg_contAll (int fd, dsm_msg *mp) {
	dsm_proc *p;

	// Unset the stop bit for all processes in the table.
	for (int i = 0; i < ptab.length; i++) {
		p = ptab.processes + i;

		// Skip unused slots.
		if (p->pid == 0) {
			continue;
		}

		// Unset stop-bit.
		p->flags.is_stopped = 0;

		// Signal only if: not-waiting.
		if (p->flags.is_waiting == 0) {
			signalProcess(p->pid, SIGCONT);
		}
	}

	// If writer was set, unset it. The write is over.
	if (ptab.writer != -1) {
		ptab.writer = -1;
	}
}

// Message requesting arbiter continue all waiting processes.
static void msg_waitDone (int fd, dsm_msg *mp) {
	dsm_proc *p;

	// Unset the waiting bit for all processes in the table.
	for (int i = 0; i < ptab.length; i++) {
		p = ptab.processes + i;

		// Skip unused slots.
		if (p->pid == 0) {
			continue;
		}

		// Unset wait-bit.
		p->flags.is_waiting = 0;

		// Signal only if: not-stopped (shouldn't occur).
		if (p->flags.is_stopped == 0) {
			signalProcess(p->pid, SIGCONT);
		}
	}
}

// Message informing arbiter that a write-operation may now proceed.
static void msg_writeOkay (int fd, dsm_msg *mp) {

	// Ensure writer exists.
	if (ptab.writer == -1) {
		dsm_cpanic("msg_writeOkay", "No writing process set!");
	}
	
	// Forward message to writer.
	dsm_sendall(ptab.writer, mp, sizeof(*mp));
}

// Message from writer with write data.
static void msg_syncInfo (int fd, dsm_msg *mp) {

	// Ensure sender is active-writer.
	if (fd != ptab.writer) {
		dsm_cpanic("msg_syncInfo", "Unauthorized synchronization message!");
	}

	// Forward message to server.
	dsm_sendall(sock_server, mp, sizeof(*mp));
}

// Message from process requesting write-access.
static void msg_syncRequest (int fd, dsm_msg *mp) {
	dsm_proc *p;

	// Ensure write-request isn't coming from server or is already writer.
	if (fd == sock_server || fd == ptab.writer) {
		dsm_cpanic("msg_syncRequest", "Unauthorized sychronization message!");
	}

	// Get process entry.
	p = ptab->processes + fd;

	// Enqueue write operation.
	enqueueOperation(p->pid, opqueue);

	// If a write is currently underway, return early.
	if (ptab.writer != -1) {
		return;
	}

	// Otherwise, set current process as writer.
	ptab.writer = fd;

	// Instruct all processes to stop.
	for (int i = 0; i < ptab.length; i++) {
		p = ptab.processes + i;

		// Skip unused slots.
		if (p->pid == 0) {
			continue;
		}

		// Skip waiting/stopped processes (they're already suspended).
		if (p->flags.is_waiting == 1 || p->flags.is_stopped == 1) {
			continue;
		}

		// Set stopped bit.
		p->flags.is_stopped = 1;

		// Send stop signal.
		signalProcess(p->pid, SIGTSTP);
	}
}

/*
 *******************************************************************************
 *                              Utility Functions                              *
 *******************************************************************************
*/

// [A] Sends signal to 'fd'. If -1 is specified, sends to all fds in ptab.
static void signalProcess (int fd, int signal) {
	int pid;

	// If file-descriptor is a natural number, send to it only.
	if (fd >= 0) {
		
		// Validate file-descriptor.
		if (fd > ptab.length) {
			dsm_cpanic("signalProcess", "Specified file not in table!");
		}

		// Send the signal.
		if (kill(ptab.processes[fd].pid, signal) == -1) {
			dsm_panicf("Couldn't signal process (%d)!", 
				ptab.processes[fd].pid);
		}

		return;
	}

	// Signal all processes in the table.
	for (int i = 0; i < ptab.length; i++) {
		pid = ptab.processes[i].pid;

		// Ignore unused slots.
		if (pid == 0) {
			continue;
		}

		// Send the signal.
		if (kill(pid, signal) == -1) {
			dsm_panicf("Couldn't signal process (%d)!", pid);
		}
	}
}

// [A] Contacts daemon with sid, sets session details. Exits fatally on error.
static int getServerSocket (const char *sid, const char *addr, 
	const char *port, unsigned int nproc) {
	dsm_msg msg;
	int s;

	// 1. Construct GET request.
	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_GET_SESSION;
	sprintf(msg.payload.get.sid, "%*s", DSM_SID_SIZE, sid);
	msg.payload.get.nproc = nproc;

	// 2. Connect to session daemon.
	s = dsm_getConnectedSocket(addr, port);

	// 3. Send request.
	dsm_sendall(s, &msg, sizeof(msg));

	// 4. Read reply.
	dsm_recvall(s, &msg, sizeof(msg));

	// 5. Verify reply.
	if (msg.type != DSM_SET_SESSION) {
		dsm_cpanic("getServerSocket", "Unrecognized response!");
	}

	// 6. Close connection.
	close(s);

	// 7. Connect to session server.
	s = dsm_getConnectedSocket(DSM_LOOPBACK_ADDR, 
			dsm_portToString(msg.payload.set.port));

	// 8. Return connected socket.
	return s; 
}

// Accepts incoming connection, and updates the list of pollable descriptors.
static void processConnection (int sock_listen) {
	struct sockaddr_storage newAddr;
	socklen_t newAddrSize = sizeof(newAddr);
	int sock_new;

	// Try accepting connection.
	if ((sock_new = accept(sock_listen, (struct sockaddr *)&newAddr,
		&newAddrSize)) == -1) {
		dsm_panic("Couldn't accept connection!");
	}

	// Register connection in pollable descriptor list.
	dsm_setPollable(sock_new, POLLIN, pollableSet);
}

// Reads message from fd. Decodes and selects appropriate action.
static void processMessage (int fd) {
	dsm_msg msg;
	void (*action)(int, dsm_msg *);

	// Read in message: If no connection -> Panic.
	if (dsm_recvall(fd, &msg, sizeof(msg)) != 0) {
		// TODO: GRACEFULLY STOP ALL OTHER PROCESSES HERE.
		if (fd == sock_server) {
			dsm_cpanic("Lost connection to server!", "Terminating!");
		} else {
			dsm_cpanic("Lost connection to process!", "Terminating!");
		}
	}

	// Determine action based on message type.
	if ((action = dsm_getMsgFunc(msg.type, fmap)) == NULL) {
		dsm_warning("No action for message type!");
		return;
	}

	// Execute action.
	action(fd, &msg);
}


/*
 *******************************************************************************
 *                                   Arbiter                                   *
 *******************************************************************************
*/


static void arbiter (const char *sid, unsigned int nproc, const char *addr,
	const char *port) {
	int new;								// Count of active file-descriptors.
	struct pollfd *pfd;						// Pointer to poll structure.
	// ------------------------------ Setup ------------------------------------

	// Register functions.
	

	// Initialize process table.
	initProcessTable(DSM_MIN_NPROC);

	// Initialize pollable-set.
	pollableSet = dsm_initPollSet(DSM_MIN_POLLABLE);

	// Initialize operation-queue.
	opqueue = initOpQueue(DSM_MIN_OPQUEUE_SIZE);

	// Setup server socket.
	sock_server = getServerSocket(sid, addr, port, nproc);

	// Setup listener socket: Any port.
	sock_listen = dsm_getBoundSocket(AI_PASSIVE, AF_UNSPEC, SOCK_STREAM, "0");

	// Listen on socket.
	if (listen(sock_listen, DSM_DEF_BACKLOG) == -1) {
		dsm_panic("Couldn't listen on socket!");
	}

	// Set server socket as pollable.
	dsm_setPollable(sock_server, POLLIN, pollableSet);

	// Set listener socket as pollable.
	dsm_setPollable(sock_listen, POLLIN, pollableSet);

	// ---------------------------- Main Body -----------------------------------

	while (alive && (new = poll(pollableSet->fds, pollableSet->fp, -1)) != -1) {
		for (int i = 0; i < pollableSet->fp; i++) {
			pfd = pollableSet->fds[i];

			// If nothing to read, ignore file-descriptor.
			if ((pfd->revents & POLLIN) == 0) {
				continue;
			}

			// If listener socket: Handle connection.
			if (pfd->fd == sock_listen) {
				processConnection(sock_listen);
				continue;
			}

			// Otherwise: Handle process/server message.
			processMessage(pfd->fd);
		}

		printf("[%d] Arbiter State\n", getpid());
		dsm_showPollable(pollableSet);
		showProcessTable();
		putchar('\n');
	}
	
	
	// ----------------------------- Cleanup ------------------------------------

	// Send disconnect message.
	send_programDone(sock_server);

	// Disconnect from server.
	close(sock_server);

	// Free operation-queue.
	freeOpQueue(opqueue);

	// Free the pollable set.
	dsm_freePollable(pollableSet);

	// Free the process table.
	freeProcessTable();

	// <UNMAP HERE>

	// Exit.
	exit(EXIT_SUCCESS);
}


/*
 *******************************************************************************
 *                            Function Definitions                             *
 *******************************************************************************
*/


// Initialize shared memory session for nproc processes using daemon at port.
void dsm_init (const char *sid, unsigned int nproc, unsigned int port) {


} 

// Exit shared memory session.
void dsm_exit (void) {


}


/*
 *******************************************************************************
 *                                    Main                                     *
 *******************************************************************************
*/


int main (int argc, const char *argv[]) {
	int input, fd, pid;
	printf("Testing out the table!\n");

	initProcessTable(4);

	while (1) {
		printf("1: Make Entry. 2: Quit\n");
		printf(": ");
		
		scanf("%d", &input);

		if (input == 2) {
			break;
		}

		printf("fd: "); scanf("%d", &fd);
		printf("pid: "); scanf("%d", &pid);

		registerProcess(fd, pid);
		showProcessTable();
	}

	freeProcessTable();
}

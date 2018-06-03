#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>

#include "dsm_util.h"
#include "dsm_inet.h"
#include "dsm_queue.h"
#include "dsm_msg.h"
#include "dsm_poll.h"

#include "dsm_arbiter.h"
#include "dsm_types.h"

/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/


// Default listener socket backlog
#define DSM_DEF_BACKLOG			32

// Minimum number of concurrent pollable connections.
#define DSM_MIN_POLLABLE		32


/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/


// Boolean flag indicating if program should continue polling.
int alive = 1;

// Boolean flag indicating if program has started.
int started;

// Process table. Indexed by file-descriptor.
dsm_ptab ptab;

// Pollable file-descriptors.
pollset *pollableSet;

// The operation queue.
dsm_opqueue *opqueue;

// Function map.
dsm_msg_func fmap[MSG_MAX_VALUE];

// Server-socket. Connects to session-server.
int sock_server;

// Listener-socket. Handles local processes.
int sock_listen;

// [EXTERN] Initialization semaphore. 
extern sem_t *sem_start;

// [EXTERN] Shared memory map.
extern dsm_smap *smap;


/*
 *******************************************************************************
 *                            Forward Declarations                             *
 *******************************************************************************
*/


// Sends fd a dsm_msg_done message.
static void send_doneMsg (int fd, dsm_msg_t type, unsigned int nproc);

// Sends basic message 'type' to fd. If fd == -1, sends to all file-descriptors.
static void send_simpleMsg (int fd, dsm_msg_t type);

/******************************************************************************/

// Initializes the global process table.
static void initProcessTable (unsigned int length);

// Resize the process table (at least minLength).
static void resizeProcessTable (unsigned int minLength);

// Deallocates the process table.
static void freeProcessTable (void);

// Registers a new file-descriptor in the process table.
static void registerProcess (int fd, int pid);

// Unregisters a given file-descriptor from the process table.
static void unregisterProcess (int fd);

// Prints the process table.
static void showProcessTable (void);

/******************************************************************************/

// [P->A] Checking-in message from process to arbiter.
static void msg_addProc (int fd, dsm_msg *mp);

// [S->A] Message requesting arbiter stop all processes and send ack.
static void msg_stopAll (int fd, dsm_msg *mp);

// [S->A] Message requesting arbiter continue all stopped processes.
static void msg_contAll (int fd, dsm_msg *mp);

// [S->A] Message requesting arbiter continue all waiting processes.
static void msg_waitDone (int fd, dsm_msg *mp);

// [S->A] Message informing arbiter that a write-operation may now proceed.
static void msg_writeOkay (int fd, dsm_msg *mp);

// [S->A->S] Message from writer with write data. Can be in or out.
static void msg_syncInfo (int fd, dsm_msg *mp);

// [P->A] Message from process requesting write-access.
static void msg_syncRequest (int fd, dsm_msg *mp);

// [P->A] Message from process indicating it is waiting on a barrier.
static void msg_waitBarr (int fd, dsm_msg *mp);

// [P->A->S] Message from process indicating it is terminating.
static void msg_prgmDone (int fd, dsm_msg *mp);

/******************************************************************************/

// Sends signal to 'fd'. If -1 is specified, sends to all fds in ptab.
static void signalProcess (int fd, int signal);

// Contacts daemon with sid, sets session details. Exits fatally on error.
static int getServerSocket (const char *sid, const char *addr, 
	const char *port, unsigned int nproc);

// Accepts incoming connection, and updates the list of pollable descriptors.
static void processConnection (int sock_listen);

// Reads message from fd. Decodes and selects appropriate action.
static void processMessage (int fd);


/*
 *******************************************************************************
 *                         Message Dispatch Functions                          *
 *******************************************************************************
*/


// Sends fd a dsm_msg_done message.
static void send_doneMsg (int fd, dsm_msg_t type, unsigned int nproc) {
	dsm_msg msg;

	// Configure message.
	memset(&msg, 0, sizeof(msg));
	msg.type = type;
	msg.payload.done.nproc = nproc;

	// Send the message.
	dsm_sendall(fd, &msg, sizeof(msg));
}

// Sends basic message 'type' to fd. If fd == -1, sends to all file-descriptors.
static void send_simpleMsg (int fd, dsm_msg_t type) {
	dsm_msg msg;

	// Configure message.
	memset(&msg, 0, sizeof(msg));
	msg.type = type;

	// If fd is non-negative, send just to fd.
	if (fd >= 0) {
		dsm_sendall(fd, &msg, sizeof(msg));
		return;
	}

	// Otherwise, send to all (skip listener + server socket at 0 and 1).
	for (int i = 2; i < pollableSet->fp; i++) {
		dsm_sendall(pollableSet->fds[i].fd, &msg, sizeof(msg));
	}
}


/*
 *******************************************************************************
 *                           Process Table Functions                           *
 *******************************************************************************
*/


// Initializes the global process table.
static void initProcessTable (unsigned int length) {
	
	// Allocate and zero out process array.
	ptab.processes = (dsm_proc *)dsm_zalloc(length * sizeof(dsm_proc));

	// Update length.
	ptab.length = length;
}

// Resize the process table (at least minLength).
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

// Deallocates the process table.
static void freeProcessTable (void) {
	free(ptab.processes);
}

// Registers a new file-descriptor in the process table.
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
		.gid = -1,
		.pid = pid,
		.flags = (dsm_pstate){.is_stopped = 0, .is_waiting = 0, .is_queued = 0}
	};
}


// Unregisters a given file-descriptor from the process table.
static void unregisterProcess (int fd) {

	// Verify location is accessible.
	if (fd < 0 || fd > ptab.length || ptab.processes == NULL) {
		dsm_cpanic("unregisterProcess", "Location inaccessible!");
	}

	memset(ptab.processes + fd, 0, sizeof(dsm_proc));
}

// Prints the process table.
static void showProcessTable (void) {
	int n = 0;
	printf("------------------------------- Process Table ---------------------------------\n");
	printf("\tFD\tGID\tPID\tSTOP\tWAIT\tQUEUE\n");
	for (int i = 0; i < ptab.length; i++) {
		if (ptab.processes[i].pid == 0) {
			continue;
		}
		n++;
		dsm_proc p = ptab.processes[i];
		char s = (p.flags.is_stopped ? 'Y' : 'N');
		char w = (p.flags.is_waiting ? 'Y' : 'N');
		char q = (p.flags.is_queued ? 'Y' : 'N');
		printf("\t%d\t%d\t%d\t%c\t%c\t%c\n", i, p.gid, p.pid, s, w, q);
	}
	printf(" [%d/%d slots occupied]\n", n, ptab.length);
	printf("-------------------------------------------------------------------------------\n");
	fflush(stdout);
}


/*
 *******************************************************************************
 *                          Message Handler Functions                          *
 *******************************************************************************
*/


// [P->A] Checking-in message from process to arbiter.
static void msg_addProc (int fd, dsm_msg *mp) {
	
	// Validate: Process cannot already have entry, or session started.
	if (started == 1 || fd > ptab.length || ptab.processes[fd].pid != 0) {
		dsm_cpanic("msg_addProc", "Received out-of-order/duplicate message!");
	}

	// Register process in the process-table.
	registerProcess(fd, mp->payload.proc.pid);

	// Set the waiting bit: All processes wait before beginning.
	ptab.processes[fd].flags.is_waiting = 1;

	// Forward registration request to the session-server.
	dsm_sendall(sock_server, mp, sizeof(*mp));

	printf("[%d] ADD_PROC: Registered new process!\n", getpid()); fflush(stdout);
}

// [S->A] Message assigning a global ID to a process identified by PID.
static void msg_setgid (int fd, dsm_msg *mp) {

	// Validate: Only server may send this.
	if (fd != sock_server) {
		dsm_cpanic("msg_setgid", "Unauthorized message!");
	}

	// Ensure session hasn't started.
	if (started == 1) {
		dsm_cpanic("msg_setgid", "Received out-of-order message!");
	}

	// Search for and update process in table.
	for (int i = 0; i < ptab.length; i++) {

		// Skip non-matching processes.
		if (ptab.processes[i].pid != mp->payload.proc.pid) {
			continue;
		}
			
		// Set the global identifier.
		ptab.processes[i].gid = mp->payload.proc.gid;

		// Forward message to relevant process.
		dsm_sendall(ptab.processes[i].fd, mp, sizeof(*mp));

		return;
	}

	// Error out: PID not in the table.
	dsm_cpanic("msg_setgid", "Table doesn't contain PID!");
}

// [S->A] Message requesting arbiter stop all processes and send ack.
static void msg_stopAll (int fd, dsm_msg *mp) {
	dsm_proc *p;

	// Validate message. Only server may send this.
	if (fd != sock_server) {
		dsm_cpanic("msg_stopAll", "Unauthorized message!");
	}

	// Stop all processes. Mark as stopped in table.
	for (int i = 0; i < ptab.length; i++) {
		p = ptab.processes + i;

		// Skip unused slots + already stopped processes.
		if (p->pid == 0 || p->flags.is_stopped == 1) {
			continue;
		}

		// Mark stopped.
		p->flags.is_stopped = 1;

		// Signal only if: not-waiting.
		if (p->flags.is_waiting == 0) {
			signalProcess(i, SIGTSTP);
		}
	}

	printf("[%d] STOP_ALL: All processes are stopped!\n", getpid()); fflush(stdout);

	// Send response: nproc = pollable - sock_listen and sock_server
	send_doneMsg(fd, MSG_STOP_DONE, pollableSet->fp - 2);
}

// [S->A] Message requesting arbiter continue all stopped processes.
static void msg_contAll (int fd, dsm_msg *mp) {
	dsm_proc *p;

	// Validate message. Only server may send this.
	if (fd != sock_server) {
		dsm_cpanic("msg_contAll", "Unauthorized message!");
	}

	// Unset the stop bit for all processes in the table.
	for (int i = 0; i < ptab.length; i++) {
		p = ptab.processes + i;

		// Skip unused slots + queued processes.
		if (p->pid == 0 || p->flags.is_queued == 1) {
			continue;
		}

		// Unset stopped bit.
		p->flags.is_stopped = 0;

		// Signal only if: not-waiting.
		if (p->flags.is_waiting == 0) {
			signalProcess(i, SIGCONT);
		}
	}

	printf("[%d] CONT_ALL: Signalled all stopped non-writing proceses!\n", getpid()); fflush(stdout);
}

// [S->A] Message requesting arbiter continue all waiting processes.
static void msg_waitDone (int fd, dsm_msg *mp) {
	dsm_proc *p;

	// Validate message. Only server may send this.
	if (fd != sock_server) {
		dsm_cpanic("msg_waitDone", "Unauthorized message!");
	}

	// If the session has not yet started, start the session!
	if (started == 0) {
		printf("[%d] WAIT_DONE: Received start signal from server!\n", getpid()); fflush(stdout);
		started = 1;

		// Unlink the shared file here. 
		dsm_unlinkSharedFile(DSM_SHM_FILE_NAME);

		// Unlink the initialization-semaphore here.
		dsm_unlinkNamedSem(DSM_SEM_INIT_NAME);

		// Message all processes to start.
		send_simpleMsg(-1, MSG_WAIT_DONE);

		// Return early.
		return;
	}

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
			signalProcess(i, SIGCONT);
		}
	}

	printf("[%d] WAIT_DONE: All waiting processes signalled (unless stopped!)\n", getpid()); fflush(stdout);
}

// [S->A] Message informing arbiter that a write-operation may now proceed.
static void msg_writeOkay (int fd, dsm_msg *mp) {

	// Validate message. Only server may send this.
	if (fd != sock_server) {
		dsm_cpanic("msg_writeOkay", "Unauthorized message!");
	}

	// Ensure that there is a writer queued.
	if (dsm_isOpQueueEmpty(opqueue) == 1) {
		dsm_cpanic("msg_writeOkay", "No writer queued!");
	}

	printf("[%d] WRITE_OKAY: Received and forwarding!\n", getpid());
	
	// Forward message to writer.
	dsm_sendall(dsm_getOpQueueHead(opqueue), mp, sizeof(*mp));
}

// [S->A->S] Message from writer with write data. Can be in or out.
static void msg_syncInfo (int fd, dsm_msg *mp) {
	dsm_msg_sync data;

	// If it's not from the server, forward to the server.
	if (fd != sock_server) {
		printf("[%d] SYNC_INFO: Forwarding syncInfo to server.\n", getpid()); fflush(stdout);
		dsm_sendall(sock_server, mp, sizeof(*mp));

		// Dequeue writer and mark as not-queued.
		dsm_dequeueOpQueue(opqueue);
		ptab.processes[fd].flags.is_queued = 0;

		return;
	}

	// Otherwise: Decode sync data. Prepare to receive.
	data = mp->payload.sync;
	printf("[%d] SYNC_INFO: Waiting for %zu bytes at %ld offset.\n", getpid(), data.size, data.offset); fflush(stdout);

	// HACK: Receive data here and insert it.
	memcpy((void *)smap + smap->data_off, mp->payload.sync.buf, sizeof(int));

	
	// Send acknowledgment to server.
	send_doneMsg(sock_server, MSG_SYNC_DONE, pollableSet->fp - 2);
	printf("[%d] SYNC_INFO: Sending receival ack!\n", getpid());
}

// [P->A] Message from process requesting write-access.
static void msg_syncRequest (int fd, dsm_msg *mp) {
	dsm_proc *p = ptab.processes + fd;

	// Validate message. Only process may issue this.
	if (fd == sock_server) {
		dsm_cpanic("msg_syncRequest", "Unauthorized sychronization message!");
	}

	// Enqueue process as wanting to write.
	dsm_enqueueOpQueue(fd, opqueue);

	// Mark process as: Stopped + Queued.
	p->flags.is_stopped = p->flags.is_queued = 1;

	// Issue request to the server.
	send_simpleMsg(sock_server, MSG_SYNC_REQ);
}

// [P->A] Message from process indicating it is waiting on a barrier.
static void msg_waitBarr (int fd, dsm_msg *mp) {

	// Validate message. Only non-writing process may issue this.
	if (fd == sock_server) {
		dsm_cpanic("msg_waitBarr", "Unauthorized barrier message!");
	}

	// Mark process as waiting.
	ptab.processes[fd].flags.is_waiting = 1;

	// Forward message to session-server. 
	dsm_sendall(sock_server, mp, sizeof(*mp));
}

// [P->A->S] Message from process indicating it is terminating.
static void msg_prgmDone (int fd, dsm_msg *mp) {

	// Validate message. Only process may issue this.
	if (fd == sock_server) {
		dsm_cpanic("msg_prgmDone", "Unauthorized termination message!");
	}

	// Close connection and remove from pollable set.
	close(fd);
	dsm_removePollable(fd, pollableSet);

	// Remove from the process-table.
	unregisterProcess(fd);

	// If no more connections remain, set the termination flag.
	if (pollableSet->fp <= 2) {
		alive = 0;
	}
}


/*
 *******************************************************************************
 *                              Utility Functions                              *
 *******************************************************************************
*/


// Sends signal to 'fd'. If -1 is specified, sends to all fds in ptab.
static void signalProcess (int fd, int signal) {
	int pid = -1;

	// If file-descriptor is a natural number, send to it only.
	if (fd >= 0) {
		
		// Validate file-descriptor.
		if (fd > ptab.length) {
			dsm_cpanic("signalProcess", "Specified file not in table!");
		}

		printf("[%d] BZZT! Sent signal to [%d]!\n", getpid(), ptab.processes[fd].pid); fflush(stdout);
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

		printf("[%d] BZZT! Sent signal to [%d]!\n", getpid(), pid); fflush(stdout);
		// Send the signal.
		if (kill(pid, signal) == -1) {
			dsm_panicf("Couldn't signal process (%d)!", pid);
		}
	}
}

// Contacts daemon with sid, sets session details. Exits fatally on error.
static int getServerSocket (const char *sid, const char *addr, 
	const char *port, unsigned int nproc) {
	dsm_msg msg;
	int s;

	/*
		****************************

	// 1. Construct GET request.
	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_GET_SESSION;
	sprintf(msg.payload.get.sid, "%*s", DSM_SID_SIZE, sid);
	msg.payload.get.nproc = nproc;

	// 2. Connect to session daemon.
	s = dsm_getConnectedSocket(addr, port);
	printf("[%d] Connected to daemon!\n", getpid()); fflush(stdout);

	// 3. Send request.
	dsm_sendall(s, &msg, sizeof(msg));

	// 4. Read reply.
	dsm_recvall(s, &msg, sizeof(msg));

	// 5. Verify reply.
	if (msg.type != DSM_SET_SESSION) {
		dsm_cpanic("getServerSocket", "Unrecognized response!");
	}

	printf("[%d] Received reply from daemon!\n", getpid());
	dsm_showMsg(&msg);

	// 6. Close connection.
	close(s);

		****************************
	*/

	char addrbuf[INET6_ADDRSTRLEN];
	printf("Enter the address of the server: "); fflush(stdout);
	scanf("%s", addrbuf);
	addrbuf[INET6_ADDRSTRLEN - 1] = '\0'; 

	printf("Enter the port of the server: "); fflush(stdout);
	scanf("%u", &(msg.payload.set.port));
	putchar('\n');

	// 7. Connect to session server.
	s = dsm_getConnectedSocket(addrbuf, 
			dsm_portToString(msg.payload.set.port));

	printf("[%d] Connected to server!\n", getpid()); fflush(stdout);

	// 8. Return connected socket.
	return s; 
}

// Accepts incoming connection, and updates the list of pollable descriptors.
static void processConnection (int sock_listen) {
	struct sockaddr_storage newAddr;
	socklen_t newAddrSize = sizeof(newAddr);
	int sock_new;

	// If the session has started. Ignore connection.
	if (started) {
		dsm_warning("Ignoring connection: Session has already started!");
		return;
	}

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

void arbiter (
	const char *sid,		// Session identifier.
	unsigned int nproc,		// Total number of expected processes.
	const char *addr,		// Daemon address.
	const char *port		// Daemon port.
	) {

	int new;								// Count of active file-descriptors.
	struct pollfd *pfd;						// Pointer to poll structure.

	// ------------------------------ Setup ------------------------------------

	// Register functions.
	if (dsm_setMsgFunc(MSG_ADD_PROC, msg_addProc, fmap) 	!= 0 ||
		dsm_setMsgFunc(MSG_SET_GID, msg_setgid, fmap)		!= 0 ||
		dsm_setMsgFunc(MSG_STOP_ALL, msg_stopAll, fmap)		!= 0 ||
		dsm_setMsgFunc(MSG_CONT_ALL, msg_contAll, fmap) 	!= 0 ||
		dsm_setMsgFunc(MSG_WAIT_DONE, msg_waitDone, fmap) 	!= 0 ||
		dsm_setMsgFunc(MSG_WRITE_OKAY, msg_writeOkay, fmap) != 0 ||
		dsm_setMsgFunc(MSG_SYNC_INFO, msg_syncInfo, fmap) 	!= 0 ||
		dsm_setMsgFunc(MSG_SYNC_REQ, msg_syncRequest, fmap) != 0 ||
		dsm_setMsgFunc(MSG_WAIT_BARR, msg_waitBarr, fmap) 	!= 0 ||
		dsm_setMsgFunc(MSG_PRGM_DONE, msg_prgmDone, fmap) 	!= 0) {
		dsm_cpanic("Couldn't set functions", "Unknown!");
	}

	// Initialize process table.
	initProcessTable(DSM_MIN_NPROC);

	// Initialize pollable-set.
	pollableSet = dsm_initPollSet(DSM_MIN_POLLABLE);

	// Initialize operation-queue.
	opqueue = dsm_initOpQueue(DSM_MIN_OPQUEUE_SIZE);

	// Setup server socket.
	sock_server = getServerSocket(sid, addr, port, nproc);

	// Setup listener socket: Designated port.
	sock_listen = dsm_getBoundSocket(AI_PASSIVE, AF_UNSPEC, SOCK_STREAM,
		DSM_DEF_ARB_PORT);

	// Listen on socket.
	if (listen(sock_listen, DSM_DEF_BACKLOG) == -1) {
		dsm_panic("Couldn't listen on socket!");
	}

	// Set listener socket as pollable.
	dsm_setPollable(sock_listen, POLLIN, pollableSet);

	// Set server socket as pollable.
	dsm_setPollable(sock_server, POLLIN, pollableSet);

	// Up the initialization semaphore.
	for (int i = 0; i < nproc; i++) {
		dsm_up(sem_start);
	}

	printf("=================================== ARBITER ===================================\n");
	printf("Listener socket: "); dsm_showSocketInfo(sock_listen);
	printf("sid = %s\n", sid);
	printf("nproc = %u\n", nproc);
	dsm_showPollable(pollableSet);
	dsm_showOpQueue(opqueue);
	showProcessTable();
	printf("===============================================================================\n");
	fflush(stdout);

	// ---------------------------- Main Body -----------------------------------

	while (alive && (new = poll(pollableSet->fds, pollableSet->fp, -1)) != -1) {
		for (int i = 0; i < pollableSet->fp; i++) {
			pfd = pollableSet->fds + i;

			// If nothing to read, ignore file-descriptor.
			if ((pfd->revents & POLLIN) == 0) {
				continue;
			}

			// If listener socket: Process connection.
			if (pfd->fd == sock_listen) {
				processConnection(sock_listen);
				continue;
			}

			// Otherwise: Handle process/server message.
			processMessage(pfd->fd);
		}

		printf("[%d] Arbiter State\n", getpid());
		dsm_showPollable(pollableSet);
		dsm_showOpQueue(opqueue);
		showProcessTable();
		putchar('\n');
		printf("===============================================================================\n");
		fflush(stdout);
	}
	
	
	// ----------------------------- Cleanup ------------------------------------

	// Send disconnect message.
	send_simpleMsg(sock_server, MSG_PRGM_DONE);

	// Disconnect from server.
	close(sock_server);

	// Close listener socket.
	close(sock_listen);

	// Free operation-queue.
	dsm_freeOpQueue(opqueue);

	// Free the pollable set.
	dsm_freePollSet(pollableSet);

	// Free the process table.
	freeProcessTable();

	// Unmap shared memory object.
	if (munmap(smap, smap->size) == -1) {
		dsm_panic("Couldn't unmap shared file!");
	}

	// Exit.
	exit(EXIT_SUCCESS);
}


/*
 *******************************************************************************
 *                                    Main                                     *
 *******************************************************************************
*/


//int main (int argc, const char *argv[]) {
//	arbiter("arethusa", 2, "127.0.0.1", "4200");
//	return 0;
//}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <unistd.h>

#include "dsm_util.h"
#include "dsm_inet.h"
#include "dsm_queue.h"
#include "dsm_msg.h"
#include "dsm_poll.h"

#include "dsm_interface.h"
#include "dsm_interface_ptab.h"
#include "dsm_interface_msg.h"

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
dsm_ptab ptab;

// [A] Pollable file-descriptors.
pollset *pollableSet;

// [A] The operation queue.
dsm_opqueue *opqueue;

// [A] Function map.
dsm_msg_func fmap[MSG_MAX_VALUE];

// [A] Server-socket. Connects to session-server.
int sock_server;

// [A] Listener-socket. Handles local processes.
int sock_listen;


/*
 *******************************************************************************
 *                            Forward Declarations                             *
 *******************************************************************************
*/


// Sends signal to 'fd'. If -1 is specified, sends to all fds in ptab.
static void signalProcess (int fd, int signal);


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

	// Otherwise, send to all (skip listener socket at index 0).
	for (int i = 1; i < pollableSet->fp; i++) {
		dsm_sendall(pollableSet->fds[i].fd, &msg, sizeof(msg));
	}
}

/*
 *******************************************************************************
 *                           Process Table Functions                           *
 *******************************************************************************
*/


#include "dsm_interface_ptab.c"


/*
 *******************************************************************************
 *                          Message Handler Functions                          *
 *******************************************************************************
*/


#include "dsm_interface_msg.c"


/*
 *******************************************************************************
 *                              Utility Functions                              *
 *******************************************************************************
*/


// Sends signal to 'fd'. If -1 is specified, sends to all fds in ptab.
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
	printf("[%d] Connected to daemon!\n", getpid());

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

	printf("Enter the port of the server: ");
	scanf("%u", &(msg.payload.set.port));
	putchar('\n');

	// 7. Connect to session server.
	s = dsm_getConnectedSocket(DSM_LOOPBACK_ADDR, 
			dsm_portToString(msg.payload.set.port));

	printf("[%d] Connected to server!\n", getpid());

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
	if (dsm_setMsgFunc(MSG_STOP_ALL, msg_stopAll, fmap)		!= 0 ||
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
	opqueue = initOpQueue(DSM_MIN_OPQUEUE_SIZE);

	// Setup server socket.
	sock_server = getServerSocket(sid, addr, port, nproc);

	// Setup listener socket: Any port.
	sock_listen = dsm_getBoundSocket(AI_PASSIVE, AF_UNSPEC, SOCK_STREAM, "0");

	// Listen on socket.
	if (listen(sock_listen, DSM_DEF_BACKLOG) == -1) {
		dsm_panic("Couldn't listen on socket!");
	}

	// Set listener socket as pollable.
	dsm_setPollable(sock_listen, POLLIN, pollableSet);

	// Set server socket as pollable.
	dsm_setPollable(sock_server, POLLIN, pollableSet);

	printf("=================== ARBITER ====================\n");
	printf("Listener socket: "); dsm_showSocketInfo(sock_listen);
	printf("sid = %s\n", sid);
	printf("nproc = %u\n", nproc);
	dsm_showPollable(pollableSet);
	showOpQueue(opqueue);
	showProcessTable();
	printf("================================================\n");


	// ---------------------------- Main Body -----------------------------------

	while (alive && (new = poll(pollableSet->fds, pollableSet->fp, -1)) != -1) {
		for (int i = 0; i < pollableSet->fp; i++) {
			pfd = pollableSet->fds + i;

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
		showOpQueue(opqueue);
		showProcessTable();
		putchar('\n');
	}
	
	
	// ----------------------------- Cleanup ------------------------------------

	// Send disconnect message.
	send_doneMsg(sock_server, MSG_PRGM_DONE, );

	// Disconnect from server.
	close(sock_server);

	// Close listener socket.
	close(sock_listen);

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
//void dsm_init (const char *sid, unsigned int nproc, unsigned int port) {
//} 

// Exit shared memory session.
//void dsm_exit (void) {
//}


/*
 *******************************************************************************
 *                                    Main                                     *
 *******************************************************************************
*/


int main (int argc, const char *argv[]) {
	arbiter("arethusa", 2, "127.0.0.1", "4200");

	return 0;
}

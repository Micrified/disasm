#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <sys/poll.h>

#include "dsm_server.h"
#include "dsm_inet.h"
#include "dsm_msg.h"
#include "dsm_util.h"
#include "dsm_poll.h"
#include "dsm_queue.h"

/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/


// Default listener socket backlog
#define DSM_DEF_BACKLOG			32

// Minimum number of concurrent pollable connections.
#define DSM_MIN_POLLABLE		32

// Minimum number of queuable operation requests.
#define DSM_MIN_OPQUEUE_SIZE	32

// Usage format.
#define DSM_ARG_FMT	"-nproc=<nproc> [-sid= <session-id> -addr=<address>"\
					"-port=<port>]"


/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/


// Boolean flag indicating if program should continue polling.
int alive = 1;

// Pollable file-descriptor set.
pollset *pollableSet;

// Function map.
dsm_msg_func fmap[MSG_MAX_VALUE];

// Operation queue (current write state, who wants to write next, etc).
dsm_opqueue *opqueue;

// The total number of participant processes.
unsigned int nproc = -1;

// The number of stopped processes.
unsigned int nproc_stopped;

// The number of waiting processes (barrier).
unsigned int nproc_waiting;

// The number of updated processes.
unsigned int nproc_synced;

// The listener socket.
int sock_listen;


/*
 *******************************************************************************
 *                         Message Dispatch Functions                          *
 *******************************************************************************
*/


// Sends update message to daemon at 'addr' and 'port'. Links own port to SID. 
static void send_setSession (const char *sid, const char *addr, 
	const char *port) {
	int s;
	dsm_msg msg;
		
	// Configure message.
	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_SET_SESSION;
	sprintf(msg.payload.set.sid, "%.*s", DSM_SID_SIZE, sid);

	// Set message port to current port.
	dsm_getSocketInfo(sock_listen, NULL, 0, &msg.payload.set.port);

	printf("[%d] Sending: TYPE = %u, SID = \"%s\", PORT= %u\n", getpid(),
		msg.type, msg.payload.set.sid, msg.payload.set.port);

	// Open socket.
	s = dsm_getConnectedSocket(addr, port);

	// Send message.
	dsm_sendall(s, &msg, sizeof(msg));

	// Close socket.
	close(s);
}

// Sends delete message to daemon at 'addr' and 'port' for given SID.
static void send_delSession (const char *sid, const char *addr, 
	const char *port) {
	int s;
	dsm_msg msg;

	// Configure message.
	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_DEL_SESSION;
	sprintf(msg.payload.del.sid, "%.*s", DSM_SID_SIZE, sid);

	// Open socket.
	s = dsm_getConnectedSocket(addr, port);
	
	// Send message.
	dsm_sendall(s, &msg, sizeof(msg));

	// Close socket.
	close(s);
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
 *                          Message Handler Functions                          *
 *******************************************************************************
*/

// Message requesting write access.
static void msg_syncRequest (int fd, dsm_msg *mp) {

	// Queue request.
	enqueueOperation(fd, opqueue);

	// If no other operation in progress, send stop instruction and set step.
	if (getQueueTail(opqueue) == fd) {

		// Assert current step is STEP_READY.
		if (opqueue->step != STEP_READY) {
			dsm_cpanic("msg_syncRequest", "Inconsistent internal state!");
		}

		// Send message to stop, advance to next step.
		send_simpleMsg(-1, MSG_STOP_ALL);
		opqueue->step = STEP_WAITING_STOP_ACK;
	}
}

// Message indicating given machine has stopped all it's processes.
static void msg_stopDone (int fd, dsm_msg *mp) {
	dsm_msg_done data = mp->payload.done;

	// Verify message is appropriate.
	if (opqueue->step != STEP_WAITING_STOP_ACK) {
		dsm_cpanic("msg_stopDone", "Received out of order message!");
	}

	// If (n-1) processes have stopped: Inform writer, advance to next step.
	printf("[%d] Received MSG_STOP_DONE (%d/%d needed)\n", getpid(), nproc_stopped + data.nproc, nproc - 1);
	if ((nproc_stopped += data.nproc) >= (nproc - 1)) {

		// Ensure writer exists.
		if (isOpQueueEmpty(opqueue)) {
			dsm_cpanic("msg_didStop", "Message to nonexistant writer!");
		}

		// Inform writer, increment step, reset stopped count.
		send_simpleMsg(getQueueTail(opqueue), MSG_WRITE_OKAY);
		opqueue->step = STEP_WAITING_SYNC_INFO;
		nproc_stopped = 0;
	}
}

// Message providing sychronization specifics.
static void msg_syncInfo (int fd, dsm_msg *mp) {

	// Verify message is appropriate.
	if (opqueue->step != STEP_WAITING_SYNC_INFO) {
		dsm_cpanic("msg_syncStart", "Received out of order message!");
	}

	// Verify sender is current writer.
	if (isOpQueueEmpty(opqueue) || getQueueTail(opqueue) != fd) {
		dsm_cpanic("msg_syncStart", "Sender is not current writer!");
	}

	printf("[%d] Received MSG_SYNC_INFO! Forwarding to all others!\n", getpid());

	// Forward to all file-descriptors except writer.
	for (int i = 1; i < pollableSet->fp; i++) {
		if (pollableSet->fds[i].fd != fd) {
			dsm_sendall(pollableSet->fds[i].fd, mp, sizeof(*mp));
		}
	}

	// Set state to next step.
	opqueue->step = STEP_WAITING_SYNC_ACK;
}

// Message indicating data was received.
static void msg_syncDone (int fd, dsm_msg *mp) {
	dsm_msg_done data = mp->payload.done;

	// Verify message is appropriate.
	if (opqueue->step != STEP_WAITING_SYNC_ACK) {
		dsm_cpanic("msg_syncDone", "Received out of order message!");
	}
	
	printf("[%d] Received MSG_SYNC_DONE!\n", getpid());

	// If (n-1) processes have updated. Check queue for new write, or continue.
	if ((nproc_synced += data.nproc) >= (nproc - 1)) {

		// Dequeue completed write-operation.
		dequeueOperation(opqueue);

		// Reset counter.
		nproc_synced = 0;

		// Check if another write is pending. Go to step 2. Inform writer.
		if (!isOpQueueEmpty(opqueue)) {
			printf("[%d] Another operation is waiting, doing it next!\n", getpid());
			opqueue->step = STEP_WAITING_SYNC_INFO;
			send_simpleMsg(getQueueTail(opqueue), MSG_WRITE_OKAY);
			return;
		}

		printf("[%d] Informing all participants to continue!\n", getpid());
		
		// Inform all processes to continue.
		send_simpleMsg(-1, MSG_CONT_ALL);

		// Reset step to ready.
		opqueue->step = STEP_READY;
	}
}

// Message indicating arbiter is waiting on a barrier.
static void msg_waitBarr (int fd, dsm_msg *mp) {
	dsm_msg_barr data = mp->payload.barr;

	// Verify that sender isn't also currently writing (unable).

	// If all processes are waiting, release and reset barrier.
	if ((nproc_waiting += data.nproc) >= nproc) {

		printf("[%d] Releasing barrier!\n", getpid());

		// Release all.
		send_simpleMsg(-1, MSG_WAIT_DONE);

		// Reset barrier.
		nproc_waiting = 0;
	}
}

// Message indicating arbiter is exiting.
static void msg_prgmDone (int fd, dsm_msg *mp) {

	// Close connection, remove from pollable set.
	dsm_removePollable(fd, pollableSet);
	close(fd);

	// If no more connections remain, destroy session.
	alive = (pollableSet->fp > 1);
}


/*
 *******************************************************************************
 *                              Utility Functions                              *
 *******************************************************************************
*/


// Returns length of match if substring is accepted. Otherwise returns zero.
static int acceptSubstring (const char *substr, const char *str) {
	int i;
	for (i = 0; *substr != '\0' && *substr == *str; substr++, str++, i++)
		;
	return i * (*substr == '\0');
}

// Parses arguments and sets pointers. Returns nonzero if full args given.
static int parseArgs (int argc, const char *argv[], const char **sid_p, 
	const char **addr_p, const char **port_p, unsigned int *nproc_p) {
	const char *arg;
	int n;

	// Verify argument count.
	if (argc != 2 && argc != 5) {
		dsm_panicf("Bad arg count (%d). Format is: " DSM_ARG_FMT, argc);
	}

	// Parse arguments.
	while (--argc > 0) {
		arg = *(++argv);

		if (*sid_p == NULL && (n = acceptSubstring("-sid=", arg)) != 0) {
			*sid_p = arg + n;
			continue;
		}

		if (*addr_p == NULL && (n = acceptSubstring("-addr=", arg)) != 0) {
			*addr_p = arg + n;
			continue;
		}

		if (*port_p == NULL && (n = acceptSubstring("-port=", arg)) != 0) {
			*port_p = arg + n;
			continue;
		}

		if (*nproc_p == -1 && (n = acceptSubstring("-nproc=", arg)) != 0) {
			if (sscanf(arg + n, "%u", nproc_p) == 1) {
				continue;
			}
		}

		dsm_panicf("Unknown/duplicate argument: \"%s\". Format is: "
			DSM_ARG_FMT, arg);
	}

	// Ensure -nproc wasn't optional.
	if (*nproc_p == -1) {
		dsm_panicf("Required argument missing \"-nproc=<nproc>\"");
	}

	// Ensure -nproc is >= 2.
	if (*nproc_p < 2) {
		dsm_cpanic("Invalid input!", "-nproc must be >= 2");
	}

	return (*sid_p && *addr_p && *port_p && *nproc_p != -1);
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
	
	printf("[%d] Receiving message...\n", getpid());

	// Read in message: If no connection -> Panic.
	if (dsm_recvall(fd, &msg, sizeof(msg)) != 0) {
		dsm_cpanic("Foreign host closed their socket!", "Imminent deadlock!");
	}

	printf("[%d] Received!\n", getpid());

	// Determine action based on message type.
	if ((action = dsm_getMsgFunc(msg.type, fmap)) == NULL) {
		dsm_warning("No action for message type!");
		dsm_removePollable(fd, pollableSet);
		close(fd);
		return;
	}

	// Execute action.
	action(fd, &msg);
}


/*
 *******************************************************************************
 *                                    Main                                     *
 *******************************************************************************
*/


int main (int argc, const char *argv[]) {
	int new = 0;							// New count (for poll syscall).
	int withDaemon = 0;						// Boolean (should contact daemon?)
	const char *sid = NULL;					// Session-identifier.
	const char *addr = NULL;				// Daemon address.
	const char *port = NULL;				// Daemon port.
	struct pollfd *pfd;						// Pointer to poll structure.
	
	// ------------------------------ Setup -----------------------------------

	// Verify and parse arguments.
	withDaemon = parseArgs(argc, argv, &sid, &addr, &port, &nproc);

	// Set functions.
	if (dsm_setMsgFunc(MSG_SYNC_REQ, msg_syncRequest, fmap) != 0 ||
		dsm_setMsgFunc(MSG_STOP_DONE, msg_stopDone, fmap) != 0 ||
		dsm_setMsgFunc(MSG_SYNC_INFO, msg_syncInfo, fmap) != 0 ||
		dsm_setMsgFunc(MSG_SYNC_DONE, msg_syncDone, fmap) != 0 ||
		dsm_setMsgFunc(MSG_WAIT_BARR, msg_waitBarr, fmap) != 0 ||
		dsm_setMsgFunc(MSG_PRGM_DONE, msg_prgmDone, fmap) != 0) {
		dsm_cpanic("Couldn't set message functions!", "Unknown");
	}

	// Initialize pollable-set.
	pollableSet = dsm_initPollSet(DSM_MIN_POLLABLE);

	// Initialize operation-queue.
	opqueue = initOpQueue(DSM_MIN_OPQUEUE_SIZE);

	// Setup listener socket: Any port.
	sock_listen = dsm_getBoundSocket(AI_PASSIVE, AF_UNSPEC, SOCK_STREAM, "0");

	// Listen on socket.
	if (listen(sock_listen, DSM_DEF_BACKLOG) == -1) {
		dsm_panic("Couldn't listen on socket!");
	}

	// Set listener socket as pollable.
	dsm_setPollable(sock_listen, POLLIN, pollableSet);

	printf("==================== SERVER ====================\n");
	printf("Listener socket: "); dsm_showSocketInfo(sock_listen);
	printf("sid = %s\n", sid);
	printf("addr = %s\n", addr);
	printf("port = %s\n", port);
	printf("nproc = %u\n", nproc);
	printf("================================================\n");

	// ----------------------------- Main Body ----------------------------------
	
	// If daemon details provided, dispatch update message.
	if (withDaemon != 0) {
		send_setSession(sid, addr, port);
	}

	// Poll while no errors and no exit 
	while (alive && (new = poll(pollableSet->fds, pollableSet->fp, -1)) != -1) {
		
		for (int i = 0; i < pollableSet->fp; i++) {
			pfd = pollableSet->fds + i;

			// If nothing to read, ignore file-descriptor.
			if ((pfd->revents & POLLIN) == 0) {
				continue;
			}

			// If listenr socket: Accept connection.
			if (pfd->fd == sock_listen) {
				processConnection(sock_listen);
			} else {
				printf("[%d] New Message!\n", getpid());
				processMessage(pfd->fd);
			}
		}

		printf("[%d] Server State Change:\n", getpid());
		dsm_showPollable(pollableSet);
		showOpQueue(opqueue);
		printf("================================================\n");
		putchar('\n');
	}
	

	// ----------------------------- Clean up ----------------------------------

	printf("[%d] Cleaning up and exiting!\n", getpid());

	// If daemon details provided, dispatch destroy message.
	if (withDaemon != 0) {
		send_delSession(sid, addr, port);
	}

	// Remove listener socket.
	dsm_removePollable(sock_listen, pollableSet);

	// Close listener socket.
	close(sock_listen);

	// Free operation-queue.
	freeOpQueue(opqueue);

	// Free pollable set.
	dsm_freePollSet(pollableSet);

	return 0;
}

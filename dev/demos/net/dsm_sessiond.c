#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#include "dsm_sessiond.h"
#include "dsm_msg.h"
#include "dsm_htab.h"
#include "dsm_inet.h"
#include "dsm_util.h"
#include "dsm_poll.h"


/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/


// Default listener socket backlog.
#define	DSM_DEF_BACKLOG			32

// Minimum number of concurrent pollable connections.
#define	DSM_MIN_POLLABLE		64


/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/


// Pollable file-descriptor set.
pollset *pollableSet;

// Message function map.
dsm_msg_func fmap[MSG_MAX_VALUE];

// The listener socket.
int sock_listen;


/*
 *******************************************************************************
 *                            Forward Declarations                             *
 *******************************************************************************
*/


// Exec's to session server. Supplies address, port, and SID as input.
static void execServer (const char *address, const char *port, const char *sid, 
	int nproc);


/*
 *******************************************************************************
 *                         Message Dispatch Functions                          *
 *******************************************************************************
*/


// Sends a response message to fd informing it to connect to port.
static void send_getSessionReply (int fd, unsigned int port) {
	dsm_msg msg;
	memset(&msg, 0, sizeof(msg));

	// Set type, port.
	msg.type = MSG_SET_SESSION;
	msg.payload.set.port = port;

	// Dispatch.
	dsm_sendall(fd, &msg, sizeof(msg));
}


/*
 *******************************************************************************
 *                          Message Handler Functions                          *
 *******************************************************************************
*/


// Action for session detail request.
static void msg_getSession (int fd, dsm_msg *mp) {
	dsm_session *entry;
	char addr_buf[INET6_ADDRSTRLEN];
	dsm_msg_get data = mp->payload.get;

	printf("[%d] Join Request: \"%s\"\n", getpid(), data.sid);

	// If no entry exists, create one.
	if ((entry = dsm_getTableEntry(data.sid)) == NULL) {
		printf("[%d] No Session: \"%s\", creating!\n", getpid(), data.sid);
		
		// Verify entry could be created.
		if ((entry = dsm_newTableEntry(data.sid, -1, data.nproc)) == NULL) {
			dsm_cpanic("Couldn't create new entry!", "Limit reached?");
		}

		// Verify file-descriptor could be queued for notification.
		if (dsm_enqueueTableEntryFD(fd, entry) != 0) {
			dsm_cpanic("Couldn't queue fd!", "Limit reached?"); 
		}

		// Get current connection details.
		dsm_getSocketInfo(sock_listen, addr_buf, sizeof(addr_buf), NULL);

		// Fork a session server.
		if (fork() == 0) {
			execServer(addr_buf, DSM_DEF_PORT, data.sid, data.nproc);
		}

		return;
	}

	// If entry exists, but unset port. Then queue for notification.
	if (entry->port == -1) {
		printf("[%d] Session but no port. Queueing!\n", getpid());

		// Verify file-descriptor could be queued for notification.
		if (dsm_enqueueTableEntryFD(fd, entry) != 0) {
			dsm_cpanic("Couldn't queue fd!", "Limit reached?");
		}

		return;
	}

	// Otherwise entry exists and has valid port.
	printf("[%d] Session \"%s\" is available. Replying!\n", getpid(), data.sid);
	send_getSessionReply(fd, entry->port);
	dsm_removePollable(fd, pollableSet);
	close(fd);	
}

// Action for session update request.
static void msg_setSession (int fd, dsm_msg *mp) {
	dsm_session *entry;
	int waiting_fd;
	dsm_msg_set data = mp->payload.set;

	// Verify session identifier exists.
	if ((entry = dsm_getTableEntry(data.sid)) == NULL) {
		dsm_cpanic("Bad table entry", "Unknown");
	}

	// Update the port.
	entry->port = data.port;

	printf("[%d] Received check-in from Session Server for \"%s\"\n", getpid(), data.sid);
	// Notify and close queued file-descriptors.
	while (dsm_dequeueTableEntryFD(&waiting_fd, entry) == 0) {
		send_getSessionReply(waiting_fd, entry->port);
		dsm_removePollable(fd, pollableSet);
		close(waiting_fd);
	}

	dsm_removePollable(fd, pollableSet);
	close(fd);
}

// Action for session deletion request.
void msg_delSession (int fd, dsm_msg *mp) {
	dsm_session *entry;
	dsm_msg_del data = mp->payload.del;

	// Verify session identifier exists.
	if ((entry = dsm_getTableEntry(data.sid)) == NULL) {
		dsm_cpanic("No session to delete!", "Unknown");
	}

	// Eject any pending processes (should never happen).
	if (entry->qp > 0) {
		dsm_warning("Session to be destroyed still has pending clients!");
		int waiting_fd;
		
		while (dsm_dequeueTableEntryFD(&waiting_fd, entry) == 0) {
			dsm_removePollable(waiting_fd, pollableSet);
			close(waiting_fd);
		}
	}

	// Remove the entry.
	if (dsm_removeTableEntry(data.sid) != 0) {
		dsm_cpanic("Unable to remove table entry!", "Unknown");
	}

	// Close connection with sender.
	dsm_removePollable(fd, pollableSet);
	close(fd);
}


/*
 *******************************************************************************
 *                              General Functions                              *
 *******************************************************************************
*/

// Exec's to session server. Supplies address, port, and SID as input.
static void execServer (const char *address, const char *port, const char *sid, 
	int nproc) {
	char *argv[5 + 1];						// Argument vector: 4 arg + NULL.
	char *filename = "server";				// Executable filename.
	char buf_sid[5 + DSM_SID_SIZE + 1];		// -sid= + <sid> + \0.
	char buf_addr[6 + INET6_ADDRSTRLEN];	// -addr= + <addr>.
	char buf_port[6 + 6];					// -port= + <port> + \0.
	char buf_nproc[7 + 10 + 1];				// -nproc= + <nproc> + \0.

	// Configure argument buffers.
	sprintf(buf_sid, "-sid=%s", sid);
	sprintf(buf_addr, "-addr=%s", address);
	sprintf(buf_port, "-port=%s", port);
	sprintf(buf_nproc, "-nproc=%d", nproc);

	// Set program arguments.
	argv[0] = filename;
	argv[1] = buf_addr;
	argv[2] = buf_port;
	argv[3] = buf_sid;
	argv[4] = buf_nproc;
	argv[5] = NULL;

	// Exec the session server.
	execve(filename, argv, NULL);

	// Error out. 
	dsm_panic("Execve failed!");
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


// Reads message from fd. Decodes and dispatches response. Then disconnects.
static void processMessage (int fd) {
	dsm_msg msg;
	void (*action)(int, dsm_msg *);
	
	printf("[%d] Receiving message...\n", getpid());

	// Read in message.
	if (dsm_recvall(fd, &msg, sizeof(msg)) != 0) {
		dsm_warning("Other party has closed connection! Closing...");
		dsm_removePollable(fd, pollableSet);
		close(fd);
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
	int new = 0;								// New count.
	struct pollfd *pfd;							// Pointer to poll structure.

	// Initialize pollable set.
	pollableSet = dsm_initPollSet(DSM_MIN_POLLABLE);

	// Register message functions.
	if (dsm_setMsgFunc(MSG_GET_SESSION, msg_getSession, fmap) != 0 ||
		dsm_setMsgFunc(MSG_SET_SESSION, msg_setSession, fmap) != 0 ||
		dsm_setMsgFunc(MSG_DEL_SESSION, msg_delSession, fmap) != 0) {
		dsm_cpanic("Couldn't set message functions!", "Unknown");
	}

	// Get bound socket.
	sock_listen = dsm_getBoundSocket(AI_PASSIVE, AF_UNSPEC, SOCK_STREAM, 
		DSM_DEF_PORT);

	// Listen on socket.
	if (listen(sock_listen, DSM_DEF_BACKLOG) == -1) {
		dsm_panic("Couldn't listen on socket!");
	}

	// Register as a pollable socket.
	dsm_setPollable(sock_listen, POLLIN, pollableSet);

	printf("[%d] Server is ready...\n", getpid());

	// Poll as long as no error occurs.
	while ((new = poll(pollableSet->fds, pollableSet->fp, -1)) != -1) {
		
		for (int i = 0; i < pollableSet->fp; i++) {
			pfd = pollableSet->fds + i;
			
			// If nothing to read, ignore file-descriptor.
			if ((pfd->revents & POLLIN) == 0) {
				continue;
			}
			
			// If listener socket: Accept connection.
			if (pfd->fd == sock_listen) {
				printf("[%d] New Connection!\n", getpid());
				processConnection(sock_listen);
			} else {
				printf("[%d] New Message!\n", getpid());
				processMessage(pfd->fd);
			}
		}

		printf("[%d] New State:\n", getpid());
		dsm_showPollable(pollableSet);
		dsm_showTable();
		putchar('\n');
	}

	// Print error message.
	fprintf(stderr, "Error: Poll failed: \"%s\"\n", strerror(errno));

	// Close listener socket.
	close(sock_listen);

	// Free pollable set.
	dsm_freePollSet(pollableSet);
}
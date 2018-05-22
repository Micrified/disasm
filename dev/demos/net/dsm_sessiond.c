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

#include <sys/poll.h>

#include "dsm_sessiond.h"
#include "dsm_htab.h"


/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/


// Default listener socket backlog.
#define	DSM_DEF_BACKLOG			32

// Maximum number of concurrent pollable connections.
#define	DSM_MAX_POLLABLE		64


/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/


// Pollable file-descriptors.
struct pollfd pollfds[DSM_MAX_POLLABLE];

// Pollable file-descriptor pointer.
unsigned int npollfds;


/*
 *******************************************************************************
 *                              Utility Functions                              *
 *******************************************************************************
*/


// [NON-REENTRANT] Converts given port to a string and returns pointer.
static const char *portToString (unsigned int port) {
	static char b[6];	// Max port is 65536 + one for null-char.
	snprintf(b, 6, "%u", port);
	return b;
}

// [REMOVE] Returns a socket bound to the given port.
static int getBoundSocket (int flags, int family, int socktype, unsigned int port) {
	struct addrinfo hints, *result, *p;
	int s, stat, y = 1;

	// Configure hints.
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = flags;
	hints.ai_family = family;
	hints.ai_socktype = socktype;
	
	printf("[%d] Creating socket to connect to port: \"%s\"\n", getpid(), portToString(port));

	// Lookup available socket.
	if ((stat = getaddrinfo(NULL, portToString(port), &hints, &result)) != 0) {
		fprintf(stderr, "Error: getaddrinfo: \"%s\"\n", gai_strerror(stat));
		exit(EXIT_FAILURE);
	}

	// Bind to first available result.
	for (p = result; p != NULL; p = p->ai_next) {

		// Try initializing a socket.
		if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			fprintf(stderr, "Warning: Bad socket: \"%s\"\n", strerror(errno));
			continue;
		}

		// Prepare port for reuse.
		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &y, sizeof(y)) == -1) {
			fprintf(stderr, "Error: No port reuse: \"%s\"\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

		// Try binding to the socket.
		if (bind(s, p->ai_addr, p->ai_addrlen) == -1) {
			fprintf(stderr, "Error: Couldn't bind: \"%s\"\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		
		break;
	}

	// Free linked list.
	freeaddrinfo(result);

	// Return result.
	return ((p == NULL) ? -1 : s);
}

// Adds or updates fd with events to pollable list. Returns nonzero on error.
int setPollable (int fd, short events) {
	
	// Search to see if fd exists. Update if it does.
	for (int i = 0; i < npollfds; i++) {
		if (pollfds[i].fd == fd) {
			pollfds[i].events = events;
			return 0;
		}
	}

	// Otherwise, check if room for new fd. Return nonzero if not.
	if (npollfds >= DSM_MAX_POLLABLE) {
		return -1;
	}

	// Add fd with events and return zero.
	pollfds[npollfds++] = (struct pollfd) {
		.fd = fd,
		.events = events,
		.revents = 0
	};

	return 0;
}

// Closes and removes fd from pollable list. Remaining sets are shuffled down.
void removePollable (int fd) {
	int i, j;

	// Search for target.
	for (i = 0; (i < npollfds && pollfds[i].fd != fd); i++)
		;

	// Close target.
	close(fd);

	// Overwrite and shuffle.
	for (j = i; j < (npollfds - 1); j++) {
		pollfds[j] = pollfds[j + 1];
	}
	
	// Decrement npollfds.
	--npollfds;
}

// [DEBUG] Prints the value of all pollable file-descriptors.
void showPollable (void) {
	printf("Pollable = [");
	for (int i = 0; i < npollfds; i++) {
		printf("%d", pollfds[i].fd);
		if (i < (npollfds - 1)) {
			putchar(',');
		}
	}
	printf("]\n");
}


// Accepts incoming connection, and updates the list of pollable descriptors.
static void acceptConnection (int sock_listen) {
	struct sockaddr_storage newAddr;
	socklen_t newAddrSize = sizeof(newAddr);
	int sock_new;

	// Try accepting connection.
	if ((sock_new = accept(sock_listen, (struct sockaddr *)&newAddr, &newAddrSize)) == -1) {
		fprintf(stderr, "Error: Couldn't accept: \"%s\"\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Register connection in pollable descriptor list.
	if (setPollable(sock_new, POLLIN) == -1) {
		fprintf(stderr, "Warning: Maximum pollable limit hit!\n");
		close(sock_new);
	}
}

// Sends a join message to fd informing it to connect to port.
static void sendJoinMsg (int fd, unsigned int port) {
	dsm_msg msg;
	memset(&msg, 0, sizeof(msg));
	
	// Set type, port.
	msg.type = 'j';
	msg.port = port;

	// Send message.
	sendall(fd, &msg, sizeof(msg));
}

// Processes a join message.
static void processJoin (int fd, dsm_msg *mp) {
	dsm_session *entry;
	printf("[%d] Join Request for: \"%s\"\n", getpid(), msg->sid);

	// Lookup SID:
	// 1) If no entry exists, create one. 
	// 2) If an entry exists, but no port. Queue fd.
	// 3) If an entry exists with a port, send response and remove.

	// 1.
	if ((entry = dsm_getTableEntry(mp->sid)) == NULL) {
	
		printf("[%d] SID doesn't exist. Creating!\n", getpid());

		// Create new entry with unset port.
		if ((entry = dsm_newTableEntry(mp->sid, -1)) == NULL) {
			fprintf(stderr, "Error: Failed to create new entry!\n");
			exit(EXIT_FAILURE);
		}

		// Enqueue file-descriptor.
		if (dsm_enqueueTableEntryFD(fd, entry) == -1) {
			fprintf(stderr, "Error: Couldn't enqueue fd!\n");
			exit(EXIT_FAILURE);
		} 

		// Fork the session server.
		if (fork() == 0) {
			// TODO: execSessionServer(sp->sid, DSM_DEFAULT_PORT);
		}

		// Exit early.
		return;
	}

	// 2.
	if (entry->port == -1) {

		printf("[%d] SID exists, but being queued!\n", getpid());

		// Enqueue file-descriptor.
		if (dsm_enqueueTableEntryFD(fd, entry) == -1) {
			fprintf(stderr, "Error: Couldn't enqueue fd!\n");
			exit(EXIT_FAILURE);
		}

		return;
	}

	printf("[%d] SID exists, sending data!\n", getpid());
	
	// 3. Send response and close connection.
	sendJoinMsg(fd, entry->port);

	// Remove fd and close connection.
	removePollable(fd);
	close(fd);
}

// Process an update message.
static void processUpdate (dsm_msg *mp) {
	dsm_session *entry;
	int fd;

	// If identifier does not exist, error out.
	if ((entry = dsm_getTableEntry(mp->sid)) == NULL) {
		fprintf(stderr, "Error: Expected entry to exist!\n");
		exit(EXIT_FAILURE);
	}

	// Update the port.
	entry->port = mp->port;

	// For all queued file-descriptors. Send response and close.
	while (dsm_dequeueTableEntryFD(&fd, entry) == 0) {
		sendJoinMsg(fd, entry->port);
		removePollable(fd);
		close(fd);
	}	
}

// Reads message from fd. Decodes and dispatches response. Then disconnects.
static void processRequest (int fd) {
	dsm_msg msg;

	// Read in message.
	recvall(fd, &msg, sizeof(msg));

	// Choose action based on type.
	switch (msg.type) {
		case 'j':
			processJoin(fd, &msg);
			break;

		case 'u':
			processUpdate(&msg);
			break;

		default: {
			fprintf(stderr, "Warning: Malformed message!\n");
			removePollable(fd);
		}
	}
}

/*
 *******************************************************************************
 *                                    Main                                     *
 *******************************************************************************
*/

// Ensures 'size' data is transmitted to fd. Exits fatally on error.
void sendall (int fd, void *b, size_t size) {
	size_t sent = 0;
	int n;

	do {
		if ((n = send(fd, b + sent, size - sent, 0)) == -1) {
			fprintf(stderr, "Error: Bad send: \"%s\"\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		sent += n;
	} while (sent < size);
}

// Ensures 'size' data is received from fd. Exits fatally on error.
void recvall (int fd, void *b, size_t size) {
	size_t received = 0;
	int n;

	do {
		if ((n = recv(fd, b + received, size - received, 0)) == -1) {
			fprintf(stderr, "Error: Bad recv: \"%s\"\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		received += n;
	} while (received < size);
}

/*
 *******************************************************************************
 *                                    Main                                     *
 *******************************************************************************
*/


int main (int argc, const char *argv[]) {
	int sock_listen, new = 0;					// Listener socket, new count.
	struct pollfd *pfd;							// Pointer to poll structure.

	// Get bound socket.
	sock_listen = getBoundSocket(AI_PASSIVE, AF_UNSPEC, SOCK_STREAM, 
		DSM_DEF_PORT);

	// Listen on socket.
	if (listen(sock_listen, DSM_DEF_BACKLOG) == -1) {
		fprintf(stderr, "Error: Bad listen: \"%s\"\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Register as a pollable socket.
	setPollable(sock_listen, POLLIN);

	printf("[%d] Server is ready...\n", getpid());

	// Poll as long as no error occurs.
	while ((new = poll(pollfds, npollfds, -1)) != -1) {
		
		for (int i = 0; i < npollfds; i++) {
			pfd = pollfds + i;
			
			// If listener socket: Accept connection.
			if (pfd->fd == sock_listen) {
				printf("[%d] Activity on listener socket!\n", getpid());
				acceptConnection(sock_listen);
			} else {
				printf("[%d] Activity on connection!\n", getpid());
				processRequest(pfd->fd);
			}
		}

		printf("[%d] New State:\n", getpid());
		showPollable();
		dsm_showTable();
		putchar('\n');
	}

	// Print error message.
	fprintf(stderr, "Error: Poll failed: \"%s\"\n", strerror(errno));

	// Close listener socket.
	close(sock_listen);
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#include "dsm_htab.h"
#include "dsm_sessiond.h"

/*
 *******************************************************************************
 *                            Function Definitions                             *
 *******************************************************************************
*/

static char namebuf[INET6_ADDRSTRLEN];

// Get string describing given addrinfo object.
const char *addrinfoToString (struct addrinfo *ap, char *b) {
	void *addr;

	// Assign address.
	if (ap->ai_family == AF_INET) {
		struct sockaddr_in *ipv4 = (struct sockaddr_in *)ap->ai_addr;
		addr = &(ipv4->sin_addr);
	} else {
		struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)ap->ai_addr;
		addr = &(ipv6->sin6_addr);
	}

	// Perform conversion.
	if (inet_ntop(ap->ai_family, addr, b, INET6_ADDRSTRLEN) == NULL) {
		fprintf(stderr, "Error: Couldn't convert: \"%s\"\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	return b;
}

// Returns a socket connected to the server.
int getStreamSocket (const char *server_addr, const char *server_port) {
	int status, s;
	struct addrinfo hints, *rp;

	// Setup hints: Don't set ai_flags because we will specify IP.
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	// Perform request for socket info.
	if ((status = getaddrinfo(server_addr, server_port, &hints, &rp)) != 0) {
		fprintf(stderr, "Error: \"%s\"\n", gai_strerror(status));
		exit(EXIT_FAILURE);
	}

	// Try to create socket based on results of lookup.
	if ((s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) == -1) {
		fprintf(stderr, "Error: No socket: \"%s\"\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Connect socket to destination.
	if (connect(s, rp->ai_addr, rp->ai_addrlen) == -1) {
		fprintf(stderr, "Error: No connection: \"%s\"\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Determine the destination name.
	addrinfoToString(rp, namebuf);

	// Free linked list of results.
	freeaddrinfo(rp);

	return s;
}

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


int main (void) {
	const char *server_addr = "192.168.2.28";
	const char *server_port = "4200";
	const char *sid = "nancy";
	dsm_msg msg;

	// Open TCP connection to server.
	int s = getStreamSocket(server_addr, server_port);

	printf("Connected to %s on port %s!\n", namebuf, server_port);

	// Design a payload.
	memset(&msg, 0, sizeof(msg));
	msg.type = 'j';
	memcpy(msg.data.sid, sid, DSM_SID_SIZE);

	printf("Payload is ready to go:\n");
	printf("msg.type = %c\n", msg.type);
	printf("msg.data.sid = \"%s\"\n", msg.data.sid);
	printf("(size = %zu)\n", sizeof(msg));

	char proceed;
	printf("Hit enter to proceed: ");
	scanf("%c", &proceed);

	// Send the payload.
	sendall(s, &msg, sizeof(msg));

	printf("Sent the payload...\n");

	// Received the response.
	recvall(s, &msg, sizeof(msg));

	printf("Received a response!\n");

	// Print the response.
	printf("Should connect to port: %u\n", msg.data.port);

	// Close the socket.
	close(s);

	return 0;
}
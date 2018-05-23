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

#include "dsm_inet.h"
#include "dsm_util.h"


/*
 *******************************************************************************
 *                            Function Definitions                             *
 *******************************************************************************
*/


// [NON-REENTRANT] Converts port (< 99999) to string. Returns pointer.
const char *dsm_portToString (unsigned int port) {
	static char b[6]; // Five digits for 65536 + one for null character.
	snprintf(b, 6, "%u", port);
	return b;
}

// Returns string describing address of addrinfo struct. Returns NULL on error.
const char *dsm_addrinfoToString (struct addrinfo *ap, char *b) {
	void *addr;

	// If IPv4, use sin_addr. Otherwise use sin6_addr for IPv6.
	if (ap->ai_family == AF_INET) {
		struct sockaddr_in *ipv4 = (struct sockaddr_in *)ap->ai_addr;
		addr = &(ipv4->sin_addr);
	} else {
		struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)ap->ai_addr;
		addr = &(ipv6->sin6_addr);
	}

	// Convert format to string: Buffer must be INET6_ADDRSTRLEN large.
	return inet_ntop(ap->ai_family, addr, b, INET6_ADDRSTRLEN);
}

// Returns a socket bound to the given port. Exits fatally on error.
int dsm_getBoundSocket (int flags, int family, int socktype, const char *port) {
	struct addrinfo hints, *res, *p;
	int s, stat, y = 1;

	// Configure hints.
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = flags;
	hints.ai_family = family;
	hints.ai_socktype = socktype;

	// Lookup connection options.
	if ((stat = getaddrinfo(NULL, port, &hints, &res)) != 0) {
		dsm_cpanic("getaddrinfo", gai_strerror(stat));
	}

	// Bind to first suitable result.
	for (p = res; p != NULL; p = p->ai_next) {

		// Try initializing a socket.
		if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			dsm_warning("Socket init failed on getaddrinfo result!");
			continue;
		}

		// Try to reuse port.
		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &y, sizeof(y)) == -1) {
			dsm_panic("Couldn't reuse port!");
		}

		// Try binding to the socket.
		if (bind(s, p->ai_addr, p->ai_addrlen) == -1) {
			dsm_panic("Couldn't bind to port!");
		}

		break;
	}

	// Free linked-list of results.
	freeaddrinfo(res);

	// Return result.
	return ((p == NULL) ? -1 : s);	
}

// Ensures 'size' data is sent to fd. Exits fatally on error.
void dsm_sendall (int fd, void *b, size_t size) {
	size_t sent = 0;
	int n;

	do {
		if ((n = send(fd, b + sent, size - sent, 0)) == -1) {
			dsm_panic("Syscall error on send!");
		}
		sent += n;
	} while (sent < size);
}

// Ensures 'size' data is received from fd. Exits fatally on error.
void dsm_recvall (int fd, void *b, size_t size) {
	size_t received = 0;
	int n;

	do {
		if ((n = recv(fd, b + received, size - received, 0)) == -1) {
			dsm_panic("Syscall error on recv!");
		}
		received += n;
	} while (received < size);
}

#include <string.h>
#include "dsm_inet.h"

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

/*
 *******************************************************************************
 *                                      x                                      *
 *******************************************************************************
*/

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
	if ((stat = getaddrinfo(NULL, port, &hints &res)) != 0) {
		fprintf(stderr, "Error: getaddrinfo: \"%s\"\n", gai_strerror(stat));
		exit(EXIT_FAILURE);
	}

	// Bind to first suitable result.
	for (p = res; p != NULL; p = p->ai_next) {

		// Try initializing a socket.
		if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			fprintf(stderr, "Warning: Bad info: \"%s\"\n", strerror(errno));
			continue;
		}

		// Try to reuse port.
		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &y, sizeof(y)) == -1) {
			fprintf(stderr, "Error: Port busy: \"%s\"\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

		// Try binding to the socket.
		if (bind(s, p->ai_addr, p->ai_addrlen) == -1) {
			fprintf(stderr, "Error: Bad bind: \"%s\"\n", strerror(errno));
			exit(EXIT_FAILURE);
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

}

// Ensures 'size' data is received from fd. Exits fatally on error.
void dsm_recvall (int fd, void *b, size_t size) {

}

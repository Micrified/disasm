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

/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/

// Server socket port.
#define PORT				"1991"

// Maximum message size.
#define MAX_MSG				64


/*
 *******************************************************************************
 *                                  Routines                                   *
 *******************************************************************************
*/

// Given a sockaddr_storage type, it prints the address. NON-REENTRANT
const char *addrToString (struct addrinfo *info_p) {
	void *src;
	static char dest[INET6_ADDRSTRLEN];

	// Determine the type.
	if (info_p->ai_family == AF_INET) {
		struct sockaddr_in *in = (struct sockaddr_in *)info_p;
		src = &(in->sin_addr);
	} else {
		struct sockaddr_in6 *in = (struct sockaddr_in6 *)info_p;
		src = &(in->sin6_addr);
	}

	// Attempt to fill the buffer.
	if (inet_ntop(info_p->ai_family, src, dest, sizeof(dest)) == NULL) {
		fprintf(stderr, "Error: Couldn't convert: \"%s\"\n", strerror(errno));
		return NULL;
	}

	return dest;
}

// Returns a socket connected to the first result matching args. Or -1 on error.
int getConnectedSocket (int ai_flags, int ai_family, int ai_socktype) {
	struct addrinfo hints, *result, *p;
	int s, status;

	// Configure hints.
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = ai_flags;
	hints.ai_family = ai_family;
	hints.ai_socktype = ai_socktype;

	// Lookup results.
	if ((status = getaddrinfo(NULL, PORT, &hints, &result)) != 0) {
		fprintf(stderr, "Error: getaddrinfo: \"%s\"\n", gai_strerror(status));
		exit(EXIT_FAILURE);
	}

	// Connect to first available socket.
	for (p = result; p != NULL; p = p->ai_next) {

		// Print message.
		printf("Trying %s on port %s\n", addrToString(p), PORT);

		// Try initializing a socket.
		if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			fprintf(stderr, "Warning: Socket creation failed...\n");
			continue;
		}

		// Try connecting to the socket.
		if (connect(s, p->ai_addr, p->ai_addrlen) == -1) {
			fprintf(stderr, "Error: Couldn't connect: \"%s\"\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

		break;

	}

	// Free linked list.
	freeaddrinfo(result);

	// Return socket, or -1.
	return (p == NULL ? -1 : s);
}


/*
 *******************************************************************************
 *                                    Main                                     *
 *******************************************************************************
*/

int main (int argc, const char *argv[]) {
	int s, put, got;
	char msg[MAX_MSG];

	// Get connected socket.
	if ((s = getConnectedSocket(AI_PASSIVE, AF_UNSPEC, SOCK_STREAM)) == -1) {
		fprintf(stderr, "Error: No connection was possible!\n");
		exit(EXIT_FAILURE);
	}

	printf("Connected!\n");

	// Begin session.
	do {

		// Receive message.
		got = recv(s, msg, MAX_MSG, 0);

		// Print result.
		printf("> \"%s\"\n", msg);


		// Print dialogue, scan message.
		printf("\ndsm :: ");
		scanf("%s", msg);

		// Cap message.
		msg[MAX_MSG - 1] = '\0';

		// Send message.
		put = send(s, msg, MAX_MSG, 0);

	} while (put == MAX_MSG && got == MAX_MSG);

	// Close.
	close(s);

	return 0; 
}

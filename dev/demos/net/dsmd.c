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


// Listening socket port.
#define PORT				"1991"

// Listening socket backlog.
#define BACKLOG				10

// Maximum message size.
#define MAX_MSG				64

// Maximum hostname size.
#define MAX_HOSTNAME		256


/*
 *******************************************************************************
 *                                  Routines                                   *
 *******************************************************************************
*/

// Handler: SIGCHLD.
void handler_sigchld (int signal) {
	int old_errno = errno;

	while (waitpid(-1, NULL, WNOHANG) > 0);

	errno = old_errno;
}

// Sets a handler for the given signal.
void setHandler (int signal, void (*f)(int)) {
	struct sigaction sa;
	
	// Zero out the mask.
	if (sigemptyset(&sa.sa_mask) == -1) {
		fprintf(stderr, "Error: Couldn't reset mask: \"%s\"\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Set flags, handler.
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = f;

	// Set the action.
	if (sigaction(signal, &sa, NULL) == -1) {
		fprintf(stderr, "Error: Couldn't set action: \"%s\"\n", strerror(errno));
	}
}

// Given a sockaddr_storage type, it prints the address. NON-REENTRANT
const char *addrToString (struct sockaddr_storage *storage_p) {
	void *src;
	static char dest[INET6_ADDRSTRLEN];

	// Determine the type.
	if (storage_p->ss_family == AF_INET) {
		struct sockaddr_in *in = (struct sockaddr_in *)storage_p;
		src = &(in->sin_addr);
	} else {
		struct sockaddr_in6 *in = (struct sockaddr_in6 *)storage_p;
		src = &(in->sin6_addr);
	}

	// Attempt to fill the buffer.
	if (inet_ntop(storage_p->ss_family, src, dest, sizeof(dest)) == NULL) {
		fprintf(stderr, "Error: Couldn't convert: \"%s\"\n", strerror(errno));
		return NULL;
	}

	return dest;
}

// Returns socket information with requested attributes. Returns NULL on error.
int getBoundSocket (int ai_flags, int ai_family, int ai_socktype) {
	struct addrinfo hints, *result, *p;
	int s, status, yes = 1;

	// Configure hints.
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = ai_flags;
	hints.ai_family = ai_family;
	hints.ai_socktype = ai_socktype;

	// Lookup bind-able socket for accepting connections.
	if ((status = getaddrinfo(NULL, PORT, &hints, &result)) != 0) {
		fprintf(stderr, "Error: Bad lookup: \"%s\"\n", gai_strerror(status));
		exit(EXIT_FAILURE);
	}

	// Bind to first available result.
	for (p = result; p != NULL; p = p->ai_next) {

		// Try initializing a socket.
		if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			fprintf(stderr, "Warning: Socket creation failed...\n");
			continue;
		}

		// Prepare port for reuse.
		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
			fprintf(stderr, "Error: Couldn't reuse: \"%s\"\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

		// Attempt to bind socket.
		if (bind(s, p->ai_addr, p->ai_addrlen) == -1) {
			fprintf(stderr, "Error: Failed to bind: \"%s\"\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

		break;

	}

	// Free linked list.
	freeaddrinfo(result);

	// Return -1 if none found, otherwise descriptor.
	return (p == NULL ? -1 : s);
}


// Fork of main program. Communicates with socket s.
void deferTo (int s, struct sockaddr_storage *storage_p, size_t size) {
	int put, got;
	struct sockaddr_storage storage;
	char msg[MAX_MSG];

	// Copy in connection information: Not required but done for comfort.
	memcpy(&storage, storage_p, size);
	
	// Print message.
	printf("[%d] New Fork serving %s\n", getpid(), addrToString(&storage));

	// Execute session.
	snprintf(msg, MAX_MSG, "Type \"q\" to quit. Or receive echos!");
	do {

		// Send the message.
		put = send(s, msg, MAX_MSG, 0);

		// Receive one.
		got = recv(s, msg, MAX_MSG, 0);


	} while (put == MAX_MSG && got == MAX_MSG && strcmp("q", msg) != 0);
	
	// Close session socket.
	close(s);

	exit(EXIT_SUCCESS);
}

/*
 *******************************************************************************
 *                                    Main                                     *
 *******************************************************************************
*/


int main (int argc, const char *argv[]) {
	int s, in;
	struct sockaddr_storage storage;
	socklen_t storageSize = sizeof(storage);

	// Register to child-events.
	setHandler(SIGCHLD, handler_sigchld);

	// Create socket bound to PORT.
	if ((s = getBoundSocket(AI_PASSIVE, AF_UNSPEC, SOCK_STREAM)) == -1) {
		fprintf(stderr, "Error: No socket could be bound!\n");
		exit(EXIT_FAILURE);
	}

	// Listen for incoming connections.
	if (listen(s, BACKLOG) == -1) {
		fprintf(stderr, "Error: Unable to listen: \"%s\"\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	printf("[%d] Up and listening...\n", getpid());

	// For all incoming connections: Fork and close respective fds.
	while ((in = accept(s, (struct sockaddr *)&storage, &storageSize)) != -1) {
		printf("[%d] Accepted connection. Forking!\n", getpid());
		if (fork() == 0) {
			close(s);
			deferTo(in, &storage, storageSize);
		} else {
			close(in);
		}
	}
	
	// Something went wrong.
	fprintf(stderr, "Error: Couldn't accept: \"%s\"\n", strerror(errno));
	exit(EXIT_FAILURE);

	return 0;
}
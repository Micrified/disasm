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


/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/


// Default listener socket backlog
#define DSM_DEF_BACKLOG			32

// Minimum number of concurrent pollable connections.
#define DSM_MIN_POLLABLE		32

// Usage format.
#define DSM_ARG_FMT	"[-sid= <session-id> -addr=<address> -port=<port>]"


/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/


// Pollable file-descriptor set.
pollset *pollableSet;

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
	msg.type = SET_SESSION;
	sprintf(msg.sid, "%.*s", DSM_SID_SIZE, sid);

	// Set message port to current port.
	dsm_getSocketInfo(sock_listen, NULL, 0, &msg.port);

	printf("[%d] Sending: TYPE = %u, SID = \"%s\", PORT= %u\n", getpid(), msg.type,
		msg.sid, msg.port); 

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
	msg.type = DEL_SESSION;
	sprintf(msg.sid, "%.*s", DSM_SID_SIZE, sid);

	// Open socket.
	s = dsm_getConnectedSocket(addr, port);
	
	// Send message.
	dsm_sendall(s, &msg, sizeof(msg));

	// Close socket.
	close(s);
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
	const char **addr_p, const char **port_p) {
	const char *arg;
	int n;

	// Verify argument count.
	if (argc != 1 && argc != 4) {
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

		dsm_panicf("Unknown/duplicate argument: \"%s\". Format is: "
			DSM_ARG_FMT, arg);
	}

	return (*sid_p != NULL && *addr_p != NULL && *port_p != NULL);
}


/*
 *******************************************************************************
 *                                    Main                                     *
 *******************************************************************************
*/


int main (int argc, const char *argv[]) {
	int withDaemon = 0;						// Boolean (should contact daemon?)
	const char *sid = NULL;					// Session-identifier.
	const char *addr = NULL;				// Daemon address.
	const char *port = NULL;				// Daemon port.

	// Verify and parse arguments.
	withDaemon = parseArgs(argc, argv, &sid, &addr, &port);

	// Initialize pollable-set.
	pollableSet = dsm_initPollSet(DSM_MIN_POLLABLE);

	// Setup listener socket: Any port.
	sock_listen = dsm_getBoundSocket(AI_PASSIVE, AF_UNSPEC, SOCK_STREAM, "0");

	dsm_showSocketInfo(sock_listen);

	// Listen on socket.
	if (listen(sock_listen, DSM_DEF_BACKLOG) == -1) {
		dsm_panic("Couldn't listen on socket!");
	}

	// Set listener socket as pollable.
	dsm_setPollable(sock_listen, POLLIN, pollableSet);

	dsm_showPollable(pollableSet);
	
	// If daemon details provided, dispatch update message.
	if (withDaemon != 0) {
		send_setSession(sid, addr, port);
	}

	char c;
	printf("Would you like to destroy the session now?\n");
	scanf("%c", &c);

	if (withDaemon != 0) {
		send_delSession(sid, addr, port);
	}

	// Remove listener socket.
	dsm_removePollable(sock_listen, pollableSet);

	// Close listener socket.
	close(sock_listen);

	// Free pollable set.
	dsm_freePollSet(pollableSet);

	return 0;
}

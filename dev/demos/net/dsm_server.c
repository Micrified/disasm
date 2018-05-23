#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "dsm_server.h"
#include "dsm_inet.h"
#include "dsm_msg.h"
#include "dsm_util.h"


/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/


#define DSM_ARG_FMT	"[-sid= <session-id> -addr=<address> -port=<port>]"


/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/


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


/*
 *******************************************************************************
 *                                    Main                                     *
 *******************************************************************************
*/


int main (int argc, const char *argv[]) {
	int n, withDaemon = 0;
 	const char *arg;						// Argument pointer.
	const char *sid = NULL;					// Session-identifier.
	const char *addr = NULL;				// Daemon address.
	const char *port = NULL;				// Daemon port.
	
	// Verify argument count.
	if (argc != 1 && argc != 4) {
		dsm_panicf("Bad arg count (%d). Format is: " DSM_ARG_FMT, argc);
	}

	// Parse arguments.
	while (--argc > 0) {
		arg = *(++argv);

		if (sid == NULL && (n = acceptSubstring("-sid=", arg)) != 0) {
			sid = arg + n;
			continue;
		}

		if (addr == NULL && (n = acceptSubstring("-addr=", arg)) != 0) {
			addr = arg + n;
			continue;
		}

		if (port == NULL && (n = acceptSubstring("-port=", arg)) != 0) {
			port = arg + n;
			continue;
		}

		dsm_panicf("Unknown/duplicate argument: \"%s\". Format is: "
			DSM_ARG_FMT, arg);
	}

	// Set withDaemon flag.
	withDaemon = (sid != NULL && addr != NULL && port != NULL);

	// Setup listener socket: Any port.
	sock_listen = dsm_getBoundSocket(AI_PASSIVE, AF_UNSPEC, SOCK_STREAM, "0");
	
	// If daemon details provided, dispatch update message.
	if (withDaemon != 0) {
		send_setSession(sid, addr, port);
	}

	dsm_showSocketInfo(sock_listen);

	char c;
	printf("Would you like to destroy the session now?\n");
	scanf("%c", &c);

	if (withDaemon != 0) {
		send_delSession(sid, addr, port);
	}

	return 0;
}

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

#include "dsm_sessiond.h"
#include "dsm_inet.h"
#include "dsm_msg.h"

/*
 *******************************************************************************
 *                            Function Definitions                             *
 *******************************************************************************
*/

int main (void) {
	const char *server_addr = "192.168.2.28";
	const char *server_port = "4200";
	const char *sid = "arethusa";
	dsm_msg msg;

	// Open TCP connection to server.
	int s = dsm_getConnectedSocket(server_addr, server_port);

	dsm_showSocketInfo(s);;

	// Design a payload.
	memset(&msg, 0, sizeof(msg));
	msg.type = GET_SESSION;
	sprintf(msg.sid, "%.*s", DSM_SID_SIZE, sid);

	printf("Payload is ready to go:\n");
	printf("msg.type = %d\n", msg.type);
	printf("msg.sid = \"%s\"\n", msg.sid);
	printf("(size = %zu)\n", sizeof(msg));

	char proceed;
	printf("Hit enter to proceed: ");
	scanf("%c", &proceed);

	// Send the payload.
	dsm_sendall(s, &msg, sizeof(msg));

	printf("Sent the payload...\n");

	// Received the response.
	dsm_recvall(s, &msg, sizeof(msg));

	printf("Received a response!\n");

	// Print the response.
	printf("Should connect to port: %u\n", msg.port);

	// Close the socket.
	close(s);

	return 0;
}
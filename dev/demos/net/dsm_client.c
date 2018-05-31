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

#include "dsm_daemon.h"
#include "dsm_inet.h"
#include "dsm_msg.h"
#include "dsm_util.h"
#include "dsm_htab.h"

/*
 *******************************************************************************
 *                               Message Sending                               *
 *******************************************************************************
*/

// Configures message payload depending on the type.
void configMsg (int i, dsm_msg *mp) {
	memset(mp, 0, sizeof(*mp));
	switch (i) {
		case 0: {
			mp->type = MSG_INIT_DONE;
			mp->payload.done.nproc = 1;
			break;
		}
		case 1: {
			mp->type = MSG_SYNC_REQ;
			break;
		}
		case 2: {
			mp->type = MSG_STOP_DONE;
			mp->payload.done.nproc = 1;
			break;
		}
		case 3: {
			mp->type = MSG_SYNC_DONE;
			mp->payload.done.nproc = 1;
			break;
		}
		case 4: {
			mp->type = MSG_PRGM_DONE;
			break;
		}
		case 5: {
			mp->type = MSG_SYNC_INFO;
			mp->payload.sync.offset = 32;
			mp->payload.sync.size = 4;
			break;
		}
		case 6: {
			mp->type = MSG_WAIT_BARR;
			mp->payload.barr.nproc = 1;
			break;
		}
		case 7: {
			mp->type = MSG_GET_SESSION;
			printf("SID: ");
			scanf("%s", mp->payload.get.sid);
			mp->payload.get.sid[DSM_SID_SIZE] = '\0';
			printf("NPROC: ");
			scanf("%u", &(mp->payload.get.nproc));
			printf("Done!\n");
			break;
		}
		case 8: {
			mp->type = MSG_ADD_PROC;
			printf("PID: ");
			scanf("%d", &(mp->payload.proc.pid));
			break;
		}
		default: {
			dsm_cpanic("Unknown input", "?");
		}
	}
}

// Dispatches a message of type 'i' to the given connection 's'.
void sendMsg (int i, int s) {
	dsm_msg msg;

	// Configure the message.
	configMsg(i, &msg);

	// Print message to be sent.
	printf("Sending...\n");
	dsm_showMsg(&msg);

	// Send message.
	dsm_sendall(s, &msg, sizeof(msg));
}


/*
 *******************************************************************************
 *                          General Utility Functions                          *
 *******************************************************************************
*/

// Receives and displays a message.
void getReply (int s) {
	dsm_msg msg;

	// Receive message.
	dsm_recvall(s, &msg, sizeof(msg));

	printf("Received a reply:\n");
	dsm_showMsg(&msg);
}

// Returns hash of user selection.
unsigned int getInput (void) {
	unsigned int input = 0;

	do {
		printf("Select the message to send:\n");

		printf("\tSERVER-REQUESTS\n");
		printf("\t\"0\" = Initialized.\n");
		printf("\t\"1\" = Request to write.\n");
		printf("\t\"2\" = Have stopped.\n");
		printf("\t\"3\" = Sync done.\n");
		printf("\t\"4\" = Exiting.\n");
		printf("\t\"5\" = Sync info (dummy).\n");
		printf("\t\"6\" = Wait on barrier.\n");

		printf("\tDAEMON-REQUESTS\n");
		printf("\t\"7\" = Get session.\n");
		printf("\tARBITER-REQUESTS\n");
		printf("\t\"8\" = Add Process.\n");
		printf("\tOTHER\n");
		printf("\n\t\"9\" = Read Response.\n");
		printf("Input: ");
		scanf("%u", &input);
	} while (input > 10);

	return input;
}

int main (int argc, const char *argv[]) {
	const char *server_addr = NULL;
	const char *server_port = NULL;

	if (argc != 3) {
		printf("Usage: ./client <address> <port>\n");
		exit(EXIT_FAILURE);
	} else {
		server_addr = *(++argv);
		server_port = *(++argv);
	}

	// Open TCP connection to server.
	int s = dsm_getConnectedSocket(server_addr, server_port);

	dsm_showSocketInfo(s);

	while (1) {
		unsigned int i = getInput();

		if (i == 9) {
			getReply(s);
		} else {
			sendMsg(i, s);
		}

	}

	// Close the socket.
	close(s);

	return 0;
}
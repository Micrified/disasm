#include <stdio.h>

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


// Returns length of match if substring is accepted. Otherwise returns zero.
int acceptSubstring (const char *substr, const char *str) {
	int i;
	for (i = 0; *substr != '\0' && *substr == *str; substr++, str++, i++)
		;
	return i * (*substr == '\0');
}

int main (int argc, const char *argv[]) {
	int n;
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

	printf("sid = \"%s\"\n", sid);
	printf("addr = \"%s\"\n", addr);
	printf("port = \"%s\"\n", port);

	// Attempt to contact daemon if needed.
	return 0;
}

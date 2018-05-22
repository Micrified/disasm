#if !defined(DSM_SESSIOND_H)
#define DSM_SESSIOND_H

#include "dsm_htab.h"


/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/


// Default listening port.
#define DSM_DEF_PORT			4200


/*
 *******************************************************************************
 *                              Type Definitions                               *
 *******************************************************************************
*/


// Structure describing message format.
typedef struct dsm_msg {
	char type;								// Type byte.
	char sid[DSM_SID_SIZE + 1];				// Session identifier.
	unsigned int port;						// Port.
} dsm_msg;


/*
 *******************************************************************************
 *                            Function Declarations                            *
 *******************************************************************************
*/


// Ensures 'size' data is transmitted to fd. Exits fatally on error.
void sendall (int fd, void *b, size_t size);

// Ensures 'size' data is received from fd. Exits fatally on error.
void recvall (int fd, void *b, size_t size);

#endif
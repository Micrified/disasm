#if !defined(DSM_MSG_H)
#define DSM_MSG_H

#include "dsm_htab.h"


/*
 *******************************************************************************
 *                              Type Definitions                               *
 *******************************************************************************
*/


// Enumeration describing message type.
typedef enum {
	MIN_MSG_VAL = 0,
	GET_SESSION,
	SET_SESSION,
	DEL_SESSION,
	MAX_MSG_VAL = 256
} dsm_msg_t;


// Structure describing message format.
typedef struct dsm_msg {
	dsm_msg_t type;						// Type.
	char sid[DSM_SID_SIZE + 1];			// Session identifier.
	unsigned int port;					// Port.
} dsm_msg;


/*
 *******************************************************************************
 *                            Function Declarations                            *
 *******************************************************************************
*/


// Links function to given message type. Returns nonzero on error.
int dsm_setMsgFunction (dsm_msg_t type, void (*f)(int, dsm_msg *));

// Returns function linked with given message type. Returns NULL on error.
void (*dsm_getMsgFunction (dsm_msg_t type))(int, dsm_msg *);


#endif
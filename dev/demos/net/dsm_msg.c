#include <stdlib.h>
#include "dsm_msg.h"


/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/


// Function Table.
void (*fmap[256])(int, dsm_msg *);


/*
 *******************************************************************************
 *                            Function Definitions                             *
 *******************************************************************************
*/


// Links function to given message type. Returns nonzero on error.
int dsm_setMsgFunction (dsm_msg_t type, void (*f)(int, dsm_msg *)) {

	// Verify value in range.
	if (type < 0 || type > 256) {
		return -1;
	}

	// Install function pointer in table.
	fmap[type] = f;

	return 0;
}

// Returns function linked with given message type. Returns NULL on error.
void (*dsm_getMsgFunction (dsm_msg_t type))(int, dsm_msg *) {

	// Verify value in range.
	if (type < 0 || type > 256) {
		return NULL;
	}

	// Return function pointer in table.
	return fmap[type];
}
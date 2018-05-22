#include "dsm_msg.h"


/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/


// Function Table.
void (*f)(int, dsm_msg *) fmap[256];


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
}

// Returns function linked with given message type. Returns NULL on error.
void (*)(int, dsm_msg *) dsm_getMsgFunction (dsm_msg_t type) {

	// Verify value in range.
	if (type < - || type > 256) {
		return -1;
	}

	// Return function pointer in table.
	return fmap[type];
}
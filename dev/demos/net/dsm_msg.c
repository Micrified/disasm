#include <stdlib.h>
#include "dsm_msg.h"

/*
 *******************************************************************************
 *                            Function Definitions                             *
 *******************************************************************************
*/

// Links function to given message type. Returns nonzero on error.
int dsm_setMsgFunc (dsm_msg_t type, dsm_msg_func func, dsm_msg_func *fmap) {
	
	// Verify message type.
	if (type < MSG_MIN_VALUE || type > MSG_MAX_VALUE) {
		return -1;
	}

	// Install function pointer.
	fmap[type] = func;

	return 0;
}

// Returns function for given message type. Returns NULL on error.
dsm_msg_func dsm_getMsgFunc (dsm_msg_t type, dsm_msg_func *fmap) {

	// Verify message type.
	if (type < MSG_MIN_VALUE || type > MSG_MAX_VALUE) {
		return NULL;
	}

	// Return function pointer.
	return fmap[type];
}

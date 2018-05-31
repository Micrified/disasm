#include <stdio.h>
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

// Prints the contents of a message, depending on it's type.
void dsm_showMsg(dsm_msg *mp) {
	printf("---- MSG ----\n");
	switch (mp->type) {
		case MSG_GET_SESSION: {
			printf("TYPE: MSG_GET_SESSION\n");
			printf("SID: \"%s\"\n", mp->payload.get.sid);
			printf("NPROC: %u\n", mp->payload.get.nproc);
			break; 
		}
		case MSG_SET_SESSION: {
			printf("TYPE: MSG_SET_SESSION\n");
			printf("SID: \"%s\"\n", mp->payload.set.sid);
			printf("PORT: %u\n", mp->payload.set.port);
			break; 			
		}
		case MSG_DEL_SESSION: {
			printf("TYPE: MSG_DEL_SESSION\n");
			printf("SID: \"%s\"\n", mp->payload.del.sid);
			break;
		}
		case MSG_STOP_ALL: {
			printf("TYPE: MSG_STOP_ALL\n");
			break;
		}
		case MSG_CONT_ALL: {
			printf("TYPE: MSG_CONT_ALL\n");
			break;
		}
		case MSG_WAIT_DONE: {
			printf("TYPE: MSG_WAIT_DONE\n");
			break;
		}
		case MSG_WRITE_OKAY: {
			printf("TYPE: MSG_WRITE_OKAY\n");
			break;
		}
		case MSG_INIT_DONE: {
			printf("TYPE: MSG_INIT_DONE\n");
			printf("NPROC: %u\n", mp->payload.done.nproc);
		}
		case MSG_SYNC_REQ: {
			printf("TYPE: MSG_SYNC_REQ\n");
			break;
		}
		case MSG_SYNC_INFO: {
			printf("TYPE: MSG_SYNC_INFO\n");
			printf("OFFSET: %ld\n", mp->payload.sync.offset);
			printf("SIZE: %zu\n", mp->payload.sync.size);
			break;
		}
		case MSG_STOP_DONE: {
			printf("TYPE: MSG_STOP_DONE\n");
			printf("NPROC: %u\n", mp->payload.done.nproc);
			break;
		}
		case MSG_SYNC_DONE: {
			printf("TYPE: MSG_SYNC_DONE\n");
			printf("NPROC: %u\n", mp->payload.done.nproc);
			break;
		}
		case MSG_WAIT_BARR: {
			printf("TYPE: MSG_WAIT_BARR\n");
			printf("NPROC: %u\n", mp->payload.barr.nproc);
			break;
		}
		case MSG_PRGM_DONE: {
			printf("TYPE: MSG_PRGM_DONE\n");
			break;
		}
		case MSG_ADD_PROC: {
			printf("TYPE: MSG_ADD_PROC\n");
			printf("PID: %d\n", mp->payload.proc.pid);
			break;
		}
		default:
			printf("TYPE: UNKNOWN\n");
			break;
	}
	printf("-------------\n");
}

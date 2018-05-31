#if !defined(DSM_MSG_H)
#define DSM_MSG_H

#include <sys/types.h>

#include "dsm_htab.h"


/*
 *******************************************************************************
 *                              Type Definitions                               *
 *******************************************************************************
*/


// TYPE: All message types. (A = arbiter, S = server, D = daemon, P = process).
typedef enum {
	MSG_MIN_VALUE = 0,

	MSG_GET_SESSION,					// [A->D] Request for session info.
	MSG_SET_SESSION,					// [S->D] Update as session owner.
	MSG_DEL_SESSION,					// [S->D] Request session deletion.

	MSG_STOP_ALL,						// [S->A] Arbiter must suspend proc's.
	MSG_CONT_ALL,						// [S->A] Arbiter may resume proc's.
	MSG_WAIT_DONE,						// [S->A] Arbiter can release barrier.
	MSG_WRITE_OKAY,						// [S->A] Arbiter may write.

	MSG_INIT_DONE,						// [A->S] Arbiter is ready to start.
	MSG_SYNC_REQ,						// [A->S] Arbiter asks for write perms.
	MSG_SYNC_INFO,						// [A->S] Arbiter synchronization info.
	MSG_STOP_DONE,						// [A->S] Confirms all proc's paused.
	MSG_SYNC_DONE,						// [A->S] Confirms received all data.
	MSG_WAIT_BARR,						// [A->S] Arbiter is waiting on barrier.
	MSG_PRGM_DONE,						// [A->S] Arbiter is exiting.

	MSG_ADD_PROC,						// [P->A] Arbiter adds a process.

	MSG_MAX_VALUE
} dsm_msg_t;

// [Not all message types require additional data. See those that do below].

// MSG_GET_SESSION: Initialization message payload.
typedef struct dsm_msg_get {
	char sid[DSM_SID_SIZE + 1];			// Session identifier.
	int nproc;							// Number of expected processes.
} dsm_msg_get;

// MSG_SET_SESSION: Setup message payload.
typedef struct dsm_msg_set {
	char sid[DSM_SID_SIZE + 1];			// Session identifier.
	unsigned int port;					// Port.
} dsm_msg_set;

// MSG_DEL_SESSION: Delete session payload.
typedef struct dsm_msg_del {
	char sid[DSM_SID_SIZE + 1];			// Session identifier.
} dsm_msg_del;

// MSG_SYNC_INFO: Sychronization message payload.
typedef struct dsm_msg_sync {
	off_t offset;						// Data offset.
	size_t size;						// Data size.
} dsm_msg_sync;

// MSG_SYNC_DONE + MSG_STOP_DONE: Data receival ack and stop ack.
typedef struct dsm_msg_done {
	unsigned int nproc;
} dsm_msg_done;

// MSG_WAIT_BARR: Reached barrier message.
typedef dsm_msg_done dsm_msg_barr;

// MSG_ADD_PROC: Add a process to an arbiter.
typedef struct dsm_msg_proc {
	int pid;
} dsm_msg_proc;

// UNION: Aggregate describing various message payloads.
typedef union dsm_msg_payload {
	dsm_msg_get get;
	dsm_msg_set set;
	dsm_msg_del del;
	dsm_msg_sync sync;
	dsm_msg_done done;
	dsm_msg_barr barr;
	dsm_msg_proc proc;
} dsm_msg_payload;

// Structure describing message format.
typedef struct dsm_msg {
	dsm_msg_t type;						// Type.
	dsm_msg_payload payload;			// Optional payload.
} dsm_msg;


// Type representing a message action function.
typedef void (*dsm_msg_func) (int, dsm_msg *);


/*
 *******************************************************************************
 *                            Function Declarations                            *
 *******************************************************************************
*/


// Links function to given message type. Returns nonzero on error.
int dsm_setMsgFunc (dsm_msg_t type, dsm_msg_func func, dsm_msg_func *fmap);

// Returns function for given message type. Returns NULL on error.
dsm_msg_func dsm_getMsgFunc (dsm_msg_t type, dsm_msg_func *fmap);

// Prints the contents of a message, depending on it's type.
void dsm_showMsg (dsm_msg *mp);



#endif
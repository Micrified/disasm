#if !defined(DSM_SERVER_H)
#define DSM_SERVER_H


/*
 *******************************************************************************
 *                              Type Definitions                               *
 *******************************************************************************
*/


// Enumeration of server states.
typedef enum dsm_syncStep {
	STEP_READY = 0,					// No write request is pending. All normal.
	STEP_WAITING_STOP_ACK,			// Waiting for stop ack from all arbiters.
	STEP_WAITING_SYNC_INFO,			// Waiting for write data information.
	STEP_WAITING_SYNC_ACK			// Waiting for data received acks.
} dsm_syncStep;

// Describes the current server state, and contains queued write-requests.
typedef struct dsm_opqueue {
	dsm_syncStep step;
	int *queue;
	size_t queueSize;
	unsigned int head, tail;
} dsm_opqueue;


#endif
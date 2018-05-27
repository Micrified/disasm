#if !defined(DSM_HTAB_H)
#define DSM_HTAB_H

/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/

// Size of a session ID.
#define DSM_SID_SIZE							32

// Maximum number of file-descriptors that can be queued.
#define DSM_MAX_SESSION_QUEUE					64


/*
 *******************************************************************************
 *                              Type Declarations                              *
 *******************************************************************************
*/


typedef struct dsm_session {
	char sid[DSM_SID_SIZE + 1];			// Session identifier.
	int queue[DSM_MAX_SESSION_QUEUE];	// Queue of waiting file-descriptors.
	unsigned int qp;					// Queue pointer.
	int port;							// Session port.
	int nproc;							// Number of expected processes.
	struct dsm_session *next;			// Linked session.
} dsm_session;


/*
 *******************************************************************************
 *                            Function Declarations                            *
 *******************************************************************************
*/


// [DEBUG] Prints the hash table to stdout.
void dsm_showTable (void);

// Performs a lookup on a session ID. Returns NULL if no entry was found.
dsm_session *dsm_getTableEntry (const char *sid);

// Creates table entry with session information. Returns NULL on error.
dsm_session *dsm_newTableEntry (const char *sid, int port, int nproc);

// Removes the table entry for the given process SID. Returns nonzero on error.
int dsm_removeTableEntry (const char *sid);

// Flushes all table entries.
void dsm_flushTable (void);

// Queue's file-descriptor to given session. Returns nonzero on error.
int dsm_enqueueTableEntryFD (int fd, dsm_session *sp);

// Dequeue's file-descriptor from given session. Returns nonzero on error.
int dsm_dequeueTableEntryFD (int *fd, dsm_session *sp);


#endif
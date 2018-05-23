#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dsm_htab.h"

/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/

// Hash table bucket count.
#define DSM_TAB_SIZE			16


/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/

// Hash Table.
dsm_session *table[DSM_TAB_SIZE];


/*
 *******************************************************************************
 *                        Internal Function Definitions                        *
 *******************************************************************************
*/


// Daniel J. Bernstein's hashing function. General Purpose.
static unsigned int DJBHash (const char *s, size_t length) {
	unsigned int hash = 5381;
	unsigned int i = 0;

	for (i = 0; i < length; ++i, ++s) {
		hash = ((hash << 5) + hash) + (*s);
	}

	return hash;
}

// [ALLOC] Creates a new dsm_session instance. Returns pointer.
static dsm_session *newTableEntry (const char *sid, int port, 
	dsm_session *next) {
	dsm_session *entry;

	// Allocate the entry.
	if ((entry = malloc(sizeof(dsm_session))) == NULL) {
		fprintf(stderr, "Error: Couldn't allocate dsm_session!\n");
		exit(EXIT_FAILURE);
	}

	// Set fields.
	memcpy(entry->sid, sid, DSM_SID_SIZE);
	entry->sid[DSM_SID_SIZE] = '\0';
	entry->qp = 0;
	entry->port = port;
	entry->next = next;

	// Return pointer.
	return entry;
}

// [DEBUG] Prints a linked list of dsm_session objects to stdout.
static void printList (dsm_session *p) {
	while (p != NULL) {
		printf("[\"%s\" | %d | {", p->sid, p->port);
		for (int i = 0; i < p->qp; i++) {
			printf("%d", p->queue[i]);
			if (i < (p->qp - 1)) {
				putchar(',');
			}
		}
		printf("} -> ");
		p = p->next;
	}
	printf("NULL\n");
}

// Recursively free's a linked list of dsm_session objects.
static void freeList (dsm_session *p) {
	if (p == NULL) {
		return;
	}
	freeList(p->next);
	free(p);
}

// Removes list item containing the offending SID value. Returns head pointer.
static dsm_session *removeWithSID (dsm_session *p, const char *sid) {
	dsm_session *ret;

	// If NULL, return.
	if (p == NULL) {
		return p;
	}

	// If target, deallocate and return.
	if (strcmp(sid, p->sid) == 0) {
		ret = p->next;
		free(p);
		return ret;
	}

	// Otherwise search next and return current.
	p->next = removeWithSID(p->next, sid);
	return p;
}


/*
 *******************************************************************************
 *                            Function Definitions                             *
 *******************************************************************************
*/

// [DEBUG] Prints the hash table to stdout.
void dsm_showTable (void) {
	printf("------------ TABLE ------------\n");
	for (unsigned int i = 0; i < DSM_TAB_SIZE; i++) {
		if (table[i] != NULL) {
			printf("%u\t", i);
			printList(table[i]);
		}
	}
	printf("-------------------------------\n");
}

// Performs a lookup on a session ID. Returns NULL if no entry was found.
dsm_session *dsm_getTableEntry (const char *sid) {
	dsm_session *entry;
	unsigned int i;

	// Compute index.
	i = (DJBHash(sid, DSM_SID_SIZE) % DSM_TAB_SIZE);

	// Locate entry.
	for (entry = table[i]; entry != NULL; entry = entry->next) {
		if (strcmp(entry->sid, sid) == 0) {
			break;
		}
	}

	return entry;
}

// Creates table entry with session information. Returns NULL on error.
dsm_session *dsm_newTableEntry (const char *sid, int port) {
	unsigned int i;

	// Compute index.
	i = (DJBHash(sid, DSM_SID_SIZE) % DSM_TAB_SIZE);

	// Set head of list to new entry.
	table[i] = newTableEntry(sid, port, table[i]);

	// Return pointer to entry.
	return table[i];
}

// Removes the table entry for the given process SID. Returns nonzero on error.
int dsm_removeTableEntry (const char *sid) {
	unsigned int i;

	// Compute index.
	i = (DJBHash(sid, DSM_SID_SIZE) % DSM_TAB_SIZE);

	// Remove the entry.
	table[i] = removeWithSID(table[i], sid);

	return 0;
}

// Flushes all table entries.
void dsm_flushTable (void) {
	for (unsigned int i = 0; i < DSM_TAB_SIZE; i++) {
		freeList(table[i]);
	}
}

// Queue's file-descriptor to given session. Returns nonzero on error.
int dsm_enqueueTableEntryFD (int fd, dsm_session *sp) {

	// Verify argument and size.
	if (sp == NULL || sp->qp >= DSM_MAX_SESSION_QUEUE) {
		return -1;
	}

	// Enqueue.
	sp->queue[sp->qp++] = fd;

	return 0;
}

// Dequeue's file-descriptor from given session. Returns nonzero on error.
int dsm_dequeueTableEntryFD (int *fd, dsm_session *sp) {
	
	// Verify arguments and size.
	if (fd == NULL || sp == NULL || sp->qp <= 0) {
		return -1;
	}

	// Dequeue.
	*fd = sp->queue[--(sp->qp)];

	return 0;
}

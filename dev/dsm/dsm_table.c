#include <stdio.h>
#include <unistd.h>
#include "dsm_table.h"
#include "dsm_util.h"


/*
 *******************************************************************************
 *                            Function Definitions                             *
 *******************************************************************************
*/


// Initializes a table at given address. Requires DSM_TAB_SIZE bytes of room.
void dsm_initTable (dsm_table *tp, size_t obj_size) {

	// Verify pointer.
	if (tp == NULL) {
		dsm_panic("Table initialized with NULL pointer!");
	}

	// Initialize the semaphore to inter-process sharing mode.
	if (sem_init(&(tp->sem_lock), 1, 1) == -1) {
		dsm_panic("Couldn't initialize table semaphore!");
	}

	// Assign the object-size.
	tp->obj_size = obj_size;

	// Assign data region offset.
	tp->data_off = DSM_TAB_SIZE;
}

// [DEBUG] [ATOMIC] Prints a table to standard out.
void dsm_showTable (dsm_table *tp) {
	
	// Verify pointer.
	if (tp == NULL) {
		dsm_panic("Cannot print NULL table pointer!");
	}
	
	// Acquire access.
	dsm_down(&(tp->sem_lock));
	printf("======== [%d] ========\n", getpid());
	printf("obj_size = %zu\n", tp->obj_size);
	printf("data_off = %zu\n", tp->data_off);
	printf("========================\n");
	dsm_up(&(tp->sem_lock));
}
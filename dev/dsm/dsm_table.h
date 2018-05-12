#if !defined(DSM_TABLE_H)
#define DSM_TABLE_H

#include <semaphore.h>


/*
 *******************************************************************************
 *                             Symbolic Constants                              *
 *******************************************************************************
*/

#define DSM_TAB_SIZE		sizeof(dsm_table)


/*
 *******************************************************************************
 *                            Data Type Definitions                            *
 *******************************************************************************
*/


typedef struct dsm_table {
	sem_t sem_lock;				// Access control semaphore.
	size_t obj_size;			// Total size of the mapped file.
	off_t data_off;				// Offset from pointer to data region.
} dsm_table;


/*
 *******************************************************************************
 *                            Function Declarations                            *
 *******************************************************************************
*/


// Initializes a table at given address. Requires DSM_TAB_SIZE bytes of room.
void dsm_initTable (dsm_table *tp, size_t obj_size);

// [DEBUG] [ATOMIC] Prints a table to standard out.
void dsm_showTable (dsm_table *tp);


#endif
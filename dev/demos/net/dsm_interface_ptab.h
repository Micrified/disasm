#if !defined(DSM_INTERFACE_PTAB_H)
#define DSM_INTERFACE_PTAB_H


/*
 *******************************************************************************
 *                            Function Declarations                            *
 *******************************************************************************
*/


// Initializes the global process table.
static void initProcessTable (unsigned int length);

// Resize the process table (at least minLength).
static void resizeProcessTable (unsigned int minLength);

// Deallocates the process table.
static void freeProcessTable (void);

// Registers a new file-descriptor in the process table.
static void registerProcess (int fd, int pid);

// Unregisters a given file-descriptor from the process table.
static void unregisterProcess (int fd);

// Prints the process table.
static void showProcessTable (void);


#endif
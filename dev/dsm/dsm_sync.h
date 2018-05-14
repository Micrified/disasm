#if !defined(DSM_SYNC_H)
#define DSM_SYNC_H

#include <signal.h>
#include "dsm_table.h"


/*
 *******************************************************************************
 *                        Global Variable Declarations                         *
 *******************************************************************************
*/


// External reference to the shared object. dsm_manager.c:42
extern dsm_table *shared_obj;

// External reference to the private object: dsm_manager.c:45
extern dsm_table *private_obj;


/*
 *******************************************************************************
 *                            Function Declarations                            *
 *******************************************************************************
*/

// Initializes the decoder tables necessary for use in the sync handlers.
void dsm_sync_init (void);

// Handler: Sychronization action for SIGSEGV.
void dsm_sync_sigsegv (int signal, siginfo_t *info, void *ucontext);

// Handler: Sychronization action for SIGILL
void dsm_sync_sigill (int signal, siginfo_t *info, void *ucontext);

// Handler: Sychronization action for SIGCONT.
void dsm_sync_sigcont (int signal, siginfo_t *info, void *ucontext);

// [DEBUG] Handler: Prints on SIGTSTP.
void dsm_sync_sigtstp (int signal, siginfo_t *info, void *ucontext);

#endif
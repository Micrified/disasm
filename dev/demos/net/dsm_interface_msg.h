#if !defined(DSM_INTERFACE_MSG_H)
#define DSM_INTERFACE_MSG_H

#include "dsm_msg.h"

/*
 *******************************************************************************
 *                            Function Declarations                            *
 *******************************************************************************
*/


// [S->A] Message requesting arbiter stop all processes and send ack.
static void msg_stopAll (int fd, dsm_msg *mp);

// [S->A] Message requesting arbiter continue all stopped processes.
static void msg_contAll (int fd, dsm_msg *mp);

// [S->A] Message requesting arbiter continue all waiting processes.
static void msg_waitDone (int fd, dsm_msg *mp);

// [S->A] Message informing arbiter that a write-operation may now proceed.
static void msg_writeOkay (int fd, dsm_msg *mp);

// [S->A->S] Message from writer with write data. Can be in or out.
static void msg_syncInfo (int fd, dsm_msg *mp);

// [P->A] Message from process requesting write-access.
static void msg_syncRequest (int fd, dsm_msg *mp);

// [P->A] Message from process indicating it is waiting on a barrier.
static void msg_waitBarr (int fd, dsm_msg *mp);

// [P->A->S] Message from process indicating it is terminating.
static void msg_prgmDone (int fd, dsm_msg *mp);


#endif

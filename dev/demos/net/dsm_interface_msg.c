#include "dsm_interface_msg.h"


/*
 *******************************************************************************
 *                            Function Definitions                             *
 *******************************************************************************
*/


// [S->A] Message requesting arbiter stop all processes and send ack.
static void msg_stopAll (int fd, dsm_msg *mp) {
	dsm_proc *p;

	// Validate message. Only server may send this.
	if (fd != sock_server) {
		dsm_cpanic("msg_stopAll", "Unauthorized message!");
	}

	// Stop all processes. Mark as stopped in table.
	for (int i = 0; i < ptab.length; i++) {
		p = ptab.processes + i;

		// Skip unused slots + already stopped processes.
		if (p->pid == 0 || p->flags.is_stopped == 1) {
			continue;
		}

		// Mark stopped.
		p->flags.is_stopped = 1;

		// Signal only if: not-waiting.
		if (p->flags.is_waiting == 0) {
			signalProcess(p->pid, SIGTSTP);
		}
	}

	printf("[%d] STOP_ALL: All processes are stopped!\n", getpid());

	// Send response: nproc = pollable - sock_listen and sock_server
	send_doneMsg(fd, MSG_STOP_DONE, pollableSet->fp - 2);
}

// [S->A] Message requesting arbiter continue all stopped processes.
static void msg_contAll (int fd, dsm_msg *mp) {
	dsm_proc *p;

	// Validate message. Only server may send this.
	if (fd != sock_server) {
		dsm_cpanic("msg_contAll", "Unauthorized message!");
	}

	// Unset the stop bit for all processes in the table.
	for (int i = 0; i < ptab.length; i++) {
		p = ptab.processes + i;

		// Skip unused slots + queued processes.
		if (p->pid == 0 || p->flags.is_queued == 1) {
			continue;
		}

		// Unset stopped bit.
		p->flags.is_stopped = 0;

		// Signal only if: not-waiting.
		if (p->flags.is_waiting == 0) {
			signalProcess(p->pid, SIGCONT);
		}
	}

	printf("[%d] CONT_ALL: Signalled all stopped non-writing proceses!\n", getpid());
}

// [S->A] Message requesting arbiter continue all waiting processes.
static void msg_waitDone (int fd, dsm_msg *mp) {
	dsm_proc *p;

	// Validate message. Only server may send this.
	if (fd != sock_server) {
		dsm_cpanic("msg_waitDone", "Unauthorized message!");
	}

	// Unset the waiting bit for all processes in the table.
	for (int i = 0; i < ptab.length; i++) {
		p = ptab.processes + i;

		// Skip unused slots.
		if (p->pid == 0) {
			continue;
		}

		// Unset wait-bit.
		p->flags.is_waiting = 0;

		// Signal only if: not-stopped (shouldn't occur).
		if (p->flags.is_stopped == 0) {
			signalProcess(p->pid, SIGCONT);
		}
	}

	printf("[%d] WAIT_DONE: All waiting processes signalled (unless stopped!)\n", getpid());
}

// [S->A] Message informing arbiter that a write-operation may now proceed.
static void msg_writeOkay (int fd, dsm_msg *mp) {

	// Validate message. Only server may send this.
	if (fd != sock_server) {
		dsm_cpanic("msg_writeOkay", "Unauthorized message!");
	}

	// Ensure that there is a writer queued.
	if (isOpQueueEmpty(opqueue) == 1) {
		dsm_cpanic("msg_writeOkay", "No writer queued!");
	}

	printf("[%d] WRITE_OKAY: Received and forwarding!\n", getpid());
	
	// Forward message to writer.
	dsm_sendall(getQueueTail(opqueue), mp, sizeof(*mp));
}

// [S->A->S] Message from writer with write data. Can be in or out.
static void msg_syncInfo (int fd, dsm_msg *mp) {
	dsm_msg_sync data;

	// If it's not from the server, forward to the server.
	if (fd != sock_server) {
		printf("[%d] SYNC_INFO: Forwarding syncInfo to server.\n", getpid());
		dsm_sendall(sock_server, mp, sizeof(*mp));

		// Dequeue writer and mark as not-queued.
		dequeueOperation(opqueue);
		ptab.processes[fd].flags.is_queued = 0;

		return;
	}

	// Otherwise: Decode sync data. Prepare to receive.
	data = mp->payload.sync;
	printf("[%d] SYNC_INFO: Waiting for %zu bytes at %lld offset.\n", getpid(), data.size, data.offset);

	// TODO: RECEIVE DATA HERE.

	
	// Send acknowledgment to server.
	send_doneMsg(sock_server, MSG_SYNC_DONE, pollableSet->fp - 2);
	printf("[%d] SYNC_INFO: Sending receival ack!\n", getpid());
}

// [P->A] Message from process requesting write-access.
static void msg_syncRequest (int fd, dsm_msg *mp) {
	dsm_proc *p = ptab.processes + fd;

	// Validate message. Only process may issue this.
	if (fd == sock_server) {
		dsm_cpanic("msg_syncRequest", "Unauthorized sychronization message!");
	}

	// Enqueue process as wanting to write.
	enqueueOperation(fd, opqueue);

	// Mark process as: Stopped + Queued.
	p->flags.is_stopped = p->flags.is_queued = 1;

	// Issue request to the server.
	send_simpleMsg(sock_server, MSG_SYNC_REQ);
}

// [P->A] Message from process indicating it is waiting on a barrier.
static void msg_waitBarr (int fd, dsm_msg *mp) {

	// Validate message. Only non-writing process may issue this.
	if (fd == sock_server) {
		dsm_cpanic("msg_waitBarr", "Unauthorized barrier message!");
	}

	// Mark process as waiting.
	ptab.processes[fd].flags.is_waiting = 1;

	// Forward message to session-server. 
	dsm_sendall(sock_server, mp, sizeof(*mp));
}

// [P->A->S] Message from process indicating it is terminating.
static void msg_prgmDone (int fd, dsm_msg *mp) {

	// Validate message. Only process may issue this.
	if (fd == sock_server) {
		dsm_cpanic("msg_prgmDone", "Unauthorized termination message!");
	}

	// Close connection and remove from pollable set.
	close(fd);
	dsm_removePollable(fd, pollableSet);

	// If no more connections remain, set the termination flag.
	if (pollableSet->fp <= 2) {
		alive = 0;
	}
}
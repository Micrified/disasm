CC=gcc
CFLAGS=-Wall -g
LFLAGS= -pthread -lrt
DFILES= dsm_daemon.c dsm_htab.c dsm_inet.c dsm_msg.c dsm_util.c dsm_poll.c
SFILES= dsm_server.c dsm_inet.c dsm_msg.c dsm_util.c dsm_poll.c dsm_queue.c
AFILES= dsm_arbiter.c dsm_msg.c dsm_poll.c dsm_queue.c dsm_util.c dsm_inet.c
TFILES= dsm_client.c dsm_inet.c dsm_msg.c dsm_util.c
IFILES= dsm_interface.c dsm_arbiter.c dsm_msg.c dsm_poll.c dsm_queue.c dsm_util.c dsm_inet.c

# Build server daemon.
daemon: ${DFILES}
	${CC} ${CFLAGS} -o daemon ${DFILES} ${LFLAGS}

# Build the session server.
server: ${SFILES}
	${CC} ${CFLAGS} -o server ${SFILES} ${LFLAGS}

# Build the arbiter.
arbiter: ${AFILES}
	${CC} ${CFLAGS} -o arbiter ${AFILES} ${LFLAGS}

# Build the interface.
interface: ${IFILES}
	${CC} ${CFLAGS} -o interface ${IFILES} ${LFLAGS}

# Build the tester.
tester: ${TFILES}
	${CC} ${CFLAGS} -o tester ${TFILES} ${LFLAGS}

# Clean up.
clean:
	rm dsm

# Reset shared objects.
reset:
	rm /dev/shm/dsm_object /dev/shm/sem.dsm_barrier /dev/shm/sem.dsm_tally
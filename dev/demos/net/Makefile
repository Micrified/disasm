CC=gcc
CFLAGS=-Wall -g
LFLAGS= -pthread
DFILES= dsm_sessiond.c dsm_htab.c dsm_inet.c dsm_msg.c dsm_util.c dsm_poll.c
SFILES= dsm_server.c dsm_inet.c dsm_msg.c dsm_util.c dsm_poll.c

# Build server daemon.
daemon: ${DFILES}
	${CC} ${CFLAGS} -o daemon ${DFILES} ${LFLAGS}

# Build the session server.
server: ${SFILES}
	${CC} ${CFLAGS} -o server ${SFILES} ${LFLAGS}

# Clean up.
clean:
	rm dsm

# Reset shared objects.
reset:
	rm /dev/shm/dsm_object /dev/shm/sem.dsm_barrier /dev/shm/sem.dsm_tally
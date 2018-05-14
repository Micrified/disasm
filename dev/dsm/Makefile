CC=gcc
CFLAGS=-Wall -Werror -D_GNU_SOURCE
LFLAGS= -lrt -pthread -lxed
CFILES= dsm_manager.c dsm_table.c dsm_signal.c dsm_sync.c dsm_util.c

# Build DSM.
dsm: ${CFILES}
	${CC} ${CFLAGS} -o dsm ${CFILES} ${LFLAGS}

# Clean up.
clean:
	rm dsm

# Reset shared objects.
reset:
	rm /dev/shm/dsm_object /dev/shm/sem.dsm_barrier /dev/shm/sem.dsm_tally
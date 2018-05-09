CC=gcc
LINKED=-lrt -pthread

# Build pingpong.
pingpong: pingpong.c
	${CC} ${CFLAGS} -o pingpong pingpong.c ${LINKED}

# Build semaphore.
semaphore: semaphore.c
	${CC} ${CFLAGS} -o semaphore semaphore.c ${LINKED}

# Clean.
clean:
	rm pingpong semaphore

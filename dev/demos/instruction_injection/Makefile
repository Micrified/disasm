CC=gcc
CFLAGS=-D_GNU_SOURCE -g
LINKED=-lxed

# Build siggen.
siggen: siggen.c
	${CC} ${CFLAGS} -o siggen siggen.c ${LINKED}

# Clean.
clean:
	rm siggen

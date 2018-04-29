CC=gcc
CFLAGS=-D_GNU_SOURCE -g
LINKED=-lxed

# Build disasm.
disasm: disasm.c
	${CC} ${CFLAGS} -o disasm disasm.c ${LINKED}

# Build siggen.
siggen: siggen.c
	${CC} ${CFLAGS} -o siggen siggen.c ${LINKED}


# Clean.
clean:
	rm disasm siggen

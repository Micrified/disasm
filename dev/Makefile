CC=gcc
CFLAGS=-D_GNU_SOURCE -g
LINKED=-lxed

# Build.
all: disasm.c
	${CC} ${CFLAGS} -o disasm disasm.c ${LINKED}

# Clean.
clean:
	rm disasm

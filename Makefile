CC := gcc
CPPFLAGS ?= -D_XOPEN_SOURCE=500
CFLAGS ?= -std=c99 -pedantic -Wall -Wextra -Wno-deprecated-declarations -Os
LDFLAGS ?= -s -lX11 -lXtst
DESTDIR ?= /usr/local

TEST_SRC := $(wildcard *_test.c)
TESTS := $(TEST_SRC:.c=)

HEADERS := config.h jot.h pk.h

all: ptrkeys

ptrkeys: ${HEADERS} ptrkeys.c pk.c
	${CC} -o $@ ${CPPFLAGS} ${CFLAGS} ${LDFLAGS} ptrkeys.c pk.c

config.h:
	cp config.def.h $@

check: ${TESTS} runtests.sh
	sh ./runtests.sh

${TESTS}: %_test: %_test.c pk.c ${HEADERS}
	${CC} -o $@ ${CPPFLAGS} ${CFLAGS} ${LDFLAGS} $< pk.c

clean:
	rm -f ptrkeys *.o ${TESTS} test.log

install: all
	cp ptrkeys ${DESTDIR}/bin
	cp ptrkeys.1 ${DESTDIR}/share/man/man1

.PHONY: all clean check install

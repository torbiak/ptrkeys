include config.mk

TEST_SRC := $(wildcard *_test.c)
TESTS := $(TEST_SRC:.c=)

SRC := pk.c
OBJ := ${SRC:.c=.o}
HEADERS := config.h jot.h pk.h

all: options ptrkeys

options:
	@echo ptrkeys build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: ${HEADERS} config.mk Makefile

ptrkeys: ${OBJ} ptrkeys.c
	@echo CC -o $@
	@${CC} -o $@ ptrkeys.c ${OBJ} ${CFLAGS} ${LDFLAGS}

${TESTS}: %_test: %_test.c ${OBJ}
	@echo CC -o $@
	@${CC} -o $@ $< ${CFLAGS} -g ${LDFLAGS} ${OBJ}

config.h:
	@echo creating $@ from config.def.h
	@cp config.def.h $@

check: ${TESTS} runtests.sh
	sh ./runtests.sh

clean:
	rm -f ptrkeys ${OBJ} ${TESTS} ptrkeys-${VERSION}.tar.gz test.log

dist: clean
	@echo creating dist tarball
	@mkdir -p ptrkeys-${VERSION}
	@cp -R LICENSE TODO BUGS Makefile README config.def.h config.mk \
		ptrkeys.1 ${SRC} ptrkeys-${VERSION}
	@tar -cf ptrkeys-${VERSION}.tar ptrkeys-${VERSION}
	@gzip ptrkeys-${VERSION}.tar
	@rm -rf ptrkeys-${VERSION}

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f ptrkeys ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/ptrkeys
	@echo installing manual page to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@sed "s/VERSION/${VERSION}/g" < ptrkeys.1 > ${DESTDIR}${MANPREFIX}/man1/ptrkeys.1
	@chmod 644 ${DESTDIR}${MANPREFIX}/man1/ptrkeys.1

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/ptrkeys
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/ptrkeys.1

.PHONY: all options clean dist install uninstall

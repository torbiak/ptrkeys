CFLAGS = -Wall -Werror -std=c99 -pedantic
LDFLAGS = -lX11 -lXtst

SRC := ptrkeys.c
OBJ := ${SRC:.c=.o}

all: ptrkeys

${OBJ}: jot.h Makefile

ptrkeys: ${OBJ}

clean:
	rm -f ptrkeys ${OBJ}

.PHONY: all clean

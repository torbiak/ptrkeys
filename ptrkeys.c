#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <string.h>

#include "pk.h"
#include "jot.h"

#define USAGE "usage: ptrkeys [-d|--debug] [-h|--help]\n"

static void onsigint();
static void setsighandler();

int jottrace = 0;

static sigjmp_buf jmpbuf;
static volatile sig_atomic_t canjump;


static void
onsigint()
{
	if (!canjump) return;
	canjump = 0;
	siglongjmp(jmpbuf, 1);
}

static void
setsighandler()
{
	struct sigaction sa;
	sa.sa_handler = onsigint;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGINT, &sa, NULL) == -1) {
		dief("set sigint handler: %s", strerror(errno));
	}
}

static void
parseargs(int argc, char *argv[])
{
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--debug")) {
			jottrace = 1;
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			fprintf(stdout, USAGE);
			exit(0);
		} else {
			fprintf(stderr, USAGE);
			exit(1);
		}
	}
}

int
main(int argc, char *argv[])
{
	parseargs(argc, argv);
	dieifduplicatebindings();
	setup();
	setsighandler();
	if (sigsetjmp(jmpbuf, 0)) {
		// Jumped from signal handler.
		exit(130);
	}
	canjump = 1;
	runeventloop();
	exit(0);
}

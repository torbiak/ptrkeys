#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <string.h>

#include "pk.h"
#include "jot.h"

static void onsigint();

int jottrace = 1;

static sigjmp_buf jmpbuf;
static volatile sig_atomic_t canjump;

static void
onsigint()
{
	if (!canjump) return;
	canjump = 0;
	siglongjmp(jmpbuf, 1);
}

void
setsighandler()
{
	struct sigaction sa;
	sa.sa_handler = onsigint;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGINT, &sa, NULL) == -1) {
		dief("set sigint handler: %s", strerror(errno));
	}
}

int
main()
{
	// TODO: add option to control verbosity
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

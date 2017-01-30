#include <stdlib.h>

#include "pk.h"

int
main()
{
	// TODO: add option to control verbosity
	dieifduplicatebindings();
	setup();
	runeventloop();
	exit(0);
}

#ifndef PK_H
#define PK_H

#include <X11/Xlib.h>

typedef struct {
	double basespeed;
	unsigned int dir;  // Bits from enum Direction.
	double mul;
	double xrem, yrem; // Subunit remainders.
	int xcont, ycont; // Continuing a movement?
} Movement;

typedef union {
	int i;
	unsigned int ui;
	unsigned long ul;
	double f;
	const void *v;
} Arg;

typedef struct {
	unsigned int mod;
	KeySym keysym;
	unsigned int opts;
	void (*pressfunc)(const Arg *);
	const Arg pressarg;
	void (*releasefunc)(const Arg *);
	const Arg releasearg;
} Key;

void setup();
void runeventloop();
void changedirection(Movement *m, unsigned int dir);
void dieifbadbindings();
void waitforrelease(KeyCode keycode);

// xserver connection
extern Display *dpy;
extern Window root;

// Pointer and scrolling movement.
extern Movement mvptr;
extern Movement mvscroll;
extern int ismove2scroll;

extern int iskeyboardgrabbed;
extern int quitting;

#endif

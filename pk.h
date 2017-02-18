#ifndef PK_H
#define PK_H

#include <X11/Xlib.h>

typedef struct {
	double basespeed;
	unsigned int dir;  // Bits from enum Direction, defined later.
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


// Exported for testing only:

typedef struct {
	int dx, dy;
} PointerUpdate;

typedef struct {
	int xevents, yevents;
	unsigned int xbutton, ybutton;
} ScrollUpdate;

void startdir(Movement *m, unsigned int dir);
void stopdir(Movement *m, unsigned int dir);
PointerUpdate pointerupdate(Movement *m, int usec);
ScrollUpdate scrollupdate(Movement *m, int usec);
void sprintkeysym(char *dst, size_t len, KeySym keysym, int mods);
int strappend(char *dst, size_t dstlen, char *src);
int duplicate_bindings_exist(Key *keys, size_t len);
int modified_key_with_release_func_exists(Key *keys, size_t len);
int modified_ungrabbed_keys_exist(Key *keys, size_t len);

#endif

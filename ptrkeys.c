#include <unistd.h>
#include <time.h>
#ifndef _POSIX_MONOTONIC_CLOCK
#error CLOCK_MONOTONIC not available
#endif

#include <stdlib.h>
#include <assert.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/XKBlib.h>

#include "dbg.h"

#define NUMLOCKMASK Mod2Mask
#define CAPSLOCKMASK LockMask

#define LEN(X) (sizeof X / sizeof X[0])
#define NOLOCKMASK(mask) (mask & ~(NUMLOCKMASK|CAPSLOCKMASK) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))

#define FPS 60
#define BASE_SPEED 200

typedef struct {
	int x, y;
	int mul;
	int xrem, yrem; /* Subpixel remainders. */
} Speed;

typedef union {
	int i;
	unsigned int ui;
	unsigned long ul;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*pressfunc)(const Arg *);
	const Arg pressarg;
	void (*releasefunc)(const Arg *);
	const Arg releasearg;
} Key;

enum Mouse {
	LEFT = Button1,
	MIDDLE = Button2,
	RIGHT = Button3,
	SCROLLUP = Button4,
	SCROLLDOWN = Button5,
	/* Left and right aren't aren't defined in X.h. */
	SCROLLLEFT = 6,
	SCROLLRIGHT = 7,
};

Display *dpy;
Window root;
int screen;
Speed speed;

/* function declarations */
int grabkey(Key *key);
void handle_pending_events();
void keypress(XEvent *e);
void keyrelease(XEvent *e);
int sprintmods(char *buf, int buflen, int modifiers);

/* bindable function declarations */
void grabmodkeys(const Arg *arg);
void grabkeys(const Arg *arg);
void grabkeyboard(const Arg *arg);
void ungrabkeyboard(const Arg *arg);
void mulspeed(const Arg *arg);
void divspeed(const Arg *arg);
void incyspeed(const Arg *arg);
void incxspeed(const Arg *arg);
void clickpress(const Arg *arg);
void clickrelease(const Arg *arg);
void scrollstart(const Arg *arg);
void scrollstop(const Arg *arg);
void resetspeed(const Arg *arg);
void printspeed(const Arg *arg);
void waitforrelease(const Arg *arg);


/* move to config.h */
int grab_unmodified_keys_on_start = 0;
static Key keys[] = {
/* modifier  key            press func    press arg           release func     release arg */
/* Enable/disable */
{Mod4Mask,   XK_w,          grabkeyboard,             {0},           NULL,     {0}},
{Mod4Mask,   XK_z,          printspeed,               {0},           NULL,     {0}},
{Mod4Mask,   XK_g,          resetspeed,               {0},           NULL,     {0}},
{0,          XK_q,          ungrabkeyboard,           {0},           NULL,     {0}},
/* Directional control with WASD. */
{0,          XK_w,          incyspeed,    {.i=BASE_SPEED},    incyspeed,       {.i=-BASE_SPEED}},
{0,          XK_a,          incxspeed,    {.i=-BASE_SPEED},   incxspeed,       {.i=BASE_SPEED}},
{0,          XK_s,          incyspeed,    {.i=-BASE_SPEED},   incyspeed,       {.i=BASE_SPEED}},
{0,          XK_d,          incxspeed,    {.i=BASE_SPEED},    incxspeed,       {.i=-BASE_SPEED}},
/* Accelerate using the right hand. */
{0,          XK_j,          mulspeed,     {.i=2},             divspeed,        {.i=2}},
{0,          XK_k,          mulspeed,     {.i=4},             divspeed,        {.i=4}},
{0,          XK_l,          mulspeed,     {.i=8},             divspeed,        {.i=8}},
{0,          XK_semicolon,  mulspeed,     {.i=16},            divspeed,        {.i=16}},
{0,          XK_space,      clickpress,   {.ui=LEFT},         clickrelease,    {.ui=LEFT}},
/* For single-handed operation. */
{0,          XK_e,          clickpress,   {.ui=RIGHT},        clickrelease,    {.ui=RIGHT}},
{0,          XK_r,          clickpress,   {.ui=MIDDLE},       clickrelease,    {.ui=MIDDLE}},
/* Two-handed operation, for dragging, etc. */
{0,          XK_n,          clickpress,   {.ui=RIGHT},        clickrelease,    {.ui=RIGHT}},
{0,          XK_m,          clickpress,   {.ui=MIDDLE},       clickrelease,    {.ui=MIDDLE}},
/* Scrolling */
{0,          XK_W,          scrollstart,  {.ui=SCROLLUP},     scrollstop,      {.ui=SCROLLUP}},
{ShiftMask,  XK_a,          scrollstart,  {.ui=SCROLLLEFT},   scrollstop,      {.ui=SCROLLLEFT}},
{ShiftMask,  XK_s,          scrollstart,  {.ui=SCROLLDOWN},   scrollstop,      {.ui=SCROLLDOWN}},
{ShiftMask,  XK_d,          scrollstart,  {.ui=SCROLLRIGHT},  scrollstop,      {.ui=SCROLLRIGHT}},
};

void normalizekeys()
{
	for (int i = 0; i < LEN(keys); i++) {
		Key *key = &keys[i];
		KeyCode code = XKeysymToKeycode(dpy, key->keysym);
		KeySym keysym = XkbKeycodeToKeysym(dpy, code, 0, key->mod & ShiftMask ? 1 : 0);
		if (keysym != key->keysym) {
			key->mod &= ~ShiftMask;
			key->keysym = keysym;
		}
	}
}

void
waitforrelease(const Arg *arg)
{
	debug("wait for release: %s", XKeysymToString(arg->ul));
	for (;;) {
		XEvent ev;
		XMaskEvent(dpy, KeyReleaseMask, &ev);
		KeySym keysym = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, ev.xkey.state & ShiftMask ? 1 : 0);
		if (keysym == arg->ul) {
			debug("released");
			return;
		}
	}
}

void
printmods(FILE *stream, int modifiers)
{
	int isfirst = 1;
	for (int i = 0; i < 8; i++) {
		if (!(modifiers & 1<<i)) continue;
		if (!isfirst) fprintf(stream, "|");
		isfirst = 0;
		switch (1<<i) {
		case ShiftMask: fprintf(stream, "Shift"); break;
		case ControlMask: fprintf(stream, "Control"); break;
		case Mod1Mask: fprintf(stream, "Mod1"); break;
		case Mod2Mask: fprintf(stream, "Mod2"); break;
		case Mod3Mask: fprintf(stream, "Mod3"); break;
		case Mod4Mask: fprintf(stream, "Mod4"); break;
		case Mod5Mask: fprintf(stream, "Mod5"); break;
		}
	}
}

void
printspeed(const Arg *arg)
{
	printf("x=%d y=%d mul=%d xrem=%d yrem=%d\n", speed.x, speed.y, speed.mul, speed.xrem, speed.yrem);
}

void
resetspeed(const Arg *arg)
{
	speed.x = 0;
	speed.y = 0;
	speed.mul = 1;
	speed.xrem = 0;
	speed.yrem = 0;
}

void 
grabmodkeys(const Arg *arg)
{
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (int i = 0; i < LEN(keys); i++) {
		Key *key = &keys[i];
		if (!key->mod || key->mod == ShiftMask) {
			continue;
		}
		grabkey(key);
	}
}

void
grabkeyboard(const Arg *arg)
{
	XGrabKeyboard(dpy, root, 0, GrabModeAsync, GrabModeAsync, CurrentTime);
	if (arg->ul) waitforrelease(arg);
}

void
ungrabkeyboard(const Arg *arg)
{
	XUngrabKeyboard(dpy, CurrentTime);
}


void
grabkeys(const Arg *arg)
{
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (int i = 0; i < LEN(keys); i++) {
		grabkey(&keys[i]);
	}
}

int
grabkey(Key *key)
{
	KeyCode code = XKeysymToKeycode(dpy, key->keysym);
	if (!code) {
		char *keystr = XKeysymToString(key->keysym);
		if (keystr) {
			log_err("grab keysym %s: no keycode", keystr);
		} else {
			log_err("grab %lu: bad keysym", key->keysym);
		}
		return 1;
	}
	unsigned modifiers[] = {0, NUMLOCKMASK, CAPSLOCKMASK, NUMLOCKMASK|CAPSLOCKMASK};
	for (int j = 0; j < LEN(modifiers); j++) {
		XGrabKey(dpy, code, key->mod | modifiers[j], root, False, GrabModeAsync, GrabModeAsync);
	}
	return 0;
}

void
keypress(XEvent *e)
{
	XKeyEvent *ev = &e->xkey;
	unsigned shiftlevel = ev->state & ShiftMask ? 1 : 0;
	KeySym keysym = XkbKeycodeToKeysym(dpy, ev->keycode, 0, shiftlevel);

	printmods(stdout, ev->state);
	printf(" press %s\n",  XKeysymToString(keysym));

	for (int i = 0; i < LEN(keys); i++) {
		if (keysym != keys[i].keysym) continue;
		if (NOLOCKMASK(keys[i].mod) != NOLOCKMASK(ev->state)) continue;
		if (!keys[i].pressfunc) continue;
		keys[i].pressfunc(&(keys[i].pressarg));
	}
}

void
keyrelease(XEvent *e)
{
	XKeyEvent *ev = &e->xkey;
	unsigned shiftlevel = ev->state & ShiftMask ? 1 : 0;
	KeySym keysym = XkbKeycodeToKeysym(dpy, ev->keycode, 0, shiftlevel);

	printmods(stdout, ev->state);
	printf(" release %s\n",  XKeysymToString(keysym));

	for (int i = 0; i < LEN(keys); i++) {
		if (keysym != keys[i].keysym) continue;
		if (NOLOCKMASK(keys[i].mod) != NOLOCKMASK(ev->state)) continue;
		if (!keys[i].releasefunc) continue;
		keys[i].releasefunc(&(keys[i].releasearg));
	}
}

void
mulspeed(const Arg *arg)
{
	speed.mul += arg->i;
}

void
divspeed(const Arg *arg)
{
	speed.mul -= arg->i;
}

void
incyspeed(const Arg *arg)
{
	speed.y += arg->i;
}

void
incxspeed(const Arg *arg)
{
	speed.x += arg->i;
}

void
clickpress(const Arg *arg)
{
	XTestFakeButtonEvent(dpy, arg->ui, True, CurrentTime);
}

void
clickrelease(const Arg *arg)
{
	XTestFakeButtonEvent(dpy, arg->ui, False, CurrentTime);
}

void
scrollstart(const Arg *arg)
{
	debug("scroll start");
	XTestFakeButtonEvent(dpy, arg->ui, True, CurrentTime);
}

void
scrollstop(const Arg *arg)
{
	debug("scroll stop");
	XTestFakeButtonEvent(dpy, arg->ui, False, CurrentTime);
}

void
handle_pending_events()
{
	for (XEvent ev; XCheckMaskEvent(dpy, ~NoEventMask, &ev);) {
		switch (ev.type) {
		case KeyPress:
			keypress(&ev); break;
		case KeyRelease:
			keyrelease(&ev); break;
		}
	}
}

int
main()
{
	dpy = XOpenDisplay(NULL);
	assert(dpy);
	root = DefaultRootWindow(dpy);
	screen = DefaultScreen(dpy);
	XSelectInput(dpy, root, KeyPressMask|KeyReleaseMask);
	XFlush(dpy);

	speed.x = 0;
	speed.y = 0;
	speed.mul = 1;

	if (grab_unmodified_keys_on_start) {
		grabkeys(0);
	} else {
		grabmodkeys(0);
	}

	struct timespec then;
	check(!clock_gettime(CLOCK_MONOTONIC, &then), "get time");
	for (;;) {
		handle_pending_events();

		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		int us = (now.tv_sec - then.tv_sec) * 1000000 \
				 + (now.tv_nsec - then.tv_nsec) / 1000;
		then = now;

		int dx = (speed.x * speed.mul * us + speed.xrem) / 1000000;
		int dy = -(speed.y * speed.mul * us + speed.yrem) / 1000000;
		speed.xrem = (speed.x * speed.mul * us + speed.xrem) % 1000000;
		speed.yrem = (speed.y * speed.mul * us + speed.yrem) % 1000000;
		// XTest*MotionEvent functions can't move the cursor across screens:
		// https://bugzilla.redhat.com/show_bug.cgi?id=518803
		XWarpPointer(dpy, None, None, 0, 0, 0, 0, dx, dy);
		XFlush(dpy);
		usleep(1000000 / FPS);
	}
	exit(0);
error:
	exit(1);
}

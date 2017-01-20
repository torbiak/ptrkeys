#define _XOPEN_SOURCE 700
#include <unistd.h>
#include <time.h>
#ifndef _POSIX_MONOTONIC_CLOCK
#error CLOCK_MONOTONIC not available
#endif

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/XKBlib.h>

#include "jot.h"

#define LEN(X) (sizeof X / sizeof X[0])
#define NOLOCKMASK(mask) (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))

#define FPS 60
#define BASE_SPEED 200

typedef struct {
	int x, y;
	int mul;
	int xrem, yrem; // Subpixel remainders.
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
	int grab;
	void (*pressfunc)(const Arg *);
	const Arg pressarg;
	void (*releasefunc)(const Arg *);
	const Arg releasearg;
} Key;

enum Mouse {
	LEFT = Button1,
	MIDDLE = Button2,
	RIGHT = Button3,
};

enum {
	RELEASE = 0,
	PRESS = 1,
};

enum PASSTHRUKEYS {
	MODIFIED   = (1 << 0),
	UNMODIFIED = (1 << 1),
};

Display *dpy;
Window root;
int screen;
Speed speed;
int iskeyboardgrabbed;
int numlockmask;
#define NUMMODS 8
KeyCode modkeycodes[NUMMODS];
int passthrukeys = MODIFIED|UNMODIFIED;

/* function declarations */
int grabkey(Key *key);
void handle_pending_events();
void keypress(XEvent *e);
void keyrelease(XEvent *e);
void printmods(FILE *stream, int modifiers);
void grabkeys();
void sendkey(KeyCode keycode, unsigned int modifiers);
void updatemodmap();
void updatemodkeycodes(XModifierKeymap *modmap);
void updatenumlockmask(XModifierKeymap *modmap);
void waitforrelease(KeyCode keycode);
int hasxtest();

/* bindable function declarations */
void grabmodkeys(const Arg *arg);
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


/* move to config.h */
/* Don't use shifted keysyms like XK_A or XK_percent. Use the unshifted value,
 * like XK_a or XK_5 instead.
 */
static Key keys[] = {
/* modifier  key            grab  press func       press arg          release func     release arg */
/* Directional control with WASD. */
{0,         XK_w,          0,  incyspeed,       {.i=BASE_SPEED},   incyspeed,       {.i=-BASE_SPEED}},
{0,         XK_a,          0,  incxspeed,       {.i=-BASE_SPEED},  incxspeed,       {.i=BASE_SPEED}},
{0,         XK_s,          0,  incyspeed,       {.i=-BASE_SPEED},  incyspeed,       {.i=BASE_SPEED}},
{0,         XK_d,          0,  incxspeed,       {.i=BASE_SPEED},   incxspeed,       {.i=-BASE_SPEED}},
/* Accelerate using the right hand. */
{0,         XK_j,          0,  mulspeed,        {.i=2},            divspeed,        {.i=2}},
{0,         XK_k,          0,  mulspeed,        {.i=4},            divspeed,        {.i=4}},
{0,         XK_l,          0,  mulspeed,        {.i=8},            divspeed,        {.i=8}},
{0,         XK_semicolon,  0,  mulspeed,        {.i=16},           divspeed,        {.i=16}},
{0,         XK_space,      0,  clickpress,      {.ui=LEFT},        clickrelease,    {.ui=LEFT}},
/* Left-handed clicking. */
{0,         XK_e,          0,  clickpress,      {.ui=RIGHT},       clickrelease,    {.ui=RIGHT}},
{0,         XK_r,          0,  clickpress,      {.ui=MIDDLE},      clickrelease,    {.ui=MIDDLE}},
/* Right-handed clicking, for dragging, etc. */
{0,         XK_n,          0,  clickpress,      {.ui=RIGHT},       clickrelease,    {.ui=RIGHT}},
{0,         XK_m,          0,  clickpress,      {.ui=MIDDLE},      clickrelease,    {.ui=MIDDLE}},
/* Enable/disable */
{Mod4Mask,  XK_w,          1,  grabkeyboard,    {.ul=XK_w},        NULL,            {0}},
{0,         XK_q,          0,  ungrabkeyboard,  {0},               NULL,            {0}},
{0,         XK_slash,      1,  grabkeyboard,    {0},               ungrabkeyboard,  {0}},
/* Debugging */
{Mod4Mask,  XK_z,          1,  printspeed,      {0},               NULL,            {0}},
{Mod4Mask,  XK_g,          1,  resetspeed,      {0},               NULL,            {0}},
};

// waitforrelease waits for a KeyRelease event for the given keycode,
// discarding other KeyPress and KeyRelease events until then.
void
waitforrelease(KeyCode keycode)
{
	tracef("wait for release: %d", keycode);
	for (;;) {
		XEvent ev;
		XMaskEvent(dpy, KeyPressMask|KeyReleaseMask, &ev);
		if (ev.xkey.keycode == keycode) {
			tracef("released %d", keycode);
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
msleep(long ms)
{
	struct timespec duration = {
		.tv_sec=ms/1000,
		.tv_nsec=((ms % 1000) * 1e6),
	};
	nanosleep(&duration, NULL);
}

// The key being sent must be released before the keyboard is regrabbed, I believe because of an implicit active grab.
void
passthrukey(KeyCode keycode)
{
	tracef("passthrukey %d", keycode);
	//waitforrelease(keycode);
	ungrabkeyboard(NULL);
	/* Use xtest instead of XSendEvent, since many applications supposedly
	 * ignore sent events. */
	XTestFakeKeyEvent(dpy, keycode, PRESS, 0);
	XTestFakeKeyEvent(dpy, keycode, RELEASE, 0);
	grabkeyboard(NULL);
}

void
updatemodmap()
{
	XModifierKeymap *modmap = XGetModifierMapping(dpy);
	updatemodkeycodes(modmap);
	updatenumlockmask(modmap);
	XFreeModifiermap(modmap);
}

void
updatemodkeycodes(XModifierKeymap *modmap)
{
	for (int i = 0; i < NUMMODS; i++) {
		for (int j = 0; j < modmap->max_keypermod; j++) {
			KeyCode code = modmap->modifiermap[i * modmap->max_keypermod + j];
			if (code) {
				modkeycodes[i] = code;
				break;
			}
		}
	}
}

void
updatenumlockmask(XModifierKeymap *modmap)
{
	numlockmask = 0;
	KeyCode target = XKeysymToKeycode(dpy, XK_Num_Lock);
	for (int i = ShiftMapIndex; i <= Mod5MapIndex; i++) {
		for (int j = 0; j < modmap->max_keypermod; j++) {
			KeyCode code = modmap->modifiermap[i * modmap->max_keypermod + j];
			if (code == target) numlockmask = (1 << i);
		}
	}
}

int
hasxtest()
{
	int event, error, major, minor;
	return XTestQueryExtension(dpy, &event, &error, &major, &minor);
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
grabkeyboard(const Arg *arg)
{
	Time time = CurrentTime;
	if (arg) {
		time = arg->ul;
	}
	int err = XGrabKeyboard(dpy, root, 0, GrabModeAsync, GrabModeAsync, time);
	if (err != GrabSuccess) {
		char *msg;
		switch (err) {
		case AlreadyGrabbed: msg = "AlreadyGrabbed"; break;
		case GrabInvalidTime: msg = "GrabInvalidTime"; break;
		case GrabNotViewable: msg = "GrabNotViewable"; break;
		case GrabFrozen: msg = "GrabFrozen"; break;
		default: msg = "unknown"; break;
		}
		jotf("grab keyboard: %s", msg);
		exit(1);
	}
	iskeyboardgrabbed = 1;
	if (arg) {
		KeyCode code = XKeysymToKeycode(dpy, arg->ul);
		waitforkey(code, KeyRelease);
	}
}

void
ungrabkeyboard(const Arg *arg)
{
	XUngrabKeyboard(dpy, CurrentTime);
	iskeyboardgrabbed = 0;
	XKeyboardControl ctrl = {.auto_repeat_mode=AutoRepeatModeDefault};
	XChangeKeyboardControl(dpy, KBAutoRepeatMode, &ctrl);
	/* Stop moving the pointer when the keyboard is ungrabbed, even if movement
	 * keys are pressed. */
	resetspeed(NULL);
}


void
grabkeys()
{
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (int i = 0; i < LEN(keys); i++) {
		if (!keys[i].grab) continue;
		if (keys[i].mod && keys[i].releasefunc) {
			jot("key binding with modifier has release function");
			exit(1);
		}
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
			jotf("grab keysym %s: no keycode", keystr);
		} else {
			jotf("grab %lu: bad keysym", key->keysym);
		}
		return 1;
	}
	unsigned int modifiers[] = {0, numlockmask, LockMask, numlockmask|LockMask};
	for (int j = 0; j < LEN(modifiers); j++) {
		int ok = XGrabKey(dpy, code, key->mod | modifiers[j], root, False, GrabModeAsync, GrabModeAsync);
		if (!ok) jotf("grab key %ld", key-keys);
	}
	return 0;
}

void
keypress(XEvent *e)
{
	XKeyEvent *ev = &e->xkey;
	KeySym keysym = XkbKeycodeToKeysym(dpy, ev->keycode, 0, 0);

	printmods(stdout, ev->state);
	printf(" press %s\n",  XKeysymToString(keysym));

	for (int i = 0; i < LEN(keys); i++) {
		if (keysym != keys[i].keysym) continue;
		if (NOLOCKMASK(keys[i].mod) != NOLOCKMASK(ev->state)) {
			printmods(stdout, NOLOCKMASK(keys[i].mod));
			printf("%s != (pressed) ", XKeysymToString(keys[i].keysym));
			printmods(stdout, NOLOCKMASK(ev->state));
			printf(" %s\n", XKeysymToString(keysym));
			continue;
		}
		if (!keys[i].pressfunc) continue;
		keys[i].pressfunc(&(keys[i].pressarg));
		if (keys[i].mod && iskeyboardgrabbed) {
			// The movement state machine assumes no keys are pressed on entry.
			// Key bindings with modifiers can use the same keysyms as
			// unmodified keybindings (used when the keyboard is grabbed), so
			// wait for the release events for modified keys so the release
			// doesn't "reverse" movement that was never started.
			waitforrelease(ev->keycode);
		}
		return;
	}
}

void
keyrelease(XEvent *e)
{
	XKeyEvent *ev = &e->xkey;
	KeySym keysym = XkbKeycodeToKeysym(dpy, ev->keycode, 0, 0);

	printmods(stdout, ev->state);
	printf(" release %s\n",  XKeysymToString(keysym));

	for (int i = 0; i < LEN(keys); i++) {
		if (keysym != keys[i].keysym) continue;
		if (NOLOCKMASK(keys[i].mod) != NOLOCKMASK(ev->state)) continue;
		if (!keys[i].releasefunc) continue;
		keys[i].releasefunc(&(keys[i].releasearg));
		return;
	}
	// Key is unmapped.
	if (IsModifierKey(keysym)) return;
	unsigned mods = NOLOCKMASK(ev->state);
	if (((passthrukeys & MODIFIED) && mods) || ((passthrukeys & UNMODIFIED) && !mods)) {
		passthrukey(ev->keycode);
	}
}

void
mulspeed(const Arg *arg)
{
	if (!arg) jotfatal("mulspeed: NULL arg");
	speed.mul += arg->i;
}

void
divspeed(const Arg *arg)
{
	if (!arg) jotfatal("divspeed: NULL arg");
	speed.mul -= arg->i;
}

void
incyspeed(const Arg *arg)
{
	if (!arg) jotfatal("incyspeed: NULL arg");
	speed.y += arg->i;
}

void
incxspeed(const Arg *arg)
{
	if (!arg) jotfatal("incxspeed: NULL arg");
	speed.x += arg->i;
}

void
clickpress(const Arg *arg)
{
	if (!arg) jotfatal("clickpress: NULL arg");
	XTestFakeButtonEvent(dpy, arg->ui, True, CurrentTime);
}

void
clickrelease(const Arg *arg)
{
	if (!arg) jotfatal("clickrelease: NULL arg");
	XTestFakeButtonEvent(dpy, arg->ui, False, CurrentTime);
}

void
scrollstart(const Arg *arg)
{
	if (!arg) jotfatal("scrollstart: NULL arg");
	trace("scroll start");
	XTestFakeButtonEvent(dpy, arg->ui, True, CurrentTime);
}

void
scrollstop(const Arg *arg)
{
	if (!arg) jotfatal("scrollstop: NULL arg");
	trace("scroll stop");
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
		case MappingNotify:
			updatemodmap(); break;
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
	XSelectInput(dpy, root, MappingNotify|KeyPressMask|KeyReleaseMask|PropertyChangeMask);
	XFlush(dpy);
	updatemodmap();

	if (!hasxtest()) {
		jot("XTEST not available. Can't passthru keys when the keyboard is grabbed.");
		passthrukeys = 0;
	}

	speed.x = 0;
	speed.y = 0;
	speed.mul = 1;
	iskeyboardgrabbed = 0;

	grabkeys();

	struct timespec then;
	if (clock_gettime(CLOCK_MONOTONIC, &then)) {
		jotfatalf("get time: %s", strerror(errno));
	}
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
		msleep(1000/FPS);
	}
	exit(0);
}

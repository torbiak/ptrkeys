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
#define MAX_PRESSED_MOD_KEYCODES 10
#define GRAB_KEYBOARD_TIMEOUT_MS 200

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

typedef struct {
	KeyCode keys[MAX_PRESSED_MOD_KEYCODES];
	int len;
} KeyCodes;

typedef struct {
	char pressed[32];
} KeyMap;

enum Mouse {
	LEFT = Button1,
	MIDDLE = Button2,
	RIGHT = Button3,
};

enum {
	RELEASE = 0,
	PRESS = 1,
};

Display *dpy = NULL;
Window root;
int screen;
Speed speed = {.x=0, .y=0, .mul=1};
int iskeyboardgrabbed = 0;
int numlockmask = Mod2Mask;
XModifierKeymap *modmap = NULL;
int passthrukeys = 1;
unsigned int keymodifiers[256] = {0};

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

typedef struct {
	KeyCode key;
	int eventtype;
} IsKeyEventArg;

Bool
iskeyevent(Display *dpy, XEvent *event, XPointer arg)
{
	KeyCode key = ((IsKeyEventArg*) arg)->key;
	int eventtype = ((IsKeyEventArg*) arg)->eventtype;
	XKeyEvent *ev = &event->xkey;
	return ev->keycode == key && ev->type == eventtype;
}

void waitforkey(KeyCode keycode, int eventtype)
{
	tracef("wait for %s: %d", eventtype == KeyPress ? "press" : "release", keycode);
	int XIfEvent(Display *display, XEvent *event_return, Bool (*predicate)(), XPointer arg);

	IsKeyEventArg arg = {.key=keycode, .eventtype=eventtype};
	XEvent ev;
	XIfEvent(dpy, &ev, iskeyevent, (XPointer) &arg);
	tracef("got %d", keycode);
}



char pressedkeys[32];
void
releaseallkeys()
{
	XQueryKeymap(dpy, pressedkeys);
	for (int i = 0; i < 256; i++) {
		int ispressed = pressedkeys[i / 8] & (1 << (i % 8));
		if (!ispressed) continue;
		XTestFakeKeyEvent(dpy, i, RELEASE, 0);
	}
}

void
presssavedkeys()
{
	for (int i = 0; i < 256; i++) {
		int ispressed = pressedkeys[i / 8] & (1 << (i % 8));
		if (!ispressed) continue;
		XTestFakeKeyEvent(dpy, i, PRESS, 0);
	}
}

void
waitforkeys(int eventtype)
{
	for (int i = 0; i < 256; i++) {
		int ispressed = pressedkeys[i / 8] & (1 << (i % 8));
		if (!ispressed) continue;
		waitforkey(i, eventtype);
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


KeyCode pressedkey = 0;
unsigned int repeattimeoutms = 660;
unsigned int repeatintervalms = 25;
struct timespec nextrepeat = {0};

void
addmillis(struct timespec *t, int ms)
{
	long ns = t->tv_nsec + ms * 1e6;
	t->tv_sec += ns / 1e9;
	t->tv_nsec = ns % (long) 1e9;
}

int
ispast(struct timespec *t)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now)) {
		jotfatalf("ispast: get time: %s", strerror(errno));
	}
	return now.tv_sec > t->tv_sec 
			|| (now.tv_sec == t->tv_sec && now.tv_nsec > t->tv_nsec);
}

void
repeatpressedkey()
{
	if (!pressedkey || !ispast(&nextrepeat)) return;
	addmillis(&nextrepeat, repeatintervalms);
	// pressedkey must be logically up from xserver's perspective, or the
	// events will ineffective.
	XTestFakeKeyEvent(dpy, pressedkey, RELEASE, 0);
	waitforkey(pressedkey, KeyRelease);
	XUngrabKeyboard(dpy, CurrentTime);
	XTestFakeKeyEvent(dpy, pressedkey, PRESS, 0);
	XTestFakeKeyEvent(dpy, pressedkey, RELEASE, 0);
	grabkeyboard(NULL);
	XTestFakeKeyEvent(dpy, pressedkey, PRESS, 0);
	waitforkey(pressedkey, KeyPress);
}

void
passthrukeypress(KeyCode keycode)
{
	tracef("passthrukeypress %d", keycode);
	pressedkey = keycode;
	clock_gettime(CLOCK_MONOTONIC, &nextrepeat);
	addmillis(&nextrepeat, repeattimeoutms);
	// xorg implicitly activates a grab when a key is pressed so that the
	// window that receiving the press also gets the corresponding release.
	// Because of this, the key being sent must be released before the keypress
	// is simulated.
	//XTestFakeKeyEvent(dpy, keycode, RELEASE, 0);
	// Eat the events used to evade the implicit key grab so the event loop
	// doesn't see them.
	//waitforkey(keycode, KeyRelease);
	// TODO
	releaseallkeys();
	waitforkeys(KeyRelease);
	XUngrabKeyboard(dpy, CurrentTime);
	iskeyboardgrabbed = 0;
	// Use xtest since many applications supposedly ignore events send using
	// XSendEvent.
	//XTestFakeKeyEvent(dpy, keycode, PRESS, 0);
	presssavedkeys();
	grabkeyboard(NULL);
	presssavedkeys();
	//waitforkeys(KeyPress);
	//XTestFakeKeyEvent(dpy, keycode, PRESS, 0);
	//waitforkey(keycode, KeyPress);
}

void
passthrukeyrelease(KeyCode keycode)
{
	tracef("passthrukeyrelease %d", keycode);
	pressedkey = 0;
	XUngrabKeyboard(dpy, CurrentTime);
	XTestFakeKeyEvent(dpy, keycode, RELEASE, 0);
	grabkeyboard(NULL);
}

// press     = 110
// release   = 101
// Before passthru:
// set         010  press & ~release (110 & 010) pressedkeycodes
// clear       001  release & ~press (101 & 001) releasedkeycodes
// After passthru
// release(pressedkeycodes)
// press(releasedkeycodes)
KeyCodes
setmods(unsigned int mods, XModifierKeymap *modmap, KeyMap *keymap)
{
	KeyCodes pressed = {.len=0};
	for (int mod = ShiftMapIndex; mod <= Mod5MapIndex; mod++) {
		if (!(mods & (1 << mod))) continue;
		// Press the first key that will set the modbit.
		for (int i = 0; i < modmap->max_keypermod; i++) {
			KeyCode key = modmap->modifiermap[mod * modmap->max_keypermod + i];
			if (!key) continue;
			int ispressed = keymap->pressed[key / 8] & (1 << (key % 8));
			if (ispressed) {
				tracef("setmods: %u already pressed for bit %d", key, mod);
				continue;
			}
			XTestFakeKeyEvent(dpy, key, PRESS, 0);
			pressed.keys[pressed.len++] = key;
			if (pressed.len >= MAX_PRESSED_MOD_KEYCODES) {
				jotfatalf("setmods: overflow: more than %d modifier keys pressed", MAX_PRESSED_MOD_KEYCODES);
			}
			break;
		}
	}
	return pressed;
}

KeyCodes
clearmods(unsigned int mods, XModifierKeymap *modmap, KeyMap *keymap)
{
	KeyCodes released = {.len=0};
	for (int mod = ShiftMapIndex; mod <= Mod5MapIndex; mod++) {
		if (!(mods & (1 << mod))) continue;
		// Release all the pressed keys setting the modbit.
		for (int i = 0; i < modmap->max_keypermod; i++) {
			KeyCode key = modmap->modifiermap[mod * modmap->max_keypermod + i];
			if (!key) continue;
			int ispressed = keymap->pressed[key / 8] & (1 << (key % 8));
			if (!ispressed) continue;
			XTestFakeKeyEvent(dpy, key, RELEASE, 0);
			released.keys[released.len++] = key;
			if (released.len >= MAX_PRESSED_MOD_KEYCODES) {
				jotfatalf("clearmods: overflow: more than %d modifier keys pressed", MAX_PRESSED_MOD_KEYCODES);
			}
		}
	}
	return released;
}

void
releasekeys(KeyCode *keys, int len)
{
	for (int i = 0; i < len; i++) {
		XTestFakeKeyEvent(dpy, keys[i], RELEASE, 0);
	}
}

void
presskeys(KeyCode *keys, int len)
{
	for (int i = 0; i < len; i++) {
		XTestFakeKeyEvent(dpy, keys[i], PRESS, 0);
	}
}

void
updatemodmap()
{
	if (modmap) XFreeModifiermap(modmap);
	XModifierKeymap *modmap = XGetModifierMapping(dpy);
	updatenumlockmask(modmap);
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
	int err = XGrabKeyboard(dpy, root, 0, GrabModeAsync, GrabModeAsync, CurrentTime);
	int waited = 0;
	while (err != GrabSuccess && waited < GRAB_KEYBOARD_TIMEOUT_MS) {
		int interval = 10;
		msleep(interval);
		waited += interval;
		err = XGrabKeyboard(dpy, root, 0, GrabModeAsync, GrabModeAsync, CurrentTime);
	}
	if (waited) {
		tracef("grabkeyboard: waited %dms for grab", waited);
	}
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
	XAutoRepeatOff(dpy);
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
		return;
	}
	// Key is unmapped.
	if (IsModifierKey(keysym)) return;
	if (passthrukeys) {
		passthrukeypress(ev->keycode);
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
	if (IsModifierKey(keysym)) return;
	if (passthrukeys) {
		passthrukeyrelease(ev->keycode);
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
	XkbGetAutoRepeatRate(dpy, XkbUseCoreKbd, &repeattimeoutms, &repeatintervalms);

	if (!hasxtest()) {
		jot("XTEST not available. Can't passthru keys when the keyboard is grabbed.");
		passthrukeys = 0;
	}

	grabkeys();

	struct timespec then;
	if (clock_gettime(CLOCK_MONOTONIC, &then)) {
		jotfatalf("get time: %s", strerror(errno));
	}
	for (;;) {
		handle_pending_events();

		repeatpressedkey();

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
	XFreeModifiermap(modmap);
	exit(0);
}

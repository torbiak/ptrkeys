#define _XOPEN_SOURCE 700
#include <unistd.h>
#include <time.h>
#ifndef _POSIX_MONOTONIC_CLOCK
#error CLOCK_MONOTONIC not available
#endif

#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <assert.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/XKBlib.h>

#include "jot.h"

#define LEN(X) (sizeof X / sizeof X[0])
#define NOLOCKMASK(mask) (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))

// "Frames" or updates per second.
#define FPS 60
// Pixels per second.
#define BASE_SPEED 150
// Events per second.
#define BASE_SCROLL 10
#define GRAB_KEYBOARD_TIMEOUT_MS 200

#define MAX_KEYSYM_DESC_LEN 100

typedef struct {
	unsigned int basespeed;
	unsigned int dir;  // Bits from enum Direction.
	unsigned int mul;
	int xrem, yrem; // Subunit remainders.
	int xcont, ycont; // Continuing a movement?
} Movement;

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
	unsigned int opts;
	void (*pressfunc)(const Arg *);
	const Arg pressarg;
	void (*releasefunc)(const Arg *);
	const Arg releasearg;
} Key;

enum Direction {
	UP    = (1<<0),
	DOWN  = (1<<1),
	LEFT  = (1<<2),
	RIGHT = (1<<3),
};

enum Mouse {
	BTNLEFT = Button1,
	BTNMIDDLE = Button2,
	BTNRIGHT = Button3,
	SCROLLUP = 4,
	SCROLLDOWN = 5,
	SCROLLLEFT = 6,
	SCROLLRIGHT = 7,
};

enum {
	RELEASE = 0,
	PRESS = 1,
};

enum KeyOpts {
	GRAB     = (1<<0),
	NOREPEAT = (1<<1),
};

Display *dpy = NULL;
Window root;
int screen;
Movement mvptr = {.mul=1, .basespeed=BASE_SPEED};
Movement mvscroll = {.mul=1, .basespeed=BASE_SCROLL};
int numlockmask = Mod2Mask;
int iskeyboardgrabbed = 0;
int quitting = 0;
int ismove2scroll = 0;
XErrorEvent savederror = {0};

// By default, set modifier bits will be included in click and scroll events
// generated while the keyboard is grabbed. For example, holding down a shift
// key to make the directional keys scroll instead will send "Shift-scroll"
// events whenever directional keys are pressed unless the ShiftMask bit is
// included here. While it's possible to remove a keysym from the xserver's
// modifier map so it can be used only as an internal ptrkeys modifier, it's
// easier and more reliable to simply remap the keycode to another,
// non-modifier keysym, as the xserver sometimes treats keys that aren't in the
// modifier map at all as modifiers. Modifiers cannot be suppressed for global
// hotkeys. Note ptrkeys doesn't see any internalmods either.
unsigned int internalmods = ShiftMask;

/* function declarations */
int grabkey(Key *key);
void handlependingevents();
void keypress(XEvent *e);
void keyrelease(XEvent *e);
void sprintkeysym(char *dst, size_t len, KeySym keysym, int mods);
void grabkeys();
void sendkey(KeyCode keycode, unsigned int modifiers);
void updatemodmap();
void updatenumlockmask();
void waitforrelease(KeyCode keycode);
void changedirection(Movement *m, unsigned int dir);

// Bindable function declarations:
// Grabbing the keyboard:
void grabkeyboard(const Arg *keysym); // Wait for keysym to be released, if given.
void ungrabkeyboard(const Arg *ignored);
void togglegrabkeyboard(const Arg *ignored);
void grabandmove2scroll(const Arg *ignored);
// Movement and scrolling:
// dir can be any of UP, DOWN, LEFT, RIGHT, bitwise or'd together.
void movestart(const Arg *dir);
void movestop(const Arg *dir);
void move2scroll(const Arg *enable); // enable is treated as a boolean.
void scroll(const Arg *dir);
// n is an unsigned int.
void multiplyspeed(const Arg *uint);
void dividespeed(const Arg *uint);
// Clicking:
// btn can be any value from enum Mouse.
void clickpress(const Arg *btn);
void clickrelease(const Arg *btn);
// Misc:
void resetmovement(const Arg *ignored);
void quit(const Arg *ignored);
void click(const Arg *btn);

void
quit(const Arg *ignored)
{
	quitting = 1;
}

// TODO: move to config.h
// Don't use shifted keysyms like XK_A or XK_percent. Use the unshifted value,
// like XK_a or XK_5 instead.
static Key keys[] = {
// modifier  key            opts            press func           press arg         release func     release arg
// Directional control with WASD.
{0,          XK_w,          0,              movestart,           {.i=UP},          movestop,        {.i=UP}},
{0,          XK_a,          0,              movestart,           {.i=LEFT},        movestop,        {.i=LEFT}},
{0,          XK_s,          0,              movestart,           {.i=DOWN},        movestop,        {.i=DOWN}},
{0,          XK_d,          0,              movestart,           {.i=RIGHT},       movestop,        {.i=RIGHT}},
// Scrolling
{0,          XK_Shift_L,    0,              move2scroll,         {.i=1},           move2scroll,     {.i=0}},
// Accelerate using the right hand.
{0,          XK_j,          0,              multiplyspeed,       {.i=2},           dividespeed,     {.i=2}},
{0,          XK_k,          0,              multiplyspeed,       {.i=4},           dividespeed,     {.i=4}},
{0,          XK_l,          0,              multiplyspeed,       {.i=8},           dividespeed,     {.i=8}},
{0,          XK_semicolon,  0,              multiplyspeed,       {.i=64},          dividespeed,     {.i=64}},
// Left-handed clicking.
{0,          XK_e,          0,              clickpress,          {.ui=BTNRIGHT},   clickrelease,    {.ui=BTNRIGHT}},
{0,          XK_r,          0,              clickpress,          {.ui=BTNMIDDLE},  clickrelease,    {.ui=BTNMIDDLE}},
// Right-handed clicking, for dragging, etc.
{0,          XK_space,      0,              clickpress,          {.ui=BTNLEFT},    clickrelease,    {.ui=BTNLEFT}},
{0,          XK_n,          0,              clickpress,          {.ui=BTNRIGHT},   clickrelease,    {.ui=BTNRIGHT}},
{0,          XK_m,          0,              clickpress,          {.ui=BTNMIDDLE},  clickrelease,    {.ui=BTNMIDDLE}},
// Enable/disable
{0,          XK_Select,     GRAB|NOREPEAT,  grabkeyboard,        {0},              ungrabkeyboard,  {0}},
{ShiftMask,  XK_Select,     GRAB|NOREPEAT,  grabandmove2scroll,  {0},              NULL,            {0}},
{Mod4Mask,   XK_v,          GRAB,           togglegrabkeyboard,  {0},              NULL,            {0}},
{Mod4Mask,   XK_w,          GRAB,           grabkeyboard,        {.ul=XK_w},       NULL,            {0}},
{0,          XK_q,          0,              ungrabkeyboard,      {0},              NULL,            {0}},
{0,          XK_x,          0,              quit,                {0},              NULL,            {0}},
// Debugging
{Mod4Mask,   XK_g,          GRAB,           resetmovement,       {0},              NULL,            {0}},
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

// strappend appends src to dst and tries to be safe about it. Returns 0 on
// success, and nonzero otherwise.
int
strappend(char *dst, size_t dstlen, char *src)
{
	if (!src || !dst) return 2;
	int err = 0;
	char *start = dst;
	for (int i = 0; *dst; i++) {
		if (i >= dstlen) goto toolong;
		dst++;
	}
	for (int i = dst - start; *src; i++) {
		if (i >= dstlen-1) goto toolong;
		*dst++ = *src++;
	}
	goto cleanup;
toolong:
	err = 1;
cleanup:
	dst = '\0';
	return err;
}

// sprintkeysym prints a representation of the given keysym and modifiers to
// dst. Dies if dst doesn't have enough space.
void
sprintkeysym(char *dst, size_t dstlen, KeySym keysym, int mods)
{
	int isfirst = 1;
	int err = 0;
	for (int i = 0; i < 8; i++) {
		if (!(mods & 1<<i)) continue;
		if (!isfirst) strappend(dst, dstlen, "+");
		isfirst = 0;
		switch (1<<i) {
		case ShiftMask: err = strappend(dst, dstlen, "Shift"); break;
		case ControlMask: err = strappend(dst, dstlen, "Control"); break;
		case Mod1Mask: err = strappend(dst, dstlen, "Mod1"); break;
		case Mod2Mask: err = strappend(dst, dstlen, "Mod2"); break;
		case Mod3Mask: err = strappend(dst, dstlen, "Mod3"); break;
		case Mod4Mask: err = strappend(dst, dstlen, "Mod4"); break;
		case Mod5Mask: err = strappend(dst, dstlen, "Mod5"); break;
		}
		if (err) goto overrun;
	}
	if (!isfirst) {
		if (strappend(dst, dstlen, "+")) goto overrun;
	}
	char *keystr = XKeysymToString(keysym);
	if (!keystr) {
		int n = snprintf(dst, dstlen, "%s%lx", dst, keysym);
		if (n >= dstlen) goto overrun;
	}
	if (strappend(dst, dstlen, keystr)) goto overrun;
	return;
overrun:
	dief("sprintkeysym: buffer overrun printing mods=%u keysym=%lx", mods, keysym);
	return;
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

void
updatenumlockmask()
{
	XModifierKeymap *modmap = XGetModifierMapping(dpy);
	numlockmask = 0;
	KeyCode target = XKeysymToKeycode(dpy, XK_Num_Lock);
	for (int i = ShiftMapIndex; i <= Mod5MapIndex; i++) {
		for (int j = 0; j < modmap->max_keypermod; j++) {
			KeyCode code = modmap->modifiermap[i * modmap->max_keypermod + j];
			if (code == target) numlockmask = (1 << i);
		}
	}
	XFreeModifiermap(modmap);
}

void
resetmovement(const Arg *ignored)
{
	Movement zero = {.mul=1};
	mvptr = zero;
	mvptr.basespeed = BASE_SPEED;
	mvscroll = zero;
	mvscroll.basespeed = BASE_SCROLL;
}

void
grabandmove2scroll(const Arg *ignored)
{
	grabkeyboard(NULL);
	Arg arg = {.i=1};
	move2scroll(&arg);
}

void
grabkeyboard(const Arg *keysym)
{
	XkbSetServerInternalMods(dpy, XkbUseCoreKbd, internalmods, internalmods, 0, 0);
	XAutoRepeatOff(dpy);
	int err = XGrabKeyboard(dpy, root, 0, GrabModeAsync, GrabModeAsync, CurrentTime);
	int waited = 0;
	// TODO: We're probably doing something wrong if we're having to wait to grab the
	// keyboard. I don't think this is needed if we aren't doing passthru.
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
	if (keysym && keysym->ul) {
		KeyCode code = XKeysymToKeycode(dpy, keysym->ul);
		waitforrelease(code);
	}
}

void
ungrabkeyboard(const Arg *ignored)
{
	XkbSetServerInternalMods(dpy, XkbUseCoreKbd, internalmods, 0, 0, 0);
	XUngrabKeyboard(dpy, CurrentTime);
	XKeyboardControl ctrl = {.auto_repeat_mode=AutoRepeatModeDefault};
	XChangeKeyboardControl(dpy, KBAutoRepeatMode, &ctrl);
	iskeyboardgrabbed = 0;
	// Stop moving the pointer when the keyboard is ungrabbed, even if movement
	// keys are pressed.
	resetmovement(NULL);
}

void
togglegrabkeyboard(const Arg *ignored)
{
	if (iskeyboardgrabbed) {
		ungrabkeyboard(NULL);
	} else {
		grabkeyboard(NULL);
	}
}


int
checkforduplicatebindings()
{
	for (int i = 0; i < LEN(keys); i++) {
		Key a = keys[i];
		for (int j = 0; j < LEN(keys); j++) {
			if (i == j) continue;
			Key b = keys[j];
			if (a.mod == b.mod && a.keysym == b.keysym) {
				char keystr[MAX_KEYSYM_DESC_LEN] = {0};
				sprintkeysym(keystr, LEN(keystr), a.keysym, a.mod);
				dief("Multiple bindings for %s", keystr);
			}
		}
	}
	return 0;
}

void
grabkeys()
{
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	int nerr = 0;
	for (int i = 0; i < LEN(keys); i++) {
		if (!(keys[i].opts & GRAB)) continue;
		if (keys[i].mod && keys[i].releasefunc) {
			jot("key binding with modifier has release function");
			exit(1);
		}
		int err = grabkey(&keys[i]);
		if (err) nerr++;
	}
	if (nerr) dief("grabkeys: failed to grab %d keys", nerr);
}

// setkeyrepeat sets the repeat mode of keys with the NOREPEAT option set to
// the given mode, which can be one of AutoRepeatModeOn, AutoRepeatModeOff, or
// AutoRepeatModeDefault.
void
setkeyrepeat(int mode)
{
	for (int i = 0; i < LEN(keys); i++) {
		if (!(keys[i].opts & NOREPEAT)) continue;
		KeyCode code = XKeysymToKeycode(dpy, keys[i].keysym);
		XKeyboardControl ctrl = {
			.auto_repeat_mode=mode,
			.key = code,
		};
		XChangeKeyboardControl(dpy, KBKey|KBAutoRepeatMode, &ctrl);
	}
}

int
saveerror(Display *dpy, XErrorEvent *ee)
{
	savederror = *ee;
	return 0;
}

int
grabkey(Key *key)
{
	int err = 0;
	char keystr[MAX_KEYSYM_DESC_LEN] = {0};
	sprintkeysym(keystr, LEN(keystr), key->keysym, key->mod);
	KeyCode code = XKeysymToKeycode(dpy, key->keysym);
	if (!code) dief("grabkey: keysym %s has no bound keycode", keystr);

	int (*defaulthandler)(Display *, XErrorEvent *);
	defaulthandler = XSetErrorHandler(saveerror);

	unsigned int modifiers[] = {0, numlockmask, LockMask, numlockmask|LockMask};
	for (int j = 0; j < LEN(modifiers); j++) {
		savederror.error_code = Success;

		XGrabKey(dpy, code, key->mod | modifiers[j], root, False, GrabModeAsync, GrabModeAsync);

		XSync(dpy, False);
		if (savederror.error_code != Success
		&& savederror.request_code == X_GrabKey
		&& savederror.error_code == BadAccess) {
			jotf("grabkey: %s already grabbed by another program", keystr);
			err = 1;
			break;
		} else if (savederror.error_code != Success) {
			jotf("grab key %s: unexpected X11 protocol error", keystr);
			defaulthandler(dpy, &savederror); // Probably calls exit.
			err = 1;
			break;
		}
	}
	XSetErrorHandler(defaulthandler);
	return err;
}

void
keypress(XEvent *e)
{
	XKeyEvent *ev = &e->xkey;
	KeySym keysym = XkbKeycodeToKeysym(dpy, ev->keycode, 0, 0);

	if (jottrace) {
		char keystr[MAX_KEYSYM_DESC_LEN] = {0};
		sprintkeysym(keystr, LEN(keystr), keysym, ev->state);
		tracef("press %s", keystr);
	}

	for (int i = 0; i < LEN(keys); i++) {
		if (keysym != keys[i].keysym) continue;
		if (iskeyboardgrabbed && keys[i].mod) continue;
		if (!iskeyboardgrabbed && NOLOCKMASK(keys[i].mod) != NOLOCKMASK(ev->state)) {
			continue;
		}
		if (!keys[i].pressfunc) continue;
		keys[i].pressfunc(&(keys[i].pressarg));
		return;
	}
	// Key is unmapped. Ignore it.
}

void
keyrelease(XEvent *e)
{
	XKeyEvent *ev = &e->xkey;
	KeySym keysym = XkbKeycodeToKeysym(dpy, ev->keycode, 0, 0);

	if (jottrace) {
		char keystr[MAX_KEYSYM_DESC_LEN] = {0};
		sprintkeysym(keystr, LEN(keystr), keysym, ev->state);
		tracef("release %s", keystr);
	}

	for (int i = 0; i < LEN(keys); i++) {
		if (keysym != keys[i].keysym) continue;
		if (iskeyboardgrabbed && keys[i].mod) continue;
		// TODO: ok to ignore mods on release, if release funcs are disallowed
		// for modded keys?
		//if (!iskeyboardgrabbed && keys[i].mod != NOLOCKMASK(ev->state)) continue;
		if (!keys[i].releasefunc) continue;
		keys[i].releasefunc(&(keys[i].releasearg));
		return;
	}
	// Key is unmapped. Ignore it.
}

void
multiplyspeed(const Arg *n)
{
	if (!n) die("multiplyspeed: NULL arg");
	mvptr.mul += n->ui;
	mvscroll.mul += n->ui;
}

void
dividespeed(const Arg *n)
{
	if (!n) die("dividespeed: NULL arg");
	mvptr.mul -= n->ui;
	mvscroll.mul -= n->ui;
}

void
movestart(const Arg *dir)
{
	if (!dir) die("movestart: NULL arg");
	changedirection(&mvptr, dir->ui);
}

void
movestop(const Arg *dir)
{
	if (!dir) die("stop: NULL arg");
	mvptr.dir &= ~(dir->ui);
}

// move2scroll changes pointer movement into scrolling based on the given
// integer (which is treated as a boolean), allowing the press/release of a
// modifier key to switch between them.
void
move2scroll(const Arg *enable)
{
	if (!enable) die("move2scroll: NULL arg");
	if (enable->i == ismove2scroll) return;
	ismove2scroll = enable->i;
	if (ismove2scroll) {
		mvptr.basespeed = BASE_SCROLL;
	} else {
		mvptr.basespeed = BASE_SPEED;
	}
	mvptr.xrem = 0;
	mvptr.yrem = 0;
	mvptr.xcont = 0;
	mvptr.ycont = 0;
}

void
changedirection(Movement *m, unsigned int dir)
{
	if (dir & UP && dir & DOWN) {
		die("changedirection: both UP and DOWN given");
	}
	if (dir & LEFT && dir & RIGHT) {
		die("changedirection: both LEFT and RIGHT given");
	}
	if (dir & (UP|DOWN)) {
		m->yrem = 0;
		m->ycont = 0;
	}
	if (dir & UP) {
		m->dir &= ~DOWN;
		m->dir |= UP;
	}
	if (dir & DOWN) {
		m->dir &= ~UP;
		m->dir |= DOWN;
	}
	if (dir & (LEFT|RIGHT)) {
		m->xrem = 0;
		m->xcont = 0;
	}
	if (dir & LEFT) {
		m->dir &= ~RIGHT;
		m->dir |= LEFT;
	}
	if (dir & RIGHT) {
		m->dir &= ~LEFT;
		m->dir |= RIGHT;
	}
}

void
scrollstart(const Arg *dir)
{
	if (!dir) die("scrollstart: NULL arg");
	changedirection(&mvscroll, dir->ui);
}

void
scrollstop(const Arg *dir)
{
	if (!dir) die("scrollstop: NULL arg");
	mvscroll.dir &= ~(dir->ui);
}

void
clickpress(const Arg *btn)
{
	if (!btn) die("clickpress: NULL arg");
	XTestFakeButtonEvent(dpy, btn->ui, True, CurrentTime);
}

void
clickrelease(const Arg *btn)
{
	if (!btn) die("clickrelease: NULL arg");
	XTestFakeButtonEvent(dpy, btn->ui, False, CurrentTime);
}

void
handlependingevents()
{
	for (XEvent ev; XCheckMaskEvent(dpy, ~NoEventMask, &ev);) {
		switch (ev.type) {
		case KeyPress:
			keypress(&ev); break;
		case KeyRelease:
			keyrelease(&ev); break;
		case MappingNotify:
			updatenumlockmask(); break;
		}
	}
}

void
cleanup()
{
	if (iskeyboardgrabbed) ungrabkeyboard(NULL);
	setkeyrepeat(AutoRepeatModeOn);
	XFlush(dpy);
}

void
onsigint()
{
	exit(0);
}

void
requestpointermovement(Movement *m, int usec)
{
	if (!m->dir) return;
	// -1, 0, 1
	int xsign = ((m->dir & RIGHT) ? 1 : 0) - ((m->dir & LEFT) ? 1 : 0);
	int ysign = ((m->dir & UP) ? 1 : 0) - ((m->dir & DOWN) ? 1 : 0);
	// x, y units per second
	int xps = m->basespeed * xsign * m->mul * usec + m->xrem;
	int yps = m->basespeed * ysign * m->mul * usec + m->yrem;
	int dx =  xps / 1000000;
	int dy = - yps / 1000000;
	// Subunit remainders.
	m->xrem = xps % 1000000;
	m->yrem = yps % 1000000;

	XWarpPointer(dpy, None, None, 0, 0, 0, 0, dx, dy);
}

void
requestscrolling(Movement *m, int usec)
{
	if (!m->dir) return;
	// -1, 0, 1
	int xsign = ((m->dir & RIGHT) ? 1 : 0) - ((m->dir & LEFT) ? 1 : 0);
	int ysign = ((m->dir & UP) ? 1 : 0) - ((m->dir & DOWN) ? 1 : 0);
	// x, y units per second
	int xps = m->basespeed * xsign * m->mul * usec + m->xrem;
	int yps = m->basespeed * ysign * m->mul * usec + m->yrem;
	int dx =  xps / 1000000;
	int dy = - yps / 1000000;
	// Subunit remainders.
	m->xrem = xps % 1000000;
	m->yrem = yps % 1000000;

	unsigned int xbutton = (m->dir & LEFT) ? SCROLLLEFT : SCROLLRIGHT;
	unsigned int ybutton = (m->dir & UP) ? 4 : 5;

	int xevents = abs(dx);
	int yevents = abs(dy);
	// Scroll immediately after a scroll key is pressed, but adjust the
	// remainder so the configured number of scroll events occur in the first
	// second.
	if (!xevents && (m->dir & (LEFT|RIGHT)) && !m->xcont) {
		xevents += 1;
		m->xrem -= 1;
	}
	if (!yevents && (m->dir & (UP|DOWN)) && !m->ycont) {
		yevents += 1;
		m->yrem -= 1;
	}
	m->xcont = 1;
	m->ycont = 1;

	for (int i = 0; i < xevents; i++) {
		XTestFakeButtonEvent(dpy, xbutton, PRESS, CurrentTime);
		XTestFakeButtonEvent(dpy, xbutton, RELEASE, CurrentTime);
	}
	for (int i = 0; i < yevents; i++) {
		XTestFakeButtonEvent(dpy, ybutton, PRESS, CurrentTime);
		XTestFakeButtonEvent(dpy, ybutton, RELEASE, CurrentTime);
	}
}

void
printmovement()
{
	Movement *p = &mvptr;
	tracef("ptr: base=%u dir=%u mul=%u xc=%d yc=%d",
			p->basespeed, p->dir, p->mul, p->xcont, p->ycont);
	Movement *s = &mvscroll;
	tracef("scroll: base=%u dir=%u mul=%u xc=%d yc=%d",
			s->basespeed, s->dir, s->mul, s->xcont, s->ycont);
}

int
main()
{
	// TODO: add option to control verbosity
	dpy = XOpenDisplay(NULL);
	if (!dpy) die("connect to xserver: failed");
	assert(dpy);
	root = DefaultRootWindow(dpy);
	screen = DefaultScreen(dpy);
	XSelectInput(dpy, root, MappingNotify|KeyPressMask|KeyReleaseMask);
	XFlush(dpy);

	updatenumlockmask();
	checkforduplicatebindings();
	grabkeys();
	setkeyrepeat(AutoRepeatModeOff);
	atexit(cleanup);
	signal(SIGINT, onsigint);

	struct timespec then;
	if (clock_gettime(CLOCK_MONOTONIC, &then)) {
		dief("get time: %s", strerror(errno));
	}
	for (; !quitting;) {
		handlependingevents();

		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		int usec = (now.tv_sec - then.tv_sec) * 1000000 \
				 + (now.tv_nsec - then.tv_nsec) / 1000;
		then = now;

		requestscrolling(&mvscroll, usec);
		if (ismove2scroll) {
			requestscrolling(&mvptr, usec);
		} else {
			requestpointermovement(&mvptr, usec);
		}
		XFlush(dpy);

		// Don't use CPU unless there's work to do.
		if (mvptr.dir || mvscroll.dir) {
			msleep(1000/FPS);
		} else {
			XEvent ev;
			XPeekEvent(dpy, &ev);
			clock_gettime(CLOCK_MONOTONIC, &then);
		}
	}
	exit(0);
}

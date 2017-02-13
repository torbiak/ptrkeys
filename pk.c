#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#ifndef _POSIX_MONOTONIC_CLOCK
#error CLOCK_MONOTONIC not available
#endif

#include "jot.h"
#include "pk.h"
#include "command.h"


#define LEN(X) (sizeof X / sizeof X[0])
#define NOLOCKMASK(mask) (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))

#define MAX_KEYSYM_DESC_LEN 100
#define GRAB_KEYBOARD_TIMEOUT_MS 200


static void handlependingevents();
static void requestpointermovement(Movement *m, int usec);
static void requestscrolling(Movement *m, int usec);
static void keypress(XEvent *e);
static void keyrelease(XEvent *e);
static void grabkeys();
static int grabkey(Key *key);
static void setkeyrepeat(int mode);
static void updatenumlockmask();
static void cleanup();
static int saveerror(Display *dpy, XErrorEvent *ee);
static void msleep(long ms);
static void sprintkeysym(char *dst, size_t len, KeySym keysym, int mods);
static int strappend(char *dst, size_t dstlen, char *src);


#include "config.h"


Display *dpy = NULL;
Window root;
Movement mvptr = {.mul=1, .basespeed=BASE_SPEED};
Movement mvscroll = {.mul=1, .basespeed=BASE_SCROLL};
int iskeyboardgrabbed = 0;
int quitting = 0;
int interuptted = 0;
int ismove2scroll = 0;

static int numlockmask = Mod2Mask;
static XErrorEvent savederror = {0};

// setup connects to the xserver, configures the keyboard, and registers exit
// and signal functions.
void
setup()
{
	dpy = XOpenDisplay(NULL);
	if (!dpy) die("connect to xserver: failed");
	root = DefaultRootWindow(dpy);

	XSelectInput(dpy, root, MappingNotify|KeyPressMask|KeyReleaseMask);
	updatenumlockmask();
	grabkeys();
	setkeyrepeat(AutoRepeatModeOff);
	if (atexit(cleanup)) dief("atexit: %s", strerror(errno));
}

// runeventloop handles events from the xserver and scrolls and moves the
// pointer until the quitting global is nonzero.
void
runeventloop()
{
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
}

// changedirection updates the direction of the given movment struct.
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
dieifduplicatebindings()
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
}

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

static void
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

static void
requestpointermovement(Movement *m, int usec)
{
	if (!m->dir) return;
	// xsign and ysign can be one of -1, 0, 1.
	double xsign = ((m->dir & RIGHT) ? 1 : 0) - ((m->dir & LEFT) ? 1 : 0);
	double ysign = ((m->dir & UP) ? 1 : 0) - ((m->dir & DOWN) ? 1 : 0);
	double dx = m->basespeed * xsign * m->mul * usec / 1e6 + m->xrem;
	double dy = - m->basespeed * ysign * m->mul * usec / 1e6 + m->yrem;
	double dummy;
	m->xrem = modf(dx, &dummy);
	m->yrem = modf(dy, &dummy);

	XWarpPointer(dpy, None, None, 0, 0, 0, 0, (int) dx, (int) dy);
}

static void
requestscrolling(Movement *m, int usec)
{
	if (!m->dir) return;
	// xsign and ysign can be one of -1, 0, 1.
	double xsign = ((m->dir & RIGHT) ? 1 : 0) - ((m->dir & LEFT) ? 1 : 0);
	double ysign = ((m->dir & UP) ? 1 : 0) - ((m->dir & DOWN) ? 1 : 0);
	double dx = m->basespeed * xsign * m->mul * usec / 1e6 + m->xrem;
	double dy = m->basespeed * ysign * m->mul * usec / 1e6 + m->yrem;
	double dummy;
	m->xrem = modf(dx, &dummy);
	m->yrem = modf(dy, &dummy);

	unsigned int xbutton = (m->dir & LEFT) ? SCROLLLEFT : SCROLLRIGHT;
	unsigned int ybutton = (m->dir & UP) ? SCROLLUP : SCROLLDOWN;

	int xevents = abs((int) dx);
	int yevents = abs((int) dy);
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

static void
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

static void
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

static void
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

static int
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

// setkeyrepeat sets the repeat mode of keys with the NOREPEAT option set to
// the given mode, which can be one of AutoRepeatModeOn, AutoRepeatModeOff, or
// AutoRepeatModeDefault.
static void
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

static void
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

static void
cleanup()
{
	if (iskeyboardgrabbed) ungrabkeyboard(NULL);
	setkeyrepeat(AutoRepeatModeOn);
	XFlush(dpy);
}

static int
saveerror(Display *dpy, XErrorEvent *ee)
{
	savederror = *ee;
	return 0;
}

static void
msleep(long ms)
{
	struct timespec duration = {
		.tv_sec=ms/1000,
		.tv_nsec=((ms % 1000) * 1e6),
	};
	nanosleep(&duration, NULL);
}

// sprintkeysym prints a representation of the given keysym and modifiers to
// dst. Dies if dst doesn't have enough space.
static void
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
		if (err) goto toolong;
	}
	if (!isfirst) {
		if (strappend(dst, dstlen, "+")) goto toolong;
	}
	char *keystr = XKeysymToString(keysym);
	if (!keystr) {
		int n = snprintf(dst, dstlen, "%s%lx", dst, keysym);
		if (n >= dstlen) goto toolong;
	}
	if (strappend(dst, dstlen, keystr)) goto toolong;
	return;
toolong:
	dief("sprintkeysym: buffer overrun printing mods=%u keysym=%lx", mods, keysym);
	return;
}

// strappend appends src to dst and tries to be safe about it. Returns 0 on
// success, and nonzero otherwise.
static int
strappend(char *dst, size_t dstlen, char *src)
{
	assert(src);
	assert(dst);
	int n = snprintf(dst, dstlen, "%s%s", dst, src);
	if (n >= dstlen) {
		return 1;
	}
	return 0;
}

void
grabkeyboard(const Arg *keysym)
{
	XkbSetServerInternalMods(dpy, XkbUseCoreKbd, internalmods, internalmods, 0, 0);
	XAutoRepeatOff(dpy);
	int err = XGrabKeyboard(dpy, root, 0, GrabModeAsync, GrabModeAsync, CurrentTime);
	int waited = 0;
	// TODO: We're probably doing something wrong if we're having to wait to
	// grab the keyboard. I don't think this is needed if we aren't doing
	// passthru.
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

void
grabandmove2scroll(const Arg *ignored)
{
	grabkeyboard(NULL);
	Arg arg = {.i=1};
	move2scroll(&arg);
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

// togglem2s toggles move2scroll behaviour.
void
togglem2s(const Arg *ignored)
{
	Arg arg = {.i=!ismove2scroll};
	move2scroll(&arg);
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
multiplyspeed(const Arg *factor)
{
	if (!factor) die("multiplyspeed: NULL arg");
	mvptr.mul *= factor->f;
	mvscroll.mul *= factor->f;
}

void
dividespeed(const Arg *factor)
{
	if (!factor) die("dividespeed: NULL arg");
	mvptr.mul /= factor->f;
	mvscroll.mul /= factor->f;
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
resetmovement(const Arg *ignored)
{
	Movement zero = {.mul=1};
	mvptr = zero;
	mvptr.basespeed = BASE_SPEED;
	mvscroll = zero;
	mvscroll.basespeed = BASE_SCROLL;
	ismove2scroll = 0;
}

void
quit(const Arg *ignored)
{
	quitting = 1;
}

// Bindable function declarations ("commands").

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
	GRAB     = (1<<0),  // Grab key, making it a "global hotkey".
	NOREPEAT = (1<<1),  // Disable autorepeat.
};

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
void togglem2s(const Arg *ignored);
void scrollstart(const Arg *dir);
void scrollstop(const Arg *dir);
// factor is a float.
void multiplyspeed(const Arg *factor);
void dividespeed(const Arg *factor);

// Clicking:
// btn can be any value from enum Mouse.
void clickpress(const Arg *btn);
void clickrelease(const Arg *btn);

// Misc:
void resetmovement(const Arg *ignored);
void quit(const Arg *ignored);

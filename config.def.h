// "Frames" or updates per second.
#define FPS 60
// Pixels per second.
#define BASE_SPEED 1000.0
// Events per second.
#define BASE_SCROLL 14

// Set modifier bits as "internal" while the keyboard is grabbed, that is, the
// xserver still keeps track of their state but doesn't pass them along in key
// events to applications.
//
// By default, set modifier bits will be included in click and scroll events
// generated while the keyboard is grabbed. For example, holding down a shift
// key to make the directional keys scroll instead will send "Shift-scroll"
// events whenever directional keys are pressed unless the ShiftMask bit is
// included here. Note that some modifier keysyms are treated as modifiers even
// if they aren't assigned to any modifier bits.
//
// Modifiers cannot be suppressed for global hotkeys.
unsigned int internalmods = ShiftMask|ControlMask|Mod1Mask;

// See command.h for the list of bindable functions.
//
// Use unshifted keysyms regardless whether shift will be pressed. Eg, use XK_a
// or XK_5 instead of XK_A or XK_percent.
//
// Keys with modifiers can't have release functions, since the order of key
// release is significant.
//
// While the keyboard is grabbed ptrkeys doesn't take modifier bits into
// account when determining a key's binding, similar to how keybindings work
// for video games. Bindings with modifiers aren't active while the keyboard is
// grabbed. Bindings with modifiers must have the GRAB option set.
static Key keys[] = {
// modifier  key            opts            press func           press arg         release func     release arg
// Enable/disable
//
// The caps lock key has been bound to Select in xmodmap to avoid changing the
// Lock modifier state. eg: `keycode 66 = Select`
//
// If the keyboard will be grabbed while a key is held down, auto-repeat must
// be disabled for the key using the NOREPEAT option. This could be
// inconvenient if the key is frequently used outside of ptrkeys.
{Mod4Mask,   XK_w,          GRAB,           grabkeyboard,        {.ul=XK_w},       NULL,            {0}},
{0,          XK_q,          0,              ungrabkeyboard,      {0},              NULL,            {0}},
{0,          XK_Select,     GRAB|NOREPEAT,  grabkeyboard,        {0},              ungrabkeyboard,  {0}},
{ShiftMask,  XK_Select,     GRAB|NOREPEAT,  grabandmove2scroll,  {0},              NULL,            {0}},
{Mod4Mask,   XK_v,          GRAB,           togglegrabkeyboard,  {0},              NULL,            {0}},
{0,          XK_x,          0,              quit,                {0},              NULL,            {0}},
// Directional control with WASD.
{0,          XK_w,          0,              movestart,           {.i=UP},          movestop,        {.i=UP}},
{0,          XK_a,          0,              movestart,           {.i=LEFT},        movestop,        {.i=LEFT}},
{0,          XK_s,          0,              movestart,           {.i=DOWN},        movestop,        {.i=DOWN}},
{0,          XK_d,          0,              movestart,           {.i=RIGHT},       movestop,        {.i=RIGHT}},
// Scrolling
{0,          XK_Shift_L,    0,              move2scroll,         {.i=1},           move2scroll,     {.i=0}},
{0,          XK_f,          0,              togglem2s,           {0},              NULL,            {0}},
// Speed multiply/divide.
{0,          XK_Alt_L,      0,              dividespeed,         {.f=8},           multiplyspeed,   {.f=8}},
{0,          XK_Control_L,  0,              multiplyspeed,       {.f=32},          dividespeed,     {.f=32}},
{0,          XK_j,          0,              dividespeed,         {.f=8},           multiplyspeed,   {.f=8}},
{0,          XK_k,          0,              dividespeed,         {.f=2},           multiplyspeed,   {.f=2}},
{0,          XK_l,          0,              multiplyspeed,       {.f=4},           dividespeed,     {.f=4}},
{0,          XK_semicolon,  0,              multiplyspeed,       {.f=8},           dividespeed,     {.f=8}},
// Left-handed clicking.
{0,          XK_space,      0,              clickpress,          {.ui=BTNLEFT},    clickrelease,    {.ui=BTNLEFT}},
{0,          XK_e,          0,              clickpress,          {.ui=BTNRIGHT},   clickrelease,    {.ui=BTNRIGHT}},
{0,          XK_r,          0,              clickpress,          {.ui=BTNMIDDLE},  clickrelease,    {.ui=BTNMIDDLE}},
// Right-handed clicking, for dragging, etc.
{0,          XK_n,          0,              clickpress,          {.ui=BTNRIGHT},   clickrelease,    {.ui=BTNRIGHT}},
{0,          XK_m,          0,              clickpress,          {.ui=BTNMIDDLE},  clickrelease,    {.ui=BTNMIDDLE}},
// Debugging
{Mod4Mask,   XK_g,          GRAB,           resetmovement,       {0},              NULL,            {0}},
};

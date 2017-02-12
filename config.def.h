// "Frames" or updates per second.
#define FPS 60
// Pixels per second.
#define BASE_SPEED 150
// Events per second.
#define BASE_SCROLL 10

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
{0,          XK_f,          0,              togglem2s,           {0},              NULL,            {0}},
// Accelerate using the right hand.
{0,          XK_j,          0,              multiplyspeed,       {.i=2},           dividespeed,     {.i=2}},
{0,          XK_k,          0,              multiplyspeed,       {.i=4},           dividespeed,     {.i=4}},
{0,          XK_Alt_L,      0,              multiplyspeed,       {.i=6},           dividespeed,     {.i=6}},
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

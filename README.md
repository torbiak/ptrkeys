# ptrkeys - smooth mouse keys for X

ptrkeys binds the keyboard to mouse movement, scrolling, and mouse button presses on X.

## Requirements

To build pointer keys the Xlib header files, GNU make, and a C99 compiler are needed.

## X keyboard model

To understand ptrkeys configuration it's necessary to understand a few things about the X keyboard model.

### Key Codes, Symbols, and Maps

Popular desktop operating systems have multiple layers of key translation tables, to support interfacing with a wide variety of keyboard hardware and changing keyboard layouts in software. X uses three layers: scancodes, keycodes, and key symbols.

Scan codes: Sent by the keyboard to a computer. Sets of codes evolved along with keyboard interfaces: "Set 1" for IBM XT, "Set 2" for IBM AT, "Set 3" for the IBM 3270 PC. PS/2 keyboards can be configured to send any of these three sets. USB keyboards send another set of scancodes defined in the [HID Usage Tables](http://www.usb.org/developers/hidpage/Hut1_12v2.pdf). Scan codes are typically translated to keycodes by the keyboard driver, but applications like X can request raw scancodes and do their own translation. [On Linux, see `KDGKBMODE` in `man console_ioctl`.]

Key codes (keycodes): An intermediate hardware-independent set of codes. On Linux keycodes are translated to text and actions by the keyboard driver or X, using a keymap. X and the keyboard driver use different sets of keycodes. X keycodes can be determined using `xev`.

Key symbols (keysyms):  X's term for its "application-level" set of codes. Multiple keycodes can map to the same keysym. X applications doing text entry can ask the xserver for the string that a keysym corresponds to (eg `XK_a` -> "a"). Most keysyms are defined in the Xlib header files `keysymdef.h` and `XF86keysym.h`.

Key map (keymap): In general a keymap is a table defining the conversion of scan or key codes to a higher-level representation. In documentation about X keymap usually refers to the map of keycodes to keysyms. `xmodmap` and `setxkbmap` can be used to change these mappings.

### Modifiers

X has 8 modifiers: Shift, Lock, Control, Mod1, Mod2, Mod3, Mod4, and Mod5, each represented by a bit in the XKeyEvent structure. Only `Shift_L` and `Shift_R` can be assigned to the Shift modifier,  and likewise for Control. The Lock modifier is interpreted as either capslock or shiftlock depending on the keysym assigned to it: `Caps_Lock` or `Shift_Lock`.

According to the Inter-Client Communications Conventions Manual (ICCCM), Mod1 to Mod5 should be interpreted based on the keysyms they're assigned: eg, if `Alt_L` is assigned to Mod1, then the Mod1 modifier bit represents Alt. Historically some applications have assumed certain modifier bits have a certain interpretation regardless of the keysyms assigned: most commonly interpreting Mod1 as Meta. Instead of depending on a rule few users are aware of or assuming the meaning of a certain modifier bit, ptrkeys requires a modifier bit (one of `Mod1Mask...Mod5Mask`) to be explicitly specified in its `config.h`. Masks for modifier bits are defined in `X.h`, and the bit a key is assigned to can be determined using the `state` output of `xev` when pressing the modifier key in question along with a non-modifier key. Eg, `state 0x40` corresponds to Mod4:

    0100 0000 = 0x40
    |||| |||Shift
    |||| ||Lock
    |||| |Control
    |||| Mod1
    |||Mod2
    ||Mod3
    |Mod4
    Mod5

[`xmodmap`](https://wiki.archlinux.org/index.php/xmodmap) is practical for making small modifications to the keymap and changing modifier assignments.

### Grabs

X generally directs keyboard events to the focused window, but a keyboard can be "grabbed" as a whole or on a per-key basis so that its events are sent elsewhere. For example, pressing a key implicitly sets up a single-key grab so the subsequent release event is received by the same window that got the keypress.

ptrkeys doesn't create a window that can be focused, so a single-key grab is necessary to setup a "global hotkey" that can be used to enable ptrkeys by grabbing the entire keyboard and thus "activating" the rest of its configured key bindings. The `GRAB` option is used in the `keys[]` definition to make a global hotkey.

## Configuration

TODO

## Installation

Edit config.mk to suit your system and set the install location.

Then build and install using GNU make:

    make clean install

## Running

To try ptrkeys out, it can simply be run from the command line.

To run ptrkeys in the background for the remainder of an X session, invoke it from a program launcher or disassociate it from a shell; for example:

    nohup ptrkeys &> ~/.ptrkeys.log & disown $!

For a more permanent arrangement, if X is being invoked using `startx`/`xinit`, run `ptrkeys` in the background from [`~/.xinitrc`](https://wiki.archlinux.org/index.php/Xinit). If a display manager is being used it's likely necessary to create a custom session; see [these instructions for Ubuntu](https://wiki.ubuntu.com/CustomXSession), for example.

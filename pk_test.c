#include <string.h>
#include <X11/keysym.h>

#include "pk.h"
#include "command.h"
#include "prove.h"
#include "jot.h"

#define LEN(X) (sizeof X / sizeof X[0])

int jottrace = 1;

int
test_strappend()
{
	int rc = 0;
	char buf[100];
	struct test {
		int wanterr;
		char *dst;
		size_t len;
		char *wantstr;
		char *strings[5];
	};
	struct test tests[] = {
		{0, buf, LEN(buf), "hi there", {"hi ", "there"}}, // Happy path.
		{1, buf, 0, "", {"a", "b"}},  // Zero length buffer.
		{0, buf, 1, "", {"", ""}}, // Empty strings with 1-byte buffer.
		{1, buf, 5, "1234", {"12345"}}, // No space for terminator.
		{1, buf, 5, "1234", {"1", "2345"}}, // Second string overflows buffer.
		{0, buf, 6, "12345", {"1\0002", "2345"}}, // Embedded null byte.
	};
	for (size_t i = 0; i < LEN(tests); i++) {
		int err = 0;
		struct test test = tests[i];
		buf[0] = '\0';
		for (char **s = test.strings; *s; s++) {
			err = strappend(test.dst, test.len, *s);
		}
		if (err != test.wanterr) {
			rc = 1;
			jotf("test=%zu err=%d want=%d", i, err, test.wanterr);
			continue;
		}
		if (strcmp(test.dst, test.wantstr)) {
			rc = 1;
			jotf("test=%zu str=\"%s\" want=\"%s\"", i, test.dst, test.wantstr);
			continue;
		}
	}
	return rc;
}

int
test_pointerupdate()
{
	int rc = 0;
	double base = 100;
	struct frame {
		unsigned int startdirs, stopdirs;
		double mul;
		int usec;
		PointerUpdate want;
	};

	struct frame each_dir_one_frame[] = {
		{RIGHT, 0,     1, 1e6, {base,  0}},
		{LEFT,  RIGHT, 1, 1e6, {-base, 0}},
		{UP,    LEFT,  1, 1e6, {0,     -base}},
		{DOWN,  UP,    1, 1e6, {0,     base}},
		{0,     DOWN,  1, 1e6, {0,     0}},
	};

	struct frame subpixel_movements_add_up[] = {
		{RIGHT, 0, 1, 3e3, {0, 0}},
		{0,     0, 1, 3e3, {0, 0}},
		{0,     0, 1, 3e3, {0, 0}},
		{0,     0, 1, 3e3, {1, 0}},
	};

	struct frame big_and_small_multipliers[] = {
		{RIGHT|UP,  0, 50,      10e3, {50, -50}},
		{0,         0, 50,      10e3, {50, -50}},
		{DOWN|LEFT, 0, 1.0/5.0, 10e3, {0,  0}},
		{0,         0, 1.0/5.0, 10e3, {0,  0}},
		{0,         0, 1.0/5.0, 10e3, {0,  0}},
		{0,         0, 1.0/5.0, 10e3, {0,  0}},
		{0,         0, 1.0/5.0, 10e3, {-1, 1}},
	};

	struct test {
		struct frame *frames;
		size_t len;
	};
	struct test tests[] = {
		{each_dir_one_frame, LEN(each_dir_one_frame)},
		{subpixel_movements_add_up, LEN(subpixel_movements_add_up)},
		{big_and_small_multipliers, LEN(big_and_small_multipliers)},
	};
	Movement init = {base, 0, 1, 0, 0, 0, 0};
	for (size_t i = 0; i < LEN(tests); i++) {
		struct test test = tests[i];
		Movement mv = init;
		for (size_t j = 0; j < test.len; j++) {
			struct frame frame = test.frames[j];
			startdir(&mv, frame.startdirs);
			stopdir(&mv, frame.stopdirs);
			mv.mul = frame.mul;
			PointerUpdate got = pointerupdate(&mv, frame.usec);
			PointerUpdate want = frame.want;
			if (got.dx != want.dx || got.dy != want.dy) {
				rc = 1;
				jotf("mv: base=%.2g dir=%u mul=%.2g xrem=%.2g yrem=%.2g xcont=%d ycont=%d",
						mv.basespeed, mv.dir, mv.mul, mv.xrem, mv.yrem, mv.xcont, mv.ycont);
				jotf("test=%zu frame=%zu got={dx=%d dy=%d}, want={dx=%d dy=%d}",
						i, j, got.dx, got.dy, want.dx, want.dy);
				break;
			}
		}
	}
	return rc;
}

int
test_scrollupdate()
{
	int rc = 0;
	double base = 10;
	struct frame {
		unsigned int startdirs, stopdirs;
		double mul;
		int usec;
		ScrollUpdate want;
	};

	struct frame each_dir_one_frame[] = {
		{RIGHT, 0,     1, 1e6, {base, 0,    SCROLLRIGHT, 0}},
		{LEFT,  RIGHT, 1, 1e6, {base, 0,    SCROLLLEFT,  0}},
		{UP,    LEFT,  1, 1e6, {0,    base, 0,           SCROLLUP}},
		{DOWN,  UP,    1, 1e6, {0,    base, 0,           SCROLLDOWN}},
		{0,     DOWN,  1, 1e6, {0,    0,    0,           0}},
	};

	struct frame event_distribution[] = {
		// One event right away...
		{RIGHT, 0, 1, 40e3,  {1, 0, SCROLLRIGHT, 0}},
		{0,     0, 1, 40e3,  {0, 0, 0,           0}},
		{0,     0, 1, 40e3,  {0, 0, 0,           0}},
		{0,     0, 1, 40e3,  {0, 0, 0,           0}},
		// ...one 2/base seconds = 200ms later.
		{0,     0, 1, 40e3,  {1, 0, SCROLLRIGHT, 0}},
		{0,     0, 1, 40e3,  {0, 0, 0,           0}},
		// ...adding up to base*mul events happening in the first second.
		{0,     0, 1, 760e3, {8, 0, SCROLLRIGHT, 0}},
	};

	struct frame big_and_small_multipliers[] = {
		// base*mul = 10*20 = 200 events per second; 200 * 0.01s = 2
		{RIGHT|UP,  0, 20,      10e3,  {2, 2, SCROLLRIGHT, SCROLLUP}},  
		{0,         0, 20,      10e3,  {2, 2, SCROLLRIGHT, SCROLLUP}},  
		// base/mul = 10/5 = 2 events per second
		{DOWN|LEFT, 0, 1.0/5.0, 10e3,  {1, 1, SCROLLLEFT,  SCROLLDOWN}},
		{0,         0, 1.0/5.0, 10e3,  {0, 0, 0,           0}},         
		{0,         0, 1.0/5.0, 970e3, {0, 0, 0,           0}},
		{0,         0, 1.0/5.0, 10e3,  {1, 1, SCROLLLEFT,  SCROLLDOWN}},
	};

	struct test {
		struct frame *frames;
		size_t len;
	};
	struct test tests[] = {
		{each_dir_one_frame, LEN(each_dir_one_frame)},
		{event_distribution, LEN(event_distribution)},
		{big_and_small_multipliers, LEN(big_and_small_multipliers)},
	};
	Movement init = {base, 0, 1, 0, 0, 0, 0};
	for (size_t i = 0; i < LEN(tests); i++) {
		struct test test = tests[i];
		Movement mv = init;
		for (size_t j = 0; j < test.len; j++) {
			struct frame frame = test.frames[j];
			startdir(&mv, frame.startdirs);
			stopdir(&mv, frame.stopdirs);
			mv.mul = frame.mul;
			ScrollUpdate got = scrollupdate(&mv, frame.usec);
			ScrollUpdate want = frame.want;
			if (got.xevents != want.xevents
			|| got.yevents != want.yevents
			|| (want.xevents && got.xbutton != want.xbutton)
			|| (want.yevents && got.ybutton != want.ybutton)) {
				rc = 1;
				jotf("mv: base=%.2g dir=%u mul=%.2g xrem=%.2g yrem=%.2g xcont=%d ycont=%d",
						mv.basespeed, mv.dir, mv.mul, mv.xrem, mv.yrem, mv.xcont, mv.ycont);
				jotf("test=%zu frame=%zu got={x=%d xbut=%d y=%d ybut=%d}, want={x=%d xbut=%d y=%d ybut=%d}",
						i, j,
						got.xevents, got.xbutton, got.yevents, got.ybutton,
						want.xevents, want.xbutton, want.yevents, want.ybutton);
				break;
			}
		}
	}
	return rc;
}
int
test_sprintkeysym()
{
	int rc = 0;
	char buf[100];
	struct test {
		char *wantstr;
		KeySym keysym;
		int mods;
	};
	unsigned int allmods = ShiftMask|LockMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask;
	struct test tests[] = {
		{"a", XK_a, 0},
		{"Shift+Control+Home", XK_Home, ShiftMask|ControlMask},
		{"Shift+Lock+Control+Mod1+Mod2+Mod3+Mod4+Mod5+Hyper_R", XK_Hyper_R, allmods},
	};
	for (size_t i = 0; i < LEN(tests); i++) {
		struct test test = tests[i];
		buf[0] = '\0';
		sprintkeysym(buf, LEN(buf), test.keysym, test.mods);
		if (strcmp(buf, test.wantstr)) {
			rc = 1;
			jotf("test=%zu str=\"%s\" want=\"%s\"", i, buf, test.wantstr);
			continue;
		}
	}
	return rc;
}

int
test_duplicate_bindings_exist()
{
	Key no_dupes_grabbed_and_ungrabbed[] = {
		{0, XK_w, 0,    NULL, {0}, NULL, {0}},
		{0, XK_a, 0,    NULL, {0}, NULL, {0}},
		{0, XK_w, GRAB, NULL, {0}, NULL, {0}},
		{0, XK_a, GRAB, NULL, {0}, NULL, {0}},
	};
	// Mods are ignored when the keyboard is grabbed.
	Key no_dupes_grabbed_mod[] = {
		{ShiftMask, XK_c, GRAB, NULL, {0}, NULL, {0}},
		{0,         XK_c, GRAB, NULL, {0}, NULL, {0}},
	};
	Key no_dupes_ungrabbed_mod_and_nomod[] = {
		{0,         XK_a, 0, NULL, {0}, NULL, {0}},
		{ShiftMask, XK_a, 0, NULL, {0}, NULL, {0}},
	};
	Key dupe_ungrabbed[] = {
		{0, XK_w, 0, NULL, {0}, NULL, {0}},
		{0, XK_w, 0, NULL, {0}, NULL, {0}},
	};
	Key no_dupes_empty[] = {0};
	struct test {
		int wantdupes;
		Key *key;
		size_t len;
	};
	struct test tests[] = {
		{0, no_dupes_grabbed_and_ungrabbed, LEN(no_dupes_grabbed_and_ungrabbed)},
		{0, no_dupes_grabbed_mod, LEN(no_dupes_grabbed_mod)},
		{0, no_dupes_ungrabbed_mod_and_nomod, LEN(no_dupes_ungrabbed_mod_and_nomod)},
		{1, dupe_ungrabbed, LEN(dupe_ungrabbed)},
		{0, no_dupes_empty, LEN(no_dupes_empty)},
	};
	int rc = 0;
	for (size_t i = 0; i < LEN(tests); i++) {
		struct test test = tests[i];
		int gotdupes = duplicate_bindings_exist(test.key, test.len);
		if (gotdupes != test.wantdupes) {
			jotf("test %zu: gotdupes=%d wantdupes=%d", i, gotdupes, test.wantdupes);
			rc = 1;
		}
	}
	return rc;
}

int
test_modified_key_with_release_func_exists()
{
	Key mod_with_release_func[] = {
		{ShiftMask, XK_w, 0, NULL, {0}, quit, {0}},
	};
	Key no_mod_with_release_func[] = {
		{0, XK_w, 0, NULL, {0}, quit, {0}},
	};
	Key empty[] = {0};
	struct test {
		int want;
		Key *key;
		size_t len;
	};
	struct test tests[] = {
		{1, mod_with_release_func, LEN(mod_with_release_func)},
		{0, no_mod_with_release_func, LEN(no_mod_with_release_func)},
		{0, empty, LEN(empty)},
	};
	int rc = 0;
	for (size_t i = 0; i < LEN(tests); i++) {
		struct test test = tests[i];
		int got = modified_key_with_release_func_exists(test.key, test.len);
		if (got != test.want) {
			jotf("test %zu: got=%d want=%d", i, got, test.want);
			rc = 1;
		}
	}
	return rc;
}

int
test_modified_ungrabbed_keys_exist()
{
	Key mod_ungrabbed[] = {
		{ShiftMask, XK_w, 0, NULL, {0}, quit, {0}},
	};
	Key mod_grabbed[] = {
		{0, XK_w, 0, NULL, {0}, quit, {0}},
		{ShiftMask, XK_w, GRAB, NULL, {0}, quit, {0}},
	};
	Key empty[] = {0};
	struct test {
		int want;
		Key *key;
		size_t len;
	};
	struct test tests[] = {
		{1, mod_ungrabbed, LEN(mod_ungrabbed)},
		{0, mod_grabbed, LEN(mod_grabbed)},
		{0, empty, LEN(empty)},
	};
	int rc = 0;
	for (size_t i = 0; i < LEN(tests); i++) {
		struct test test = tests[i];
		int got = modified_ungrabbed_keys_exist(test.key, test.len);
		if (got != test.want) {
			jotf("test %zu: got=%d want=%d", i, got, test.want);
			rc = 1;
		}
	}
	return rc;
}

int
main()
{
	prove_init();
	prove_run(test_strappend);
	prove_run(test_sprintkeysym);
	prove_run(test_pointerupdate);
	prove_run(test_scrollupdate);
	prove_run(test_duplicate_bindings_exist);
	prove_run(test_modified_key_with_release_func_exists);
	prove_run(test_modified_ungrabbed_keys_exist);
	prove_exit();
}

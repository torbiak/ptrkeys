#ifndef JOT_H
#define JOT_H
// Basic logging for C99.

#include <stdio.h>
#include <stdlib.h>

extern int jottrace;

#define trace(MSG) do { \
	if (!jottrace) break; \
	fprintf(stderr, "[trace] %s:%d: " MSG "\n", __FILE__, __LINE__); \
} while (0)

#define tracef(MSG, ...) do { \
	if (!jottrace) break; \
	fprintf(stderr, "[trace] %s:%d: " MSG "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
} while (0)

#define jot(MSG) fprintf(stderr, MSG "\n")
#define jotf(MSG, ...) fprintf(stderr, MSG "\n", ##__VA_ARGS__)

#define die(MSG) do { \
	fprintf(stderr, "[fatal] " MSG "\n"); \
	exit(1); \
} while (0)

#define dief(MSG, ...) do { \
	fprintf(stderr, "[fatal] " MSG "\n", ##__VA_ARGS__); \
	exit(1); \
} while (0)

#endif

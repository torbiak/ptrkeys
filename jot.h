#ifndef JOT_H
#define JOT_H

#include <stdio.h>
#include <stdlib.h>

int trace_enabled = 1;

#define trace(MSG) do { \
	if (!trace_enabled) break; \
	fprintf(stderr, "[trace] %s:%d: " MSG "\n", __FILE__, __LINE__); \
} while (0)

#define tracef(MSG, ...) do { \
	if (!trace_enabled) break; \
	fprintf(stderr, "[trace] %s:%d: " MSG "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
} while (0)

#define jot(MSG) fprintf(stderr, MSG "\n")
#define jotf(MSG, ...) fprintf(stderr, MSG "\n", ##__VA_ARGS__)

#define jotfatal(MSG) do { \
	fprintf(stderr, MSG "\n"); \
	exit(1); \
} while (0)

#define jotfatalf(MSG, ...) do { \
	fprintf(stderr, MSG, ##__VA_ARGS__); \
	exit(1); \
} while (0)

#endif

#ifndef __UTIL_H
#define __UTIL_H

#include <cstdio>

#define error(msg) do { \
	fprintf(stderr, "\e[1;31m[Error from %s:%i]: %s\e[0m\n", __FILE__, __LINE__, msg); \
	} while (0)

#endif


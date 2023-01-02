#ifndef __UTIL_H
#define __UTIL_H

#include <endian.h>
#include <byteswap.h>

#include <cstdio>

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define LE_SHORT(v)              (v)
#define LE_INT(v)                (v)
#define BE_SHORT(v)              bswap_16(v)
#define BE_INT(v)                bswap_32(v)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define LE_SHORT(v)              bswap_16(v)
#define LE_INT(v)                bswap_32(v)
#define BE_SHORT(v)              (v)
#define BE_INT(v)                (v)
#else
#error "Wrong Endian"
#endif

#define error(msg) do { \
	fprintf(stderr, "\e[1;31m[Error from %s:%i]: %s\e[0m\n", __FILE__, __LINE__, msg); \
	} while (0)

#endif


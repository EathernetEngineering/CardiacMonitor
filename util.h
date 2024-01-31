#ifndef CEE_UTIL_H_
#define CEE_UTIL_H_

#include <endian.h>
#include <byteswap.h>
#include <stdint.h>
#include <stddef.h>

#include <sys/time.h>

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

#define NSEC_PER_SEC 1000000000l
#define USEC_PER_SEC 1000000l

#if defined(__cplusplus)
extern "C" {
#endif
static inline uint8_t checksum(uint8_t* ptr, size_t sz) {
	uint8_t chk = 0;
	while (sz-- != 0){
		chk -= *ptr++;
	}
	return chk;
}

static inline void TimespecSub(struct timespec* diff, struct timespec* start, struct timespec* end) {
	diff->tv_sec = end->tv_sec - start->tv_sec;
	diff->tv_nsec = end->tv_nsec - start->tv_nsec;

	if (diff->tv_nsec < 0) {
		diff->tv_nsec += NSEC_PER_SEC;
		diff->tv_sec -= 1;
	}
}

static inline void TimevalSub(struct timeval* diff, struct timeval* start, struct timeval* end) {
	diff->tv_sec = end->tv_sec - start->tv_sec;
	diff->tv_usec = end->tv_usec - start->tv_usec;

	if (diff->tv_usec < 0) {
		diff->tv_usec += USEC_PER_SEC;
		diff->tv_sec -= 1;
	}
}

static inline float map8BitToFloat(uint16_t in) {
	if (in > 255)
		in = 255;
	return in/255.f;
}

static inline float map10BitToFloat(uint16_t in) {
	if (in > 1023)
		in = 1023;
	return in/1023.f;
}

static inline float map(float in, float inMin, float inMax, float outMin, float outMax) {
	return (in - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}
#if defined(__cplusplus)
}
#endif
#endif


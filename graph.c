#include "graph.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

#include <signal.h>

float* createGraphBuffer(
		float* data,
		size_t length,
		float yAlignment,
		float yScaling,
		float xAlignment,
		float xScaling,
		float r,
		float g,
		float b,
		float a,
		float buffer[])
{
	const size_t elements = length/sizeof(data[0]);
	if (data == NULL) {
		raise(SIGABRT);
	}

	uint32_t vtxIndex = 0;
	for (uint32_t i = 0; i < elements; i++) {
		buffer[vtxIndex + 0] = (((float)i / (float)elements) * 2.0f - 1.0f) * xScaling + xAlignment;
		buffer[vtxIndex + 1] = data[i] * yScaling + yAlignment;
		buffer[vtxIndex + 2] = 0.0f;
		buffer[vtxIndex + 3] = 1.0f;
		buffer[vtxIndex + 4] = r;
		buffer[vtxIndex + 5] = g;
		buffer[vtxIndex + 6] = b;
		buffer[vtxIndex + 7] = a;
		vtxIndex += 8;
	}

	return buffer;
}


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

float* createPeakChevrons(float* locations, size_t length, float yAlignment, float scale, float r, float g, float b, float a, float* buffer, size_t bufferLength) {
	const size_t elements = length/sizeof(locations[0]);

	uint32_t vtxIndex = 0;
	for (uint32_t i = 0; i < elements; i++) {
		buffer[vtxIndex +  0] = locations[i];
		buffer[vtxIndex +  1] = yAlignment;
		buffer[vtxIndex +  2] = 0.0f;
		buffer[vtxIndex +  3] = 1.0f;
		buffer[vtxIndex +  4] = r;
		buffer[vtxIndex +  5] = g;
		buffer[vtxIndex +  6] = b;
		buffer[vtxIndex +  7] = a;
		
		buffer[vtxIndex +  8] = locations[i] - (0.1f * scale);
		buffer[vtxIndex +  9] = yAlignment + (0.15f * scale);
		buffer[vtxIndex + 10] = 0.0f;
		buffer[vtxIndex + 11] = 1.0f;
		buffer[vtxIndex + 12] = r;
		buffer[vtxIndex + 13] = g;
		buffer[vtxIndex + 14] = b;
		buffer[vtxIndex + 15] = a;
		
		buffer[vtxIndex + 16] = locations[i] + (0.1f * scale);
		buffer[vtxIndex + 17] = yAlignment + (0.15f * scale);
		buffer[vtxIndex + 18] = 0.0f;
		buffer[vtxIndex + 19] = 1.0f;
		buffer[vtxIndex + 20] = r;
		buffer[vtxIndex + 21] = g;
		buffer[vtxIndex + 22] = b;
		buffer[vtxIndex + 23] = a;
		
		vtxIndex += 8 * 3;
		if (bufferLength < vtxIndex) {
			printf("Warning: Could not draw all chevrons.\n");
			break;
		}
	}

	return buffer;
}


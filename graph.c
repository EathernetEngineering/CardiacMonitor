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
		int32_t* data,
		size_t length,
		uint32_t screenWidth,
		int32_t yMidpoint,
		float yScaling,
		uint8_t normalize,
		float r,
		float g,
		float b,
		float a,
		float buffer[])
{
	size_t elements = length/sizeof(data[0]);
	if (buffer == NULL) {
		raise(SIGABRT);
	}

	if (screenWidth >= elements) {
		float ratio = ((float)screenWidth)/((float)elements);
		for (int32_t i = 0; i < elements; i += 8) {
			buffer[i + 0] = ((float)i * ratio/((float)screenWidth/2.0f)) - 1.0f;
			buffer[i + 1] = (float)data[i] * yScaling + yMidpoint;
			buffer[i + 2] = 0.0f;
			buffer[i + 3] = 1.0f;
			buffer[i + 4] = r;
			buffer[i + 5] = g;
			buffer[i + 6] = b;
			buffer[i + 7] = a;
		}
	} else {
		float ratio = ((float)elements)/((float)screenWidth);
		int32_t vertex = 0;
		for (int32_t i = 0; i < screenWidth * 8; i += 8) {
			buffer[i + 0] = (2.0f * (float)vertex / (float)screenWidth) - 1.0f;
			buffer[i + 1] = (float)data[(int32_t)(vertex * ratio)] * yScaling + yMidpoint;
			buffer[i + 2] = 0.0f;
			buffer[i + 3] = 1.0f;
			buffer[i + 4] = r;
			buffer[i + 5] = g;
			buffer[i + 6] = b;
			buffer[i + 7] = a;
			vertex++;
		}
	}

	return buffer;
}


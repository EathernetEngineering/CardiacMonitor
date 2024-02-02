#ifndef CEE_GRAPH_H_
#define CEE_GRAPH_H_

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 *  Function to construct a vertex buffer in OpenGL style from an array from
 *  raw data.
 *
 *  This function takes the an input of raw data, and creates a vertex buffer
 *  for a line to be rendered by OpenGL. This function does not create a
 *  vertex buffer, it creates the data to be used by a vertex buffer.
 *  Format of buffer is x, y, z, w, r, g, b, a.
 *  Where x, y, z, w, r, g, b, a are 32 bit floats.
 *
 *  @param data The raw data to be graphed.
 *  @param length The length in bytes of the array passed into parameter one.
 *  @param yAlignment The y value when the point is zero.
 *  @param yScaling The value to multiply the y value by.
 *  @param xAlignment The x offset from the 0.0.
 *  @param xScaling The percentage of the width of the screen to use.
 *  @param r The red value of the line from 0 to 1.
 *  @param g The green value of the line from 0 to 1.
 *  @param b The blue value of the line from 0 to 1.
 *  @param a The alpha value of the line from 0 to 1.
 *  @param buffer The out buffer to be used.
 *
 *  @return The pointer to the buffer provided in buffer parameter.
 *
 */
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
		float buffer[]);

/**
 *  Function to construct a vertex buffer in OpenGL style from an array from
 *  raw data.
 *
 *  This function takes the an input of raw data, and creates a vertex buffer
 *  for several chevrons to be rendered by OpenGL. This function does not create a
 *  vertex buffer, it creates the data to be used by a vertex buffer.
 *  Format of buffer is x, y, z, w, r, g, b, a.
 *  Where x, y, z, w, r, g, b, a are 32 bit floats.
 *
 *  @param locations The x positions of the chevrons to be drawn.
 *  @param length The length in bytes of the array passed into parameter one.
 *  @param yAlignment The y position of the base of the chevrons.
 *  @param scale The size of the chevrons to be drawn.
 *  @param r The red value of the line from 0 to 1.
 *  @param g The green value of the line from 0 to 1.
 *  @param b The blue value of the line from 0 to 1.
 *  @param a The alpha value of the line from 0 to 1.
 *  @param buffer The out buffer to be used.
 *  @param bufferLength The length out buffer to be used.
 *
 *  @return The pointer to the buffer provided in buffer parameter.
 *
 */
float* createPeakChevrons(
		float* locations,
		size_t length,
		float yAlignment,
		float scale,
		float r,
		float g,
		float b,
		float a,
		float* buffer,
		size_t bufferLength);

#if defined(__cplusplus)
}
#endif

#endif


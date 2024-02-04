#ifndef CEE_FONT_RENDERER_H_
#define CEE_FONT_RENDERER_H_

#include "stdint.h"
#include "stddef.h"

#if defined(__cplusplus)
extern "C" {
#endif

int32_t ceeFontRendererIntialize(const char* fontFile, float scale);
void ceeFontRendererShutdown();

// TODO: Maybe add ability for user to provide vertex buffer for batching, or
// make user call a flush function so there isnt a draw call on every call to
// this function?
//
// x and y are in screen space
void ceeFontRendererDraw(const char* str, float screenWidth, float screenHeight, float* x, float* y);

#if defined(__cplusplus)
}
#endif

#endif


#ifndef CEE_FONT_RENDERER_H_
#define CEE_FONT_RENDERER_H_

#include "stdint.h"
#include "stddef.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _ceeFont ceeFont;


int32_t ceeFontRendererIntialize(uint32_t screenWidth, uint32_t screenHeight);
ceeFont* ceeFontRendererCreateFont(const char* fontFile, float scale, uint32_t texWidth, uint32_t texHeight);
void ceeFontRendererDeleteFont(ceeFont* font);
void ceeFontRendererShutdown();

// x and y are in screen space
void ceeFontRendererDraw(ceeFont* font, const char* str, float* x, float* y);
void ceeFontRendererFlush();

#if defined(__cplusplus)
}
#endif

#endif


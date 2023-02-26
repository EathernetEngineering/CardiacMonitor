#ifndef _GRAPHICS_H
#define _GRAPHICS_H

typedef struct _ceeEglState ceeEglState;

#if defined(__cplusplus)
extern "C" {
#endif

ceeEglState* ceeGraphicsCreateState();
void ceeGraphicsDestroyState(ceeEglState* state);
void ceeGraphicsInitialize(ceeEglState* state);
void ceeGraphicsShutdown();
int ceeGraphicsCreateShaderProgram(const char vertexSource[], const char fragmentSource[], unsigned* program);

void ceeGraphicsStartFrame(ceeEglState* state);
void ceeGraphicsEndFrame(ceeEglState* state);

void ceeGraphicsClearColor(float r, float g, float b, float a);

#if defined(__cplusplus)
}
#endif

#endif


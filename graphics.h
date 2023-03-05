#ifndef _GRAPHICS_H
#define _GRAPHICS_H

#include <stdint.h>

enum GlDataType {
	GL_TYPE_BOOL,
	GL_TYPE_INT,
	GL_TYPE_INT2,
	GL_TYPE_INT3,
	GL_TYPE_INT4,
	GL_TYPE_FLOAT,
	GL_TYPE_FLOAT2,
	GL_TYPE_FLOAT3,
	GL_TYPE_FLOAT4,
	GL_TYPE_MAT3,
	GL_TYPE_MAT4
};

typedef struct _ceeGraphicsState ceeGraphicsState;
typedef struct _ceeGraphicsVertexBufferElement {
	int16_t type;
	uint32_t size;
	uint32_t offset;
	uint8_t normalized;
} ceeGraphicsVertexBufferElement;

#if defined(__cplusplus)
extern "C" {
#endif

ceeGraphicsState* ceeGraphicsMallocState();
void ceeGraphicsFreeState(ceeGraphicsState* state);
void ceeGraphicsInitialize(ceeGraphicsState* state);
void ceeGraphicsShutdown(ceeGraphicsState* state);
int32_t ceeGraphicsCreateShaderProgram(
		const char vertexSource[],
		const char fragmentSource[],
		uint32_t* program,
		const char* arrtibuteNames[],
		uint32_t attributeLocations[],
		uint32_t attributeCount);
void ceeGraphicsUseShaderProgram(uint32_t program);


/**
 *  IMPORTANT!! ONLY COMPATIBLE WITH STATIC DRAW!!
 */
void ceeGraphicsCreateVertexBuffer(uint32_t* buffer);
void ceeGraphicsBindVertexBuffer(uint32_t buffer);
void ceeGraphicsUnbindVertexBuffer();
void ceeGraphicsSetVertexBufferLayout(ceeGraphicsVertexBufferElement layout[], uint32_t elements, uint32_t stride);
void ceeGraphicsSetVertices(float* vertices, uint32_t size);
void ceeGraphicsDeleteVertexBuffer(uint32_t* buffer);

void ceeGraphicsCreateIndexBuffer(uint32_t* buffer);
void ceeGraphicsBindIndexBuffer(uint32_t buffer);
void ceeGraphicsUnbindIndexBuffer();
void ceeGraphicsSetIndices(uint16_t* indices, uint32_t size);
void ceeGraphicsDeleteIndexBuffer(uint32_t* buffer);

void ceeGraphicsStartFrame(ceeGraphicsState* state);
void ceeGraphicsFlush(uint32_t indicesCount);
void ceeGraphicsEndFrame(ceeGraphicsState* state);

void ceeGraphicsClearColor(float r, float g, float b, float a);

#if defined(__cplusplus)
}
#endif

#endif


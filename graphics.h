#ifndef CEE_GRAPHICS_H_
#define CEE_GRAPHICS_H_

#include <stdint.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

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

#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0
#endif

#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID ((((__u64)0) << 56) | ((1ULL << 56) - 1))
#endif

#ifndef EGL_KHR_platform_gbm
#define EGL_KHR_platform_gbm 1
#define EGL_PLATFORM_GBM_KHR              0x31D7
#endif /* EGL_KHR_platform_gbm */

#ifndef EGL_EXT_platform_base
#define EGL_EXT_platform_base 1
typedef EGLDisplay (EGLAPIENTRYP PFNEGLGETPLATFORMDISPLAYEXTPROC) (EGLenum platform, void *native_display, const EGLint *attrib_list);
typedef EGLSurface (EGLAPIENTRYP PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC) (EGLDisplay dpy, EGLConfig config, void *native_window, const EGLint *attrib_list);
typedef EGLSurface (EGLAPIENTRYP PFNEGLCREATEPLATFORMPIXMAPSURFACEEXTPROC) (EGLDisplay dpy, EGLConfig config, void *native_pixmap, const EGLint *attrib_list);
#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI EGLDisplay EGLAPIENTRY eglGetPlatformDisplayEXT (EGLenum platform, void *native_display, const EGLint *attrib_list);
EGLAPI EGLSurface EGLAPIENTRY eglCreatePlatformWindowSurfaceEXT (EGLDisplay dpy, EGLConfig config, void *native_window, const EGLint *attrib_list);
EGLAPI EGLSurface EGLAPIENTRY eglCreatePlatformPixmapSurfaceEXT (EGLDisplay dpy, EGLConfig config, void *native_pixmap, const EGLint *attrib_list);
#endif
#endif /* EGL_EXT_platform_base */

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

void ceeGraphicsCreateVertexBuffer(uint32_t* buffer);
void ceeGraphicsBindVertexBuffer(uint32_t buffer);
void ceeGraphicsUnbindVertexBuffer();
void ceeGraphicsSetVertexBufferLayout(ceeGraphicsVertexBufferElement layout[], uint32_t elements, uint32_t stride);
void ceeGraphicsSetVertices(float* vertices, uint32_t size);
void ceeGraphicsSetSubVertices(float* vertices, uint32_t size);
void ceeGraphicsDeleteVertexBuffer(uint32_t* buffer);

void ceeGraphicsCreateIndexBuffer(uint32_t* buffer);
void ceeGraphicsBindIndexBuffer(uint32_t buffer);
void ceeGraphicsUnbindIndexBuffer();
void ceeGraphicsSetIndices(uint16_t* indices, uint32_t size);
void ceeGraphicsDeleteIndexBuffer(uint32_t* buffer);

void ceeGraphicsStartFrame(ceeGraphicsState* state);
void ceeGraphicsFlushTriangles(uint32_t vertexCount);
void ceeGraphicsFlushLines(uint32_t indicesCount);
void ceeGraphicsFlushLineStrip(uint32_t vertexCount, uint32_t firstVertex);
void ceeGraphicsEndFrame(ceeGraphicsState* state);

void ceeGraphicsClearColor(float r, float g, float b, float a);

#if defined(__cplusplus)
}
#endif

#endif


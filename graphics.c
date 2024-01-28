#include "graphics.h"

#include <EGL/eglplatform.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#include <bcm_host.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <gbm.h>

static EGLint ceeGraphicsCompileShader(GLenum type, const char source[], GLuint* shader);
static void ceeGraphicsSwapBuffers(ceeGraphicsState* state);
static uint32_t FindCrtcIdForEncoder(const drmModeRes* resources, const drmModeEncoder* encoder);
static uint32_t FindCrtcIdForConnector(const ceeGraphicsState* state, const drmModeRes* resources, const drmModeConnector* connector);
static bool HasExt(const char *extensionList, const char *ext);

static GLuint getComponentCount(int type) {
	switch (type) {
	case GL_TYPE_BOOL:      return 1;
	case GL_TYPE_INT:       return 1;
	case GL_TYPE_INT2:      return 2;
	case GL_TYPE_INT3:      return 3;
	case GL_TYPE_INT4:      return 4;
	case GL_TYPE_FLOAT:     return 1;
	case GL_TYPE_FLOAT2:    return 2;
	case GL_TYPE_FLOAT3:    return 3;
	case GL_TYPE_FLOAT4:    return 4;
	case GL_TYPE_MAT3:      return 3 * 3;
	case GL_TYPE_MAT4:      return 4 * 4;
	}
	assert(0);
}

static GLenum dataTypeToGlBaseType(int type) {
	switch (type) {
	case GL_TYPE_BOOL:      return GL_BOOL;
	case GL_TYPE_INT:       return GL_INT;
	case GL_TYPE_INT2:      return GL_INT;
	case GL_TYPE_INT3:      return GL_INT;
	case GL_TYPE_INT4:      return GL_INT;
	case GL_TYPE_FLOAT:     return GL_FLOAT;
	case GL_TYPE_FLOAT2:    return GL_FLOAT;
	case GL_TYPE_FLOAT3:    return GL_FLOAT;
	case GL_TYPE_FLOAT4:    return GL_FLOAT;
	case GL_TYPE_MAT3:      return GL_FLOAT;
	case GL_TYPE_MAT4:      return GL_FLOAT;
	}
	assert(0);
}

struct DrmConnector {
	drmModeConnector* connector;
	drmModeObjectProperties* properties;
	drmModePropertyRes** propertyResources;
};

struct _ceeGraphicsState {
	GLuint screenWidth;
	GLuint screenHeight;

	EGLNativeWindowType hWnd;

	EGLDisplay display;
	EGLContext context;
	EGLSurface surface;

	DISPMANX_DISPLAY_HANDLE_T dispmanDisplay;
	DISPMANX_ELEMENT_HANDLE_T dispmanElement;

	int32_t DrmFd;
	struct DrmConnector DrmConnector;
	uint32_t DrmConnectorId;
	int32_t DrmCrtcId;
	uint32_t DrmCrtcIndex;
	drmModeModeInfo* DrmMode;

	struct gbm_device* GbmDevice;
	struct gbm_surface* GbmSurface;
	uint32_t SurfaceWidth, SurfaceHeight;
	uint32_t SurfaceFormat;
};

ceeGraphicsState* ceeGraphicsMallocState() {
	return calloc(1, sizeof(struct _ceeGraphicsState));
}

void ceeGraphicsFreeState(ceeGraphicsState* state) {
	free(state);
}

void ceeGraphicsInitialize(ceeGraphicsState* state) {
	/*
	static EGL_DISPMANX_WINDOW_T nativeWindow;

	DISPMANX_UPDATE_HANDLE_T dispmanUpdate;
	VC_RECT_T srcRect;
	VC_RECT_T dstRect;
	*/

	EGLint const configAttribs[] = {
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	EGLint const contextAttribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	EGLConfig config;
	EGLint numConfigs;

	EGLBoolean result;
	int32_t success = 0;

	memset(state, 0, sizeof(struct _ceeGraphicsState));

	state->DrmFd = open("/dev/dri/card0", O_RDWR);
	if (state->DrmFd < 0) {
		printf("Failed to open DRM device.\n");
		exit(EXIT_FAILURE);
	}

	drmModeRes* resources = drmModeGetResources(state->DrmFd);
	if (resources == NULL) {
		printf("Failed to get DRM resources: %s\n", strerror(errno));
		close(state->DrmFd);
		exit(EXIT_FAILURE);
	}

	drmModeConnector* connector = NULL;
	for (uint32_t i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(state->DrmFd, resources->connectors[i]);
		if (connector->connection == DRM_MODE_CONNECTED) {
			break;
		}
		drmModeFreeConnector(connector);
		connector = NULL;
	}
	if (connector == NULL) {
		printf("Failed to find conected connector.\n");
		drmModeFreeResources(resources);
		close(state->DrmFd);
		exit(EXIT_FAILURE);
	}

	size_t area = 0;
	for (uint32_t i = 0; i < connector->count_modes; i++) {
		drmModeModeInfo* currentMode = &connector->modes[i];

		if (currentMode->type == DRM_MODE_TYPE_PREFERRED) {
			state->DrmMode = currentMode;
		}

		size_t currentArea = currentMode->hdisplay * currentMode->vdisplay;
		if (currentArea > area) {
			state->DrmMode = currentMode;
			area = currentArea;
		}
	}

	if (state->DrmMode == NULL) {
		printf("Failed to find valid connector mode.\n");
		drmModeFreeConnector(connector);
		drmModeFreeResources(resources);
		close(state->DrmFd);
		exit(EXIT_FAILURE);
	}

	drmModeEncoder* encoder = NULL;
	for (uint32_t i = 0; i < resources->count_encoders; i++) {
		encoder = drmModeGetEncoder(state->DrmFd, resources->encoders[i]);
		if (encoder->encoder_id == connector->encoder_id) {
			break;
		}
		drmModeFreeEncoder(encoder);
		encoder = NULL;
	}

	if (encoder == NULL) {
		uint32_t crtcId = FindCrtcIdForConnector(state, resources, connector);
		if (crtcId == 0) {
			printf("No CRTC found.\n");
			exit(EXIT_FAILURE);
		}
		state->DrmCrtcId = crtcId;
	} else {
		state->DrmCrtcId = encoder->crtc_id;
	}

	for (uint32_t i = 0; i < resources->count_crtcs; i++) {
		if (state->DrmCrtcId == resources->crtcs[i]) {
			state->DrmCrtcIndex = i;
			break;
		}
	}

	state->DrmConnectorId = connector->connector_id;
	drmModeFreeConnector(connector);
	drmModeFreeResources(resources);

	state->GbmDevice = gbm_create_device(state->DrmFd);
	state->SurfaceFormat = GBM_FORMAT_XRGB8888;

	state->GbmSurface = gbm_surface_create(state->GbmDevice, state->DrmMode->hdisplay, state->DrmMode->vdisplay, state->SurfaceFormat, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

	if (state->GbmSurface == NULL) {
		printf("Failed to create surface.\n");
		exit(EXIT_FAILURE);
	}

	state->SurfaceWidth = state->DrmMode->hdisplay;
	state->SurfaceHeight = state->DrmMode->vdisplay;

	const char* eglExts, *clientExts, *dpyExts;
	clientExts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	
	PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT;
	if (HasExt("EGL_EXT_platform_base", "eglGetPlatformDisplayEXT")) {
		eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	}
	if (eglGetPlatformDisplayEXT) {
		eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR, state->GbmDevice, NULL);
	} else {
		state->display = eglGetDisplay((void*)state->GbmDevice);
	}
	assert(state->display != EGL_NO_DISPLAY);

	result = eglInitialize(state->display, NULL, NULL);
	assert(result != EGL_FALSE);

	result = eglChooseConfig(state->display, configAttribs, &config, 1, &numConfigs);
	assert(result != EGL_FALSE);

	state->context = eglCreateContext(state->display, config, EGL_NO_CONTEXT, contextAttribs);
	assert(state->context != EGL_NO_CONTEXT);

	/*
	dstRect.x = 0;
	dstRect.y = 0;
	dstRect.width = state->screenWidth;
	dstRect.height = state->screenHeight;
	
	srcRect.x = 0;
	srcRect.y = 0;
	srcRect.width = state->screenWidth << 16;
	srcRect.height = state->screenHeight << 16;

	state->dispmanDisplay = vc_dispmanx_display_open(0);
	dispmanUpdate = vc_dispmanx_update_start(0);
	state->dispmanElement = vc_dispmanx_element_add(
			dispmanUpdate,
			state->dispmanDisplay,
			0,
			&dstRect,
			0,
			&srcRect,
			DISPMANX_PROTECTION_NONE,
			0,
			0,
			(DISPMANX_TRANSFORM_T)0);

	nativeWindow.element = state->dispmanElement;
	nativeWindow.width = state->screenWidth;
	nativeWindow.height = state->screenHeight;
	vc_dispmanx_update_submit_sync(dispmanUpdate);
	*/

	state->screenWidth = state->SurfaceWidth;
	state->screenHeight = state->SurfaceHeight;

	state->surface = eglCreateWindowSurface(state->display, config, (EGLNativeWindowType)state->GbmSurface, NULL);
	assert(state->surface != EGL_NO_SURFACE);

	result = eglMakeCurrent(state->display, state->surface, state->surface, state->context);
	assert(result != EGL_FALSE);

	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

	fprintf(stderr, "\e[0;32mGL Version: %s\e[0m\n", glGetString(GL_VERSION));
}

void ceeGraphicsShutdown(ceeGraphicsState* state) {
	DISPMANX_UPDATE_HANDLE_T dispmanUpdate;
	int result;

	glClear(GL_COLOR_BUFFER_BIT);
	eglSwapBuffers(state->display, state->surface);

	eglDestroySurface(state->display, state->surface);

	dispmanUpdate = vc_dispmanx_update_start(0);
	result = vc_dispmanx_element_remove(dispmanUpdate, state->dispmanElement);
	assert(result == 0);
	vc_dispmanx_update_submit_sync(dispmanUpdate);
	result = vc_dispmanx_display_close(state->dispmanDisplay);
	assert(result == 0);

	eglMakeCurrent(state->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(state->display, state->context);
	eglTerminate(state->display);
}

static EGLint ceeGraphicsCompileShader(GLenum type, const char source[], GLuint* shader) {
	GLint compiled;
	*shader = glCreateShader(type);
	if (*shader == 0) {
		return GL_FALSE;
	}

	glShaderSource(*shader, 1, &source, NULL);
	glCompileShader(*shader);
	glGetShaderiv(*shader, GL_COMPILE_STATUS, &compiled);

	if (!compiled) {
		GLint infoLength;

		glGetShaderiv(*shader, GL_INFO_LOG_LENGTH, &infoLength);
		
		if (infoLength > 1) {
			char* infoLog = (char*)malloc(sizeof(char) * infoLength);

			glGetShaderInfoLog(*shader, infoLength, NULL, infoLog);
			fprintf(stderr, "Failed to compile shader. OpenGL info log:\n%s\n", infoLog);
			fflush(stderr);

			free(infoLog);
		}
		glDeleteShader(*shader);
		return GL_FALSE;
	}
	return GL_TRUE;
}

EGLint ceeGraphicsCreateShaderProgram(
		const char vertexSource[],
		const char fragmentSource[],
		GLuint* program,
		const char* attributeNames[],
		uint32_t attributeLocations[],
		uint32_t attributeCount)
{
	GLuint vertexShader;
	GLuint fragmentShader;
	EGLint result;
	GLint linked;

	result = ceeGraphicsCompileShader(GL_VERTEX_SHADER, vertexSource, &vertexShader);
	if (result == GL_FALSE) { return result; }
	result = ceeGraphicsCompileShader(GL_FRAGMENT_SHADER, fragmentSource, &fragmentShader);
	if (result == GL_FALSE) { return result; }

	*program = glCreateProgram();
	if (*program == 0) {
		return GL_FALSE;
	}

	glAttachShader(*program, vertexShader);
	glAttachShader(*program, fragmentShader);
	for (uint32_t i = 0; i < attributeCount; i++) {
		glBindAttribLocation(*program, attributeLocations[i], attributeNames[i]);
	}
	glLinkProgram(*program);

	glGetProgramiv(*program, GL_LINK_STATUS, &linked);
	if (!linked) {
		GLint infoLength;

		glGetProgramiv(*program, GL_INFO_LOG_LENGTH, &infoLength);
		if (infoLength > 1) {
			char* infoLog = malloc(sizeof(char) * infoLength);

			glGetProgramInfoLog(*program, infoLength, NULL, infoLog);
			fprintf(stderr, "Failed to link program. OpenGL info log:\n%s\n", infoLog);
			fflush(stderr);

			free(infoLog);
		}
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);
		glDeleteProgram(*program);

		return GL_FALSE;
	}
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	return GL_TRUE;
}

void ceeGraphicsUseShaderProgram(uint32_t program) {
	glUseProgram(program);
}

static void ceeGraphicsSwapBuffers(ceeGraphicsState* state) {
	EGLBoolean result = eglSwapBuffers(state->display, state->surface);
	assert(result != EGL_FALSE);
}

void ceeGraphicsCreateVertexBuffer(uint32_t* buffer) {
	glGenBuffers(1, buffer);
}

void ceeGraphicsBindVertexBuffer(uint32_t buffer) {
	glBindBuffer(GL_ARRAY_BUFFER, buffer);
}

void ceeGraphicsUnbindVertexBuffer() {
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ceeGraphicsSetVertexBufferLayout(ceeGraphicsVertexBufferElement layout[], uint32_t elements, uint32_t stride) {
	for (uint32_t i = 0; i < elements; i++) {
		glVertexAttribPointer(i,
				getComponentCount(layout[i].type),
				dataTypeToGlBaseType(layout[i].type),
				layout[i].normalized ? GL_TRUE : GL_FALSE,
				stride,
				(void*)layout[i].offset);
		glEnableVertexAttribArray(i);
	}
}

void ceeGraphicsSetVertices(float* vertices, uint32_t size) {
	glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_DYNAMIC_DRAW);
}

void ceeGraphicsSetSubVertices(float* vertices, uint32_t size) {
	glBufferSubData(GL_ARRAY_BUFFER, 0, size, vertices);
}

void ceeGraphicsDeleteVertexBuffer(uint32_t* buffer) {
	glDeleteBuffers(1, buffer);
	*buffer = 0;
}

void ceeGraphicsCreateIndexBuffer(uint32_t* buffer) {
	glGenBuffers(1, buffer);
}

void ceeGraphicsBindIndexBuffer(uint32_t buffer) {
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
}

void ceeGraphicsUnbindIndexBuffer() {
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void ceeGraphicsSetIndices(uint16_t* indices, uint32_t size) {
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, indices, GL_STATIC_DRAW);
}

void ceeGraphicsDeleteIndexBuffer(uint32_t* buffer) {
	glDeleteBuffers(1, buffer);
	*buffer = 0;
}

void ceeGraphicsStartFrame(ceeGraphicsState* state) {
	glViewport(0, 0, state->screenWidth, state->screenHeight);
	glClear(GL_COLOR_BUFFER_BIT);
}

void ceeGraphicsFlushTriangles(uint32_t indicesCount) {
	glDrawElements(GL_TRIANGLES, indicesCount, GL_UNSIGNED_SHORT, (void*)0);
}

void ceeGraphicsFlushLines(uint32_t indicesCount) {
	glDrawElements(GL_LINES, indicesCount, GL_UNSIGNED_SHORT, (void*)0);
}

void ceeGraphicsEndFrame(ceeGraphicsState* state) {
	ceeGraphicsSwapBuffers(state);
}

void ceeGraphicsClearColor(float r, float g, float b, float a) {
	glClearColor(r, g, b, a);
}

static uint32_t FindCrtcIdForEncoder(const drmModeRes* resources, const drmModeEncoder* encoder) {
	for (uint32_t i = 0; i < resources->count_crtcs; i++) {
		const uint32_t crtcMask = i << i;
		const uint32_t crtcId = resources->crtcs[i];
		if (encoder->possible_crtcs & crtcMask) {
			return crtcId;
		}
	}

	return 0;
}

static uint32_t FindCrtcIdForConnector(const ceeGraphicsState* state, const drmModeRes* resources, const drmModeConnector* connector) {
	for (uint32_t i = 0; i < connector->count_encoders; i++) {
		const uint32_t encoderId = connector->encoders[i];
		drmModeEncoder* encoder = drmModeGetEncoder(state->DrmFd, encoderId);

		if (encoder != NULL) {
			const uint32_t crtcId = FindCrtcIdForEncoder(resources, encoder);

			drmModeFreeEncoder(encoder);
			if (crtcId != 0) {
				return crtcId;
			}
		}
	}
	return 0;
}

static bool HasExt(const char *extensionList, const char *ext) {
	const char *ptr = extensionList;
	int len = strlen(ext);

	if (ptr == NULL || *ptr == '\0')
		return false;

	while (true) {
		ptr = strstr(ptr, ext);
		if (!ptr)
			return false;

		if (ptr[len] == ' ' || ptr[len] == '\0')
			return true;

		ptr += len;
	}
}


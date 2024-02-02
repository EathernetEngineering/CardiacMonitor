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
#include <sys/time.h>

#include <bcm_host.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <gbm.h>

#define WEAK __attribute__((weak))
#define NSEC_PER_SEC 1000000000

WEAK union gbm_bo_handle
gbm_bo_get_handle_for_plane(struct gbm_bo *bo, int plane);

WEAK uint64_t
gbm_bo_get_modifier(struct gbm_bo *bo);

WEAK int
gbm_bo_get_plane_count(struct gbm_bo *bo);

WEAK uint32_t
gbm_bo_get_stride_for_plane(struct gbm_bo *bo, int plane);

WEAK uint32_t
gbm_bo_get_offset(struct gbm_bo *bo, int plane);

WEAK struct gbm_surface *
gbm_surface_create_with_modifiers(struct gbm_device *gbm,
                                  uint32_t width, uint32_t height,
                                  uint32_t format,
                                  const uint64_t *modifiers,
                                  const unsigned int count);
WEAK struct gbm_bo *
gbm_bo_create_with_modifiers(struct gbm_device *gbm,
                             uint32_t width, uint32_t height,
                             uint32_t format,
                             const uint64_t *modifiers,
                             const unsigned int count);


static EGLint ceeGraphicsCompileShader(GLenum type, const char source[], GLuint* shader);
static void ceeGraphicsSwapBuffers(ceeGraphicsState* state);
static int32_t FindDrmDevice(drmModeRes** resources);
static uint32_t FindCrtcIdForEncoder(const drmModeRes* resources, const drmModeEncoder* encoder);
static uint32_t FindCrtcIdForConnector(const ceeGraphicsState* state, const drmModeRes* resources, const drmModeConnector* connector);
static bool HasExt(const char *extensionList, const char *ext);
static int32_t GetDrmFbFromBo(struct gbm_bo* bo, struct gbm_bo** fb, uint32_t* fb_id);
static void PageFlipHandler(int32_t fd, uint32_t frame, uint32_t sec, uint32_t usec, void* data);

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

	EGLDisplay display;
	EGLContext context;
	EGLSurface surface;

	int32_t DrmFd;
	drmModeRes* DrmResources;
	struct DrmConnector DrmConnector;
	uint32_t DrmConnectorId;
	int32_t DrmCrtcId;
	uint32_t DrmCrtcIndex;
	drmModeModeInfo DrmMode;

	struct gbm_device* GbmDevice;
	struct gbm_surface* GbmSurface;
	uint32_t SurfaceWidth, SurfaceHeight;
	uint32_t SurfaceFormat;

	struct gbm_bo* GbmBo;
	struct gbm_bo* DrmFb;
	uint32_t DrmFbId;

	fd_set fds;
	drmEventContext DrmEventContext;

	size_t FrameIndex;
};

ceeGraphicsState* ceeGraphicsMallocState() {
	return calloc(1, sizeof(struct _ceeGraphicsState));
}

void ceeGraphicsFreeState(ceeGraphicsState* state) {
	free(state);
}

void ceeGraphicsInitialize(ceeGraphicsState* state) {
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

	drmModeRes* resources = drmModeGetResources(state->DrmFd);
	state->DrmFd = FindDrmDevice(&resources);
	if (state->DrmFd < 0) {
		printf("Failed to open DRM device.\n");
		exit(EXIT_FAILURE);
	}

	drmModeConnector* connector = NULL;
	for (uint32_t i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(state->DrmFd, resources->connectors[i]);
		if (connector->connection == DRM_MODE_CONNECTED) {
			printf("Using display: 0x%X\n", connector->connector_id);
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

	if (connector == NULL) {
		printf("Failed to find conected connector.\n");
		drmModeFreeResources(resources);
		close(state->DrmFd);
		exit(EXIT_FAILURE);
	}

	size_t area = 0;
	drmModeModeInfo* selectedMode = NULL;
	for (uint32_t i = 0; i < connector->count_modes; i++) {
		drmModeModeInfo* currentMode = &connector->modes[i];

		if (currentMode->type == DRM_MODE_TYPE_PREFERRED) {
			selectedMode = currentMode;
		}

		size_t currentArea = currentMode->hdisplay * currentMode->vdisplay;
		if (currentArea > area) {
			selectedMode = currentMode;
			area = currentArea;
		}
	}

	if (selectedMode == NULL) {
		printf("Failed to find valid connector mode.\n");
		drmModeFreeConnector(connector);
		drmModeFreeResources(resources);
		close(state->DrmFd);
		exit(EXIT_FAILURE);
	}
	state->DrmMode = *selectedMode;

	printf("Selected display mode %ux%u\n", state->DrmMode.hdisplay, state->DrmMode.vdisplay);
	fflush(stdout);

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
	state->DrmConnector.connector = connector;
	state->DrmResources = resources;

	state->GbmDevice = gbm_create_device(state->DrmFd);
	state->SurfaceFormat = GBM_FORMAT_XRGB8888;

	uint64_t modifier = DRM_FORMAT_MOD_LINEAR;

	if (gbm_surface_create_with_modifiers) {
		state->GbmSurface = gbm_surface_create_with_modifiers(state->GbmDevice, state->DrmMode.hdisplay, state->DrmMode.vdisplay, state->SurfaceFormat,&modifier, 1);
	}

	if (state->GbmSurface == NULL) {
		state->GbmSurface = gbm_surface_create(state->GbmDevice, state->DrmMode.hdisplay, state->DrmMode.vdisplay, state->SurfaceFormat, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	}

	if (state->GbmSurface == NULL) {
		printf("Failed to create surface.\n");
		exit(EXIT_FAILURE);
	}

	state->SurfaceWidth = state->DrmMode.hdisplay;
	state->SurfaceHeight = state->DrmMode.vdisplay;

	const char* eglExts, *clientExts, *dpyExts;
	clientExts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	
	PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT = NULL;
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

	printf("EGL Information:\n");
	printf("\tVERSION: \"%s\".\n", eglQueryString(state->display, EGL_VERSION));
	printf("\tVENDOR:  \"%s\".\n", eglQueryString(state->display, EGL_VENDOR));
	printf("\tDISPLAY: \"%p\".\n", state->display);
	fflush(stdout);

	result = eglBindAPI(EGL_OPENGL_ES_API);
	if (result == EGL_FALSE) {
		printf("Failed to bind api EGL_OPENGL_ES_API.\n");
	}

	result = eglChooseConfig(state->display, configAttribs, &config, 1, &numConfigs);
	if (result == EGL_FALSE) {
		printf("Failed to choose EGL config.\n");
	}

	state->context = eglCreateContext(state->display, config, EGL_NO_CONTEXT, contextAttribs);
	if (state->context == EGL_NO_CONTEXT) {
		printf("Failed to create EGL context.\n");
	}

	state->screenWidth = state->SurfaceWidth;
	state->screenHeight = state->SurfaceHeight;

	state->surface = eglCreateWindowSurface(state->display, config, (EGLNativeWindowType)state->GbmSurface, NULL);
	if (state->surface == EGL_NO_SURFACE) {
		printf("Failed to create window surface.\n");
	}

	result = eglMakeCurrent(state->display, state->surface, state->surface, state->context);
	if (result == EGL_FALSE) {
		printf("Failed to make EGL context current.\n");
	}

	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	eglSwapBuffers(state->display, state->surface);
	state->GbmBo = gbm_surface_lock_front_buffer(state->GbmSurface);
	result = GetDrmFbFromBo(state->GbmBo, &state->DrmFb, &state->DrmFbId);

	result = drmModeSetCrtc(state->DrmFd, state->DrmCrtcId, state->DrmFbId, 0, 0, &state->DrmConnectorId, 1, &state->DrmMode);

	state->DrmEventContext.page_flip_handler = PageFlipHandler;
	state->DrmEventContext.version = 2;
}

void ceeGraphicsShutdown(ceeGraphicsState* state) {
	glClear(GL_COLOR_BUFFER_BIT);
	eglSwapBuffers(state->display, state->surface);

	eglDestroySurface(state->display, state->surface);

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
				(void*)(intptr_t)layout[i].offset);
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

void ceeGraphicsFlushTriangles(uint32_t vertexCount) {
	glDrawArrays(GL_TRIANGLES, 0, vertexCount);
}

void ceeGraphicsFlushLines(uint32_t indicesCount) {
	glDrawElements(GL_LINES, indicesCount, GL_UNSIGNED_SHORT, (void*)0);
}

void ceeGraphicsFlushLineStrip(uint32_t vertexCount, uint32_t firstVertex) {
	glDrawArrays(GL_LINE_STRIP, firstVertex, vertexCount);
}

void ceeGraphicsEndFrame(ceeGraphicsState* state) {
	int32_t waitingForFlip = 1;
	eglSwapBuffers(state->display, state->surface);
	struct gbm_bo* nextBo = gbm_surface_lock_front_buffer(state->GbmSurface);
	if (GetDrmFbFromBo(nextBo, &state->DrmFb, &state->DrmFbId) != 0) {
		printf("GetDrmFbFromBo failed.\n");
	}

	int32_t result = drmModePageFlip(state->DrmFd, state->DrmCrtcId, state->DrmFbId, DRM_MODE_PAGE_FLIP_EVENT, &waitingForFlip);
	if (result) {
		printf("Failed to queue page flip: \"%s\"\n", strerror(-result));
		return;
	}

	while (waitingForFlip) {
		FD_ZERO(&state->fds);
		FD_SET(0, &state->fds);
		FD_SET(state->DrmFd, &state->fds);

		result = select(state->DrmFd + 1, &state->fds, NULL, NULL, NULL);
		if (result < 0) {
			printf("Select erorr: \"%s\"\n", strerror(errno));
			return;
		} else if (result == 0) {
			printf("Select timeout.\n");
			return;
		} else if (FD_ISSET(0, &state->fds)) {
			printf("User interrupted.\n");
			return;
		}
		drmHandleEvent(state->DrmFd, &state->DrmEventContext);
	}

	gbm_surface_release_buffer(state->GbmSurface, state->GbmBo);
	state->GbmBo = nextBo;

	state->FrameIndex++;
}

void ceeGraphicsClearColor(float r, float g, float b, float a) {
	glClearColor(r, g, b, a);
}

#define MAX_DRM_DEVICES 64

static int32_t FindDrmDevice(drmModeRes** resources) {
	drmDevicePtr devices[MAX_DRM_DEVICES] = { NULL };
	int32_t deviceCount, fd = -1;

	deviceCount = drmGetDevices2(0, devices, MAX_DRM_DEVICES);
	if (deviceCount < 0) {
		printf("Failed to find any DRM devices: %s\n", strerror(-deviceCount));
		return -1;
	}

	for (uint32_t i = 0; i < deviceCount; i++) {
		drmDevicePtr currentDevice = devices[i];

		if (!(currentDevice->available_nodes & (1 << DRM_NODE_PRIMARY))) {
			continue;
		}
		fd = open(currentDevice->nodes[DRM_NODE_PRIMARY], O_RDWR);
		if (fd < 0) {
			continue;
		}
		*resources = drmModeGetResources(fd);
		if (*resources != NULL) {
			printf("Using DRM device \"%s\".\n", currentDevice->nodes[DRM_NODE_PRIMARY]);
			break;
		}
		close(fd);
		fd = -1;
	}
	drmFreeDevices(devices, deviceCount);
	return fd;
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

static void DestroyGbmUserDataCallback(struct gbm_bo* bo, void* userData) {
	(void)bo; // Supress compiler unused parameter warning.
	free(userData);
}

static int32_t GetDrmFbFromBo(struct gbm_bo* bo, struct gbm_bo** fb, uint32_t* fbId) {
	int drmFd = gbm_device_get_fd(gbm_bo_get_device(bo));
	struct drmFbInfo {
		struct gbm_bo* bo;
		uint32_t id;
	} *fbInfo;
	fbInfo = gbm_bo_get_user_data(bo);

	if (fbInfo) {
		*fb = fbInfo->bo;
		*fbId = fbInfo->id;
		return 0;
	}

	fbInfo = calloc(1, sizeof(*fbInfo));
	*fb = bo;
	*fbId = 0;

	uint32_t width = gbm_bo_get_width(bo);
	uint32_t height = gbm_bo_get_height(bo);
	uint32_t format = gbm_bo_get_format(bo);
	uint32_t strides[4] = { 0 };
	uint32_t offsets[4] = { 0 };
	uint32_t handles[4] = { 0 };
	uint32_t flags = 0;
	int32_t result = -1;

	if (gbm_bo_get_handle_for_plane && gbm_bo_get_modifier &&
		gbm_bo_get_plane_count && gbm_bo_get_stride_for_plane &&
		gbm_bo_get_offset) {
		uint64_t modifiers[4] = {0};
		modifiers[0] = gbm_bo_get_modifier(bo);
		const int planeCount = gbm_bo_get_plane_count(bo);
		for (uint32_t i = 0; i < planeCount; i++) {
			handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
			strides[i] = gbm_bo_get_stride_for_plane(bo, i);
			offsets[i] = gbm_bo_get_offset(bo, i);
			modifiers[i] = modifiers[0];
		}

		if (modifiers[0] && (modifiers[0] != DRM_FORMAT_MOD_INVALID)) {
			flags = DRM_MODE_FB_MODIFIERS;
			printf("Using modifier %" PRIx64 "\n", modifiers[0]);
		}

		result = drmModeAddFB2WithModifiers(drmFd, width, height, format, handles, strides, offsets, modifiers, fbId, 0);
	}

	if (result) {
		if (flags) {
			printf("Modifiers failed.\n");
		}
		memcpy(handles, (uint32_t[4]){gbm_bo_get_handle(bo).u32, 0, 0, 0}, sizeof(uint32_t)*4);
		memcpy(strides, (uint32_t[4]){gbm_bo_get_stride(bo), 0, 0, 0}, sizeof(uint32_t)*4);
		memset(offsets, 0, sizeof(uint32_t)*4);

		result = drmModeAddFB2(drmFd, width, height, format, handles, strides, offsets, fbId, 0);
	}

	if (result) {
		printf("Failed to create fb %s\n", strerror(errno));
		free(fbInfo);
		return -1;
	}
	fbInfo->bo = *fb;
	fbInfo->id = *fbId;

	gbm_bo_set_user_data(bo, fbInfo, DestroyGbmUserDataCallback);

	return 0;
}

static void PageFlipHandler(int32_t fd, uint32_t frame, uint32_t sec, uint32_t usec, void* data) {
	(void)fd, (void)frame, (void)sec, (void)usec; // surpress compiler unuesed parameter warning.

	int32_t* waitingForFlip = (int32_t*)data;
	*waitingForFlip = 0;
}


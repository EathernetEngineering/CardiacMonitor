#include "fontRenderer.h"

#include "external/stb/stb_rect_pack.h"
#include "external/stb/stb_truetype.h"

#include "stdio.h"
#include "stdlib.h"
#include "fcntl.h"
#include "unistd.h"
#include "string.h"
#include "errno.h"

#include "graphics.h"
#include <X11/Xlib.h>

#define MAX_INDICES 1020
#define MAX_VERTICES 680

static const char g_VertexShader[] =
		"attribute vec4 aPosition;\n"
		"attribute vec4 aColor;\n"
		"attribute vec2 aTexCoords;\n"
		"\n"
		"varying vec4 vColor;\n"
		"varying vec2 vTexCoords;\n"
		"\n"
		"void main() {\n"
		"	gl_Position = aPosition;\n"
		"	vColor = aColor;\n"
		"	vTexCoords = aTexCoords;\n"
		"}\n";
static const char g_FragmentShader[] =
		"precision mediump float;\n"
		"\n"
		"varying vec4 vColor;\n"
		"varying vec2 vTexCoords;\n"
		"\n"
		"uniform sampler2D uSampler;\n"
		"\n"
		"void main() {\n"
		"	vec4 texelColor = vec4(1.0f, 1.0f, 1.0f, texture2D(uSampler, vTexCoords).w);\n"
		"	gl_FragColor = vColor * texelColor;\n"
		"}\n";

static uint32_t g_ShaderProgram;
static uint32_t g_FontTexId;
static uint32_t g_Vbo, g_Ibo;
static float* g_Vertices;
static ceeGraphicsVertexBufferElement* g_VboLayout;
static uint32_t g_ScreenWidth;
static uint32_t g_ScreenHeight;


struct _ceeFont {
	stbtt_bakedchar* bakedChars;
	uint32_t fontTexId;
	uint32_t texWidth, texHeight;
};

int32_t ceeFontRendererIntialize(uint32_t screenWidth, uint32_t screenHeight) {
	const char* shaderAttribNames[] = {
		"aPosition",
		"aColor",
		"aTexCoords"
	};
	uint32_t shaderAttribLocations[] = {
		0,
		1,
		2
	};
	uint32_t shaderAttribCount = 3;

	uint32_t textShaderProgram;

	if (ceeGraphicsCreateShaderProgram(g_VertexShader,
				g_FragmentShader,
				&g_ShaderProgram,
				shaderAttribNames,
				shaderAttribLocations,
				shaderAttribCount)
			== 0) {
		printf("Failed to compile shaders for font rendering.\n");
		return -1;
	}

	g_VboLayout = (ceeGraphicsVertexBufferElement*)calloc(3, sizeof(ceeGraphicsVertexBufferElement));
	g_VboLayout[0].type = GL_TYPE_FLOAT4;
	g_VboLayout[0].size = 4 * sizeof(float);
	g_VboLayout[0].offset = 0;
	g_VboLayout[0].normalized = 0;
	g_VboLayout[1].type = GL_TYPE_FLOAT4;
	g_VboLayout[1].size = 4 * sizeof(float);
	g_VboLayout[1].offset = 4 * sizeof(float);
	g_VboLayout[1].normalized = 0;
	g_VboLayout[2].type = GL_TYPE_FLOAT2;
	g_VboLayout[2].size = 2 * sizeof(float);
	g_VboLayout[2].offset = 8 * sizeof(float);
	g_VboLayout[2].normalized = 0;

	uint16_t* indices = calloc(MAX_INDICES, sizeof(uint16_t));
	uint16_t idx = 0;
	for (uint16_t i = 0; i < MAX_INDICES; i += 6) {
		indices[i + 0] = idx + 0;
		indices[i + 1] = idx + 1;
		indices[i + 2] = idx + 2;
		indices[i + 3] = idx + 2;
		indices[i + 4] = idx + 3;
		indices[i + 5] = idx + 0;
		idx += 4;
	}

	ceeGraphicsCreateIndexBuffer(&g_Ibo);
	ceeGraphicsBindIndexBuffer(g_Ibo);
	ceeGraphicsSetIndices(indices, MAX_INDICES * sizeof(uint16_t));

	ceeGraphicsCreateVertexBuffer(&g_Vbo);
	ceeGraphicsBindVertexBuffer(g_Vbo);
	ceeGraphicsSetVertices(NULL, MAX_VERTICES * 10 * sizeof(float));

	free(indices);
	g_Vertices = calloc(MAX_VERTICES, 10 * sizeof(float));

	g_ScreenWidth = screenWidth;
	g_ScreenHeight = screenHeight;

	return 0;
}

void ceeFontRendererShutdown() {
	if (g_VboLayout)
		free(g_VboLayout);

	if (g_Vertices)
		free(g_Vertices);
}

ceeFont* ceeFontRendererCreateFont(const char* fontFile, float scale, uint32_t texWidth, uint32_t texHeight) {
	ceeFont* font = calloc(1, sizeof(ceeFont));
	font->texWidth = texWidth;
	font->texHeight = texHeight;

	int32_t ttfFile = open(fontFile, O_RDONLY);
	if (ttfFile < 0) {
		printf("Failed to open font \"%s\". Error: \"%s\" (%d)\n", fontFile, strerror(errno), errno);
		return NULL;
	}
	size_t ttfFileSize = lseek(ttfFile, 0, SEEK_END);
	lseek(ttfFile, 0, SEEK_SET);

	uint8_t* ttfData = (uint8_t*)malloc(ttfFileSize);
	if (read(ttfFile, ttfData, ttfFileSize) < ttfFileSize) {
		printf("Failed to read entire ttf file\n");
		free(ttfData);
		close(ttfFile);
		return NULL;
	}
	close(ttfFile);
	ttfFile = 0;

	font->bakedChars = (stbtt_bakedchar*)calloc(93, sizeof(stbtt_bakedchar));
	uint8_t* pixels = (uint8_t*)calloc(font->texWidth * font->texHeight, sizeof(uint8_t));
	stbtt_BakeFontBitmap(ttfData, 0, scale, pixels, font->texWidth, font->texHeight, 32, 93, font->bakedChars);
	free(ttfData);
	ttfData = NULL;

	ceeGraphicsCreateTexture(&font->fontTexId);
	ceeGraphicsBindTexture(font->fontTexId);
	ceeGraphicsSetTextureData(font->texWidth, font->texHeight, GL_FORMAT_ALPHA, GL_TYPE_UNSIGNED_BYTE, pixels);
	free(pixels);

	return font;
}

void ceeFontRendererDeleteFont(ceeFont* font) {
	if (font) {
		if (font->bakedChars) {
			free(font->bakedChars);
		}
		ceeGraphicsDeleteTexture(&font->fontTexId);
		free(font);
	}
}

void ceeFontRendererDraw(ceeFont* font, const char* str, float* x, float* y) {
	uint32_t chars = strlen(str);
	if (chars == 0)
		return;

	const size_t quadVerticesSize = 10 * 4;

	ceeGraphicsBindVertexBuffer(g_Vbo);

	for (uint32_t i = 0; i < chars; i++) {
		stbtt_aligned_quad q;
		stbtt_GetBakedQuad(font->bakedChars, font->texWidth, font->texHeight, *(str+i) - 32, x, y, &q, 1);

		float vertices[] = {
			q.x0/(g_ScreenWidth / 2.0f) - 1.0f, q.y1/(g_ScreenHeight / 2.0f) - 1.0f, 0.0f, 1.0f,    0.0f, 1.0f, 0.0f, 1.0f,    q.s0, q.t0,
			q.x0/(g_ScreenWidth / 2.0f) - 1.0f, q.y0/(g_ScreenHeight / 2.0f) - 1.0f, 0.0f, 1.0f,    0.0f, 1.0f, 0.0f, 1.0f,    q.s0, q.t1,
			q.x1/(g_ScreenWidth / 2.0f) - 1.0f, q.y0/(g_ScreenHeight / 2.0f) - 1.0f, 0.0f, 1.0f,    0.0f, 1.0f, 0.0f, 1.0f,    q.s1, q.t1,
			q.x1/(g_ScreenWidth / 2.0f) - 1.0f, q.y1/(g_ScreenHeight / 2.0f) - 1.0f, 0.0f, 1.0f,    0.0f, 1.0f, 0.0f, 1.0f,    q.s1, q.t0,
		};
		memcpy(g_Vertices + (i * quadVerticesSize), vertices, quadVerticesSize * sizeof(float));
	}
	ceeGraphicsSetSubVertices(g_Vertices, quadVerticesSize * chars * sizeof(float));
	ceeGraphicsBindIndexBuffer(g_Ibo);
	ceeGraphicsUseShaderProgram(g_ShaderProgram);
	ceeGraphicsSetVertexBufferLayout(g_VboLayout, 3, 10 * sizeof(float));
	ceeGraphicsBindTexture(font->fontTexId);
	ceeGraphicsFlushQuads(chars * 6);
}


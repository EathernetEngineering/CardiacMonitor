#ifndef CEE_AUDIO_FORMAT_H_
#define CEE_AUDIO_FORMAT_H_

#include "util.h"

#define VOC_MAGIC_STRING "Creative Voice File\x1A"
#define VOC_ACTUAL_VERSION 0x010A
#define VOC_SAMPLE_SIZE 8

#define VOC_MODE_MONO 0
#define VOC_MODE_STEREO 1

typedef struct {
	uint8_t magic[20];
	uint16_t hearerLen;
	uint16_t version;
	uint16_t codedVersion;
} VocHeader;

typedef struct {
	uint8_t type;
	uint8_t lenLow;
	uint8_t lenMed;
	uint8_t lenHigh;
} VocBlockType;

typedef struct {
	uint8_t tc;
	uint8_t pack;
} VocVoiceData;

typedef struct {
	uint16_t tc;
	uint8_t pack;
	uint8_t mode;
} VocExtType;

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define MAGIC_ENDIAN(x, y, z, w) ((x) | ((y) << 8) | ((z) << 16) | ((w) << 24))
#elif __BYTE_ORDER == __BIG_ENDIAN
#define MAGIC_ENDIAN(x, y, z, w) ((w) | ((z) << 8) | ((y) << 16) | ((x) << 24))
#else
#error "Wrong Endian"
#endif

#define WAV_RIFF                 MAGIC_ENDIAN('R', 'I', 'F', 'F')
#define WAV_WAVE                 MAGIC_ENDIAN('W', 'A', 'V', 'E')
#define WAV_FMT                  MAGIC_ENDIAN('f', 'm', 't', ' ')
#define WAV_DATA                 MAGIC_ENDIAN('d', 'a', 't', 'a')

#define WAV_FMT_PCM              0x0001
#define WAV_FMT_IEEE_FLOAT       0x0003
#define WAV_FMT_DOLBY_AC3_SPDIF  0x0092
#define WAV_FMT_EXTENSIBLE       0xFFFE

typedef struct {
	char magic[4];
	uint32_t length;
	char type[4];
} __attribute__((packed)) WaveHeader;

typedef struct {
	uint16_t format;
	uint16_t channels;
	uint32_t sampleFreq;
	uint32_t bytePerSecond;
	uint16_t bytePerSample;
	uint16_t bitPerSample;
} __attribute__((packed)) WaveFmtBody;

typedef struct {
	char type[4];
	uint32_t length;
} __attribute__((packed)) WaveChunkHeader;

#define AU_MAGIC MAGIC_ENDIAN('.', 's', 'n', 'd')

#define AU_FMT_ULAW 1
#define AU_FMT_LINT8 2
#define AU_FMT_LINT16 3

typedef struct {
	uint32_t magic;
	uint32_t headerSize;
	uint32_t dataSize;
	uint32_t encoding;
	uint32_t sampleRate;
	uint32_t channels;
} AuHeader;

#endif


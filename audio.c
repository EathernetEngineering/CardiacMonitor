#include "audio.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

#include <alsa/asoundlib.h>
#include <alsa/pcm.h>
#include <alsa/error.h>

#include "audioFormat.h"
#include "util.h"

#define MAX_AUDIO_BUFFER_SIZE 0x800000 

static void XRun(snd_pcm_t* handle, int32_t monotonic);
static void ParseWaveHeaders(ceeAudioPlayer* player,
							 ceeAudioOutStream* stream,
							 int32_t fd,
							 off_t* dataChunkStart);
static void Prepare(ceeAudioPlayer* player, ceeAudioOutStream* stream);
static void PlayBuffer(ceeAudioPlayer* player, ceeAudioOutStream* stream);
static void PlayStream(ceeAudioPlayer* player, ceeAudioOutStream* stream);

struct _ceeAudioPlayer {
	snd_pcm_t* handle;

	snd_pcm_hw_params_t* hwParams;
	snd_pcm_sw_params_t* swParams;
	int32_t monotonic;

	snd_pcm_uframes_t bufferSize;
	snd_pcm_uframes_t chunkSize;
	uint16_t frameSize;
};

struct _ceeAudioStream {
	int32_t fd;
	off_t audioStart;
	off_t currentOffset; // relative to audioStart

	uint8_t* audioBuffer;
	size_t audioBufferSize;

	int32_t wholeBufferLoaded;

	snd_pcm_hw_params_t* hwParams;
	snd_pcm_sw_params_t* swParams;

	uint32_t rate;
	uint32_t channels;

	uint32_t bytesPerSample;
};

ceeAudioPlayer* ceeAudioAllocatePlayer() {
	return calloc(1, sizeof(struct _ceeAudioPlayer));
}

ceeAudioInStream* ceeAudioAllocateInStream() {
	return calloc(1, sizeof(struct _ceeAudioStream));
}

ceeAudioOutStream* ceeAudioAllocateOutStream() {
	return calloc(1, sizeof(struct _ceeAudioStream));
}

void ceeAudioFreePlayer(ceeAudioPlayer* player) {
	free(player);
}

void ceeAudioFreeInStream(ceeAudioInStream* stream) {
	if (stream->hwParams) {
		snd_pcm_hw_params_free(stream->hwParams);
		stream->hwParams = NULL;
	}
	if (stream->swParams) {
		snd_pcm_sw_params_free(stream->swParams);
		stream->swParams = NULL;
	}
	if (stream)
		free(stream);
}

void ceeAudioFreeOutStream(ceeAudioOutStream* stream) {
	if (stream->hwParams) {
		snd_pcm_hw_params_free(stream->hwParams);
		stream->hwParams = NULL;
	}
	if (stream->swParams) {
		snd_pcm_sw_params_free(stream->swParams);
		stream->swParams = NULL;
	}
	if (stream->audioBuffer) {
		free(stream->audioBuffer);
		stream->audioBuffer = NULL;
	}
	if (stream)
		free(stream);
}

void ceeAudioInitialize(ceeAudioPlayer* player) {
	int32_t result;
	const char* device = "default";
	result = snd_pcm_open(&player->handle, device, SND_PCM_STREAM_PLAYBACK, 0);
	snd_pcm_hw_params_malloc(&player->hwParams);
	snd_pcm_sw_params_malloc(&player->swParams);
	assert(result >= 0 && "Failed to open PCM device");
}

void ceeAudioShutdown(ceeAudioPlayer* player) {
	if (player->hwParams) {
		snd_pcm_hw_params_free(player->hwParams);
		player->hwParams = NULL;
	}
	if (player->swParams) {
		snd_pcm_sw_params_free(player->swParams);
		player->swParams = NULL;
	}
	snd_pcm_close(player->handle);
};

void ceeAudioOpenWav(ceeAudioPlayer* player, ceeAudioOutStream* stream, const char* filename) {
	ssize_t result;
	WaveChunkHeader chunkHeader;

	stream->fd = open(filename, O_RDONLY);

	ParseWaveHeaders(player, stream, stream->fd, &stream->audioStart);
	

	// Do audio buffer read.
	result = read(stream->fd, &chunkHeader, sizeof(WaveChunkHeader));
	assert(result == sizeof(WaveChunkHeader) && "Failed to read header. Corrupt file?");
	while (chunkHeader.type != WAV_DATA) {
		lseek(stream->fd, LE_INT(chunkHeader.length), SEEK_CUR);
		read(stream->fd, &chunkHeader, sizeof(WaveChunkHeader));
		assert(result == sizeof(WaveChunkHeader) && "Failed to read header. EOF?");
	}

	if (LE_INT(chunkHeader.length) <= MAX_AUDIO_BUFFER_SIZE) { 
		stream->audioBufferSize = LE_INT(chunkHeader.length);
		stream->audioBuffer = malloc(stream->audioBufferSize);
		assert(stream->audioBuffer != NULL);

		result = read(stream->fd, stream->audioBuffer, stream->audioBufferSize);
		assert(result == stream->audioBufferSize);

		close(stream->fd);

		stream->wholeBufferLoaded = 1;
	} else {
		assert(!"Not yet implemented");
	}
}

void ceeAudioPlay(ceeAudioPlayer* player, ceeAudioOutStream* stream) {
	if (stream->wholeBufferLoaded)
		PlayBuffer(player, stream);
	else
		PlayStream(player, stream);
}

static void XRun(snd_pcm_t* handle, int32_t monotonic) {
	snd_pcm_status_t* status;
	snd_pcm_status_alloca(&status);
	int32_t result;
	if ((result = snd_pcm_status(handle, status)) < 0) {
		printf("Failed to get status: \"%s\" (%i)", snd_strerror(result), result);
	}

	if (snd_pcm_status_get_state(status) == SND_PCM_STATE_XRUN) {
		if (monotonic) {
			struct timespec now, tstamp, diff;
			clock_gettime(CLOCK_MONOTONIC, &now);
			snd_pcm_status_get_trigger_htstamp(status, &tstamp);
			TimespecSub(&diff, &tstamp, &now);

			printf("Underrun!!! (at least %.3f long)\n", diff.tv_sec * 1000.f + diff.tv_nsec / 1000000.f);
		} else {
			struct timeval now, tstamp, diff;
			gettimeofday(&now, NULL);
			snd_pcm_status_get_trigger_tstamp(status, &tstamp);
			TimevalSub(&diff, &tstamp, &now);
			printf("Underrun!!! (at least %.3f long)\n", diff.tv_sec * 1000.f + diff.tv_usec / 1000.f);
		}
		if ((result = snd_pcm_prepare(handle)) < 0) {
			printf("XRun: prepare error: \"%s\" (%i)", snd_strerror(result), result);
			fflush(stdout);
			assert(0);
		}
		return;
	} if (snd_pcm_status_get_state(status) == SND_PCM_STATE_DRAINING) {
		printf("Draining");
	}
	assert(0);
}

static void ParseWaveHeaders(ceeAudioPlayer* player,
							 ceeAudioOutStream* stream,
							 int32_t fd,
							 off_t* dataChunkStart) {
	uint8_t* fileBuffer = NULL;
	size_t fileSize = 0;
	snd_pcm_uframes_t bufferSize = 0;
	snd_pcm_uframes_t periodSize = 0;
	uint32_t rate = 0;
	WaveFmtBody fmt;
	WaveChunkHeader chunkHeader;
	snd_pcm_format_t format;
	ssize_t result = 0;

	fileSize = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);

	memset(&fmt, 0, sizeof(WaveFmtBody));
	memset(&chunkHeader, 0, sizeof(WaveChunkHeader));

	fileBuffer = malloc(sizeof(WaveHeader));
	assert(fileBuffer != 0 && "malloc failed");

	result = read(fd, fileBuffer, sizeof(WaveHeader));
	assert(result == sizeof(WaveHeader) && "Failed to read from file");

	assert(((WaveHeader*)fileBuffer)->magic == WAV_RIFF && "Not a WAVE file");
	assert(((WaveHeader*)fileBuffer)->type  == WAV_WAVE && "Not a WAVE file");

	free(fileBuffer);

	read(fd, &chunkHeader, sizeof(WaveChunkHeader));
	while (chunkHeader.type != WAV_FMT) {
		lseek(fd, LE_INT(chunkHeader.length), SEEK_CUR);
		result = read(fd, &chunkHeader, sizeof(WaveChunkHeader));
		assert(result == sizeof(WaveChunkHeader) && "Read Failed. EOF?");
	}

	result = read(fd, &fmt, sizeof(WaveFmtBody));
	assert(result == sizeof(WaveFmtBody) && "Read Failed. Corrupt file?");

	snd_pcm_hw_params_malloc(&stream->hwParams);
	snd_pcm_sw_params_malloc(&stream->swParams);

	result = snd_pcm_hw_params_any(player->handle, stream->hwParams);
	assert(result >= 0 && "Failed to get hw params");

	result = snd_pcm_hw_params_set_access(player->handle, stream->hwParams,
									   SND_PCM_ACCESS_RW_INTERLEAVED);
	assert(result >= 0 && "Failed to set pcm access");

	switch (LE_SHORT(fmt.bitPerSample)) {
		case 8:
			format = SND_PCM_FORMAT_U8;
			break;

		case 16:
			format = SND_PCM_FORMAT_S16_LE;
			break;

		case 24:
			switch(LE_SHORT(fmt.bytePerSample) / LE_SHORT(fmt.channels)) {
				case 3:
					format = SND_PCM_FORMAT_S24_3LE;
					break;

				case 4:
					format = SND_PCM_FORMAT_S24_LE;
					break;

				default:
					assert(0);
			}

		case 32:
			if (LE_SHORT(fmt.format) == WAV_FMT_PCM) {
				format = SND_PCM_FORMAT_S32_LE;
				break;
			} else if (LE_SHORT(fmt.format) == WAV_FMT_IEEE_FLOAT) {
				format = SND_PCM_FORMAT_FLOAT_LE;
				break;
			}
			// fall through

		default:
			assert(!"Invalid format");
	}

	result = snd_pcm_hw_params_set_format(player->handle, stream->hwParams, format);
	assert(result >= 0 && "Failed to set format");

	result = snd_pcm_hw_params_set_channels(player->handle, stream->hwParams,
										 LE_SHORT(fmt.channels));
	assert(result >= 0 && "Failed to set channel count");
	stream->channels = LE_SHORT(fmt.channels);

	stream->rate = LE_INT(fmt.sampleFreq);
	result = snd_pcm_hw_params_set_rate_near(player->handle, stream->hwParams, &stream->rate, 0);
	assert(result >= 0 && "Failed to set frequency");

	if (stream->rate != LE_INT(fmt.sampleFreq)) {
		printf("Sample rate mismatch! Requested rate: %u\tActual rate: %u\n",
		 LE_INT(fmt.sampleFreq), stream->rate);
	}

	result = snd_pcm_hw_params_get_buffer_size_max(stream->hwParams, &bufferSize);
	assert(result >= 0 && "Failed to get buffer size");

	periodSize = bufferSize / 4;
	result = snd_pcm_hw_params_set_period_size_near(player->handle, stream->hwParams,
												 &periodSize, 0);
	assert(result >= 0 && "Failed to set period size");

	result = snd_pcm_hw_params_set_buffer_size_near(player->handle, stream->hwParams,
												 &bufferSize);
	assert(result >= 0 && "Failed to set buffer size");

	result = snd_pcm_hw_params(player->handle, stream->hwParams);
	assert(result >= 0 && "Failed to set hw params");

	stream->bytesPerSample = fmt.bitPerSample / 8;
}

static void Prepare(ceeAudioPlayer* player, ceeAudioOutStream* stream) {
	ssize_t result = 0;
	snd_pcm_hw_params_copy(player->hwParams, stream->hwParams);
	snd_pcm_sw_params_copy(player->swParams, stream->swParams);

	result = snd_pcm_hw_params(player->handle, player->hwParams);
	assert(result >= 0 && "Failed to copy hw params");

	snd_pcm_hw_params_get_period_size(player->hwParams, &player->chunkSize, 0);
	snd_pcm_hw_params_get_buffer_size(player->hwParams, &player->bufferSize);
	player->frameSize = stream->bytesPerSample;

	player->monotonic = snd_pcm_hw_params_is_monotonic(player->hwParams);
}

static void PlayBuffer(ceeAudioPlayer* player, ceeAudioOutStream* stream) {
	int32_t i = 0;
	int32_t result;
	
	Prepare(player, stream);
	const uint32_t chunkBytes = player->chunkSize * player->frameSize;

	while (i < stream->audioBufferSize) {
		if (stream->audioBufferSize - i < chunkBytes) {
			result = snd_pcm_writei(player->handle, stream->audioBuffer + i,
						   (stream->audioBufferSize - i) / player->frameSize);
			if (result < 0) {
				if (result == -EPIPE) {
					XRun(player->handle, player->monotonic);
				}
				else {
					printf("Failed to write to pcm: %s (%i)\n", snd_strerror(result), result);
					fflush(stdout);
					assert(0);
				}
			}
			break;
		}
		result = snd_pcm_writei(player->handle, stream->audioBuffer + i, player->chunkSize);
		if (result < 0) {
			if (result == -EPIPE) {
				XRun(player->handle, player->monotonic);
			}
			else {
				printf("Failed to write to pcm: %s (%i)\n", snd_strerror(result), result);
				fflush(stdout);
				assert(0);
			}
		}
		if (result > 0) {
			i += result * player->frameSize;
		}
	}
	snd_pcm_drain(player->handle);
	snd_pcm_prepare(player->handle);
}

static void PlayStream(ceeAudioPlayer* player, ceeAudioOutStream* stream) {
	(void)player;
	(void)stream;
	assert(!"Not yet implemented.");
}


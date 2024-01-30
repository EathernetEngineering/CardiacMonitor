#include "audio.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

#include <alsa/asoundlib.h>
#include <alsa/pcm.h>
#include <alsa/error.h>

#include "audioFormat.h"
#include "util.h"

static void XRun(snd_pcm_t* handle, int32_t monotonic);

struct _ceeAudioState {
	snd_pcm_t* handle;
	int32_t rate;
	int32_t channels;

	snd_pcm_hw_params_t* hwParams;
	snd_pcm_sw_params_t* swParams;
	int monotonic;

	snd_pcm_uframes_t bufferSize;
	snd_pcm_uframes_t chunkSize;
	uint16_t frameSize;

	uint8_t* audioBuffer;
	size_t audioBufferSize;
};

ceeAudioState* ceeAudioMallocState() {
	return calloc(1, sizeof(ceeAudioState));
}

void ceeAudioFreeState(ceeAudioState* state) {
	free(state);
}

void ceeAudioInitialize(ceeAudioState* state) {
	int32_t result;
	result = snd_pcm_open(&state->handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
	assert(result >= 0);
}

void ceeAudioShutdown(ceeAudioState* state) {
	if (state->audioBuffer) {
		free(state->audioBuffer);
		state->audioBuffer = NULL;
		state->audioBufferSize = 0;
	}
	if (state->hwParams) {
		free(state->hwParams);
		state->hwParams = NULL;
	}
	if (state->swParams) {
		free(state->swParams);
		state->swParams = NULL;
	}
	snd_pcm_close(state->handle);
};

void ceeAudioOpenWav(ceeAudioState* state, const char* filename) {
	uint8_t* fileBuffer;
	ssize_t result;
	int32_t fd;
	WaveFmtBody fmt;
	WaveChunkHeader chunkHeader;
	snd_pcm_uframes_t bufferSize;
	snd_pcm_uframes_t periodSize;

	fileBuffer = malloc(sizeof(WaveHeader));
	assert(fileBuffer != 0);

	fd = open(filename, O_RDONLY);
	assert(fd != -1);

	result = read(fd, fileBuffer, sizeof(WaveHeader));
	assert(result == sizeof(WaveHeader));

	result = read(fd, &chunkHeader, sizeof(WaveChunkHeader));
	assert(result == sizeof(WaveChunkHeader));

	assert(memcmp(chunkHeader.type, "fmt ", 4) == 0);
	assert(chunkHeader.length == 16);

	result = read(fd, &fmt, sizeof(WaveFmtBody));
	assert(result == sizeof(WaveFmtBody));

	// Do FMT/Config.
	snd_pcm_hw_params_malloc(&state->hwParams);
	snd_pcm_sw_params_malloc(&state->swParams);

	// Read hw params.
	result = snd_pcm_hw_params_any(state->handle, state->hwParams);
	assert(result >= 0);

	// Set hw params.
	result = snd_pcm_hw_params_set_access(state->handle, state->hwParams,
			SND_PCM_ACCESS_RW_INTERLEAVED);
	assert(result >= 0);

	snd_pcm_format_t format;
	switch(LE_SHORT(fmt.bitPerSample)) {
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
			assert(0);
	}

	result = snd_pcm_hw_params_set_format(state->handle, state->hwParams,
			format);
	assert(result >= 0);

	result = snd_pcm_hw_params_set_channels(state->handle, state->hwParams,
			LE_SHORT(fmt.channels));
	assert(result >= 0);

	uint32_t rate = LE_INT(fmt.sampleFreq);
	result = snd_pcm_hw_params_set_rate_near(state->handle, state->hwParams,
			&rate, 0);
	assert(result >= 0);
	if (rate != LE_INT(fmt.sampleFreq)) {
		// TODO warn about rate mismatch.
	}

	result = snd_pcm_hw_params_get_buffer_size_max(state->hwParams,
			&bufferSize);
	assert(result >= 0);

	periodSize = bufferSize / 4;
	result = snd_pcm_hw_params_set_period_size_near(state->handle, state->hwParams,
			&periodSize, 0);
	assert(result >= 0);
	result = snd_pcm_hw_params_set_buffer_size_near(state->handle, state->hwParams,
			&bufferSize);
	assert(result >= 0);

	// Write hw Params.
	result = snd_pcm_hw_params(state->handle, state->hwParams);
	assert(result >= 0);

	snd_pcm_hw_params_get_period_size(state->hwParams, &state->chunkSize, 0);
	snd_pcm_hw_params_get_buffer_size(state->hwParams, &state->bufferSize);
	state->frameSize = fmt.bytePerSample;

	state->monotonic = snd_pcm_hw_params_is_monotonic(state->hwParams);

	// Do audio buffer read.
	result = read(fd, &chunkHeader, sizeof(WaveChunkHeader));
	assert(result == sizeof(WaveChunkHeader));

	assert(memcmp(chunkHeader.type, "data", 4) == 0);

	state->audioBufferSize = LE_INT(chunkHeader.length);
	state->audioBuffer = malloc(state->audioBufferSize);
	assert(state->audioBuffer != NULL);

	result = read(fd, state->audioBuffer, state->audioBufferSize);
	assert(result == state->audioBufferSize);

	free(fileBuffer);
	close(fd);
}

void ceeAudioPlay(ceeAudioState* state) {
	int32_t i = 0;
	int32_t result;
	const uint32_t chunkBytes = state->chunkSize * state->frameSize;
	while (i < state->audioBufferSize) {
		if (state->audioBufferSize - i < chunkBytes) {
			result = snd_pcm_writei(state->handle, state->audioBuffer + i, (state->audioBufferSize - i) / state->frameSize);
			if (result < 0) {
				if (result == -EPIPE) {
					XRun(state->handle, state->monotonic);
				}
				else {
					printf("Failed to write to pcm: %s (%i)\n", snd_strerror(result), result);
					fflush(stdout);
					assert(0);
				}
			}
			break;
		}
		result = snd_pcm_writei(state->handle, state->audioBuffer + i, state->chunkSize);
		if (result < 0) {
			if (result == -EPIPE) {
				XRun(state->handle, state->monotonic);
			}
			else {
				printf("Failed to write to pcm: %s (%i)\n", snd_strerror(result), result);
				fflush(stdout);
				assert(0);
			}
		}
		if (result > 0) {
			i += result * state->frameSize;
		}
	}
	snd_pcm_drain(state->handle);
	snd_pcm_prepare(state->handle);
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


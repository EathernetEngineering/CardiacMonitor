#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <climits>

#include <iostream>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <libintl.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>

#include <alsa/asoundlib.h>

#include "format.h"
#include "util.h"
#include "play.h"

#define HAVE_CLOCK_GETTIME 1

namespace cee {
	namespace monitor {

		/*
		 * Safe read (for pipes)
		 */
		static ssize_t safeRead(int fd, void* buffer, size_t count) {
			ssize_t result = 0, res;

			while (count > 0) {
				if ((res = read(fd, buffer, count)) == 0)
					break;
				if (res < 0)
					return result > 0 ? result : res;
				count -= res;
				result += res;
				buffer = (uint8_t*)buffer + res;
			}
			return result;
		}

		/*
		 * Helper function for `.WAV` files.
		 */
		static size_t testWaveRead(int fd, uint8_t* buffer, size_t& pos, size_t bytesRequired) {
			if (pos >= bytesRequired) return pos;
			if ((size_t)safeRead(fd, buffer + pos, bytesRequired - pos) !=
					bytesRequired - pos) {
				char s[FILENAME_MAX];
				sprintf(s, "Read failed (%i): %s", errno, strerror(errno));
				error(s);
				raise(SIGABRT);
			}
			return pos = bytesRequired;
		}

		/*
		 * Helper function to write to pcm.
		 */
		ssize_t LinuxAudio::PcmWrite(uint8_t* data, size_t count) {
			ssize_t r = 0, result = 0;

			if (count > this->m_ChunkSize) {
				snd_pcm_format_set_silence(this->m_HwParams.format,
						data + count * this->m_BitsPerFrame / 8,
						(this->m_ChunkSize - count) * this->m_HwParams.channels);
				count = this->m_ChunkSize;
			}
			r = snd_pcm_writei(this->m_Handle, data, count);
			if (r == -EAGAIN || (r >= 0 && (size_t)r < count)) {
				snd_pcm_wait(this->m_Handle, 100);
			} else if (r == -EPIPE) {
				this->XRun();
			} else if (r == -ESTRPIPE) {
				this->Suspend();
			} else if (r < 0) {
				error("Write error");
				raise(SIGABRT);
			}
			if (r > 0) {
				result += r;
				count -= r;
				data += r * this->m_BitsPerFrame / 8;
			}
			return result;
		}

		void LinuxAudio::XRun() {
			snd_pcm_status_t* status;
			int res;

			snd_pcm_status_alloca(&status);
			if ((res = snd_pcm_status(m_Handle, status)) < 0) {
				char s[FILENAME_MAX];
				sprintf(s, "Status error (%i): %s", res, snd_strerror(res));
				error(s);
				this->Panic(EXIT_FAILURE);
			}
			if (snd_pcm_status_get_state(status) == SND_PCM_STATE_XRUN) {
				if (m_Monotonic) {
#ifdef HAVE_CLOCK_GETTIME
					timespec now, diff, tstamp;
					clock_gettime(CLOCK_MONOTONIC, &now);
					snd_pcm_status_get_trigger_htstamp(status, &tstamp);
					diff.tv_nsec = now.tv_nsec - tstamp.tv_nsec;
					diff.tv_sec = now.tv_sec - tstamp.tv_sec;
					if (diff.tv_nsec < 0) {
						--diff.tv_sec;
						diff.tv_nsec += 1000000000L;
					}
					fprintf(stderr, "%s!!! (at least %.3fms long)\n",
							m_Stream == SND_PCM_STREAM_PLAYBACK ? "underrrun" : "overrun",
							diff.tv_sec * 1000 + diff.tv_nsec / 10000000.0);
#else
					fprintf(stderr, "%s!!!\n", "underrun");
#endif
				} else {
					timeval now, diff, tstamp;
					gettimeofday(&now, 0);
					
					snd_pcm_status_get_trigger_tstamp(status, &tstamp);
					diff.tv_usec = now.tv_usec - tstamp.tv_usec;
					diff.tv_sec = now.tv_sec - tstamp.tv_sec;
					if (diff.tv_usec < 0) {
						--diff.tv_sec;
						diff.tv_usec += 1000000L;
					}
					fprintf(stderr, "%s!!! (at least %.3fms long)\n",
							m_Stream == SND_PCM_STREAM_PLAYBACK ? "underrun" : "overrrun",
							diff.tv_sec * 1000 + diff.tv_usec / 1000.0);
				}
				if ((res = snd_pcm_prepare(m_Handle)) < 0) {
					char s[FILENAME_MAX];
					sprintf(s, "XRun: Prepare error (%i): %s", res, snd_strerror(res));
					error(s);
					this->Panic(EXIT_FAILURE);
				}
				return;
			}
			if (snd_pcm_status_get_state(status) == SND_PCM_STATE_DRAINING) {
				if (m_Stream == SND_PCM_STREAM_CAPTURE) {
					error("Capture stream format change? attempting recover...\n");
					if ((res = snd_pcm_prepare(m_Handle)) < 0) {
						char s[FILENAME_MAX];
						sprintf(s, "XRun(DRAINING): Prepare error (%i): %s", res, snd_strerror(res));
						error(s);
						this->Panic(EXIT_FAILURE);
					}
				}
			}
			char s[FILENAME_MAX];
			sprintf(s, "read/write error, state = %s", snd_pcm_state_name(snd_pcm_status_get_state(status)));
			error(s);
			this->Panic(EXIT_FAILURE);
		}

		void LinuxAudio::Suspend() {
			int res;

			fprintf(stderr, "Suspended. Trying resume. ");
			fflush(stderr);

			while ((res = snd_pcm_resume(m_Handle)) == -EAGAIN)
				sleep(1);
			if (res < 0) {
				fprintf(stderr, "Failed. Restarting Stream. ");
				fflush(stderr);

				if ((res = snd_pcm_prepare(m_Handle)) < 0) {
					char s[FILENAME_MAX];
					sprintf(s, "Suspend: Prepare error (%i): %s", res, snd_strerror(res));
					error(s);
					this->Panic(EXIT_FAILURE);
				}
			}
		}

		LinuxAudio::LinuxAudio(const std::string& filename)
			: m_ChunkSize(1024)
		{
			char* pcmName = (char*)("default");
			int openMode = 0 | m_Block ? 0 : O_NONBLOCK;
			snd_pcm_info_t* info;
			m_PbrecCount = LONG_MAX;

			snd_pcm_info_alloca(&info);

			int err = snd_pcm_open(&m_Handle, pcmName, m_Stream, openMode);
			if (err < 0) {
				error("Failed to open pcm device");
				this->Panic(EXIT_FAILURE);
			}

			snd_output_stdio_attach(&m_StdioOut, stderr, 0);

			err = snd_pcm_info(m_Handle, info);
			if (err < 0) {
				char s[FILENAME_MAX];
				sprintf(s, "Info failed (%i): %s", err, snd_strerror(err));
				error(s);
			}

			snd_pcm_nonblock(m_Handle, !m_Block);

			m_Fd = open(filename.c_str(), O_RDONLY);
			if (m_Fd < 0) {
				char s[FILENAME_MAX];
				sprintf(s, "Failed to open file \"%s\". (%i): %s", filename.c_str(), errno, strerror(errno));
				error(s);
				this->Panic(EXIT_FAILURE);
			}
			this->ReadFileHeader();

			this->SetParams();
		}

		LinuxAudio::~LinuxAudio() {
			if (m_Handle) snd_pcm_close(m_Handle);
			if (m_Fd > 0) close(m_Fd);
			if (m_AudioBuffer) {
				free(m_AudioBuffer);
				m_AudioBuffer = nullptr;
			}
			if (m_FileBuffer) {
				free(m_FileBuffer);
				m_FileBuffer = nullptr;
			}

			snd_output_close(m_StdioOut);
		}

		void LinuxAudio::Playback() {
			this->Prepare();

			this->PlaybackGo();
		}

		void LinuxAudio::Panic(int errc) {
			if (m_Handle) snd_pcm_close(m_Handle);
			if (m_Fd > 0) close(m_Fd);
			if (m_AudioBuffer) {
				free(m_AudioBuffer);
				m_AudioBuffer = nullptr;
			}
			if (m_FileBuffer) {
				free(m_FileBuffer);
				m_FileBuffer = nullptr;
			}

			raise(SIGABRT);
		}

		void LinuxAudio::ReadFileHeader() {
			m_FileBuffer = (uint8_t*)malloc(m_ChunkSize);
			if (!m_FileBuffer) {
				error("Out of memory!");
				this->Panic(EXIT_FAILURE);
			}
			m_FileBufferSize = m_ChunkSize;
			ssize_t r = safeRead(m_Fd, m_FileBuffer, sizeof(AuHeader));
			if (r < 0) {
				char s[FILENAME_MAX];
				sprintf(s, "Failed to read file (%i): %s", errno, strerror(errno));
				error(s);
				this->Panic(EXIT_FAILURE);
			}
			m_BytesRead += r;
			if (((WaveHeader*)m_FileBuffer)->magic == WAV_RIFF) {
				this->ReadWave();
			} else if (((AuHeader*)m_FileBuffer)->magic == AU_MAGIC) {
				this->ReadAu();
			} else if (memcmp(((VocHeader*)m_FileBuffer)->magic, VOC_MAGIC_STRING, sizeof(VOC_MAGIC_STRING)) == 0) {
				this->ReadVoc();
			} else {
				error("Unknown file type!");
				this->Panic(EXIT_FAILURE);
			}
		}

		void LinuxAudio::ReadVoc() {

		}

		void LinuxAudio::ReadWave() {
			WaveHeader* header = (WaveHeader*)m_FileBuffer;
			uint8_t* buffer = m_FileBuffer;
			size_t bufferLimit = m_FileBufferSize;
			WaveFmtBody* fmtBody = nullptr;
			WaveChunkHeader* chunkHeader;
			size_t pos = m_BytesRead;

			if (pos < sizeof(WaveHeader)) {
				error("An unknown error occured!");
				this->Panic(EXIT_FAILURE);
			}
			if (header->magic != WAV_RIFF || header->type != WAV_WAVE) {
				error("An unknown error occured!");
				this->Panic(EXIT_FAILURE);
			}

			buffer += sizeof(WaveHeader);
			bufferLimit -= sizeof(WaveHeader);
			pos -= sizeof(WaveHeader);

			for (;;) {
				if (bufferLimit < sizeof(WaveChunkHeader)) {
					m_FileBufferSize += sizeof(WaveChunkHeader);
					m_FileBuffer = (uint8_t*)realloc(m_FileBuffer, m_FileBufferSize);
					if (m_FileBuffer == nullptr) {
						error("Out of memory!");
						this->Panic(EXIT_FAILURE);
					}
					buffer = m_FileBuffer + sizeof(WaveHeader);
					bufferLimit = m_FileBufferSize - sizeof(WaveHeader);
				}
				m_BytesRead += testWaveRead(m_Fd, buffer, pos, sizeof(WaveChunkHeader));
				pos -= sizeof(WaveChunkHeader);

				chunkHeader = (WaveChunkHeader*)buffer;
				chunkHeader->length = LE_INT(chunkHeader->length);
				chunkHeader->length += chunkHeader->length % 2;
				if (chunkHeader->type == WAV_FMT)
					break;
				if (bufferLimit < chunkHeader->length) {
					m_FileBufferSize += chunkHeader->length;
					m_FileBuffer = (uint8_t*)realloc(m_FileBuffer, m_FileBufferSize);
					if (m_FileBuffer == nullptr) {
						error("Out of memory!");
						this->Panic(EXIT_FAILURE);
					}
					buffer = m_FileBuffer + sizeof(WaveHeader);
					bufferLimit = m_FileBufferSize - sizeof(WaveHeader);
				}
				m_BytesRead += testWaveRead(m_Fd, buffer, pos, chunkHeader->length);
				pos -= chunkHeader->length;
			}
			buffer += sizeof(WaveChunkHeader);
			bufferLimit -= sizeof(WaveChunkHeader);

			if (bufferLimit < sizeof(WaveFmtBody)) {
				m_FileBufferSize += chunkHeader->length;
				m_FileBuffer = (uint8_t*)realloc(m_FileBuffer, m_FileBufferSize);
				if (m_FileBuffer == nullptr) {
					error("Out of memory!");
					this->Panic(EXIT_FAILURE);
				}
				buffer = m_FileBuffer + sizeof(WaveHeader);
				bufferLimit = m_FileBufferSize - sizeof(WaveHeader);
			}
			m_BytesRead =+ testWaveRead(m_Fd, buffer, pos, sizeof(WaveFmtBody));
			if (chunkHeader->length < sizeof(WaveFmtBody)) {
				char s[FILENAME_MAX];
				sprintf(s, "Unknown 'fmt ' chunk size %u; should be at least %u", chunkHeader->length, sizeof(WaveFmtBody));
				error(s);
				this->Panic(EXIT_FAILURE);
			}

			fmtBody = (WaveFmtBody*)buffer;
			if (LE_SHORT(fmtBody->format) != WAV_FMT_PCM &&
					LE_SHORT(fmtBody->format) != WAV_FMT_IEEE_FLOAT) {
				error("Cannot play .WAV files that are not float for pcm encoded");
				this->Panic(EXIT_FAILURE);
			}
			if (LE_SHORT(fmtBody->channels) > 1) {
				char s[FILENAME_MAX];
				sprintf(s, "Can only play mono tracks, file has %hi channels", LE_SHORT(fmtBody->channels));
				error(s);
				this->Panic(EXIT_FAILURE);
			}
			m_HwParams.channels = LE_SHORT(fmtBody->channels);
			switch (LE_SHORT(fmtBody->bitPerSample)) {
				case 8:
					m_HwParams.format = SND_PCM_FORMAT_U8;
					break;

				case 16:
					m_HwParams.format = SND_PCM_FORMAT_S16_LE;
					break;

				case 24:
					switch (LE_SHORT(fmtBody->bytePerSample) / m_HwParams.channels) {
						case 3:
							m_HwParams.format = SND_PCM_FORMAT_S24_3LE;
							break;

						case 4:
							m_HwParams.format = SND_PCM_FORMAT_S24_LE;
							break;

						default:
							error("Unknown format");
							this->Panic(EXIT_FAILURE);
					}
					break;

				case 32:
					if (LE_SHORT(fmtBody->format) == WAV_FMT_PCM) {
						m_HwParams.format = SND_PCM_FORMAT_S32_LE;
						break;
					} else if (LE_SHORT(fmtBody->format) == WAV_FMT_IEEE_FLOAT) {
						m_HwParams.format = SND_PCM_FORMAT_FLOAT_LE;
						break;
					}

				default:
					error("Unknown format");
					this->Panic(EXIT_FAILURE);
			}

			m_HwParams.rate = LE_INT(fmtBody->sampleFreq);

			buffer += chunkHeader->length;
			bufferLimit -= chunkHeader->length;
			pos -= chunkHeader->length;

			for (;;) {
				if (bufferLimit < sizeof(WaveChunkHeader)) {
					m_FileBufferSize += sizeof(WaveChunkHeader);
					m_FileBuffer = (uint8_t*)realloc(m_FileBuffer, m_FileBufferSize);
					if (m_FileBuffer == nullptr) {
						error("Out of memory!");
						this->Panic(EXIT_FAILURE);
					}
					buffer = m_FileBuffer + sizeof(WaveHeader);
					bufferLimit = m_FileBufferSize - sizeof(WaveHeader);
				}
				m_BytesRead += testWaveRead(m_Fd, buffer, pos, sizeof(WaveChunkHeader));
				pos -= sizeof(WaveChunkHeader);

				chunkHeader = (WaveChunkHeader*)buffer;
				chunkHeader->length = LE_INT(chunkHeader->length);

				if (chunkHeader->type == WAV_DATA) {
					if (chunkHeader->length <= 0) {
						error("An Unknown Error Has Occured");
						this->Panic(EXIT_FAILURE);
					}

					if (chunkHeader->length < m_PbrecCount && chunkHeader->length < 0x7FFFFFFE)
						m_PbrecCount = chunkHeader->length;

					m_AudioBuffer = (uint8_t*)malloc(chunkHeader->length);
					m_AudioBufferSize = chunkHeader->length;
					if (!m_AudioBuffer) {
						error("Out of memory!");
						this->Panic(EXIT_FAILURE);
					}
					testWaveRead(m_Fd, m_AudioBuffer, pos, chunkHeader->length);
					free(m_FileBuffer);
					m_FileBuffer = nullptr;
					return;
				}

				chunkHeader->length += chunkHeader->length % 2;

				if (bufferLimit < chunkHeader->length) {
					m_FileBufferSize += chunkHeader->length;
					m_FileBuffer = (uint8_t*)realloc(m_FileBuffer, m_FileBufferSize);
					if (m_FileBuffer == nullptr) {
						error("Out of memory!");
						this->Panic(EXIT_FAILURE);
					}
					buffer = m_FileBuffer + sizeof(WaveHeader) + sizeof(WaveChunkHeader) + sizeof(WaveFmtBody);
					bufferLimit = m_FileBufferSize - (sizeof(WaveHeader) + sizeof(WaveChunkHeader) + sizeof(WaveFmtBody));
				}
				m_BytesRead += testWaveRead(m_Fd, buffer, pos, chunkHeader->length);
				pos -= chunkHeader->length;
			}
		}

		void LinuxAudio::ReadAu() {

		}

		void LinuxAudio::SetParams() {
			snd_pcm_hw_params_t* hwParams = nullptr;
			snd_pcm_sw_params_t* swParams = nullptr;
			uint32_t bufferTime = 0, periodTime = 0;
			snd_pcm_uframes_t periodFrames = 0, bufferFrames = 0;

			int err = 0;
			size_t n = 0;
			uint32_t rate = m_HwParams.rate;
			snd_pcm_uframes_t stopThreshold = 0, startThreshold = 0;

			snd_pcm_hw_params_alloca(&hwParams);
			snd_pcm_sw_params_alloca(&swParams);

			err = snd_pcm_hw_params_any(m_Handle, hwParams);
			if (err < 0) {
				error("Broken configuration for this pcm; no configuration available");
				this->Panic(EXIT_FAILURE);
			}

			err = snd_pcm_hw_params_set_access(m_Handle, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED);
			if (err < 0) {
				error("Access type not available");
				this->Panic(EXIT_FAILURE);
			}

			err = snd_pcm_hw_params_set_format(m_Handle, hwParams, m_HwParams.format);
			if (err < 0) {
				error("format type not available");
				this->Panic(EXIT_FAILURE);
			}

			err = snd_pcm_hw_params_set_channels(m_Handle, hwParams, m_HwParams.channels);
			if (err < 0) {
				error("Channels count not available");
				this->Panic(EXIT_FAILURE);
			}

			rate = m_HwParams.rate;
			err = snd_pcm_hw_params_set_rate_near(m_Handle, hwParams, &m_HwParams.rate, 0);
			if (err < 0) {
				error("rate could not be set");
				this->Panic(EXIT_FAILURE);
			}
			if ((float)rate * 1.05 < m_HwParams.rate ||
					(float)rate * 0.95 > m_HwParams.rate) {
				char plugex[64];
				const char* pcmName = snd_pcm_name(m_Handle);
				fprintf(stderr, "\e[1;33mWarning: Rate is not accurate. (requested %i, got %i)\n\e[0m",
						rate, m_HwParams.rate);
				if (!pcmName || strchr(snd_pcm_name(m_Handle), ':')) {
					*plugex = 0;
				} else {
					snprintf(plugex, sizeof(plugex), "(-Dplug:%s)", snd_pcm_name(m_Handle));
					fprintf(stderr, "\e[1;32m        please, try the plug plugin %s\e[0m\n", plugex);
				}
			}
			rate = m_HwParams.rate;
			err = snd_pcm_hw_params_get_buffer_time_max(hwParams, &bufferTime, 0);
			if (err < 0) {
				error("Failed to get buffer time");
				this->Panic(EXIT_FAILURE);
			}
			if (bufferTime > 0)
				periodTime = bufferTime / 4;
			else
				periodFrames = bufferFrames / 4;
			if (periodTime > 0)
				err = snd_pcm_hw_params_set_period_time_near(m_Handle, hwParams, &periodTime, 0);
			else
				err = snd_pcm_hw_params_set_period_size_near(m_Handle, hwParams, &periodFrames, 0);
			if (err < 0) {
				error("Failed to set period time/size");
				this->Panic(EXIT_FAILURE);
			}
			if (bufferTime > 0)
				err = snd_pcm_hw_params_set_buffer_time_near(m_Handle, hwParams, &bufferTime, 0);
			else
				err = snd_pcm_hw_params_set_buffer_size_near(m_Handle, hwParams, &bufferFrames);
			if (err < 0) {
				error("Failed to set buffer time/size");
				this->Panic(EXIT_FAILURE);
			}

			m_Monotonic = snd_pcm_hw_params_is_monotonic(hwParams);

			err = snd_pcm_hw_params(m_Handle, hwParams);
			if (err < 0) {
				char s[FILENAME_MAX];
				sprintf(s, "Failed to set hardware params (%i): %s", err, snd_strerror(err));
				error(s);
				this->Panic(EXIT_FAILURE);
			}

			snd_pcm_hw_params_get_period_size(hwParams, &m_ChunkSize, 0);
			snd_pcm_hw_params_get_buffer_size(hwParams, &m_BufferSize);
			if (m_BufferSize == m_ChunkSize) {
				error("Cannot use buffer size equal to period size");
				this->Panic(EXIT_FAILURE);
			}

			snd_pcm_sw_params_current(m_Handle, swParams);
			n = m_ChunkSize;
			err = snd_pcm_sw_params_set_avail_min(m_Handle, swParams, n);

			n = m_ChunkSize;
			startThreshold = n;
			if (startThreshold < 1)
				startThreshold = 1;
			if (startThreshold > n)
				startThreshold = n;
			err = snd_pcm_sw_params_set_start_threshold(m_Handle, swParams, startThreshold);
			if (err < 0) {
				error("Failed to set start threshold");
				this->Panic(EXIT_FAILURE);
			}

			stopThreshold = m_BufferSize;
			err = snd_pcm_sw_params_set_stop_threshold(m_Handle, swParams, stopThreshold);
			if (err < 0) {
				error("Failed to set stop threshold");
				this->Panic(EXIT_FAILURE);
			}

			if (snd_pcm_sw_params(m_Handle, swParams) < 0) {
				error("Failed to set software params");
				this->Panic(EXIT_FAILURE);
			}

			m_BitsPerFrame = snd_pcm_format_physical_width(m_HwParams.format) * m_HwParams.channels;
			m_ChunkBytes = m_ChunkSize * m_BitsPerFrame / 8;
		}

		void LinuxAudio::Prepare() {
			int err = snd_pcm_prepare(m_Handle);
			if (err < 0) {
				fprintf(stderr, "Prepare failed (%i): %s.\nexiting...\n", err, snd_strerror(err));
				this->Panic(EXIT_FAILURE);
			}
		}

		void LinuxAudio::PlaybackGo() {
			uint32_t l, r;
			uint32_t loaded = m_AudioBufferSize;
			uint32_t written = 0;
			off64_t c;

			while (loaded > m_ChunkBytes && written < m_PbrecCount) {
				if (this->PcmWrite(m_AudioBuffer + written, m_ChunkSize) <= 0)
					return;
				written += m_ChunkBytes;
				loaded -= m_ChunkBytes;
			}

			if (written > 0 && loaded > 0)
				memmove(m_AudioBuffer, m_AudioBuffer + written, loaded);

			l = loaded;
			while (written < m_PbrecCount) {
				do {
					c = m_PbrecCount - written;
					if (c > m_ChunkBytes)
						c = m_ChunkBytes;
					c -= l;

					if (c == 0)
						break;
					r = safeRead(m_Fd, m_AudioBuffer + l, c);
					if (r < 0) {
						error("Failed read");
						this->Panic(EXIT_FAILURE);
					}
					if (r == 0)
						break;
					l += r;
				} while((size_t)l < m_ChunkBytes);
				l = l * 8 / m_BitsPerFrame;
				r = this->PcmWrite(m_AudioBuffer, l);
				if (r != l)
					break;
				
				r = r * m_BitsPerFrame / 8;
				written += r;
				l = 0;
			}
			snd_pcm_nonblock(m_Handle, 0);
			snd_pcm_drain(m_Handle);
			snd_pcm_nonblock(m_Handle, !m_Block);
		}

	}
}


#ifndef __PLAY_H
#define __PLAY_H

#include <memory>

#include <alsa/asoundlib.h>

#include "format.h"

namespace cee {
	namespace monitor {
		class LinuxAudio {
			public:
				LinuxAudio(const std::string& filename);
				virtual ~LinuxAudio();

			public:
				void Playback();

			private:
				void Panic(int errc);
				void ReadFileHeader();
				void ReadVoc();
				void ReadWave();
				void ReadAu();
				void SetParams();
				void Prepare();
				void PlaybackGo();

				ssize_t PcmWrite(uint8_t* data, size_t count);
				void XRun();
				void Suspend();

			private:
				struct {
					snd_pcm_format_t format;
					uint32_t channels;
					uint32_t rate;
				} m_HwParams, m_RHwParams;

				snd_output_t* m_StdioOut;

				snd_pcm_t* m_Handle = nullptr;
				size_t m_PbrecCount = 0;
				int m_Fd = 0;
				size_t m_BytesRead = 0;
				uint8_t* m_AudioBuffer = nullptr;
				size_t m_AudioBufferSize;
				uint8_t* m_FileBuffer = nullptr;
				size_t m_FileBufferSize;

				snd_pcm_uframes_t m_BufferSize;
				size_t m_BufferBytes = 0;
				snd_pcm_uframes_t m_ChunkSize;
				size_t m_ChunkBytes = 0;
				size_t m_BitsPerFrame = 0;
				int m_Monotonic = 0;

				bool m_Block = 0;

				snd_pcm_stream_t m_Stream = SND_PCM_STREAM_PLAYBACK;

			public:
				friend ssize_t pcmWrite(LinuxAudio* la, uint8_t* data, size_t count);
		};
	}
}

#endif


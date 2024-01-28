#ifndef _SERIAL_H
#define _SERIAL_H

#include <string>

namespace cee {
	namespace monitor {
		class Serial {
			public:
				Serial(const std::string& filename);
				virtual ~Serial();

			private:
				void Panic(int errc);

			public:
				const uint8_t* GetReadBuffer() const;
				int GetBuffered() const;
				int Read();
				int Consume(size_t bytes);

				void ResetConnection();

			private:
				void Connect();

			private:
				int m_Fd = 0;
				uint32_t m_Index = 0;
				uint8_t* m_ReadBuffer = nullptr;
				size_t m_ReadBufferLength = 0;
				const std::string m_Filename;
		};
	}
}

#endif


#include "serial.h"

#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

#include "util.h"

namespace cee {
	namespace monitor {
		Serial::Serial(const std::string& filename) {
			m_Fd = open(filename.c_str(), O_RDWR);
			if (m_Fd < 0) {
				char s[FILENAME_MAX];
				sprintf(s, "Failed to open file (%i): %s\n", errno, strerror(errno));
				error(s);
				this->Panic(EXIT_FAILURE);
			}

			termios tty;

			if (tcgetattr(m_Fd, &tty) != 0) {
				char s[FILENAME_MAX];
				sprintf(s, "Failed get tty attributes (%i): %s\n", errno, strerror(errno));
				error(s);
				this->Panic(EXIT_FAILURE);
			}

			tty.c_cflag &= ~PARENB;
			tty.c_cflag &= ~CSTOPB;
			tty.c_cflag &= ~CSIZE;
			tty.c_cflag |= CS8;
			tty.c_cflag &= ~CRTSCTS;
			tty.c_cflag |= CREAD | CLOCAL;

			tty.c_lflag &= ~ICANON;
			tty.c_lflag &= ~ECHO;
			tty.c_lflag &= ~ECHOE;
			tty.c_lflag &= ~ECHONL;
			tty.c_lflag &= ~ISIG;
			tty.c_iflag &= ~(IXON | IXOFF | IXANY);
			tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL);

			tty.c_oflag &= ~OPOST;
			tty.c_oflag &= ~ONLCR;

			tty.c_cc[VTIME] = 0;
			tty.c_cc[VMIN] = 0;

			cfsetispeed(&tty, B115200);
			cfsetospeed(&tty, B115200);

			if (tcsetattr(m_Fd, TCSANOW, &tty) != 0) {
				char s[FILENAME_MAX];
				sprintf(s, "Failed set tty attributes (%i): %s\n", errno, strerror(errno));
				error(s);
				this->Panic(EXIT_FAILURE);
			}

			m_ReadBuffer = reinterpret_cast<uint8_t*>(malloc(8192));
			if (m_ReadBuffer == nullptr) {
				error("Out of memory!");
				this->Panic(EXIT_FAILURE);
			}

			memset(m_ReadBuffer, 0, sizeof(m_ReadBuffer));
		}

		Serial::~Serial() {
			if (m_Fd > 0) {
				close(m_Fd);
				m_Fd = 0;
			}

			if (m_ReadBuffer != nullptr) {
				free(m_ReadBuffer);
				m_ReadBuffer = nullptr;
			}

		}

		void Serial::Panic(int errc) {
			if (m_Fd > 0) {
				close(m_Fd);
				m_Fd = 0;
			}

			if (m_ReadBuffer != nullptr) {
				free(m_ReadBuffer);
				m_ReadBuffer = nullptr;
			}

			raise(SIGABRT);
		}

		const uint8_t* Serial::GetReadBuffer() {
			return m_ReadBuffer;
		}

		void Serial::Read() {
			if (m_Index < sizeof(m_ReadBuffer) - 512)
				this->Consume(sizeof(m_ReadBuffer) - m_Index);

			int err = read(m_Fd, m_ReadBuffer + m_Index, sizeof(m_ReadBuffer) - m_Index);
			if (err < 0) {
				char s[FILENAME_MAX];
				sprintf(s, "Failed read port (%i): %s\n", errno, strerror(errno));
				error(s);
				this->Panic(EXIT_FAILURE);
			}
			m_Index += err;
		}

		int Serial::Consume(size_t bytes) {
			if (bytes > m_Index) return -1;

			memmove(m_ReadBuffer, m_ReadBuffer + bytes, m_Index - bytes);

			m_Index -= bytes;
			memset(m_ReadBuffer + m_Index, 0, sizeof(m_ReadBuffer) - m_Index);
			return bytes;
		}
	}
}


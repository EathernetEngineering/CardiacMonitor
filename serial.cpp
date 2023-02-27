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
		Serial::Serial(const std::string& filename)
		 : m_Filename(filename)
		{
			m_ReadBuffer = reinterpret_cast<uint8_t*>(malloc(8192));
			if (m_ReadBuffer == nullptr) {
				error("Out of memory!");
				this->Panic(EXIT_FAILURE);
			}

			m_ReadBufferLength = 8192;

			memset(m_ReadBuffer, 0, m_ReadBufferLength);
			this->Connect();
		}

		Serial::~Serial() {
			if (m_Fd > 0) {
				close(m_Fd);
				m_Fd = 0;
			}

			if (m_ReadBuffer != nullptr) {
				free(m_ReadBuffer);
				m_ReadBuffer = nullptr;
				m_ReadBufferLength = 0;
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
				m_ReadBufferLength = 0;
			}

			raise(SIGABRT);
		}

		const uint8_t* Serial::GetReadBuffer() const {
			return m_ReadBuffer;
		}

		int Serial::GetBuffered() const {
			return m_Index;
		}

		int Serial::Read() {
			if (m_Index > m_ReadBufferLength - 512)
				this->Consume(512);

			int err = read(m_Fd, m_ReadBuffer + m_Index, m_ReadBufferLength - m_Index);
			if (err < 0) {
				char s[FILENAME_MAX];
				sprintf(s, "Failed read port (%i): %s\n", errno, strerror(errno));
				error(s);
				this->Panic(EXIT_FAILURE);
			}
			m_Index += err;
			return err;
		}

		int Serial::Consume(size_t bytes) {
			if (bytes > m_Index) bytes = m_Index;

			memmove(m_ReadBuffer, m_ReadBuffer + bytes, m_Index - bytes);

			m_Index -= bytes;
			memset(m_ReadBuffer + m_Index, 0, m_ReadBufferLength - m_Index);
			return bytes;
		}

		void Serial::ResetConnection() {
			close(m_Fd);
			m_Fd = 0;

			memset(m_ReadBuffer, 0, m_ReadBufferLength);
			m_Index = 0;

			Connect();
		}

		void Serial::Connect() {
			m_Fd = open(m_Filename.c_str(), O_RDWR);
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
		}
	}
}


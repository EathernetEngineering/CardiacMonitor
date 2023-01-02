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
		}

		Serial::~Serial() {
			if (m_Fd > 0) {
				close(m_Fd);
				m_Fd = 0;
			}
		}

		void Serial::Panic(int errc) {
			if (m_Fd > 0) {
				close(m_Fd);
				m_Fd = 0;
			}

			raise(SIGABRT);
		}
	}
}


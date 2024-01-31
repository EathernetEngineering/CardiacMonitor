#include "i2c.hh"

#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <linux/i2c-dev.h>

namespace cee {
	I2C::I2C(const std::string& filename)
	 : m_Fd(0)
	{
		// Open I2C device
		m_Fd = open(filename.c_str(), O_RDWR);
		if (m_Fd < 0) {
			printf("Failed to open I2C device \"%s\".\n", filename.c_str());
			assert(0);
		}
	}

	I2C::~I2C() {
		if (m_Fd)
			close(m_Fd);
	}

	bool I2C::CheckForDevice(uint32_t address) {
		if(ioctl(m_Fd, I2C_SLAVE, address) < 0) {
			return false;
		}

		if (write(m_Fd, NULL, 0) == 0) {
			return true;
		}
		return false;
	}

	std::vector<uint32_t> I2C::ScanForDevices() {
		std::vector<uint32_t> addresses;

		for (uint32_t i = 0; i < 128; i++) {
			if (ioctl(m_Fd, I2C_SLAVE, i) < 0) {
				continue;
			}

			if (write(m_Fd, NULL, 0) == 0) {
				addresses.push_back(i);
			}
		}
		return addresses;
	}

	ssize_t I2C::WriteToDevice(uint32_t address, void* data, size_t size) {
		if (!this->CheckForDevice(address))
			return -1;

		return write(m_Fd, data, size);
	}

	ssize_t I2C::ReadFromDevice(uint32_t address, void* data, size_t size) {
		if (!this->CheckForDevice(address))
			return -1;

		return read(m_Fd, data, size);
	}
}


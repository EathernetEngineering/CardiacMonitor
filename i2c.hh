#ifndef CEE_I2C_H_
#define CEE_I2C_H_

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace cee {
	class I2C {
	public:
		I2C(const std::string& filename = "/dev/i2c-1");
		virtual ~I2C();

		bool CheckForDevice(uint32_t address);
		std::vector<uint32_t> ScanForDevices();

		ssize_t WriteToDevice(uint32_t address, void* data, size_t size);
		ssize_t ReadFromDevice(uint32_t address, void* data, size_t size);

	private:
		int32_t m_Fd;
	};
}

#endif


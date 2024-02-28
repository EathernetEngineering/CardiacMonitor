#ifndef CEE_ADC_H_
#define CEE_ADC_H_

#include <memory>

#include "i2c.hh"

namespace cee {
	enum class ADCType {
		UNKNOWN = 0,
		PCF8591
	};

	class ADC {
	public:
		ADC(const std::shared_ptr<I2C>& i2c, ADCType type, uint32_t address, uint32_t channel = 0, bool autoIncrement = false);
		~ADC() = default;

		void SetAutoIncrement(bool autoIncrement);

		uint8_t ReadChannel(uint32_t channel);
		uint8_t Read();

	private:
		void SetControlByte();
		void SetChannel(uint32_t channel);

	private:
		std::shared_ptr<I2C> m_I2CBus;
		ADCType m_Type;
		uint32_t m_Address;
		uint32_t m_Channel;
		bool m_AutoIncrement;
	};
}

#endif


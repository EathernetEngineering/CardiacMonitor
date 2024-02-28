#include "adc.hh"

#include <cassert>

namespace cee {
	ADC::ADC(const std::shared_ptr<I2C>& i2c, ADCType type, uint32_t address, uint32_t channel, bool autoIncrement)
	 : m_I2CBus(i2c), m_Type(type), m_Address(address), m_Channel(channel), m_AutoIncrement(autoIncrement)
	{
		SetControlByte();
	}

	void ADC::SetChannel(uint32_t channel) {
		m_Channel = channel;
		SetControlByte();
	}

	void ADC::SetAutoIncrement(bool autoIncrement) {
		m_AutoIncrement = autoIncrement;
		SetControlByte();
	}

	uint8_t ADC::ReadChannel(uint32_t channel) {
		SetChannel(channel);
		return Read();
	}

	uint8_t ADC::Read() {
		switch (m_Type) {
		case ADCType::PCF8591:
		{
			uint8_t buffer[] = { 0 };
			if (m_I2CBus->ReadFromDevice(m_Address, buffer, 1) == 1) {
				return buffer[0];
			}
		}
		break;

		default:
			assert(0 &&"Unsupported ADC type");
		}
		return static_cast<uint8_t>(-1);
	}

	void ADC::SetControlByte() {
		switch (m_Type) {
		case ADCType::PCF8591:
		{
			if (m_Channel > 3) {
				printf("ADC WARNING: setting wrong channel (%u), setting to 0\n", m_Channel);
				m_Channel = 0;
			}
			uint8_t controlByte = 0x40 | ((!!m_AutoIncrement) << 2) | (m_Channel & 0b11);
			if (m_I2CBus->WriteToDevice(m_Address, &controlByte, 1) != 1) {
				printf("Failed setting ADC 0x%.2X control byte\n", m_Address);
				fflush(stdout);
			}
		}
		break;

		default:
			assert(0 &&"Unsupported ADC type");
		}
	}
}

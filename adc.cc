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

	uint16_t ADC::ReadChannel(uint32_t channel) {
		SetChannel(channel);
		return Read();
	}

	uint16_t ADC::Read() {
		switch (m_Type) {
		case ADCType::PCF8591:
		{
			uint8_t buffer[] = { 0 };
			if (m_I2CBus->ReadFromDevice(m_Address, buffer, 1) == 1) {
				return static_cast<uint16_t>(buffer[0]);
			}
		}
		break;

		default:
			assert(0 &&"Unsupported ADC type");
		}
		return static_cast<uint16_t>(-1);
	}

	void ADC::SetControlByte() {
		switch (m_Type) {
		case ADCType::PCF8591:
		{
			uint8_t controlByte = 0x40 | ((!!m_AutoIncrement) << 2) | (m_Channel & 0b11);
			if (m_I2CBus->WriteToDevice(m_Address, &controlByte, 1) != 1) {
				printf("Failed setting ADC 0x%.2X control byte", m_Address);
			}
		}
		break;

		default:
			assert(0 &&"Unsupported ADC type");
		}
	}
}

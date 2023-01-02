#include <iostream>
#include <chrono>
#include <thread>
#include <mutex>

#include <signal.h>

#include "play.h"
#include "serial.h"
#include "util.h"
#include "common.h"

void signalHandler(int signum) {
	static int inAborting = false;
	if (inAborting)
		return;

	inAborting = true;

	fprintf(stderr, "Recieved signal: %s...\taborting.\n", strsignal(signum));

	std::exit(signum);
}

enum class AlarmSounds : int8_t {
	UNKNOWN    = -1,
	NONE       =  0,
	CYAN       =  1,
	YELLOW     =  2,
	RED        =  3
};

static std::chrono::duration<long int, std::milli> g_AlarmTimes[] = {
	std::chrono::milliseconds(500),
	std::chrono::milliseconds(2000),
	std::chrono::milliseconds(2000),
	std::chrono::milliseconds(1000)
};

static AlarmSounds alarmSound = AlarmSounds::NONE;

void doAlarms() {
	using namespace std::chrono_literals;

	auto start = std::chrono::high_resolution_clock::now();
	cee::monitor::LinuxAudio redAlarm("/home/chloe/Documents/Philips intellivue red.wav");
	cee::monitor::LinuxAudio yellowAlarm("/home/chloe/Documents/Philips intellivue yellow.wav");
	cee::monitor::LinuxAudio cyanAlarm("/home/chloe/Documents/Philips intellivue cyan.wav");

	for (;;) {
		if ((std::chrono::high_resolution_clock::now() - start) > g_AlarmTimes[int(alarmSound)]) {
			start = std::chrono::high_resolution_clock::now();
			switch (alarmSound) {
				case AlarmSounds::NONE:
					break;

				case AlarmSounds::CYAN:
					cyanAlarm.Playback();
					break;

				case AlarmSounds::YELLOW:
					yellowAlarm.Playback();
					break;

				case AlarmSounds::RED:
					redAlarm.Playback();
					break;
			}
		}
	}
}

void updateTime() {
	using namespace std::chrono_literals;
	static auto start = std::chrono::steady_clock::now();

	if (std::chrono::steady_clock::now() - start > 10s) {
		start = std::chrono::steady_clock::now();
		alarmSound = (AlarmSounds)(((int8_t)alarmSound) + 1);
		if (alarmSound > AlarmSounds::RED) alarmSound = AlarmSounds::NONE;
	}
}

void updateSerial(cee::monitor::Serial& Ser) {
	Ser.Read();
	int buffered;
	if ((buffered = Ser.GetBuffered()) >= sizeof(MonitorPacket)) {
		for (uint32_t i = 0; i < buffered - sizeof(MonitorPacket); i++) {
			if (strcmp((const char*)(Ser.GetReadBuffer() + i), MONITOR_MAGIC) == 0) {
				const MonitorPacket* packet = reinterpret_cast<const MonitorPacket*>(Ser.GetReadBuffer() + i);
				uint8_t checksum = 0;
				for (size_t i = 0; i < sizeof(MonitorPacket); i++) {
					checksum ^= ((uint8_t*)packet)[i];
				}

				fprintf(stderr, "Packet recieved:\n\e[32m\tLead I:   %i\n\tLead II:  %i\n\tLead III: %i\n\tResp:     %i\n\t\tchecksum = %i\e[0m\n",
						LE_INT(packet->lead1), LE_INT(packet->lead2), LE_INT(packet->lead3), LE_INT(packet->resp), checksum);

				Ser.Consume(sizeof(MonitorPacket) + i);
			}
		}
	}
}

int main(int argc, char** arg) {

	signal(SIGINT, signalHandler);
	signal(SIGABRT, signalHandler);
	signal(SIGTERM, signalHandler);

	std::thread alarmThread(doAlarms);

	cee::monitor::Serial serial("/dev/ttyUSB0");

	for (;;) {
		updateTime();
		updateSerial(serial);
	}

	return EXIT_SUCCESS;
}

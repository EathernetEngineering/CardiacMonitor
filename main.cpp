#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

#include <cassert>
#include <cstring>

#include <signal.h>

#include "audio.h"
#include "serial.h"
#include "util.h"
#include "common.h"
#include "graphics.h"

#define ECG_DATA_POINTS          6000
#define ECG_DATA_TIME_MS         60000
#define ECG_DATA_MS_PER_POINT    (ECG_DATA_TIME_MS / ECG_DATA_POINTS)

struct _data {
	int32_t leadI[ECG_DATA_POINTS];
	int32_t leadII[ECG_DATA_POINTS];
	int32_t leadIII[ECG_DATA_POINTS];
	int32_t resp[ECG_DATA_POINTS];
} g_Data;

int g_Idx;

std::atomic<bool> terminate = false;

void signalHandler(int signum) {
	static int inAborting = false;
	if (inAborting)
		return;

	inAborting = true;

	fprintf(stderr, "Recieved signal: %s...\taborting.\n", strsignal(signum));

	terminate = true;
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
static std::mutex alarmSoundMutex;

void doAlarms() {
	using namespace std::chrono_literals;

	auto start = std::chrono::high_resolution_clock::now();
	ceeAudioState* cyanAlarm = ceeAudioMallocState();
	ceeAudioInitialize(cyanAlarm);
	ceeAudioOpenWav(cyanAlarm, "/home/chloe/Documents/Philips intellivue cyan.wav");
	ceeAudioState* yellowAlarm = ceeAudioMallocState();
	ceeAudioInitialize(yellowAlarm);
	ceeAudioOpenWav(yellowAlarm, "/home/chloe/Documents/Philips intellivue cyan.wav");
	ceeAudioState* redAlarm = ceeAudioMallocState();
	ceeAudioInitialize(redAlarm);
	ceeAudioOpenWav(redAlarm, "/home/chloe/Documents/Philips intellivue cyan.wav");

	for (;;) {
		if ((std::chrono::high_resolution_clock::now() - start) > g_AlarmTimes[int(alarmSound)]) {
			start = std::chrono::high_resolution_clock::now();
			switch (alarmSound) {
				case AlarmSounds::NONE:
					break;

				case AlarmSounds::CYAN:
					ceeAudioPlay(cyanAlarm);
					break;

				case AlarmSounds::YELLOW:
					ceeAudioPlay(yellowAlarm);
					break;

				case AlarmSounds::RED:
					ceeAudioPlay(redAlarm);
					break;

				default:
					fprintf(stderr, "\e[32mWarning: Unknown alarm sound, setting to red.");
					std::lock_guard<std::mutex> guard(alarmSoundMutex);
					alarmSound = AlarmSounds::RED;
			}
		}
		if (terminate) {
			break;
		}
	}
	ceeAudioShutdown(cyanAlarm);
	ceeAudioFreeState(cyanAlarm);
	ceeAudioShutdown(yellowAlarm);
	ceeAudioFreeState(yellowAlarm);
	ceeAudioShutdown(redAlarm);
	ceeAudioFreeState(redAlarm);
}

void updateTime() {
	using namespace std::chrono_literals;
	static auto start = std::chrono::steady_clock::now();

	if (std::chrono::steady_clock::now() - start > 10s) {
		start = std::chrono::steady_clock::now();
		std::lock_guard<std::mutex> guard(alarmSoundMutex);
		alarmSound = (AlarmSounds)(((int8_t)alarmSound) + 1);
		if (alarmSound > AlarmSounds::RED) alarmSound = AlarmSounds::NONE;
	}
}

void updateSerial(cee::monitor::Serial& Ser) {
	Ser.Read();
	uint32_t buffered;
	if ((buffered = Ser.GetBuffered()) >= sizeof(MonitorPacket)) {
		for (uint32_t i = 0; i < buffered - sizeof(MonitorPacket); i++) {
			if (strcmp((const char*)(Ser.GetReadBuffer() + i), MONITOR_MAGIC) == 0) {
				const MonitorPacket* packet = reinterpret_cast<const MonitorPacket*>(Ser.GetReadBuffer() + i);
				uint8_t chk = checksum((uint8_t*)packet, sizeof(MonitorPacket));
				if (chk != 0) {
					if (g_Idx > 0) {
						g_Data.leadI[g_Idx] = g_Data.leadI[g_Idx-1];
						g_Data.leadII[g_Idx] = g_Data.leadII[g_Idx-1];
						g_Data.leadIII[g_Idx] = g_Data.leadIII[g_Idx-1];
						g_Data.resp[g_Idx] = g_Data.resp[g_Idx-1];
					} else {
						g_Data.leadI[g_Idx] = g_Data.leadI[ECG_DATA_POINTS-1];
						g_Data.leadII[g_Idx] = g_Data.leadII[ECG_DATA_POINTS-1];
						g_Data.leadIII[g_Idx] = g_Data.leadIII[ECG_DATA_POINTS-1];
						g_Data.resp[g_Idx] = g_Data.resp[ECG_DATA_POINTS-1];
					}
					g_Idx++;
					if (g_Idx >= ECG_DATA_POINTS) g_Idx = 0;
					fprintf(stderr, "\e[1;93mChecksum failed. discarding packet.\n\e[0m");
					continue;
				}

				g_Data.leadI[g_Idx] = packet->lead1;
				g_Data.leadII[g_Idx] = packet->lead2;
				g_Data.leadIII[g_Idx] = packet->lead3;
				g_Data.resp[g_Idx] = packet->resp;

				g_Idx++;
				if (g_Idx >= ECG_DATA_POINTS) g_Idx = 0;

//				fprintf(stderr, "Packet recieved:\n\e[32m\tLead I:   %i\n\tLead II:  %i\n\tLead III: %i\n\tResp:     %i\n"
//						"\tchecksum = %i\n\tmagic: %.4s\n\n\tChk = %u\e[0m\n",
//						LE_INT(packet->lead1), LE_INT(packet->lead2), LE_INT(packet->lead3), LE_INT(packet->resp), chk,
//						packet->magic, chk);

				Ser.Consume(sizeof(MonitorPacket) + i);
			}
		}
	}
}

int main(int argc, char** arg) {

	signal(SIGINT, signalHandler);
	signal(SIGABRT, signalHandler);
	signal(SIGTERM, signalHandler);

	ceeGraphicsState* graphicsState = ceeGraphicsMallocState();
	assert(graphicsState != 0);

	ceeGraphicsInitialize(graphicsState);

	std::thread alarmThread(doAlarms);

	cee::monitor::Serial serial("/dev/ttyUSB0");

	const char* vertexShaderSource =
		"attribute vec4 aPosition;\n"
		"attribute vec4 aColor;\n"
		"\n"
		"varying vec4 vColor;\n"
		"\n"
		"void main() {\n"
		"	gl_Position = aPosition;\n"
		"	vColor = aColor;\n"
		"}\n";

	const char* fragmentShaderSource =
		"precision mediump float;\n"
		"\n"
		"varying vec4 vColor;\n"
		"\n"
		"void main() {\n"
		"	gl_FragColor = vColor;\n"
		"}\n";

	const char* shaderAttribNames[] = {
		"aPosition",
		"aColor"
	};
	uint32_t shaderAttribLocations[] = {
		0,
		1,
	};
	uint32_t shaderAttribCount = 1;

	uint32_t program;

	assert(
			ceeGraphicsCreateShaderProgram(vertexShaderSource,
				fragmentShaderSource,
				&program,
				shaderAttribNames,
				shaderAttribLocations,
				shaderAttribCount)
			!= false);

	ceeGraphicsUseShaderProgram(program);

	float triangle[] = {
		 0.0f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
		 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
		-0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f
	};
	uint16_t indices[] = {
		0, 1, 2
	};
	ceeGraphicsVertexBufferElement layout[] = {
		{ GL_TYPE_FLOAT4, 4 * sizeof(float), 0, false },
		{ GL_TYPE_FLOAT4, 4 * sizeof(float), 16, false }
	};

	uint32_t vbo, ibo;

	ceeGraphicsCreateVertexBuffer(&vbo);
	ceeGraphicsBindVertexBuffer(vbo);
	ceeGraphicsSetVertices(triangle, 32 * sizeof(float));
	ceeGraphicsSetVertexBufferLayout(layout, 2, 8 * sizeof(float));

	ceeGraphicsCreateIndexBuffer(&ibo);
	ceeGraphicsBindIndexBuffer(ibo);
	ceeGraphicsSetIndices(indices, 3 * sizeof(uint16_t));

	uint16_t graphIndices[1440 * 2];
	for (uint32_t i = 0; i < 1440; i++) {
		graphIndices[(i * 2) + 0] = i;
		graphIndices[(i * 2) + 1] = i;
	}

	float graphVertices[1440 * 8];

	(void)graphIndices;
	(void)graphVertices;

	while (!terminate) {
		updateTime();
		updateSerial(serial);

//		createGraphBuffer(
//				g_Data.leadII,
//				6000 * sizeof(int32_t),
//				1440,
//				0.7f,
//				0.12f,
//				false,
//				0.0f,
//				1.0f,
//				0.0f,
//				1.0f,
//				&graphVertices);

		ceeGraphicsStartFrame(graphicsState);

		ceeGraphicsClearColor(0.0f, 0.0f, 0.0f, 1.0f);

		ceeGraphicsBindVertexBuffer(vbo);
		ceeGraphicsBindIndexBuffer(ibo);
		ceeGraphicsFlush(3);

		ceeGraphicsEndFrame(graphicsState);
	}

	alarmThread.join();

	ceeGraphicsShutdown(graphicsState);
	ceeGraphicsFreeState(graphicsState);

	return EXIT_SUCCESS;
}

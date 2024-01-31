#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

#include <cassert>
#include <cmath>
#include <cstring>

#include <signal.h>
#include <sys/time.h>
#include <pthread.h>

#include "audio.h"
#include "common.h"
#include "graph.h"
#include "graphics.h"
#include "i2c.hh"
#include "adc.hh"
#include "util.h"

#define ECG_DATA_POINTS          1024
#define ECG_DATA_TIME_MS         15000
#define ECG_DATA_MS_PER_POINT    (ECG_DATA_TIME_MS / ECG_DATA_POINTS)

struct _data {
	float leadI[ECG_DATA_POINTS];
	float leadII[ECG_DATA_POINTS];
	float leadIII[ECG_DATA_POINTS];
	float resp[ECG_DATA_POINTS];
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
	ceeAudioOpenWav(cyanAlarm, "/home/chloe/Music/Philips intellivue cyan.wav");
	ceeAudioState* yellowAlarm = ceeAudioMallocState();
	ceeAudioInitialize(yellowAlarm);
	ceeAudioOpenWav(yellowAlarm, "/home/chloe/Music/Philips intellivue yellow.wav");
	ceeAudioState* redAlarm = ceeAudioMallocState();
	ceeAudioInitialize(redAlarm);
	ceeAudioOpenWav(redAlarm, "/home/chloe/Music/Philips intellivue red.wav");

	for (;;) {
		if ((std::chrono::high_resolution_clock::now() - start) > g_AlarmTimes[int(alarmSound)]) {
			start = std::chrono::high_resolution_clock::now();
			switch (alarmSound) {
				case AlarmSounds::NONE:
					break;

				case AlarmSounds::CYAN:
					{
						ceeAudioPlay(cyanAlarm);
					}
					break;

				case AlarmSounds::YELLOW:
					{
						ceeAudioPlay(yellowAlarm);
					}
					break;

				case AlarmSounds::RED:
					{
						ceeAudioPlay(redAlarm);
					}
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

int main(int argc, char** arg) {

	signal(SIGINT, signalHandler);
	signal(SIGABRT, signalHandler);
	signal(SIGTERM, signalHandler);

	ceeGraphicsState* graphicsState = ceeGraphicsMallocState();
	assert(graphicsState != 0);

	ceeGraphicsInitialize(graphicsState);

	std::thread alarmThread(doAlarms);

	std::shared_ptr<cee::I2C> i2cBus = std::make_shared<cee::I2C>();

	cee::ADC adc = cee::ADC(i2cBus, cee::ADCType::PCF8591, 0x48);

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
	uint32_t shaderAttribCount = 2;

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

	ceeGraphicsVertexBufferElement layout[] = {
		{ GL_TYPE_FLOAT4, 4 * sizeof(float), 0, false },
		{ GL_TYPE_FLOAT4, 4 * sizeof(float), 4 * sizeof(float), false }
	};

	float* graphVertices = (float*)malloc(ECG_DATA_POINTS * sizeof(float) * 8);;

	uint32_t graphVbo;
	ceeGraphicsCreateVertexBuffer(&graphVbo);
	ceeGraphicsBindVertexBuffer(graphVbo);
	ceeGraphicsSetVertexBufferLayout(layout, 2, 8 * sizeof(float));
	ceeGraphicsSetVertices(graphVertices, ECG_DATA_POINTS * sizeof(float) * 8);

	uint32_t i = 0;
	timeval startTime, currentTime, diffTime;
	gettimeofday(&startTime, NULL);
	while (!terminate) {
		updateTime();

		i++;
		i %= ECG_DATA_POINTS;
		gettimeofday(&currentTime, NULL);
		if (startTime.tv_usec > currentTime.tv_usec) {
			currentTime.tv_sec--;
			currentTime.tv_usec += 1000000;
		}
		diffTime.tv_sec = currentTime.tv_sec - startTime.tv_sec;
		diffTime.tv_usec = currentTime.tv_usec - startTime.tv_usec;
//		g_Data.leadII[i] = 0.25f * std::sin(((float)diffTime.tv_sec + ((float)diffTime.tv_usec / 1000000.0f)) * 1.25f);
		g_Data.leadII[i] = map8BitToFloat(adc.Read());

		createGraphBuffer(
				g_Data.leadII,
				ECG_DATA_POINTS * sizeof(float),
				0.0f,
				1.0f,
				0.0f,
				1.0f,
				0.0f,
				1.0f,
				0.0f,
				1.0f,
				graphVertices);

		ceeGraphicsBindVertexBuffer(graphVbo);
		ceeGraphicsSetSubVertices(graphVertices, ECG_DATA_POINTS * sizeof(float) * 8);

		ceeGraphicsStartFrame(graphicsState);

		ceeGraphicsClearColor(0.0f, 0.0f, 0.0f, 1.0f);

		ceeGraphicsFlushLineStrip(i + 1, 0);
		ceeGraphicsFlushLineStrip(ECG_DATA_POINTS - i - 1, i + 1);

		ceeGraphicsEndFrame(graphicsState);
	}

	alarmThread.join();

	ceeGraphicsShutdown(graphicsState);
	ceeGraphicsFreeState(graphicsState);

	return EXIT_SUCCESS;
}

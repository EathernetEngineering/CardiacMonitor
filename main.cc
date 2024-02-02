#include <GLES2/gl2.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <ostream>
#include <thread>

#include <cassert>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include <signal.h>
#include <sys/time.h>
#include <pthread.h>

#include "audio.h"
#include "graph.h"
#include "graphics.h"
#include "i2c.hh"
#include "adc.hh"
#include "util.h"
#include "dataProcessing.hh"

#define ECG_DATA_POINTS          1024
#define ECG_DATA_TIME_MS         15000
#define ECG_DATA_MS_PER_POINT    ECG_DATA_TIME_MS / ECG_DATA_POINTS

struct _data {
	float leadI[ECG_DATA_POINTS];
	float leadII[ECG_DATA_POINTS];
	float leadIII[ECG_DATA_POINTS];
	float resp[ECG_DATA_POINTS];
} g_Data;

int g_Idx;

std::atomic<bool> terminate = false;

extern "C" {
	void signalHandler(int signum) {
		static int inAborting = false;
		if (inAborting)
			return;

		inAborting = true;

		const char* errorMessage = "Recieved signal...\taborting.\n";
		write(STDERR_FILENO, errorMessage, strlen(errorMessage));

		terminate = true;
	}
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

	float* graphVertices = (float*)calloc(ECG_DATA_POINTS * 8, sizeof(float));
	float* processedVertices = (float*)calloc((ECG_DATA_POINTS) * 8, sizeof(float));
	std::vector<float> peakMarkersVertices;
	peakMarkersVertices.resize(1024, 0.0f);

	uint32_t graphVbo, processedGraphVbo, peakMarkersVbo;
	ceeGraphicsCreateVertexBuffer(&graphVbo);
	ceeGraphicsBindVertexBuffer(graphVbo);
	ceeGraphicsSetVertexBufferLayout(layout, 2, 8 * sizeof(float));
	ceeGraphicsSetVertices(graphVertices, ECG_DATA_POINTS * sizeof(float) * 8);

	ceeGraphicsCreateVertexBuffer(&processedGraphVbo);
	ceeGraphicsBindVertexBuffer(processedGraphVbo);
	ceeGraphicsSetVertexBufferLayout(layout, 2, 8 * sizeof(float));
	ceeGraphicsSetVertices(processedVertices, (ECG_DATA_POINTS) * sizeof(float) * 8);

	ceeGraphicsCreateVertexBuffer(&peakMarkersVbo);
	ceeGraphicsBindVertexBuffer(peakMarkersVbo);
	ceeGraphicsSetVertexBufferLayout(layout, 2, 8 * sizeof(float));
	ceeGraphicsSetVertices(peakMarkersVertices.data(), peakMarkersVertices.size() * sizeof(float));

	std::vector<float> qrsPeakLocations;
	std::vector<float> doubleDifferenceSquared;

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
		 float sinVal = (static_cast<float>(diffTime.tv_sec) + (static_cast<float>(diffTime.tv_usec) / 1000000.0f));
//		g_Data.leadII[i] = 0.1f * ((2.0f * pow(std::sin(sinVal*5.0f), 50.f)) + (0.3f * std::pow(std::sin(sinVal*5.f - 1.f), 1.f)) + (0.2f * std::pow(std::sin(sinVal*5.f + 1.f), 50.f)) - (0.5f * std::pow(std::sin(sinVal*5.f - 0.2f), 50.f)) - (0.2f * std::pow(std::sin(sinVal*5.f + 0.4f), 50.f)));
		g_Data.leadII[i] = map8BitToFloat(adc.Read());
		cee::CalculateDoubleDifferenceSquared(std::vector<float>(g_Data.leadII, g_Data.leadII + ECG_DATA_POINTS), doubleDifferenceSquared);

		createGraphBuffer(
				g_Data.leadII,
				ECG_DATA_POINTS * sizeof(float),
				0.25f,
				1.0f,
				0.0f,
				1.0f,
				0.0f,
				1.0f,
				0.0f,
				1.0f,
				graphVertices);
/*
		createGraphBuffer(
				doubleDifferenceSquared.data(),
				doubleDifferenceSquared.size() * sizeof(float),
				-0.25f,
				1.0f,
				0.0f,
				1.0f,
				0.0f,
				0.0f,
				1.0f,
				1.0f,
				processedVertices);
*/
		cee::FindQrsPeaks(doubleDifferenceSquared, 0.5f, qrsPeakLocations);
		createPeakChevrons(
				qrsPeakLocations.data(),
				qrsPeakLocations.size() * sizeof(float),
				0.5f,
				0.1f,
				0.0f,
				1.0f,
				0.0f,
				1.0f,
				peakMarkersVertices.data(),
				peakMarkersVertices.size() * sizeof(float));

		ceeGraphicsClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		ceeGraphicsStartFrame(graphicsState);

		ceeGraphicsBindVertexBuffer(graphVbo);
		ceeGraphicsSetVertexBufferLayout(layout, 2, 8 * sizeof(float));
		ceeGraphicsSetSubVertices(graphVertices, ECG_DATA_POINTS * sizeof(float) * 8);
		ceeGraphicsFlushLineStrip(i + 1, 0);
		ceeGraphicsFlushLineStrip(ECG_DATA_POINTS - i - 1, i + 1);

		//ceeGraphicsBindVertexBuffer(processedGraphVbo);
		//ceeGraphicsSetVertexBufferLayout(layout, 2, 8 * sizeof(float));
		//ceeGraphicsSetSubVertices(processedVertices, (ECG_DATA_POINTS) * sizeof(float) * 8);
		//ceeGraphicsFlushLineStrip(i + 1, 0);
		//ceeGraphicsFlushLineStrip(ECG_DATA_POINTS - i - 1, i + 1);

		if (qrsPeakLocations.size()) {
			ceeGraphicsBindVertexBuffer(peakMarkersVbo);
			ceeGraphicsSetVertexBufferLayout(layout, 2, 8 * sizeof(float));
			ceeGraphicsSetSubVertices(peakMarkersVertices.data(), qrsPeakLocations.size() * 8 * 3 * sizeof(float));
			ceeGraphicsFlushTriangles(qrsPeakLocations.size() * 3);
		}

		ceeGraphicsEndFrame(graphicsState);
		GLenum ec;
		if ((ec = glGetError()) != GL_NO_ERROR) {
			printf("OpenGL error: (%d)\n", ec);
			raise(SIGINT);
		}
	}

	alarmThread.join();

	ceeGraphicsShutdown(graphicsState);
	ceeGraphicsFreeState(graphicsState);

	return EXIT_SUCCESS;
}

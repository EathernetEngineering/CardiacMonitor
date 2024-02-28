#include <GLES2/gl2.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
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
#include "fontRenderer.h"

#define ECG_DATA_POINTS          1024
#define ECG_DATA_TIME_MS         15000.f
#define ECG_DATA_MS_PER_POINT    ECG_DATA_TIME_MS / ECG_DATA_POINTS
#define SAMPLE_RATE              ECG_DATA_POINTS/(ECG_DATA_TIME_MS/1000)

struct EcgData {
	float leadI[ECG_DATA_POINTS];
	float leadII[ECG_DATA_POINTS];
	float leadIII[ECG_DATA_POINTS];
	float resp[ECG_DATA_POINTS];
	bool leadsConnected;
};

static EcgData g_Data;
std::mutex g_DataMutex;

int g_Idx;

std::atomic<bool> g_Terminate = false;

extern "C" {
	void signalHandler(int signum) {
		static int inAborting = false;
		if (inAborting)
			return;

		inAborting = true;

		const char* errorMessage = "Recieved signal...\taborting.\n";
		write(STDERR_FILENO, errorMessage, strlen(errorMessage));

		g_Terminate = true;
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
	std::chrono::milliseconds(25),      // NONE
	std::chrono::milliseconds(2000),    // CYAN
	std::chrono::milliseconds(2000),    // YELLOW
	std::chrono::milliseconds(1000)     // RED
};

static AlarmSounds g_AlarmSound = AlarmSounds::NONE;
static std::mutex g_AlarmSoundMutex;

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
		if ((std::chrono::high_resolution_clock::now() - start) > g_AlarmTimes[int(g_AlarmSound)]) {
			start = std::chrono::high_resolution_clock::now();
			switch (g_AlarmSound) {
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
					std::lock_guard<std::mutex> guard(g_AlarmSoundMutex);
					g_AlarmSound = AlarmSounds::RED;
			}
		}
		if (g_Terminate) {
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

void doSensors() {
	using namespace std::chrono_literals;

	std::shared_ptr<cee::I2C> i2cBus = std::make_shared<cee::I2C>();
	cee::ADC adc = cee::ADC(i2cBus, cee::ADCType::PCF8591, 0x48);
	
	timespec startTime, currentTime, diffTime;
	clock_gettime(CLOCK_MONOTONIC, &startTime);
	
	while (!g_Terminate) {
		float sinFreq = (int)((map8BitToFloat(adc.ReadChannel(3)) + 0.3f) * 17.f) / 2.f;
//		float sinFreq = 7;
		clock_gettime(CLOCK_MONOTONIC, &currentTime);
		TimespecSub(&diffTime, &startTime, &currentTime);
		 float sinVal = (static_cast<float>(diffTime.tv_sec) + (static_cast<float>(diffTime.tv_nsec) / NSEC_PER_SEC));
		{
			std::scoped_lock lock(g_DataMutex);
			g_Data.leadII[g_Idx] = 0.1f * ((2.0f * pow(std::sin(sinVal*sinFreq), 50.f)) + (0.3f * std::pow(std::sin(sinVal*sinFreq - 1.f), 1.f)) + (0.2f * std::pow(std::sin(sinVal*sinFreq + 1.f), 50.f)) - (0.5f * std::pow(std::sin(sinVal*sinFreq - 0.2f), 50.f)) - (0.2f * std::pow(std::sin(sinVal*sinFreq + 0.4f), 50.f)));
//			g_Data.leadII[g_Idx] = (map8BitToFloat(adc.ReadChannel(3)) * 2 - 1.f) * 0.4;

			g_Data.leadsConnected = true;
		}

		g_Idx++;
		g_Idx %= ECG_DATA_POINTS;
		std::this_thread::sleep_for(std::chrono::duration<float, std::milli>(ECG_DATA_MS_PER_POINT));
	}
}

int main(int argc, char** arg) {

	signal(SIGINT, signalHandler);
	signal(SIGABRT, signalHandler);
	signal(SIGTERM, signalHandler);

	ceeGraphicsState* graphicsState = ceeGraphicsMallocState();
	assert(graphicsState != 0);

	ceeGraphicsInitialize(graphicsState);

	g_AlarmSound = AlarmSounds::NONE;

	std::thread alarmThread(doAlarms);
	std::thread sensorsThread(doSensors);

	const char* basicVertexShaderSource =
		"attribute vec4 aPosition;\n"
		"attribute vec4 aColor;\n"
		"\n"
		"varying vec4 vColor;\n"
		"\n"
		"void main() {\n"
		"	gl_Position = aPosition;\n"
		"	vColor = aColor;\n"
		"}\n";

	const char* basicFragmentShaderSource =
		"precision mediump float;\n"
		"\n"
		"varying vec4 vColor;\n"
		"\n"
		"void main() {\n"
		"	gl_FragColor = vColor;\n"
		"}\n";

	const char* basicShaderAttribNames[] = {
		"aPosition",
		"aColor"
	};
	uint32_t basicShaderAttribLocations[] = {
		0,
		1,
	};
	uint32_t basicShaderAttribCount = 2;

	uint32_t basicShaderProgram;

	assert(
			ceeGraphicsCreateShaderProgram(basicVertexShaderSource,
				basicFragmentShaderSource,
				&basicShaderProgram,
				basicShaderAttribNames,
				basicShaderAttribLocations,
				basicShaderAttribCount)
			!= false);

	ceeGraphicsVertexBufferElement basicVertexlayout[] = {
		{ GL_TYPE_FLOAT4, 4 * sizeof(float), 0, false },
		{ GL_TYPE_FLOAT4, 4 * sizeof(float), 4 * sizeof(float), false }
	};

	const char* ttfFileName = "/usr/share/fonts/truetype/lato/Lato-Regular.ttf";
	ceeFontRendererIntialize(1920, 1080);
	ceeFont* numberFont = ceeFontRendererCreateFont(ttfFileName, 175.0f, 1024, 1024);
	ceeFont* warningFont = ceeFontRendererCreateFont(ttfFileName, 50.0f, 1024, 1024);
	if (!numberFont || !warningFont) {
		printf("Failed to create font");
	}

	float* graphVertices = reinterpret_cast<float*>(calloc(ECG_DATA_POINTS * 8, sizeof(float)));
	float* processedVertices = reinterpret_cast<float*>(calloc((ECG_DATA_POINTS) * 8, sizeof(float)));
	float* peakMarkersVertices = reinterpret_cast<float*>(calloc(1024, sizeof(float)));

	uint32_t graphVbo, processedGraphVbo, peakMarkersVbo;
	ceeGraphicsCreateVertexBuffer(&graphVbo);
	ceeGraphicsBindVertexBuffer(graphVbo);
	ceeGraphicsSetVertexBufferLayout(basicVertexlayout, 2, 8 * sizeof(float));
	ceeGraphicsSetVertices(graphVertices, ECG_DATA_POINTS * sizeof(float) * 8);

	ceeGraphicsCreateVertexBuffer(&processedGraphVbo);
	ceeGraphicsBindVertexBuffer(processedGraphVbo);
	ceeGraphicsSetVertexBufferLayout(basicVertexlayout, 2, 8 * sizeof(float));
	ceeGraphicsSetVertices(processedVertices, (ECG_DATA_POINTS) * sizeof(float) * 8);

	ceeGraphicsCreateVertexBuffer(&peakMarkersVbo);
	ceeGraphicsBindVertexBuffer(peakMarkersVbo);
	ceeGraphicsSetVertexBufferLayout(basicVertexlayout, 2, 8 * sizeof(float));
	ceeGraphicsSetVertices(peakMarkersVertices, 1024 * sizeof(float));

	std::vector<float> qrsPeakLocations;
	std::vector<float> rrIntevals;
	rrIntevals.reserve(128);
	std::vector<float> doubleDifferenceSquared;

	std::vector<float> leadIICopy(ECG_DATA_POINTS, 0.f);
	bool leadsConnected;

	g_Terminate.store(false);
	while (!g_Terminate) {
		{
			std::scoped_lock lock(g_DataMutex);
			std::copy(g_Data.leadII, g_Data.leadII + ECG_DATA_POINTS, leadIICopy.begin());

			leadsConnected = g_Data.leadsConnected;
		}

		uint32_t i = g_Idx;
		cee::CalculateDoubleDifferenceSquared(leadIICopy, doubleDifferenceSquared);

		createGraphBuffer(
				leadIICopy.data(),
				leadIICopy.size() * sizeof(float),
				0.25f,
				1.0f,
				-0.2f,
				0.8f,
				0.0f,
				1.0f,
				0.0f,
				1.0f,
				graphVertices);

		createGraphBuffer(
				doubleDifferenceSquared.data(),
				doubleDifferenceSquared.size() * sizeof(float),
				-0.25f,
				1.0f,
				-0.2f,
				0.8f,
				0.0f,
				0.0f,
				1.0f,
				1.0f,
				processedVertices);

		cee::FindQrsPeaks(doubleDifferenceSquared, 1.1f, qrsPeakLocations, ECG_DATA_TIME_MS);
		createPeakChevrons(
				qrsPeakLocations.data(),
				qrsPeakLocations.size() * sizeof(float),
				0.5f,
				0.1f,
				0.8f,
				-0.2f,
				0.0f,
				1.0f,
				0.0f,
				1.0f,
				peakMarkersVertices,
				1024 * sizeof(float));

		ceeGraphicsUseShaderProgram(basicShaderProgram);
		ceeGraphicsClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		ceeGraphicsStartFrame(graphicsState);

		ceeGraphicsBindVertexBuffer(graphVbo);
		ceeGraphicsSetVertexBufferLayout(basicVertexlayout, 2, 8 * sizeof(float));
		ceeGraphicsSetSubVertices(graphVertices, ECG_DATA_POINTS * sizeof(float) * 8);
		ceeGraphicsFlushLineStrip(g_Idx + 1, 0);
		ceeGraphicsFlushLineStrip(ECG_DATA_POINTS - g_Idx - 1, g_Idx + 1);

//		ceeGraphicsBindVertexBuffer(processedGraphVbo);
//		ceeGraphicsSetVertexBufferLayout(basicVertexlayout, 2, 8 * sizeof(float));
//		ceeGraphicsSetSubVertices(processedVertices, ECG_DATA_POINTS * sizeof(float) * 8);
//		ceeGraphicsFlushLineStrip(g_Idx + 1, 0);
//		ceeGraphicsFlushLineStrip(ECG_DATA_POINTS - g_Idx - 1, g_Idx + 1);

		if (qrsPeakLocations.size() > 0) {
			ceeGraphicsBindVertexBuffer(peakMarkersVbo);
			ceeGraphicsSetVertexBufferLayout(basicVertexlayout, 2, 8 * sizeof(float));
			ceeGraphicsSetSubVertices(peakMarkersVertices, qrsPeakLocations.size() * 8 * 3 * sizeof(float));
			ceeGraphicsFlushTriangles(qrsPeakLocations.size() * 3);
		}

		uint32_t rate;
		if (qrsPeakLocations.size() > 3) {
			float idxLoc = static_cast<float>(i)/static_cast<float>(ECG_DATA_POINTS);
			for (auto&& peakLoc : qrsPeakLocations) {
				peakLoc += 1.f;
				peakLoc /= 2.f;
			}
			for (auto it = qrsPeakLocations.begin(); it != qrsPeakLocations.end(); it++) {
				if (idxLoc > *it) {
					for (auto itMinusOne = it; itMinusOne != qrsPeakLocations.end(); itMinusOne++) {
						*itMinusOne -= 1.f;
					}
					std::rotate(qrsPeakLocations.begin(), it, qrsPeakLocations.end());
					break;
				}
			}
			rrIntevals.clear();
			float rrAvg = 0.0;
			for (auto it = qrsPeakLocations.begin(); it != (qrsPeakLocations.end() - 1); it++) {
				rrIntevals.push_back(*(it + 1) - *(it));
				rrAvg += (*(it + 1) - *(it)) * ECG_DATA_TIME_MS;
			}
			rrAvg /= (rrIntevals.size() - 1);
			rate = static_cast<uint32_t>(60000.f / rrAvg);
		} else {
			rate = 0;
		}

		char rateStr[3];
		if (rate > 999) {
			rate = 999;
		}

		sprintf(rateStr, "%d", rate);
		float rateTextX = 1600.f, rateTextY = 750.f;
		ceeFontRendererDraw(numberFont, rateStr, &rateTextX, &rateTextY);

		float warningX = 0.f, warningY = 1040.f;
		if (rate > 150) {
			std::scoped_lock lock(g_AlarmSoundMutex);
			g_AlarmSound = AlarmSounds::RED;
			ceeFontRendererDraw(warningFont, "**XTREME TACHY", &warningX, &warningY);
		} else if (rate > 120) {
			std::scoped_lock lock(g_AlarmSoundMutex);
			ceeFontRendererDraw(warningFont, "*TACHY", &warningX, &warningY);
			g_AlarmSound = AlarmSounds::YELLOW;
		} else if (rate == 0) {
			std::scoped_lock lock(g_AlarmSoundMutex);
			g_AlarmSound = AlarmSounds::RED;
			ceeFontRendererDraw(warningFont, "***ASYSTOLE", &warningX, &warningY);
		} else if (rate < 40) {
			std::scoped_lock lock(g_AlarmSoundMutex);
			g_AlarmSound = AlarmSounds::RED;
			ceeFontRendererDraw(warningFont, "**XTREME BRADY", &warningX, &warningY);
		} else if (rate < 50) {
			std::scoped_lock lock(g_AlarmSoundMutex);
			ceeFontRendererDraw(warningFont, "*BRADY", &warningX, &warningY);
			g_AlarmSound = AlarmSounds::YELLOW;
		} else if (!leadsConnected) {
			std::scoped_lock lock(g_AlarmSoundMutex);
			g_AlarmSound = AlarmSounds::CYAN;
			ceeFontRendererDraw(warningFont, "ECG LEADS OFF", &warningX, &warningY);
		} else {
			std::scoped_lock lock(g_AlarmSoundMutex);
			g_AlarmSound = AlarmSounds::NONE;
		}

		ceeGraphicsEndFrame(graphicsState);
		GLenum ec;
		if ((ec = glGetError()) != GL_NO_ERROR) {
			printf("OpenGL error: (%d) on line %d\n", ec, __LINE__);
			raise(SIGINT);
		}
	}

	sensorsThread.join();
	alarmThread.join();

	ceeFontRendererDeleteFont(numberFont);
	ceeFontRendererDeleteFont(warningFont);
	ceeFontRendererShutdown();
	ceeGraphicsShutdown(graphicsState);
	ceeGraphicsFreeState(graphicsState);

	return EXIT_SUCCESS;
}

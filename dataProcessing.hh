#ifndef CEE_DATA_PROCESSING_H_
#define CEE_DATA_PROCESSING_H_

#include <algorithm>
#include <vector>
#include <type_traits>

#include <cstdint>
#include <cstddef>
#include <cmath>

namespace cee {
	template<typename T>
	std::vector<T>& CalculateDoubleDifferenceSquared(const std::vector<T>& data, std::vector<T>& out) {
		static_assert(std::is_floating_point<T>::value, "T must be a floating point type.");
		static std::vector<T> intemediate;
		intemediate.clear();
		intemediate.resize(data.size());
		out.clear();
		out.resize(data.size());

		for (uint32_t i = 2; i < data.size() - 2; i++) {
			intemediate[i] = (-2.f * data[i - 2]) + (-1.f * data[i - 1]) +  (1.f * data[i + 1]) + (2.f * data[i + 2]);
			intemediate[i] = intemediate[i] * intemediate[i];
		}
		for (uint32_t i = 1; i < intemediate.size() - 1; i++) {
			out[i] = (1.f * intemediate[i - 1]) +  (2.f * intemediate[i]) + (1.f * intemediate[i + 1]);
		}

		return out;
	}

	template<typename T>
	std::vector<T>& FindQrsPeaks(const std::vector<T>& data, T threshold, std::vector<T>& peaks) {
		static_assert(std::is_floating_point<T>::value, "T must be a floating point type.");
		peaks.clear();
		threshold = std::max((*std::max_element(data.begin(), data.end())) * 0.75f, threshold);
		for (uint32_t i = 0; i < data.size(); i++) {
			if (data[i] > threshold) {
				peaks.push_back( 2.0f * (static_cast<float>(i) / static_cast<float>(data.size())) - 1.0f);
				i += 10;
			}
		}
		return peaks;
	}
}

#endif


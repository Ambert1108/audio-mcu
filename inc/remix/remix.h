#pragma once

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <queue>
#include <mutex>
#include <map> 
#include "seeker/common.h"
#include "seeker/logger.h"
#include "seeker/loggerApi.h"
#include "seeker/iniConfig.hpp"

#define AL_FORMAT AL_FORMAT_MONO16
#define MAX_PCM_VALUE           32767    // 16-bit PCM 最大值
#define MIN_PCM_VALUE           -32768   // 16-bit PCM 最小值

namespace Remix {

	class AudioMixer {
	public:
		AudioMixer();

		bool addStreamId(std::string streamId);

		bool pushData(std::string streamId, const std::vector<int16_t> audio);

		std::vector<int16_t> getData();

		std::vector<int16_t> getData(std::queue<std::string> streamId);

		bool removeId(std::string streamId);

		void clearData();

	private:
		std::mutex pushLocker;
		std::mutex getLocker;
		std::mutex clearLocker;
		std::map<std::string, std::vector<int16_t>> audioMap;

		std::vector <int16_t> mixAudio(std::queue<std::string> streamId);
	};
}
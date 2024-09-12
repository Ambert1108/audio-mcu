#include "seeker/common.h"
#include "seeker/logger.h"
#include "seeker/loggerApi.h"
#include <cstdlib>
#include <iostream>
#include <queue>

namespace hybird {
	class AudioMixer {

	public:

		bool addStreamId(std::string id);

		bool pushData(std::string id, const std::vector<int16_t> data);

		std::vector<int16_t> getData();

		std::vector<int16_t> getData(std::queue<std::string> idList);

		void clearData();

		bool removeId(std::string id);

		AudioMixer();

	private:
		std::map<std::string, std::vector<int16_t>> mediaBuffer{};
	};
}
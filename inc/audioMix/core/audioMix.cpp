#include "audioMix.h"

namespace hybird {
	bool AudioMixer::addStreamId(std::string id) {
		/*
		增加一路新的音频流，id为音频流id
		初始化一个与id绑定的缓冲区
		返回是否添加成功
		*/
		auto it = mediaBuffer.find(id);
		if (it == mediaBuffer.end()) {
			std::vector<int16_t> pcm;
			mediaBuffer.insert(std::make_pair(id, std::move(pcm)));
			return true;
		}
		else {
			E_LOG("[audioMix::addStream] addStream error");
			return false;
		}
	}

	bool AudioMixer::pushData(std::string id, const std::vector<int16_t> data) {
		/*
		向混音源id中添加pcm音频Data
		将Data存储到与id绑定的缓冲区中(uint8_t->int16_t)
		添加成功返回1，失败返回0
		*/
		//I_LOG("mediaBuffer size:{}", mediaBuffer.size());
		auto it = mediaBuffer.find(id);
		if (it != mediaBuffer.end()) {
			for (int i = 0; i < data.size(); i++) {
				it->second.push_back(data[i]);
			}
			return true;
		}
		else {
			E_LOG("[audioMix::pushData] no stream id");
			return false;
		}
	}

	std::vector<int16_t> AudioMixer::getData() {
		/*
		取所有缓冲区中的最大缓冲区长度作为混音长度，对空余pcm数据进行补0操作
		混合混音长度的音频数据
		返回混合后的pcm音频流
		*/
		int streamNum = mediaBuffer.size();
		auto it = mediaBuffer.begin();
		int maxLength = it->second.size();
		for (auto& p : mediaBuffer) {
			if (p.second.size() > maxLength)
				maxLength = p.second.size();
		}
		I_LOG("maxLength:{}", maxLength);
		std::vector<int16_t> data(maxLength);
		int16_t mixed = 0;
		for (int i = 0; i < maxLength; i++) {
			for (auto& p : mediaBuffer) {
				if (i >= p.second.size())
					continue;
				mixed = mixed + (p.second[i] / streamNum);
				/*
				if (mixed > INT16_MAX) {
					std::cout << "too big" << std::endl;
					mixed = INT16_MAX;
				}
				else if (mixed < INT16_MIN) {
					std::cout << "too small" << std::endl;
					mixed = INT16_MIN;
				}
				*/
			}
			data[i] = mixed;
			mixed = 0;
		}
		return data;
	}

	std::vector<int16_t> AudioMixer::getData(std::queue<std::string> idList) {
		/*
		根据streamId，对比需要的缓冲区大小，按照最大长度作为混音长度，对空余pcm数据进行补0操作
		混合streamId需要的缓冲区中已有的音频数据
		返回混合后的pcm音频流
		*/
		std::map<std::string, std::vector<int16_t>> newBuffer{};
		int streamNum = idList.size();

		for (int i = 0; i < streamNum; i++) {
			std::string id = idList.front();
			idList.pop();
			auto it = mediaBuffer.find(id);
			if (it != mediaBuffer.end()) {
				newBuffer.insert(std::make_pair(id, it->second));
			}
			else {
				E_LOG("[audioMix::pushData] no stream id");
				std::vector<int16_t> data;
				return data;
			}
		}

		auto it = newBuffer.begin();
		int maxLength = it->second.size();
		for (auto& p : newBuffer) {
			if (p.second.size() > maxLength)
				maxLength = p.second.size();
		}
		std::vector<int16_t> data(maxLength);
		int16_t mixed = 0;
		for (int i = 0; i < maxLength; i++) {
			for (auto& p : newBuffer) {
				if (i >= p.second.size())
					continue;
				mixed = mixed + (p.second[i] / streamNum);
			}
			data[i] = mixed;
			mixed = 0;
		}
		return data;

	}

	void AudioMixer::clearData() {
		for (auto& p : mediaBuffer) {
			p.second.clear();
		}
	}

	bool AudioMixer::removeId(std::string id) {
		auto it = mediaBuffer.find(id);
		if (it != mediaBuffer.end()) {
			mediaBuffer.erase(it);
			return true;
		}
		else {
			E_LOG("[audioMix::pushData] no stream id");
			return false;
		}
	}

	AudioMixer::AudioMixer() {

	}
}

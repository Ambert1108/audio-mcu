#include "remix.h"

namespace Remix {
    AudioMixer::AudioMixer() {}

    bool AudioMixer::addStreamId(std::string streamId) {
        //I_LOG("-----------  AudioMixer::addStreamId -----------");
        /*
        增加一路新的音频流，id为音频流id
        初始化一个与id绑定的缓冲区
        返回是否添加成功
        */
        pushLocker.lock();
        auto it = audioMap.find(streamId);
        if (it == audioMap.end()) {
            std::vector<int16_t> pcm;
            audioMap.insert(std::make_pair(streamId, std::move(pcm)));
            pushLocker.unlock();
            return true;
        }
        else {
            pushLocker.unlock();
            E_LOG("[AudioMixer::addStreamId] streamId[{}] existed", streamId);
            return false;
        }
    }

    bool AudioMixer::pushData(std::string streamId, const std::vector<int16_t> audio) {
        //I_LOG("-----------  AudioMixer::pushData -----------");
        /*
          向混音源id中添加pcm音频Data
          将Data存储到与id绑定的缓冲区中
          返回是否添加成功
        */
        pushLocker.lock();
        auto it = audioMap.find(streamId);
        if (it != audioMap.end()) {     //audioMap.end(),表示 Map的“结束”位置
            for (int16_t sample : audio) {
                it->second.push_back(sample);
            }
            pushLocker.unlock();
            return true;
        }
        else {
            pushLocker.unlock();
            E_LOG("[AudioMixer::pushData] no stream id");
            return false;
        }
    }

    std::vector<int16_t> AudioMixer::getData() {
        //I_LOG("-----------  AudioMixer::getData  ALL -----------");    
        /*
          混合所有缓冲区的音频数据
          返回混合后的pcm音频流
        */
        std::vector<int16_t> mixed;
        std::queue<std::string> streamId;
        for (auto pair : audioMap) {
            //std::cout << "Audio ID: " << pair.first << std::endl;
            streamId.push(pair.first);
        }
        mixed = mixAudio(streamId);

        return mixed;
    }

    std::vector<int16_t> AudioMixer::getData(std::queue<std::string> streamId) {
        //I_LOG("-----------  AudioMixer::getData  SELECT -----------");    
        /*
          混合指定的缓冲区的音频数据
          返回混合后的pcm音频流
        */
        std::vector<int16_t> mixed;
        mixed = mixAudio(streamId);

        return mixed;
    }

    bool AudioMixer::removeId(std::string streamId) {        
        //I_LOG("-----------  AudioMixer::removeId -----------");
        /*
          遍历map，删除指定的streamId
          返回是否移除成功
        */
        clearLocker.lock();
        auto it = audioMap.find(streamId);
        if (it != audioMap.end()) {
            audioMap.erase(it);
            clearLocker.unlock();
            //std::cout << "streamId [" << streamId << "] was deleted." << std::endl;
            return true;
        }
        else {
            clearLocker.unlock();
            E_LOG("[AudioMixer::removeId] streamId[{}] not found", streamId);
            return false;
        }
    }

    void AudioMixer::clearData() {
        //I_LOG("-----------  AudioMixer::clear -----------");
        /*
          遍历map，将所有缓冲区的数据清空
        */
        clearLocker.lock();
        for (auto& p : audioMap) {
            p.second.clear();
        }
        clearLocker.unlock();
    }

    std::vector <int16_t> AudioMixer::mixAudio(std::queue<std::string> streamId) {
        //I_LOG("-----------  AudioMixer::mixAudio  -----------");
        //I_LOG("queue has [{}] streamId", streamId.size());
        /*
          取指定的缓冲区中的最大缓冲区长度作为混音长度
          混合混音长度的音频数据
          返回混合后的pcm音频流
        */
        int maxLength = 0;
        std::vector<int16_t> mixData;
        std::vector<std::vector<int16_t>> audioData;

        //确定最大缓冲区长度
        int streamNum = streamId.size();
        for (int i = 0; i < streamNum; i++) {
            getLocker.lock();
            auto it = audioMap.find(streamId.front());
            if (it != audioMap.end()) {
                audioData.push_back(it->second);
                if (it->second.size() > maxLength)
                    maxLength = it->second.size();
                getLocker.unlock();
            }
            else {
                E_LOG("[AudioMixer::mixAudio] streamId[{}] not found", it->first);
                getLocker.unlock(); 
                return mixData;
            }
            streamId.pop();
        }
        //std::cout << "maxLength : " << maxLength << std::endl;

        //混音
        for (int m = 0; m < maxLength; m++) {
            int16_t mixed = 0;
            for (int n = 0; n < audioData.size(); n++) {
                int16_t sample = (m + 1 > audioData[n].size() ? 0 : audioData[n][m]); //补0：如果 m超出了 audioData[n]的有效索引范围，则将 sample设置为 0
                mixed += sample;
            }
            //if (mixed > MAX_PCM_VALUE) {
            //    //mixed = MAX_PCM_VALUE;
            //    std::cout << "mixed > MAX_PCM_VALUE : " << mixed << std::endl;
            //}
            //else if (mixed < MIN_PCM_VALUE) {
            //    //mixed = MIN_PCM_VALUE;
            //    std::cout << "mixed < MIN_PCM_VALUE : " << mixed << std::endl;
            //}
            mixData.push_back(mixed);
        }

        return mixData;
    }
}
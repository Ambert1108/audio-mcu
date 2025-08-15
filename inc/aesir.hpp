#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include "seeker/logger.h"
#include "seeker/loggerApi.h"
#include "seeker/common.h"
#include "whisper/include/whisper.h" 
#include <functional>

#define TRANSER_STATUS int
#define TRANSER_STATUS_NULL 0
#define TRANSER_STATUS_IDLE 1
#define TRANSER_STATUS_BUSY 2

namespace aesir {
    //// 定义要过滤的幻觉文本模式（支持中英文变体）
    //const std::vector<std::regex> hallucination_patterns = {
    //    std::regex("请不吝点赞|订阅|转发|打赏支持"),
    //    std::regex("Please like|subscribe|share"),
    //    std::regex("明镜与点点栏目")
    //};
    //// 文本净化函数
    //std::string FilterHallucination(const std::string& text) {
    //    std::string cleaned_text = text;
    //    for (const auto& pattern : hallucination_patterns) {
    //        cleaned_text = std::regex_replace(cleaned_text, pattern, "");
    //    }
    //    return cleaned_text;
    //}
    static bool containKeyword(const std::string& input,
        const std::vector<std::string>& keywords,
        bool wholeWord = false) {
        // 处理空关键字列表
        if (keywords.empty()) {
            return false;
        }

        // 子字符串匹配模式（高效简单）
        if (!wholeWord) {
            for (const auto& keyword : keywords) {
                if (input.find(keyword) != std::string::npos) {
                    return true;
                }
            }
            return false;
        }

        // 整词匹配模式（使用正则表达式）
        try {
            for (const auto& keyword : keywords) {
                // 构造单词边界正则表达式，注意转义特殊字符
                std::string pattern = "\\b" + std::regex_replace(keyword,
                    std::regex(R"([\.\*\+\?\|\(\)\[\]\{\}\\^\$])"), "\\$&") + "\\b";

                if (std::regex_search(input, std::regex(pattern))) {
                    return true;
                }
            }
        }
        catch (const std::regex_error& e) {
            // 处理无效的正则表达式
            std::cerr << "正则表达式错误: " << e.what() << std::endl;
            return false;
        }

        return false;
    }


    class TranscriberUnit {
    private:
        std::vector<std::string> suppressString = { 
            "请不吝点赞 订阅 转发 打赏支持明镜与点点栏目",
            "明镜与点点",
            "不吝点赞",
            "打赏",
            "中文字幕",
            "中文字体",
            "李宗盛",
            "优优独播剧场",
            "规范标点符号",
            "完整句子",
            "请忽略",
            "志愿者",
            "好好",
            "谢谢",
            "规范"
        };
        std::string transerId;
        std::atomic<TRANSER_STATUS> transerStatus = TRANSER_STATUS_IDLE;
        std::thread processThread;

        std::vector<float> audioData;
        std::string transcribeText;
        std::mutex audioMutex;
        std::mutex textMutex;

        whisper_context* wctx = nullptr;
        whisper_full_params wparams;
        whisper_state* wstate = nullptr;
        const int sampleRate = 16000;       // 音频采样率（默认16kHz）
        const int chunkSize = 100000;          // 处理块大小（样本数，默认3000≈188ms）
        const int slideGap = 50000;

    public:
        TranscriberUnit() {
            I_LOG("aesir unit constructed!");
        }
        ~TranscriberUnit() {
            I_LOG("aesir unit destructed!");
        }
        /*TRANSER_STATUS getStatus() {
            return transerStatus.load();
        }*/
        /*bool setStatus(TRANSER_STATUS status) {
            transerStatus.store(status);
            return 0;
        }*/
        bool setBusy() {
            transerStatus.store(TRANSER_STATUS_BUSY);
            return true;
        }
        bool setIdle() {
            transerStatus.store(TRANSER_STATUS_IDLE);
            return true;
        }
        bool isIdle() {
            if (transerStatus.load() == TRANSER_STATUS_IDLE) {
                return true;
            }
            return false;
        }
        bool init(
            const std::string& asrModelFile = "/home/blueFlower/workspace/aesir/whispercpp/whisper.cpp-master/models/ggml-large-v3-turbo.bin",
            const std::string& vadModelFile = "/home/blueFlower/workspace/aesir/whispercpp/whisper.cpp-master/models/ggml-silero-v5.1.2.bin"
        ) {
            struct whisper_context_params cparams = whisper_context_default_params();
            cparams.use_gpu = true;  // 全局启用 GPU
            cparams.gpu_device = 0;
            if (wctx == nullptr) {
                wctx = whisper_init_from_file_with_params(asrModelFile.c_str(), cparams);
                if (wctx) {
                    I_LOG("aesir transcriber startup success!");
                }
                else {
                    E_LOG("aesir transcriber startup failed!");
                }
            }
            if (wstate != nullptr) {
                whisper_free_state(wstate);
            }
            wstate = whisper_init_state(wctx);

            wparams = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
            wparams.no_speech_thold = 0.7; // 从默认0.6降至0.3-0.4
            wparams.logprob_thold = -1.5; // 平均对数概率阈值
            wparams.suppress_blank = true; // 强制抑制空白输出
            wparams.suppress_nst = true; 
            wparams.language = "zh";               // 指定中文识别
            wparams.n_threads = 32;                  // 线程数（根据CPU核心调整）
            //wparams.single_segment = true;          // 合并所有分段为单一段落
            //wparams.suppress_non_speech_tokens = true; // 抑制非语音标记（减少冗余词）
            wparams.initial_prompt = "以下是普通话的对话，请忽略噪音，使用简体中文和规范标点符号输出完整句子"; // 提升简体中文识别准确率
            wparams.length_penalty = 2.0;                     // 长度惩罚系数（>1鼓励长句，<1鼓励短句）
            wparams.beam_search.beam_size = 8;     // 默认2，实时场景不超过10
            wparams.temperature = 0.0; // 关闭随机性，避免错误生成
            wparams.max_len = 128;  // 每行最多20字符（中文）
            //wparams.suppress_regex = R"(.*请不吝点赞 订阅 转发 打赏支持明镜与点点栏目.*)";

            //流式设置
            wparams.strategy = WHISPER_SAMPLING_BEAM_SEARCH;
            wparams.print_special = false;      // 内部打印
            wparams.print_timestamps = false;      // 内部打印
            wparams.print_realtime = true;      // 内部打印
            wparams.print_progress = true;  // 显示实时解码进度
            wparams.no_context = true;           // 禁用长上下文（降低延迟）
            wparams.single_segment = false;       // 强制单段落输出（流式必需）

            //VAD
            wparams.vad = true;
            wparams.vad_model_path = vadModelFile.c_str();  // 模型路径
            wparams.vad_params.threshold = 0.8f;
            wparams.vad_params.min_speech_duration_ms = 400;
            wparams.vad_params.min_silence_duration_ms = 700;
            wparams.vad_params.max_speech_duration_s = 20;
            wparams.vad_params.speech_pad_ms = 30;

            if (!processThread.joinable()) {
                transerStatus.store(TRANSER_STATUS_IDLE);
                processThread = std::thread(&TranscriberUnit::processFull, this);
            }
            return true;
        }
        int stop() {
            if (processThread.joinable()) {
                transerStatus.store(TRANSER_STATUS_NULL);
                processThread.join();
            }
            if (wctx != nullptr) {
                whisper_free(wctx);
                wctx = nullptr;
                I_LOG("aesir free ctx: free success!");
            }
            else {
                I_LOG("aesir free ctx: no need to free!");
            }
            if (wstate != nullptr) {
                whisper_free_state(wstate);
                wstate = nullptr;
                I_LOG("aesir free state: free success!");
            }
            else {
                I_LOG("aesir free state: no need to free!");
            }
            return 0;
        }
        int input(const std::vector<int16_t>& inputData) {
          std::vector<float> pcmData(inputData.size());
          for (size_t i = 0; i < inputData.size(); i++) {
            pcmData[i] = static_cast<float>(inputData[i]);
          }
          input(pcmData);
          return 0;
        }
        int input(const std::vector<float>& inputData) {
          std::vector<float> normData(inputData.size());
          for (size_t i = 0; i < inputData.size(); i++) {
            normData[i] = inputData[i] / 32768.0f;
          }
          std::lock_guard<std::mutex> lock(audioMutex);
          audioData.insert(audioData.end(), normData.begin(), normData.end());
          return 0;
        }
        int output(std::string& outputText) {
            std::lock_guard<std::mutex> lock(textMutex);
            if (transcribeText.empty()) {
                return 1;
            }
            outputText = transcribeText;
            transcribeText = "";
            return 0;
        }
        void processFull() {
            while (transerStatus.load() != TRANSER_STATUS_NULL) {
                if (transerStatus.load() == TRANSER_STATUS_IDLE) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    //I_LOG("idle transer running");
                    continue;
                }
                std::lock_guard<std::mutex> lock(audioMutex);
                if (audioData.size() < chunkSize) {
                    continue;     // 空输入
                }
                if (!wctx) {
                    continue;       // 上下文未初始化
                }
                size_t processed = 0;
                while (processed < audioData.size()) {
                    // 计算当前块范围
                    const size_t remaining = audioData.size() - processed;
                    const size_t chunk_samples = (remaining < chunkSize * 2) ? remaining : chunkSize;

                    // 执行流式推理
                    const int ret = whisper_full_parallel(
                        wctx, wparams,
                        audioData.data() + processed, chunk_samples, 1
                    );
                    if (ret != 0) {
                        continue;
                    }

                    // 提取最新识别结果
                    int n_segments = whisper_full_n_segments(wctx);
                    if (n_segments > 0) {
                        const char* text = whisper_full_get_segment_text(wctx, n_segments - 1);
                        //I_LOG("latest result: [{}]", text);
                        std::lock_guard<std::mutex> lock(textMutex);
                        if (!containKeyword(text, suppressString)) {
                            transcribeText = text;
                        }
                    }
                    processed += chunk_samples;
                }
                //audioData.clear();
                if (slideGap >= audioData.size()) {  // 边界检查：n 过大时清空容器
                    audioData.clear();
                }
                else {
                    audioData.erase(audioData.begin(), audioData.begin() + slideGap);  // 删除前 n 个元素
                }
            }
            return;
        }
        void processState() {
            while (transerStatus.load()!= TRANSER_STATUS_NULL) {
                if (transerStatus.load() == TRANSER_STATUS_IDLE) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    //I_LOG("idle transer running");
                    continue;
                }
                std::lock_guard<std::mutex> lock(audioMutex);
                if (audioData.size() < chunkSize) {
                    continue;     // 空输入
                }
                if (!wctx) {
                    continue;       // 上下文未初始化
                }
                size_t processed = 0;
                while (processed < audioData.size()) {
                    // 计算当前块范围
                    const size_t remaining = audioData.size() - processed;
                    const size_t chunk_samples = (remaining < chunkSize * 2) ? remaining : chunkSize;

                    // 执行流式推理
                    const int ret = whisper_full_with_state(
                        wctx, wstate, wparams,
                        audioData.data() + processed, chunk_samples
                    );
                    if (ret != 0) {
                        continue;
                    }

                    // 提取最新识别结果
                    //int n_segments = whisper_full_n_segments(wctx);
                    int n_segments = whisper_full_n_segments_from_state(wstate);
                    if (n_segments > 0) {
                        const char* text = whisper_full_get_segment_text_from_state(wstate, n_segments - 1);
                        //I_LOG("latest result: [{}]", text);
                        std::lock_guard<std::mutex> lock(textMutex);
                        transcribeText += text;
                    }
                    processed += chunk_samples;
                }
                audioData.clear();
            }
        }
    };

    class TranscriberManager {
    private:
        std::map<std::string, TranscriberUnit> transerMap;
        std::mutex transerMapMutex;
        std::string asrModelPath;
        std::string vadModelPath;

    public:
        static TranscriberManager& getInstance() {
            static TranscriberManager instance;
            return instance;
        }
        TranscriberManager() {
            I_LOG("aesir manager constructed!");
        }
        ~TranscriberManager() {
            I_LOG("aesir manager destructed!");
        }
        bool startup(const int preInit,const std::string& asrModelFile, const std::string& vadModelFile) {
            asrModelPath = asrModelFile;
            vadModelPath = vadModelFile;
            for (int i = 0; i < preInit; i++) {
                std::string transerId = std::to_string(i);
                transerMap.try_emplace(transerId);
                auto pair = transerMap.find(transerId);
                if (pair == transerMap.end()) {
                    continue;
                }
                pair->second.init(asrModelPath, vadModelPath);
                I_LOG("aesir preinit [{}] done", i);
            }
            return true;
        }
        int shutdown() {
            for (auto& pair : transerMap) {
                pair.second.stop();
            }
            return 0;
        }
        bool getTranserId(std::string& transerId) {
            std::lock_guard<std::mutex> lock(transerMapMutex);
            int i = 0;
            for (auto& pair : transerMap) {
                if (pair.second.isIdle()) {
                    pair.second.setBusy();
                    transerId = pair.first;
                    return true;
                }
                i++;
            }
            std::string tmpId = std::to_string(i);
            transerMap.try_emplace(tmpId);
            auto pair = transerMap.find(tmpId);
            if (pair == transerMap.end()) {
                return false;
            }
            pair->second.init(asrModelPath, vadModelPath);
            pair->second.setBusy();
            transerId = tmpId;
            I_LOG("aesir init [{}] done", i);
            return true;
        }
        bool releaseTranser(const std::string& transerId) {
            std::lock_guard<std::mutex> lock(transerMapMutex);
            auto pair = transerMap.find(transerId);
            if (pair == transerMap.end()) {
                return false;
            }
            pair->second.setIdle();
            return true;
        }
        int pushInput(const std::string& transerId, const std::vector<int16_t>& pcmData) {
            std::lock_guard<std::mutex> lock(transerMapMutex);
            auto pair = transerMap.find(transerId);
            if (pair == transerMap.end()) {
                return -1;
            }
            int iret = pair->second.input(pcmData);
            return iret;
        }
        int pushInput(const std::string& transerId, const std::vector<float>& pcmData) {
          std::lock_guard<std::mutex> lock(transerMapMutex);
          auto pair = transerMap.find(transerId);
          if (pair == transerMap.end()) {
            return -1;
          }
          int iret = pair->second.input(pcmData);
          return iret;
        }
        int popOutput(const std::string& transerId, std::string& outputText) {
            std::lock_guard<std::mutex> lock(transerMapMutex);
            auto pair = transerMap.find(transerId);
            if (pair == transerMap.end()) {
                return -1;
            }
            int oret = pair->second.output(outputText);
            return oret;
        }
    };
}

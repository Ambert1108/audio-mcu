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

#define TRANSER_STATUS int
#define TRANSER_STATUS_NULL 0
#define TRANSER_STATUS_IDLE 1
#define TRANSER_STATUS_BUSY 2

namespace aesir {
    // 定义要过滤的幻觉文本模式（支持中英文变体）
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

    static bool read_wav(const std::string& filename, std::vector<int16_t>& pcm16) {
        // 打开文件并定位到末尾获取文件大小
        std::ifstream fin(filename, std::ios::binary | std::ios::ate);
        if (!fin) {
            // log_message("无法打开文件: " + filename);
            return false;
        }

        // 获取文件大小并重置读取位置
        size_t file_size = fin.tellg();
        fin.seekg(0, std::ios::beg);

        // 检查最小文件大小（至少包含RIFF头和data块标识）
        if (file_size < 44) { // WAV头最小为44字节[1,3](@ref)
            // log_message("错误：文件过小（" + std::to_string(file_size) + "字节），可能损坏");
            return false;
        }

        // 读取RIFF头验证文件格式
        char riff_header[12];
        fin.read(riff_header, 12);
        if (std::string(riff_header, 4) != "RIFF" ||
            std::string(riff_header + 8, 4) != "WAVE") {
            // log_message("错误：无效的WAV文件头");
            return false;
        }

        // 定位data块位置（兼容非标准头结构）[1,3](@ref)
        bool found_data = false;
        uint32_t data_offset = 0;
        uint32_t data_size = 0;

        while (fin.tellg() < file_size - 8) {
            char chunk_id[4];
            uint32_t chunk_size;
            fin.read(chunk_id, 4);
            fin.read(reinterpret_cast<char*>(&chunk_size), 4);

            if (std::string(chunk_id, 4) == "data") {
                found_data = true;
                data_offset = static_cast<uint32_t>(fin.tellg());
                data_size = chunk_size;
                break;
            }
            else {
                fin.seekg(chunk_size, std::ios::cur); // 跳过当前块
            }
        }

        if (!found_data) {
            // log_message("错误：未找到音频数据块");
            return false;
        }

        // 移动到数据块起始位置
        fin.seekg(data_offset, std::ios::beg);

        // 读取PCM数据
        pcm16.resize(data_size / sizeof(int16_t));
        fin.read(reinterpret_cast<char*>(pcm16.data()), data_size);

        // 检查实际读取量
        size_t bytes_read = fin.gcount();
        if (bytes_read != data_size) {
            pcm16.resize(bytes_read / sizeof(int16_t)); // 调整vector至实际大小
            // log_message("警告：部分读取（预期" + 
            //    std::to_string(data_size) + "字节，实际" + 
            //    std::to_string(bytes_read) + "字节）");
        }

        // 短音频检查（100ms = 1600样本@16kHz）
        if (pcm16.size() < 1600) {
            // E_LOG("wav sample [{}] <1600", pcm16.size());
        }

        return true;
    }
    // 读取WAV文件（16kHz单声道16位PCM格式）
    static bool read_wav(const std::string& filename, std::vector<float>& pcmf32) {
        I_LOG("读取音频");
        std::ifstream fin(filename, std::ios::binary | std::ios::ate); // 直接定位到文件末尾
        if (!fin) {
            //log_message("无法打开文件: " + filename);
            return false;
        }

        // 获取文件大小并定位回文件头
        size_t file_size = fin.tellg();
        fin.seekg(0, std::ios::beg);

        // 检查文件大小是否足够（至少44字节头+2字节数据）
        if (file_size < 46) {
            //log_message("错误：文件过小（" + std::to_string(file_size) + "字节），可能损坏");
            return false;
        }

        // 跳过44字节WAV头
        fin.seekg(44, std::ios::beg);
        size_t data_size = file_size - 44;

        // 读取PCM数据
        std::vector<int16_t> pcm16(data_size / sizeof(int16_t));
        fin.read(reinterpret_cast<char*>(pcm16.data()), data_size);
        size_t bytes_read = fin.gcount(); // 必须在read后调用！
        size_t num_samples = bytes_read / sizeof(int16_t); // 实际读取的样本数

        // 检查是否读取完整
        if (bytes_read != data_size) {
            //log_message("警告：部分读取（预期" + std::to_string(data_size) + "字节，实际" + std::to_string(bytes_read) + "字节）");
        }

        // 转换为float并归一化
        pcmf32.resize(num_samples);
        for (size_t i = 0; i < num_samples; ++i) {
            pcmf32[i] = static_cast<float>(pcm16[i]) / 32768.0f;
        }

        // 短音频填充（100ms = 1600样本@16kHz）
        if (num_samples < 1600) {
            E_LOG("wav sample [{}] <1600", num_samples);
        }

        return true;
    }

    static bool savePcmToWav(const std::string& filename,
        const std::vector<int16_t>& pcmData,
        uint32_t sampleRate,
        uint16_t numChannels)
    {
        // 1. 打开输出文件（二进制模式）
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return false;  // 文件打开失败
        }

        // 2. 计算关键参数
        const uint32_t dataSize = pcmData.size() * sizeof(int16_t); // PCM 数据字节数
        const uint32_t riffSize = 36 + dataSize;                     // RIFF 块总大小
        const uint32_t byteRate = sampleRate * numChannels * sizeof(int16_t); // 字节率
        const uint16_t blockAlign = numChannels * sizeof(int16_t);  // 块对齐字节数

        // 3. 写入 RIFF 文件头 [3,8](@ref)
        char riffHeader[12] = { 'R', 'I', 'F', 'F' };
        file.write(riffHeader, 4);
        file.write(reinterpret_cast<const char*>(&riffSize), 4);
        file.write("WAVE", 4);

        // 4. 写入 fmt 格式块 [8,9](@ref)
        char fmtHeader[24] = {
            'f', 'm', 't', ' ',  // Subchunk ID
            16, 0, 0, 0,         // fmt 块大小 (16字节)
            1, 0,                // 音频格式 (PCM=1)
            static_cast<char>(numChannels), 0,        // 声道数
            static_cast<char>(sampleRate & 0xFF),      // 采样率 (小端)
            static_cast<char>((sampleRate >> 8) & 0xFF),
            static_cast<char>((sampleRate >> 16) & 0xFF),
            static_cast<char>((sampleRate >> 24) & 0xFF),
            static_cast<char>(byteRate & 0xFF),        // 字节率 (小端)
            static_cast<char>((byteRate >> 8) & 0xFF),
            static_cast<char>((byteRate >> 16) & 0xFF),
            static_cast<char>((byteRate >> 24) & 0xFF),
            static_cast<char>(blockAlign & 0xFF), 0,   // 块对齐
            16, 0                                     // 位深度 (16-bit)
        };
        file.write(fmtHeader, sizeof(fmtHeader));

        // 5. 写入 data 块头 [3,9](@ref)
        char dataHeader[8] = { 'd', 'a', 't', 'a' };
        file.write(dataHeader, 4);
        file.write(reinterpret_cast<const char*>(&dataSize), 4);

        // 6. 写入 PCM 数据 [8](@ref)
        file.write(reinterpret_cast<const char*>(pcmData.data()), dataSize);

        // 7. 关闭文件 (RAII 自动处理)
        return true;
    }

    class TranscriberUnit {
    private:
        std::string transerId;
        std::atomic<TRANSER_STATUS> transerStatus = TRANSER_STATUS_IDLE;
        std::thread processThread;

        std::vector<float> audioData;
        std::string transcribeText;
        std::mutex audioMutex;
        std::mutex textMutex;

        whisper_context* wctx = nullptr;
        whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        whisper_state* wstate = nullptr;
        const int sampleRate = 16000;       // 音频采样率（默认16kHz）
        const int chunkSize = 90000;          // 处理块大小（样本数，默认3000≈188ms）
        const int minDataSize = 90000;
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
        bool init(const std::string& modelFile = "/home/blueFlower/workspace/aesir/whispercpp/whisper.cpp-master/models/ggml-large-v3-turbo.bin") {
            if (wctx == nullptr) {
                wctx = whisper_init_from_file(modelFile.c_str());
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

            wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
            wparams.no_speech_thold = 0.3; // 从默认0.6降至0.3-0.4
            wparams.logprob_thold = -0.8; // 平均对数概率阈值
            wparams.suppress_blank = true; // 强制抑制空白输出
            wparams.language = "zh";               // 指定中文识别
            wparams.n_threads = 32;                  // 线程数（根据CPU核心调整）
            //wparams.single_segment = true;          // 合并所有分段为单一段落
            //wparams.suppress_non_speech_tokens = true; // 抑制非语音标记（减少冗余词）
            wparams.initial_prompt = "以下是普通话的句子，请使用简体中文和规范标点符号。"; // 提升简体中文识别准确率[3](@ref)

            //流式设置
            wparams.strategy = WHISPER_SAMPLING_GREEDY;
            wparams.print_realtime = false;      // 内部打印
            wparams.print_progress = false;  // 显示实时解码进度
            wparams.no_context = true;           // 禁用长上下文（降低延迟）
            wparams.single_segment = false;       // 强制单段落输出（流式必需）

            if (!processThread.joinable()) {
                transerStatus.store(TRANSER_STATUS_IDLE);
                processThread = std::thread(&TranscriberUnit::process, this);
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
                pcmData[i] = static_cast<float>(inputData[i]) / 32768.0f;
            }
            input(pcmData);
            return 0;
        }
        int input(const std::vector<float>& inputData) {
            std::lock_guard<std::mutex> lock(audioMutex);
            audioData.insert(audioData.end(), inputData.begin(), inputData.end());
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
        void process() {
            while (transerStatus.load()!= TRANSER_STATUS_NULL) {
                if (transerStatus.load() == TRANSER_STATUS_IDLE) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    I_LOG("idle transer running");
                    continue;
                }
                std::lock_guard<std::mutex> lock(audioMutex);
                if (audioData.size() < 90000) {
                    continue;     // 空输入
                }
                if (!wctx) {
                    continue;       // 上下文未初始化
                }
                size_t processed = 0;
                while (processed < audioData.size()) {
                    // 计算当前块范围
                    const size_t remaining = audioData.size() - processed;
                    const size_t chunk_samples = (remaining < chunkSize) ? remaining : chunkSize;

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
                        I_LOG("latest result: [{}]", text);
                        std::lock_guard<std::mutex> lock(textMutex);
                        transcribeText += text;
                    }
                    processed += chunk_samples;
                }
                audioData.clear();
            }
        }
        int streamTranscribe(const std::vector<float>& pcmData, std::string& outputText) {
            if (pcmData.empty()) return -1;     // 错误码 -1：空输入
            if (!wctx) return -2;       // 错误码 -2：上下文未初始化
            I_LOG("a1");
            /*std::vector<float> pcmData(pcmf.size());
            for (size_t i = 0; i < pcmf.size(); i++) {
                pcmData[i] = static_cast<float>(pcmf[i]) / 32768.0f;
            }*/
            I_LOG("a2");
            size_t processed = 0;
            while (processed < pcmData.size()) {
                // 计算当前块范围
                const size_t remaining = pcmData.size() - processed;
                const size_t chunk_samples = (remaining < chunkSize) ? remaining : chunkSize;
                I_LOG("1");
                // 执行流式推理
                const int ret = whisper_full_with_state(
                    wctx, wstate, wparams,
                    pcmData.data() + processed, chunk_samples
                );
                if (ret != 0) return ret;        // 返回Whisper原生错误码
                
                // 提取最新识别结果
                //int n_segments = whisper_full_n_segments(wctx);
                int n_segments = whisper_full_n_segments_from_state(wstate);
                I_LOG("2 segment = {}", n_segments);
                if (n_segments > 0) {
                    const char* text = whisper_full_get_segment_text_from_state(wstate, n_segments - 1);
                    I_LOG("latest result: [{}]", text);
                    outputText = text;           // 覆盖更新为最新内容
                }
                I_LOG("3");
                processed += chunk_samples;
            }
            return 0;
        }

        int streamTranscribe(const std::vector<int16_t>& pcm16, std::string& outputText) {
            if (pcm16.empty()) return -1;     // 错误码 -1：空输入
            if (!wctx ) return -2;       // 错误码 -2：上下文未初始化
            I_LOG("a1");
            std::vector<float> pcmData(pcm16.size());
            for (size_t i = 0; i < pcm16.size(); i++) {
                pcmData[i] = static_cast<float>(pcm16[i]) / 32768.0f;
            }
            I_LOG("a2");
            size_t processed = 0;
            while (processed < pcmData.size()) {
                // 计算当前块范围
                const size_t remaining = pcmData.size() - processed;
                const size_t chunk_samples = (remaining < chunkSize) ? remaining : chunkSize;

                // 执行流式推理
                const int ret = whisper_full_with_state(
                    wctx, wstate, wparams,
                    pcmData.data() + processed, chunk_samples
                );
                if (ret != 0) return ret;        // 返回Whisper原生错误码

                // 提取最新识别结果
                //int n_segments = whisper_full_n_segments(wctx);
                int n_segments = whisper_full_n_segments_from_state(wstate);
                if (n_segments > 0) {
                    const char* text = whisper_full_get_segment_text_from_state(wstate, n_segments - 1);
                    I_LOG("latest result: [{}]", text);
                    outputText = text;           // 覆盖更新为最新内容
                }
                processed += chunk_samples;
            }
            return 0;
        }

        int localTranscribe(const std::string& filename = "/home/blueFlower/workspace/aesir/whispercpp/whisper.cpp-master/build/bin/tjt.wav") {

            // 2. 读取音频文件
            std::vector<float> pcmf32;
            if (!read_wav(filename, pcmf32)) {  // 替换为实际音频路径
                E_LOG("读取文件失败");
                return 1;
            }
            I_LOG("配置参数");
            // 3. 配置识别参数
            whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
            wparams.language = "zh";               // 指定中文识别
            wparams.n_threads = 4;                  // 线程数（根据CPU核心调整）
            //wparams.single_segment = true;          // 合并所有分段为单一段落
            //wparams.suppress_non_speech_tokens = true; // 抑制非语音标记（减少冗余词）
            wparams.initial_prompt = "以下是普通话的句子。"; // 提升简体中文识别准确率[3](@ref)
            I_LOG("开始识别");
            // 4. 执行识别
            if (whisper_full(wctx, wparams, pcmf32.data(), pcmf32.size()) != 0) {
                E_LOG("识别失败");
                return 1;
            }

            // 5. 输出结果
            I_LOG("识别结果如下");
            for (int i = 0; i < whisper_full_n_segments(wctx); ++i) {
                const char* text = whisper_full_get_segment_text(wctx, i);
                I_LOG("[{}]result:{}", i, text);
            }
            return 0;
        }
    };

    class TranscriberManager {
    private:
        std::map<std::string, TranscriberUnit> transerMap;
        std::mutex transerMapMutex;
        std::string modelPath;

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
        bool startup(const std::string& modelFile, const int preInit) {
            modelPath = modelFile;
            for (int i = 0; i < preInit; i++) {
                std::string transerId = std::to_string(i);
                transerMap.try_emplace(transerId);
                auto pair = transerMap.find(transerId);
                if (pair == transerMap.end()) {
                    continue;
                }
                pair->second.init(modelFile);
                I_LOG("preinit [{}] done", i);
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
            pair->second.init(modelPath);
            pair->second.setBusy();
            transerId = tmpId;
            I_LOG("init [{}] done", i);
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

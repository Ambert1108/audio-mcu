// @brief: 音频数据处理通道
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.9.9]
// @version: V0.0.1
// @revision: [Ambert@2024.9.9]

#pragma once

#include "Config.h"
#include "MediaType.h"

#include "rtpTrs/rtpTrs.hpp"
#include "AudioEngine23/AudioEngine23.hpp"
#include "utils/InvokeTimer.hpp"
#include "aesir.hpp" 

#include <deque>
#include <vector>
#include <thread>
#include <chrono>
#include <functional>
#include <cmath>

namespace aom {
	struct Point {
		std::string ip;
		port_t port;
	};

	/*
	* 任务状态类型
	* raw: 初始态，表示mpu正在初始化并加载资源。由内部控制
	* run: 运行态，表示mpu正在工作。初始态结束后自然成为运行态。外部可以修改
	* down:释放态，表示mpu正在释放资源并停止工作。由外部控制
	* end: 结束态，表示mpu已经完成所有工作，可以销毁。由内部控制
	* exce:异常态，表示mpu存在异常已无法正常工作，需要销毁。由内部控制
	* <Ambert 14-May-2024>
	*/
	enum class TaskStatusType : uint8_t {
		raw = 0,
		run,
		down,
		end,
		exce
	};

	class TaskStatus {
	public:
		TaskStatus() { };
		operator bool() const { return type.load(std::memory_order_acquire) == TaskStatusType::run; }
		TaskStatus& operator <<(TaskStatusType statusType) {
			this->type.store(statusType, std::memory_order_release);
			return *this;
		}
		TaskStatusType getStatus() const { return type.load(std::memory_order_acquire); }
	private:
		std::atomic<TaskStatusType> type{ TaskStatusType::raw };
	};

	struct ChnlData {
		uint32_t readPktNum = 0;
		uint32_t sendPktNum = 0;
		uint32_t sendPayloadNum = 0;

		int64_t firstKeyFrameTime = -1;

		uint32_t readTimeOutNum = 0;
		uint32_t procTimeOutNum = 0;
		uint32_t sendTimeOutNum = 0;

		uint16_t lastSendSeq = 0;

		uint32_t decodeCount = 0;
		uint32_t renderCount = 0;
		uint32_t encodeCount = 0;
	};

	using namespace seeker::rtp;
	typedef std::unique_ptr<AudioEngine23::Decoder> Decoder;
	typedef std::unique_ptr<AudioEngine23::Encoder> Encoder;
	typedef std::unique_ptr<AudioEngine23::Demuxer> Demuxer;
	typedef std::unique_ptr<AudioEngine23::Muxer> Muxer;
	typedef std::unique_ptr<RtpTransceiver> RtpTrxer;
	typedef std::shared_ptr<Notifier> RtpNotifier;
	typedef std::queue<std::vector<uint8_t>> NaluBuffer;
	typedef std::function<void(port_t)> FreePortCallback;

	class AudioPorcessChnl {
	public:
		AudioPorcessChnl(const std::string& jobid, const std::string& chnlid, Point listen, Point dst, FreePortCallback callback);
		~AudioPorcessChnl();
		bool open(int codecType, int inputRate, int outputRate, int payloadType);
		void close();
		float getVolume() const;
		size_t getLength() const;
		void getBuffer(std::vector<int16_t>& dst, size_t length);
		void setMicType(int val);
		void updateDestition(const std::string& ip, port_t port);
		TaskStatusType getStatus() const;
		int16_t getPort() const;
		void sendRtp(uint8_t* pcmData, int nb_samples, uint32_t ts);
		bool ready() const;
		bool micOpen() const;
	private:
		Decoder decoder;
		Encoder encoder;
		RtpTrxer switcher;
		mutable std::mutex switchLocker{};
		RtpNotifier notifier;
		AVFrame* encFrame;
		AVPacket* encPkt;
		SwrContext* swrContext;

		TaskStatus status;
		ChnlData data;
		std::vector<int16_t> srcBuffer{}; //源缓存区
		mutable std::mutex srcBufLocker{};
		std::atomic<bool> chnlReady{ false };
		FreePortCallback freePortCallback = nullptr;

		std::vector<int16_t> speechRecognitionPcmData{};
		std::mutex speechRecognitionPcmDataMtx;

		std::string jobId;
		std::string chnlId;
		std::string transerId;
		Point listenPoint, dstPoint;
		std::atomic<float> db = 0.0f;
		std::atomic<int> micType = 0; //0:off, !0:on
		int codecType = 1;
		int sampleRate = 1;
		int payloadType = 97;
		uint16_t seqNum = 0;
		uint32_t ts = 0;
		uint32_t ssrc = 0;
		const int64_t mpucheckInterval = seeker::IniConfig::GetInteger("log", "mpu_check_interval", 1);
		const int saveInput = seeker::IniConfig::GetInteger("test", "save_input", 0);
		const int saveOutput = seeker::IniConfig::GetInteger("test", "save_output", 0);
		const std::string zimuId = seeker::IniConfig::Get("test", "zimu_id", "3082");
		const bool isZimu = seeker::IniConfig::GetBoolean("test", "is_zimu", false);

		FILE* decFile = nullptr;
		FILE* encFile = nullptr;
		std::thread work1Th{};
		std::thread work2Th{};
		bool micOpenNeedClear = false;

		void workingLoop();
		void zimuLoop();
		int setDecoder(int codecType, int sampleRate);
		int setEncoder(int codecType, int sampleRate);
	};

	using UniqueAPC = std::unique_ptr<AudioPorcessChnl>;
}
// @brief: 媒体资源处理单元
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.9.9]
// @version: V0.0.1
// @revision: [Ambert@2024.9.9]

#pragma once

#include "Config.h"
#include "seeker/common.h"
#include "seeker/logger.h"
#include "seeker/loggerApi.h"
#include "AudioPorcessChnl.h"

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

	/*
	* 任务状态封装
	*/
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

	struct MediaProcessData {
		uint32_t readPktNum = 0;
		uint32_t sendPktNum = 0;
		uint32_t sendPayloadNum = 0;

		int64_t firstKeyFrameTime = -1;
		uint32_t timeOutNum = 0;

		uint16_t lastSendSeq = 0;

		int64_t creatingDuration = -1;
		int64_t destroyingDuration = -1;
		int64_t runningDuration = -1;

		uint32_t updateCount = 0;

		std::string closeMethod = "HttpRequest";
		std::string jobId = "";
		std::vector<port_t> portList{};
	};

	struct MpuContext {
		std::string jobId;
		int codecType;
		int outSampleRate;
		int bitrate;
		double interval; //ms
		int payloadType;
		MpuContext(std::string id, int type, int samplerate, int rate, int ti, int pt) 
			: jobId(id), codecType(type), outSampleRate(samplerate), bitrate(rate), payloadType(pt) {
			if (ti > 0) interval = ti;
			else {
				try {
					interval = 1.0 / outSampleRate * 1000;
				}
				catch (std::exception& ex) {
					E_LOG("count time interval failed, out sample rate is {}", outSampleRate);
					interval = 1.0;
				}
			}
		}
	};
	typedef std::unique_ptr<MpuContext> MpuCtxPtr;

	typedef std::unique_ptr<class MediaProcessUnit> UniqueMPU;
	typedef std::function<HandleError(std::string)> RemoveCallback;
	using namespace std::chrono_literals;

	class MediaProcessUnit {
	public:
		MediaProcessUnit(MpuCtxPtr&& ctxPtr, RemoveCallback callback);
		~MediaProcessUnit();

		void reportMediaInfo(std::unique_ptr<Event> info);

		TaskStatusType getStatus() const;
		const MediaProcessData& getData() const;
		int getChnlNum() const;

	private:
		std::thread workTh{};
		std::thread eventTh{};
		std::thread stopTh{};
		mutable std::mutex eventLocker{};
		std::condition_variable eventCondition{};
		const std::chrono::milliseconds wakeUpInterval = 1ms;
		RemoveCallback autoCloseCallback = nullptr;

		int64_t startTime = 0;
		bool isFirstFrameFlag = true;
		bool isNoRecvFlag = false;
		bool initDecFlag = true;
		std::atomic<bool> updateVideo = false;
		std::atomic<bool> updateTemplate = false;

		std::deque<std::unique_ptr<Event>> eventQue{};
		mutable std::shared_mutex eventQueLocker{};
		std::unordered_map<int32_t, UniqueAPC> APCs;

		TaskStatus status;
		MpuCtxPtr ctx;
		MediaProcessData data;
		//UniqueEPU epu = nullptr;

		const int64_t mpucheckInterval = seeker::IniConfig::GetInteger("log", "mpu_check_interval", 1);
		const int noRtpTime = seeker::IniConfig::GetInteger("auto", "no_rtp_time", 30);

		void stop();
		void output();
		void workingLoop();
		void eventHandle();

		friend class AddChnlEvent;
	};

	class EndEvent : public Event {
	public:
		EndEvent() : Event(JobHandleType::stop) {};
	};

	class AddChnlEvent : public Event {
	public:
		AddChnlEvent(const AddChnlContext& c) : Event(JobHandleType::update), context(std::move(c)) {};

		void handle(void* ptr) override {
			MediaProcessUnit* master = nullptr;
			if (ptr != nullptr) master = (MediaProcessUnit*)ptr;
		};

		AddChnlContext context;
	};

	class RemoveChnlEvent : public Event {
	public:
		RemoveChnlEvent(const RemoveChnlContext& c) : Event(JobHandleType::update), context(std::move(c)) {};

		void handle(void* ptr) override {
			MediaProcessUnit* master = nullptr;
			if (ptr != nullptr) master = (MediaProcessUnit*)ptr;
		};

		RemoveChnlContext context;
	};

	class OpenMicEvent : public Event {
	public:
		OpenMicEvent(const MicCtrlContext& c) : Event(JobHandleType::update), context(std::move(c)) {};

		void handle(void* ptr) override {
			MediaProcessUnit* master = nullptr;
			if (ptr != nullptr) master = (MediaProcessUnit*)ptr;
		};

		MicCtrlContext context;
	};

	class CloseMicEvent : public Event {
	public:
		CloseMicEvent(const MicCtrlContext& c) : Event(JobHandleType::update), context(std::move(c)) {};

		void handle(void* ptr) override {
			MediaProcessUnit* master = nullptr;
			if (ptr != nullptr) master = (MediaProcessUnit*)ptr;
		};

		MicCtrlContext context;
	};
}
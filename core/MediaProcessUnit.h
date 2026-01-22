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
#include "seeker/json.hpp"
#include "utils/InvokeTimer.hpp"
#include "utils/httplib.h"

#include "remix/remix.h"
//#include "sherpaonnx.h"
using namespace Remix;

#include "SignalMessage.hpp"
#include "AudioPorcessChnl.h"

namespace aom {

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

		uint16_t maxChnl = 0;
		uint16_t currentChnl = 0;
		uint16_t openMic = 0;

		std::string closeMethod = "HttpRequest";
		std::string jobId = "";
		std::vector<port_t> portList{};
	};

	struct MpuContext {
		std::string jobId;
		int codecType = -1;
		int outSampleRate = -1;
		std::string callbackUrl;
		std::string zimuUrl = "/zimu?roomId=video111222";
		std::string transerId;
		MpuContext(std::string id, std::string url)
			: jobId(id), callbackUrl(url) {};
	};
	typedef std::unique_ptr<MpuContext> MpuCtxPtr;

	typedef std::unique_ptr<class MediaProcessUnit> UniqueMPU;
	typedef std::function<bool(std::string)> RemoveCallback;
	using UniqueMix = std::unique_ptr<AudioMixer>;
	using namespace std::chrono_literals;

	class MediaProcessUnit {
	public:
		MediaProcessUnit(MpuCtxPtr&& ctxPtr, RemoveCallback callback1, FreePortCallback callback2);
		~MediaProcessUnit();

		void reportMediaInfo(std::unique_ptr<Event> info);

		TaskStatusType getStatus() const;
		const MediaProcessData& getData() const;
		const MpuInfo& getInfo();
		int getChnlNum() const;
		int getCodecType() const;
		void addChannel(const std::string& id, const Point& src, const Point& dst, int pt, 
			int codecType, int inRate, int outRate);
		void removeChannel(const std::string& id);
		void openChnlMic(const std::string&);
		void closeChnlMic(const std::string&);
		void updateDest(const std::string& id, const std::string& ip, port_t port);
	private:
		std::thread workTh{};
		std::thread eventTh{};
		std::thread stopTh{};
		mutable std::mutex eventLocker{};
		std::condition_variable eventCondition{};
		const std::chrono::milliseconds wakeUpInterval = 1ms;
		RemoveCallback autoCloseCallback = nullptr;
		FreePortCallback freePortCallback = nullptr;

		int64_t startTime = 0;
		int64_t noChnlTime = seeker::IniConfig::GetInteger("main", "auto_stop", 15);
		int64_t waitChnlTime = 0;
		bool isFirstFrameFlag = true;
		bool isNoRecvFlag = false;
		bool initDecFlag = true;
		std::atomic<bool> updateVideo = false;
		std::atomic<bool> updateTemplate = false;
		int bitrate = 0;

		std::deque<std::unique_ptr<Event>> eventQue{};
		std::unordered_map<std::string, UniqueAPC> APCs;
		mutable std::shared_mutex eventQueLocker{};
		mutable std::mutex apcLocker{};
		mutable std::mutex mixerLocker{};
		mutable std::mutex infoLocker{};

		InvokeTimerPtr callback;
		TaskStatus status;
		MpuCtxPtr ctx;
		MediaProcessData data;
		MpuInfo mpuInfo;
		UniqueMix mixer;
		SwrContext* swrContext = nullptr;
		SwrContext* trsSwrContext = nullptr;
		std::string url, chnlIdRecord, chnlId;
		std::shared_ptr<httplib::Client> client;
		int64_t callbackTimePoint = 0;

		const int64_t mpucheckInterval = seeker::IniConfig::GetInteger("log", "mpu_check_interval", 1);
		const int callbackTime = seeker::IniConfig::GetInteger("main", "call_back", 1);
		const int dbThreshold = seeker::IniConfig::GetInteger("media", "db_threshold", 55);
		const int noVoiceTime = seeker::IniConfig::GetInteger("media", "no_voice_time", 3);

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
		AddChnlEvent(const AddChnlContext& c) : Event(JobHandleType::add), context(std::move(c)) {};

		void handle(void* ptr) override {
			MediaProcessUnit* master = nullptr;
			if (ptr != nullptr) master = (MediaProcessUnit*)ptr;
			master->addChannel(context.chnlId, Point{ context.listenIp, context.listenPort
				}, Point{ context.dstIp, context.dstPort }, context.payloadType, context.codecType,
				context.inSampleRate, context.outSampleRate);
		};

		AddChnlContext context;
	};

	class RemoveChnlEvent : public Event {
	public:
		RemoveChnlEvent(const RemoveChnlContext& c) : Event(JobHandleType::remove), context(std::move(c)) {};

		void handle(void* ptr) override {
			MediaProcessUnit* master = nullptr;
			if (ptr != nullptr) master = (MediaProcessUnit*)ptr;
			master->removeChannel(context.chnlId);
		};

		RemoveChnlContext context;
	};

	class OpenMicEvent : public Event {
	public:
		OpenMicEvent(const MicCtrlContext& c) : Event(JobHandleType::open), context(std::move(c)) {};

		void handle(void* ptr) override {
			MediaProcessUnit* master = nullptr;
			if (ptr != nullptr) master = (MediaProcessUnit*)ptr;
			master->openChnlMic(context.channelId);
		};

		MicCtrlContext context;
	};

	class CloseMicEvent : public Event {
	public:
		CloseMicEvent(const MicCtrlContext& c) : Event(JobHandleType::close), context(std::move(c)) {};

		void handle(void* ptr) override {
			MediaProcessUnit* master = nullptr;
			if (ptr != nullptr) master = (MediaProcessUnit*)ptr;
			master->closeChnlMic(context.channelId);
		};

		MicCtrlContext context;
	};

	class UpdateDestEvent : public Event {
	public:
		UpdateDestEvent(const UpdateContext& c) : Event(JobHandleType::close), context(std::move(c)) {};

		void handle(void* ptr) override {
			MediaProcessUnit* master = nullptr;
			if (ptr != nullptr) master = (MediaProcessUnit*)ptr;
			master->updateDest(context.channelId, context.dstIp, context.dstPort);
		};

		UpdateContext context;
	};
}
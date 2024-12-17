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
#if USE_X_MIX
#include "audioMix/core/audioMix.h"
using namespace hybird;
#else
#include "remix/remix.h"
using namespace Remix;
#endif

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
		int codecType;
		int inSampleRate;
		int outSampleRate;
		int bitrate;
		double interval; //ms
		int payloadType;
		MpuContext(std::string id, int type, int samplerate, int rate, int ti, int pt) 
			: jobId(id), codecType(type), inSampleRate(0), outSampleRate(samplerate), 
			bitrate(rate), payloadType(pt) {
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
	using UniqueMix = std::unique_ptr<AudioMixer>;
	using namespace std::chrono_literals;

	class MediaProcessUnit {
	public:
		MediaProcessUnit(MpuCtxPtr&& ctxPtr, RemoveCallback callback1, FreePortCallback callback2);
		~MediaProcessUnit();

		void reportMediaInfo(std::unique_ptr<Event> info);

		TaskStatusType getStatus() const;
		const MediaProcessData& getData() const;
		int getChnlNum() const;
		void addChannel(const std::string& id, const Point& src, const Point& dst, int pt, int sampleRate);
		void removeChannel(const std::string& id);
		void openChnlMic(const std::string&);
		void closeChnlMic(const std::string&);
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

		std::deque<std::unique_ptr<Event>> eventQue{};
		std::unordered_map<std::string, UniqueAPC> APCs;
		mutable std::shared_mutex eventQueLocker{};
		mutable std::mutex apcLocker{};
		mutable std::mutex mixerLocker{};

		TaskStatus status;
		MpuCtxPtr ctx;
		MediaProcessData data;
		UniqueMix mixer;
		Encoder encoder;

		const int64_t mpucheckInterval = seeker::IniConfig::GetInteger("log", "mpu_check_interval", 1);
		const int noRtpTime = seeker::IniConfig::GetInteger("auto", "no_rtp_time", 30);

		void stop();
		void output();
		void workingLoop();
		void eventHandle();
		int setEncoder(int sampleRate);
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
				}, Point{ context.dstIp, context.dstPort }, context.payloadType, context.sampleRate);
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
}
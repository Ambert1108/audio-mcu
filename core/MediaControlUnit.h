// @brief: 媒体能力控制单元
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.9.9]
// @version: V0.0.1
// @revision: [Ambert@2024.9.9]

#pragma once

#include "Config.h"
#include "HttpProcesser.hpp"
#include "MediaProcessUnit.h"

#include "utils/InvokeTimer.hpp"

namespace aom {

	struct MCUStatus {
		/* 最大主机内存,单位mb */
		size_t maxHostMem = 0;
		int64_t hostMemKeepTimePoint = 0;

		std::atomic<uint64_t> runningJob = 0;
		std::atomic<uint64_t> runningChnl = 0;
	};

	using PortList = std::unordered_set<port_t>;
	class PortTool {
	public:
		PortTool(port_t point, int range, const std::string& name) 
			: portPoint(point), portRange(range), portTypeName(name) {};
		~PortTool() {};

		port_t applyPort() {
			port_t port = 0;
			int failNum = 0;

			while (true) {
				if (portIndex.load() > portRange) {
					portIndex.store(0);
				}
				port = portIndex.fetch_add(2) + portPoint;
				{
					lockGuard lck(portLocker);
					if (ports.count(port) == 0) {
						ports.emplace(port);
						break;
					}
				}
				E_LOG("PortTool::applyPort::Error: {} port {} is occur, apply failed", portTypeName, port);
				failNum += 2;
				if (failNum > portRange) {
					E_LOG("PortTool::applyPort::Error: apply available {} port failed", portTypeName);
					return APPLY_UDP_PORT_ERROR;
				}
			}

			T_LOG("JobManager apply port:{}", port);
			return port;
		}

		/*
		* 释放指定的占用端口
		* <Ambert 9-May-2024>
		*/
		inline void freePort(const port_t& port) {
			if (port < portPoint) {
				E_LOG("PortTool::freePort::Error:free {} port={} is invalid", portTypeName, port);
				return;
			}
			lockGuard Lock(portLocker);
			ports.erase(port);
		}

		/*
		* 释放所有占用端口，端口标志位重置为0
		* <Ambert 9-May-2024>
		*/
		inline void freePort() {
			lockGuard Lock(portLocker);
			ports.clear();
			portIndex = 0;
		}

	private:
		const std::string portTypeName;
		const port_t portPoint;
		const int portRange;
		std::atomic<port_t> portIndex = 0;
		PortList ports = {};
		mutable std::mutex portLocker = {};
	};

	struct ListenAddr {
		std::string ip;
		port_t port;
	};

	typedef RemoveCallback RemoveFunc;
	typedef std::string JobId;
	using mpuCloseForm = std::unordered_set<UniqueMPU>;
	using mpuForm = std::unordered_map<JobId, UniqueMPU>;
	using mpuIdList = std::vector<std::string>;

	class MediaControlUnit {
		MediaControlUnit();
		void autoClose();

		const int64_t mcucheckInterval = seeker::IniConfig::GetInteger("log", "mcu_check_interval", 1);
		const int autocheckInterval = seeker::IniConfig::GetInteger("log", "auto_check_interval", 300);

		const port_t portPoint = seeker::IniConfig::GetInteger("main", "port_point", 62300);
		const int portRange = seeker::IniConfig::GetInteger("main", "ort_range", 200);

		const std::string mediaIp = seeker::IniConfig::Get("media", "ip", "0.0.0.0");
		const int codecType = seeker::IniConfig::GetInteger("media", "codec_type", 1);
		const int bitrate = seeker::IniConfig::GetInteger("media", "bit_rate", 960000);
		const int samplerate = seeker::IniConfig::GetInteger("media", "sample_rate", 8000);
		const int pt = seeker::IniConfig::GetInteger("media", "payload_type", 97);


		static MediaControlUnit* mcu;
		static std::atomic<uint16_t> refCount_;

		std::unique_ptr<PortTool> audioPortTool = nullptr;
		mpuForm mpus;
		mpuCloseForm closeMpus;

		std::thread AutoCloseThr = {};
		mutable std::shared_mutex mpuFormLocker = {};
		mutable std::shared_mutex closeMpuFormLocker = {};
		mutable std::mutex closeLocker = {};
		std::condition_variable closeCondition = {};
		RemoveFunc func = nullptr;

		std::atomic<bool> keepWork{ true };
		std::atomic<uint64_t> autoCloseNum{ 0 };
		uint32_t createNum = 0;

		MCUStatus status;
		InvokeTimerPtr jobPoll = nullptr;
		InvokeTimerPtr mcuCheck = nullptr;

	public:
		MediaControlUnit(const MediaControlUnit&) = delete;
		MediaControlUnit& operator=(const MediaControlUnit&) = delete;
		MediaControlUnit& operator=(MediaControlUnit&&) = delete;

		~MediaControlUnit();
		int init();

		static MediaControlUnit* getInstance() {
			if (!mcu) mcu = new MediaControlUnit();
			refCount_.fetch_add(1);
			return mcu;
		}

		static void giveInstance(MediaControlUnit*& m) {
			if (!m) return;
			m = nullptr;
			refCount_.fetch_sub(1);
		}
		static uint16_t refCount() { return refCount_.load(); }
		static bool own() { return refCount_ == 1; }
		uint64_t autoCloseCount() const { return autoCloseNum.load(); }

		HandleError createMpu(const CreateJobContext& context);
		HandleError endMpu(const std::string& id);
		HandleError addChnl(const AddChnlContext& context, ListenAddr& addr);
		HandleError removeChnl(const RemoveChnlContext& context);
		HandleError openMic(const MicCtrlContext& context);
		HandleError closeMic(const MicCtrlContext& context);

		HandleError getMpuIdList(mpuIdList& list);
	};

}
// @brief: 媒体能力控制单元
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.9.9]
// @version: V0.0.1
// @revision: [Ambert@2024.9.9]

#pragma once

#include "Config.h"
#include "MediaProcessUnit.h"

#include "utils/InvokeTimer.hpp"

namespace aom {

	struct MCUStatus {
		std::atomic<uint64_t> runningJob{ 0 };
		std::atomic<uint64_t> runningChnl{ 0 };
		std::atomic<uint64_t> createErrNum{ 0 };
		std::atomic<uint64_t> createTotalNum{ 0 };
		std::atomic<uint64_t> joinErrNum{ 0 };
		std::atomic<uint64_t> joinTotalNum{ 0 };
		std::atomic<uint64_t> leaveErrNum{ 0 };
		std::atomic<uint64_t> leaveTotalNum{ 0 };
		std::atomic<uint64_t> destoryErrNum{ 0 };
		std::atomic<uint64_t> destoryTotalNum{ 0 };
	};

	class MemCheckTool{
	public:
		void start(int checkTime, int printTime) {
			peakMemKeepTimePoint = seeker::time::currentTime();
			std::time_t now = std::time(nullptr);
			std::tm* local_time = std::localtime(&now);
			currentSession = local_time->tm_hour;
			lastPeakTime.store(seeker::time::currentTime());
			memCheck = InvokeTimer::CreateTimer(std::chrono::seconds(checkTime), true, [&]() {
				// 统计当前内存占用
				currentMem.store(seeker::file::getVmRSS());

				// 判断并统计峰值内存占用
				if (currentMem.load() > peakMem.load()) {
					peakMemKeepTimePoint = seeker::time::currentTime();
					peakMem.store(currentMem.load());
					lastPeakTime.store(seeker::time::currentTime());
				}

				// 判断周期是否变化
				if (checkSession()) {
					// 统计上一周期峰值内存
					lastSessionPeakMem.store(sessionPeakMem.load());
					sessionPeakMem.store(0);
				}
				else {
					// 统计当前周期峰值内存
					if (currentMem.load() > sessionPeakMem.load()) {
						sessionPeakMem.store(currentMem.load());
					}
				}

				});
			memCheck->Start();
			double peakMemKeepTime = 0;
			memPrint = InvokeTimer::CreateTimer(std::chrono::seconds(printTime), true, [&]() {
				peakMemKeepTime = static_cast<double>(seeker::time::currentTime() - peakMemKeepTimePoint)
					/ (1000.0 * 60 * 60); // 峰值内存持续时长
				std::string lastPeakTimeStr = seeker::time::toString(lastPeakTime); // 最近一次峰值时间
				I_LOG("memcheck: session:{} | mem:{}KB | peak mem:{}KB | last peek time:{} | "
					"peakKeep:{:.3f}h | last session peak:{}KB | session peak:{}KB",
					currentSession.load(), currentMem.load(), peakMem.load(), lastPeakTimeStr, 
					peakMemKeepTime, lastSessionPeakMem.load(), sessionPeakMem.load());
				});
			memPrint->Start();
		}

		void stop() {
			if (memCheck) memCheck->Cancel();
			if (memPrint) memPrint->Cancel();
		}

	private:
		bool checkSession() {
			std::time_t now = std::time(nullptr);
			std::tm* local_time = std::localtime(&now);

			// 判断是否为整点（分钟为0）
			if (local_time->tm_min == 0) {
				// 如果小时数变化，且不是首次检查的整点
				if (currentSession.load() != local_time->tm_hour) {
					currentSession.store(local_time->tm_hour);
					return true;
				}
			}
			return false;
		}

		InvokeTimerPtr memCheck = nullptr;
		InvokeTimerPtr memPrint = nullptr;
		int64_t peakMemKeepTimePoint = 0;
		/* 内存监控相关字段 */
		std::atomic<size_t> currentMem{ 0 };           // 当前内存使用(KB)
		std::atomic<size_t> peakMem{ 0 };              // 历史峰值内存使用(KB)
		std::atomic<time_t> lastPeakTime{ 0 };         // 最近一次峰值时间
		std::atomic<size_t> lastSessionPeakMem{ 0 };   // 上一周期运行峰值(KB)
		std::atomic<size_t> sessionPeakMem{ 0 };       // 当前周期运行峰值(KB)
		std::atomic<int> currentSession{ 0 };          // 当前时间周期（每小时）
	};

	class EventManager {
	private:
		std::vector<EventInfo> eventList;
		size_t _capacity;
		mutable std::mutex eventLocker = {};
	public:
		EventManager(size_t cap) {
			_capacity = cap;
			eventList.reserve(_capacity);
		}

		void add(const EventInfo& event) {
			uniqueLock lck(eventLocker);
			if (eventList.size() >= _capacity) {
				size_t half = eventList.size() / 2;

				std::move(eventList.begin() + half, eventList.end(), eventList.begin());
				eventList.resize(eventList.size() - half);
			}

			eventList.push_back(event);
		}

		void get(std::vector<EventInfo>& list) {
			uniqueLock lck(eventLocker);
			list.swap(eventList);
		}

		size_t size() const { return eventList.size(); }
		size_t capacity() const { return eventList.capacity(); }
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
				if (portIndex.load() >= portRange) {
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
					return -1;
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

	using RemoveFunc = RemoveCallback;
	using FreePort = FreePortCallback;
	using JobId = std::string;
	using mpuCloseForm = std::unordered_set<UniqueMPU>;
	using mpuForm = std::unordered_map<JobId, UniqueMPU>;
	using mpuIdList = std::vector<std::string>;

	class MediaControlUnit {
		MediaControlUnit();
		void autoClose();
		void freePort(port_t);

		const int64_t mcucheckInterval = seeker::IniConfig::GetInteger("log", "mcu_check_interval", 1);
		const int64_t memcheckInterval = seeker::IniConfig::GetInteger("log", "mem_check_interval", 5);
		const int64_t memprintInterval = seeker::IniConfig::GetInteger("log", "mem_print_interval", 60);
		const int autocheckInterval = seeker::IniConfig::GetInteger("log", "auto_check_interval", 300);

		const port_t portPoint = seeker::IniConfig::GetInteger("main", "port_point", 62300);
		const int portRange = seeker::IniConfig::GetInteger("main", "port_range", 200);

		const std::string mediaIp = seeker::IniConfig::Get("media", "ip", "0.0.0.0");
		const int pt = seeker::IniConfig::GetInteger("media", "payload_type", 97);

		const bool isZimu = seeker::IniConfig::GetBoolean("test", "is_zimu", false);

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
		RemoveFunc endJobFunc = nullptr;
		FreePort freePortFunc = nullptr;
		EventManager eventMg;

		std::atomic<bool> keepWork{ true };
		std::atomic<uint64_t> autoCloseNum{ 0 };
		uint32_t createNum = 0;

		InvokeTimerPtr jobPoll = nullptr;
		MemCheckTool memCheck;
		MCUStatus status;
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

		bool checkJob(const std::string& id);
		bool createMpu(const CreateJobContext& context);
		bool endMpu(const std::string& id, const std::string& uid = "");
		bool addChnl(const AddChnlContext& context, ListenAddr& addr);
		bool removeChnl(const RemoveChnlContext& context);
		bool openMic(const MicCtrlContext& context);
		bool closeMic(const MicCtrlContext& context);
		bool updateDestition(const UpdateContext& context);

		bool getMpuIdList(mpuIdList& list);
		void getMpuBase(int& jobNum, int& chnlNum);
		void getMpuInfo(std::vector<MpuInfo>& info);
		void getEventList(std::vector<EventInfo>& infolist);

		void setCreateErr();
		void setJoinErr();
		void setLeaveErr();
		void setDestoryErr();
		void setEventInfo(const EventInfo& info);
	};

}
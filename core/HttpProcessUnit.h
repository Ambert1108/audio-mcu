// @brief: Http消息处理单元
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.9.9]
// @version: V0.0.1
// @revision: [Ambert@2024.9.9]

#pragma once

#include "Config.h"
#include "MediaControlUnit.h"
#include "utils/InvokeTimer.hpp"

#include "seeker/common.h"
#include "seeker/logger.h"
#include "seeker/loggerApi.h"

namespace aom {
	struct HPUStatus {
		/* 创建任务/创建成功 次数统计 */
		std::atomic<uint32_t> createCount = 0;
		std::atomic<uint32_t> createOk = 0;

		/* 添加通道/添加成功 次数统计 */
		std::atomic<uint32_t> addCount = 0;
		std::atomic<uint32_t> addOk = 0;

		/* 移除通道/移除成功 次数统计 */
		std::atomic<uint32_t> removeCount = 0;
		std::atomic<uint32_t> removeOk = 0;

		/* 结束任务/结束成功 次数统计 */
		std::atomic<uint32_t> endCount = 0;
		std::atomic<uint32_t> endOk = 0;

		/* 打开麦克风/打开成功 次数统计 */
		std::atomic<uint32_t> openCount = 0;
		std::atomic<uint32_t> openOk = 0;

		/* 关闭麦克风/关闭成功 次数统计 */
		std::atomic<uint32_t> closeCount = 0;
		std::atomic<uint32_t> closeOk = 0;

		/* 创建任务HPU处理耗时统计 */
		std::atomic<uint32_t> createConsumeSum = 0;
		std::atomic<uint32_t> createConsumeCount = 0;
		float createConsumeAvg = 0.0f;

		/* 添加通道HPU处理耗时统计 */
		std::atomic<uint32_t> addConsumeSum = 0;
		std::atomic<uint32_t> addConsumeCount = 0;
		float addConsumeAvg = 0.0f;

		/* 移除通道HPU处理耗时统计 */
		std::atomic<uint32_t> removeConsumeSum = 0;
		std::atomic<uint32_t> removeConsumeCount = 0;
		float removeConsumeAvg = 0.0f;

		/* 结束任务HPU处理耗时统计 */
		std::atomic<uint32_t> endConsumeSum = 0;
		std::atomic<uint32_t> endConsumeCount = 0;
		float endConsumeAvg = 0.0f;

		/* 打开麦克风HPU处理耗时统计 */
		std::atomic<uint32_t> openConsumeSum = 0;
		std::atomic<uint32_t> openConsumeCount = 0;
		float openConsumeAvg = 0.0f;

		/* 关闭麦克风HPU处理耗时统计 */
		std::atomic<uint32_t> closeConsumeSum = 0;
		std::atomic<uint32_t> closeConsumeCount = 0;
		float closeConsumeAvg = 0.0f;

		/* 保活/轮询次数统计 */
		std::atomic<uint64_t> pollCount = 0;
		std::atomic<uint64_t> pollSum = 0;
	};

	class CPUQuery {
	public:
		CPUQuery() {
			pid = getpid();
			clockTicks = sysconf(_SC_CLK_TCK);
			lastTotalTime = 0;
			lastSampleTime = 0;

			getCurrentCPUUsage();
		}

		double getCurrentCPUUsage() {
			static unsigned long long lastCPUTime = 0;
			static unsigned long long lastSysTime = 0;

			// 获取当前进程CPU时间
			unsigned long long currentCPUTime = getProcessCPUTime();
			unsigned long long currentSysTime = getCurrentTimeMS();

			if (lastSysTime == 0) {
				lastCPUTime = currentCPUTime;
				lastSysTime = currentSysTime;
				return 0.0;
			}

			// 计算增量
			unsigned long long cpuDiff = currentCPUTime - lastCPUTime;  // 时钟滴答
			unsigned long long timeDiff = currentSysTime - lastSysTime; // 毫秒

			// 更新
			lastCPUTime = currentCPUTime;
			lastSysTime = currentSysTime;

			if (timeDiff == 0) return 0.0;

			// 转换为百分比（与top一致，可超过100%）
			long clockTicks = sysconf(_SC_CLK_TCK);
			double cpuUsage = (static_cast<double>(cpuDiff) / clockTicks) / (timeDiff / 1000.0) * 100.0;

			return cpuUsage;
		}

	private:
		unsigned long long getCurrentTimeMS() {
			return std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		unsigned long long getProcessCPUTime() {
			std::string statPath = "/proc/" + std::to_string(pid) + "/stat";
			std::ifstream statFile(statPath);

			if (!statFile.is_open()) {
				return 0;
			}

			std::string line;
			std::getline(statFile, line);
			statFile.close();

			std::istringstream iss(line);
			std::vector<std::string> tokens;
			std::string token;

			while (std::getline(iss, token, ' ')) {
				tokens.push_back(token);
			}

			if (tokens.size() >= 15) {
				unsigned long long utime = std::stoull(tokens[13]);
				unsigned long long stime = std::stoull(tokens[14]);
				return utime + stime;
			}

			return 0;
		}

		int getCPUCount() {
			return sysconf(_SC_NPROCESSORS_ONLN);
		}

		pid_t pid;
		long clockTicks;
		unsigned long long lastTotalTime;
		unsigned long long lastSampleTime;
	};

	class HttpProcessUnit {
	public:
		HttpProcessUnit(const std::string& httpIp, const port_t& httpPort);
		~HttpProcessUnit();

		void open();

	private:
		void close();

		void createRequest(const Request& req, Response& rsp, const std::string& name);

		void addRequest(const Request& req, Response& rsp, const std::string& name);

		void removeRequest(const Request& req, Response& rsp, const std::string& name);

		void openRequest(const Request& req, Response& rsp, const std::string& name);

		void closeRequest(const Request& req, Response& rsp, const std::string& name);

		void endRequest(const Request& req, Response& rsp, const std::string& name);

		void pollRequest(const Request& req, Response& rsp, const std::string& name);

		void queryBaseRequest(const Request& req, Response& rsp, const std::string& name);

		void queryListRequest(const Request& req, Response& rsp, const std::string& name);

		HttpTask setWork(const std::string& actionName, Handle func);

		HttpTask setOption(UndefineHandle func);

		void workingLoop();

		Server svr;
		MediaControlUnit* mcu = nullptr;
		InvokeTimerPtr hpuCheck = nullptr;
		HPUStatus status;
		CPUQuery  cpuQuery;
		int64_t serverStartTime = 0;
		std::string startTimePoint{};
		const std::string _httpIp = {};
		const port_t _httpPort = 0;
		std::string controlUrl;
		bool isOpen = false;
		const int64_t hpucheckInterval = seeker::IniConfig::GetInteger("log", "hpu_check_interval", 1);
	};
}
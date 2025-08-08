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

		HttpTask setWork(const std::string& actionName, Handle func);

		HttpTask setOption(UndefineHandle func);

		void workingLoop();

		Server svr;
		MediaControlUnit* mcu = nullptr;
		InvokeTimerPtr hpuCheck = nullptr;
		HPUStatus status;

		int64_t serverStartTime = 0;
		const std::string _httpIp = {};
		const port_t _httpPort = 0;
		std::string controlUrl;
		bool isOpen = false;
		const int64_t hpucheckInterval = seeker::IniConfig::GetInteger("log", "hpu_check_interval", 1);
	};
}
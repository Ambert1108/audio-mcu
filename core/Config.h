// @brief: 程序配置项
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.4.29]
// @version: V0.0.1
// @revision: [Ambert@2024.9.9]

#pragma once

namespace aom {
	static void define_spdlog_level(const int& level) {
#define SPDLOG_ACTIVE_LEVEL level
	}

#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif  // !SPDLOG_ACTIVE_LEVEL
}

#include "seeker/common.h"
#include "seeker/logger.h"
#include "seeker/loggerApi.h"
#include "seeker/iniConfig.hpp"

#include <mutex>
#include <shared_mutex>
#include <tuple>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace aom {

	static const int httpThreadPoolCount = 16;
	static const std::string version = "0.1.0";

	using uniqueLock = std::unique_lock<std::mutex>;
	using lockGuard = std::lock_guard<std::mutex>;
	using readLock = std::shared_lock<std::shared_mutex>;
	using writeLock = std::unique_lock<std::shared_mutex>;
	using port_t = uint32_t;
}
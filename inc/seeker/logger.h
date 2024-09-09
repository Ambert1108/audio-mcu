/**
@project seeker
@author Tao Zhang
@since 2020/3/1
@version 0.1.4 2022/11/19
*/
#pragma once
#pragma warning(push, 0)
#include "../spdlog/spdlog.h"
#include "../spdlog/sinks/stdout_color_sinks.h"
#include "../spdlog/sinks/daily_file_sink.h"
#include "../spdlog/async.h"
#pragma warning(pop)

#include <iostream>
#include <memory>
#include <string>

#ifndef LOG_FILE_NAME
#define LOG_FILE_NAME "application.log"
#endif  // !LOG_FILE_NAME

#ifdef LOG_USE_ASYN
#define DEFAULT_USE_AYSN true
#else
#define DEFAULT_USE_AYSN false
#endif  // LOG_USE_ASYN


#ifndef LOG_THREAD_COUNT
#define LOG_THREAD_COUNT 1
#endif



namespace seeker {


// TODO test logger.
class Logger {
 private:
  std::string logFileName;
  std::string usePattern;

  inline static bool inited = false;
  inline static Logger* instence = nullptr;

  Logger(const std::string& logFile, bool stdOutOn, bool fileOutOn, const std::string& pattern,
         bool useAsyn, int level) {
    const std::string defaultLogFile = LOG_FILE_NAME;
    logFileName = logFile.length() > 0 ? logFile : defaultLogFile;

    const std::string defaultPattern = "[%Y%m%d %H:%M:%S.%e %s:%#] %^[%L]%$: %v";
    usePattern = pattern.length() > 0 ? pattern : defaultPattern;
    spdlog::level::level_enum logLevel;
    if (level < 0 || level > 6) {
      logLevel = spdlog::level::trace;
    } else {
      logLevel = (spdlog::level::level_enum)level;
    }

    std::cout << "Logger pattern     = [" << usePattern << "]" << std::endl;
    std::cout << "Logger stdOutOn    = [" << stdOutOn << "]" << std::endl;
    std::cout << "Logger fileOutOn   = [" << fileOutOn << "]" << std::endl;
    std::cout << "Logger logFileName = [" << logFileName << "]" << std::endl;
    std::cout << "Logger level       = [" << (int)logLevel << "]" << std::endl;
    std::cout << "Logger useAsyn     = [" << useAsyn << "]" << std::endl;

    try {
      std::vector<spdlog::sink_ptr> sinks;

      if (stdOutOn) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(logLevel);
        console_sink->set_pattern(usePattern);
        sinks.push_back(console_sink);
      }

      if (fileOutOn) {
        auto file_sink =
            std::make_shared<spdlog::sinks::daily_file_sink_mt>(logFileName, 2, 0);
        file_sink->set_level(logLevel);
        file_sink->set_pattern(usePattern);
        sinks.push_back(file_sink);
      }

      std::shared_ptr<spdlog::logger> logger;
      if (useAsyn) {
        logger = getAsyncLogger(sinks);
      } else {
        logger = getLogger(sinks);
      }

      spdlog::set_default_logger(logger);

      spdlog::set_level(logLevel);
      spdlog::flush_on(spdlog::level::warn);
      spdlog::flush_every(std::chrono::seconds(3));

    } catch (const spdlog::spdlog_ex& ex) {
      std::cout << "Log initialization failed: " << ex.what() << std::endl;
    }
  };

  std::shared_ptr<spdlog::async_logger> getAsyncLogger(std::vector<spdlog::sink_ptr> sinks) {
    spdlog::init_thread_pool(1024, LOG_THREAD_COUNT);
    auto combined_logger = std::make_shared<spdlog::async_logger>(
        "asy_multi_sink", begin(sinks), end(sinks), spdlog::thread_pool());
    std::cout << "Logger LOG_THREAD_COUNT=[" << LOG_THREAD_COUNT << "]" << std::endl;
    return combined_logger;
  };

  std::shared_ptr<spdlog::logger> getLogger(std::vector<spdlog::sink_ptr> sinks) {
    auto combined_logger =
        std::make_shared<spdlog::logger>("multi_sink", begin(sinks), end(sinks));
    return combined_logger;
  };

 public:
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  ~Logger() {
    SPDLOG_INFO("Logger is shutting down.");
    spdlog::drop_all();
    spdlog::shutdown();
    std::cout << "Logger shutdown." << std::endl;
  };

  static void shutdown() {
    SPDLOG_INFO("Logger will be shutting down...");
    if (inited && instence != nullptr) {
      delete instence;
      instence = nullptr;
      inited = false;
    }
  }

  static void reset(const std::string& logFile = "", bool useAsyn = DEFAULT_USE_AYSN,
                    bool stdOutOn = true, bool fileOutOn = true,
                    const std::string& pattern = "") {
    SPDLOG_INFO("Logger is resetting.");
    shutdown();
    init(logFile, useAsyn, stdOutOn, fileOutOn, pattern);
  }

  static void init(const std::string& logFile = "", bool useAsyn = DEFAULT_USE_AYSN,
                   bool stdOutOn = true, bool fileOutOn = true,
                   const std::string& pattern = "", int level = SPDLOG_LEVEL_TRACE) {
    // static bool inited = false;
    if (!inited) {
      inited = true;
      // static Logger instence{logFile, stdOutOn, fileOutOn, pattern, useAsyn};
      instence = new Logger(logFile, stdOutOn, fileOutOn, pattern, useAsyn, level);
      SPDLOG_INFO("Logger inited success: logFile [{}]", instence->logFileName);
      SPDLOG_INFO("Logger setting: stdOutOn[{}] fileOutOn[{}] useAsyn[{}]",
                  stdOutOn,
                  fileOutOn,
                  useAsyn);
      SPDLOG_INFO("Logger pattern: {}", instence->usePattern);
    } else {
      SPDLOG_WARN("Logger has been inited before, do nothing.");
    }
  };
};

}  // namespace seeker

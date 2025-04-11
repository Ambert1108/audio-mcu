// @brief: Sip消息封装体
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2025.4.8]
// @version: V0.0.1
// @revision: [Ambert@2025.4.8]

#pragma once

#include "Config.h"
#include "seeker/json.hpp"

#include "nlohmann/json.hpp"
#include "nlohmann/fifomap.hpp"

#include <string>
#include <vector>

namespace aom {
  struct CreateJobContext {
    std::string jobId = "unknown";
    std::string url = "";
  };

  struct AddChnlContext {
    std::string jobId = "unknown";
    std::string chnlId = "unknown";
    int payloadType;
    int codecType = -1;
    int inSampleRate = -1;
    int outSampleRate = -1;
    std::string dstIp;
    port_t dstPort;
    mutable std::string listenIp{};
    mutable port_t listenPort = 0;
  };

  struct AddChnlRespInfo {
    int errCode = 0;
    std::string msg = "ok";
    std::string listenIp;
    int listenPort;
  };

  struct RemoveChnlContext {
    std::string jobId = "unknown";
    std::string chnlId = "unknown";
    RemoveChnlContext() = default;
    RemoveChnlContext(std::string jId, std::string cId) : jobId(jId), chnlId(cId) {}
  };

  struct MicCtrlContext {
    std::string jobId = "unknown";
    std::string channelId = "unknown";
  };

  struct CallbackRequest {
    std::string channelId;
  };
}
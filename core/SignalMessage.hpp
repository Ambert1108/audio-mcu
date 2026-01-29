// @brief: 信令消息封装体
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.4.29]
// @version: V0.0.1
// @revision: [Ambert@2024.5.8]

#pragma once

#include "Config.h"
#include "HttpDefinition.h"

#include "seeker/http.hpp"
#include "seeker/json.hpp"

#include "utils/httplib.h"
#include "nlohmann/json.hpp"
#include "nlohmann/fifomap.hpp"

#include <string>
#include <vector>

namespace aom {
  class HttpProcessUnit;
  typedef HttpProcessUnit HPU;

  using Server = httplib::Server;
  using Request = httplib::Request;
  using Response = httplib::Response;
  using ContentReader = httplib::ContentReader;
  using ContentType = seeker::http::contentType;
  using Handle = void (HPU::*)(const Request& req, Response& res, const std::string& taskName);
  using UndefineHandle = void(const Request& req, Response& res);
  using HttpTask = std::function<void(const Request&, Response&)>;
  using UndefineHttpTask = std::function<void(const Request&, Response&, const std::string& taskName)>;
  template<class K, class V, class dummy_compare, class A>
  using json_fifo_map = nlohmann::fifo_map<K, V, nlohmann::fifo_map_compare<K>, A>;
  using json = nlohmann::basic_json<json_fifo_map>;

  struct NullData {};
  inline void to_json(nlohmann::json&, const NullData&) { /* do nothing*/ };
  inline void from_json(const nlohmann::json&, NullData&) { /* do nothing*/ };

  template <typename T>
  struct HttpResponse {
    int errCode = 0;
    std::string msg = {};
    T obj;

    HttpResponse(int err = 0, const std::string& message = "ok") : HttpResponse(T{}, err, message) {};
    HttpResponse(T d, int err = 0, const std::string& message = "ok")
      : errCode(err), msg(message), obj(d) {};
  };

  template <typename T>
  void to_json(nlohmann::json& j, const HttpResponse<T>& resp) {
    if constexpr (std::is_same<T, NullData>::value) {
      j = nlohmann::json{ {"errCode", resp.errCode}, {"msg", resp.msg} };
    }
    else {
      j = nlohmann::json{ {"errCode", resp.errCode}, {"msg", resp.msg}, {"rspInfo", resp.obj} };
    };
  }

  template <typename T>
  void from_json(const nlohmann::json& j, HttpResponse<T>& resp) {
    j.at("errCode").get_to(resp.errCode);
    j.at("msg").get_to(resp.msg);

    if constexpr (std::is_same<T, NullData>::value) {
      j.at("rspInfo").get_to<T>(resp.obj);
    }
  }
  typedef HttpResponse<NullData> DefaultResponse;

  struct PollRespInfo {
    int jobNumber;
    std::vector<std::string> jobList;
  };
  NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PollRespInfo, jobNumber, jobList);
  typedef PollRespInfo PollResponse;

  struct QueryBaseResponse {
    double cpu;
    int64_t mem;
    int jobNum;
    int chnlNum;
    std::string startTime;
    int64_t workTime;
  };
  NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(QueryBaseResponse, cpu, mem, jobNum, chnlNum, startTime, workTime);

  struct MpuInfo {
    std::string meetingId{};
    std::string createTime{};
    int64_t meetingDuration = 0;
    int32_t userNum = 0;
    int mediaBitrate = 0;
    std::vector<std::string> userIdList{};

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(
      MpuInfo,
      meetingId,
      createTime,
      meetingDuration,
      userNum,
      mediaBitrate,
      userIdList
    )
  };

  struct QueryInfoResponse {
    std::vector<MpuInfo> list;
  };
  NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(QueryInfoResponse, list);

  struct CreateJobContext {
    std::string jobId = "unknown";
    std::string url = "";
    friend void from_json(const nlohmann::json& j, CreateJobContext& context) {
      if(j.contains("url")) j.at("url").get_to(context.url);
    }
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

    friend void from_json(const nlohmann::json& j, AddChnlContext& context) {
      j.at("jobId").get_to(context.jobId);
      j.at("payloadType").get_to(context.payloadType);
      j.at("codecType").get_to(context.codecType);
      j.at("inSampleRate").get_to(context.inSampleRate);
      j.at("outSampleRate").get_to(context.outSampleRate);
      j.at("dstIp").get_to(context.dstIp);
      j.at("dstPort").get_to(context.dstPort);
    }
  };

  struct AddChnlRespInfo {
    int errCode = 0;
    std::string msg = "ok";
    std::string listenIp;
    int listenPort;
  };
  NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AddChnlRespInfo, errCode, msg, listenIp, listenPort);

  struct RemoveChnlContext {
    std::string jobId = "unknown";
    std::string chnlId = "unknown";
    RemoveChnlContext() = default;
    RemoveChnlContext(std::string jId, std::string cId) : jobId(jId), chnlId(cId) {}
  };
  NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RemoveChnlContext, jobId);

  struct MicCtrlContext {
    std::string jobId = "unknown";
    std::string channelId = "unknown";
  };
  NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MicCtrlContext, jobId, channelId);

  struct UpdateContext {
    std::string jobId = "unknown";
    std::string channelId = "unknown";
    std::string dstIp;
    port_t dstPort;
    int codecType = -1;
    
    UpdateContext() = default;
    UpdateContext(std::string jId, std::string cId, std::string ip, port_t port)
      : jobId(jId), channelId(cId), dstIp(ip), dstPort(port) {
    }
  };

  //struct 

  struct CallbackRequest {
    std::string channelId;
  };
  NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CallbackRequest, channelId);

  struct CaptureRequest {
    std::string channelId = "all";
    std::string text;
  };
  NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CaptureRequest, channelId, text);
}
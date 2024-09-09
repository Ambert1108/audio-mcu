// @brief: Http消息封装体
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

  struct Element {
    /* 图片特效共有 */

    std::string mediaId;

    /* 动态图片特有 */

    int framerate = 30;

    /* 提示字幕特有 */

    std::string text;
    std::string fontSize;
    std::string fontColor = "#000000";
    std::string fontFamily{};

    /* 所有特效共有 */

    std::string type;
    int marginTop = 0;
    int marginLeft = 0;
    int width = 0;
    int height = 0;

    friend void from_json(const nlohmann::json& j, Element& e) {
      j.at("type").get_to(e.type);
      if (e.type == "image") {
        j.at("mediaId").get_to(e.mediaId);
      }
      else if (e.type == "animation") {
        j.at("mediaId").get_to(e.mediaId);
        if (j.contains("framerate")) j.at("framerate").get_to(e.framerate);
      }
      else if (e.type == "text") {
        j.at("text").get_to(e.text);
        j.at("fontSize").get_to(e.fontSize);
        if (j.contains("fontColor")) j.at("fontColor").get_to(e.fontColor);
      }
      else throw std::logic_error("receive type " + e.type + " is error");
      if (j.contains("marginTop")) j.at("marginTop").get_to(e.marginTop);
      if (j.contains("marginLeft")) j.at("marginLeft").get_to(e.marginLeft);
      if (j.contains("width")) j.at("width").get_to(e.width);
      if (j.contains("height")) j.at("height").get_to(e.height);
    }
  };

  struct Canvas {
    std::string backgroundColor = "#FFFFFF";
    int marginTop = 0;
    int marginLeft = 0;
    int width = 0;
    int height = 0;
    int opacity = 0;
    std::vector<Element> elements;

    friend void from_json(const nlohmann::json& j, Canvas& c) {
      if (j.contains("backgroundColor")) j.at("backgroundColor").get_to(c.backgroundColor);
      if (j.contains("marginTop")) j.at("marginTop").get_to(c.marginTop);
      if (j.contains("marginLeft")) j.at("marginLeft").get_to(c.marginLeft);
      if (j.contains("width")) j.at("width").get_to(c.width);
      if (j.contains("height")) j.at("height").get_to(c.height);
      if (j.contains("opacity")) j.at("opacity").get_to(c.opacity);
      const auto& arr = j.at("elements");
      c.elements.reserve(10);
      for (const auto& e : arr) {
        c.elements.push_back(e);
      }
    }
  };

  struct Module {
    std::vector<Canvas> canvas;
    friend void from_json(const nlohmann::json& j, Module& m) {
      const auto& arr = j.at("canvas");
      m.canvas.reserve(5);
      for (const auto& c : arr) {
        m.canvas.push_back(c);
      }
    }
  };


  struct EffectInfo {
    int duration = 0;
    Module landscape;
    Module portrait;
    friend void from_json(const nlohmann::json& j, EffectInfo& ei) {
      if (j.contains("duration")) j.at("duration").get_to(ei.duration);
      j.at("landscape").get_to(ei.landscape);
      j.at("portrait").get_to(ei.portrait);
    }
  };

  struct CreateJobContext {
    std::string jobId = "unknown";
    std::string targetIp;
    port_t targetPort;
    std::string mediaFilePath;
    EffectInfo effectInfo;

    friend void from_json(const nlohmann::json& j, CreateJobContext& context) {
      j.at("targetIp").get_to(context.targetIp);
      j.at("targetPort").get_to(context.targetPort);
      j.at("mediaFilePath").get_to(context.mediaFilePath);
      j.at("effectInfo").get_to(context.effectInfo);
    }
  };

  struct UpdateJobContext {
    std::string jobId = "unknown";
    std::string mediaFilePath{};
    int updateMode = 0;
    EffectInfo effectInfo;
    bool noTemplate = false;
    friend void from_json(const nlohmann::json& j, UpdateJobContext& context) {
      if (j.contains("mediaFilePath")) j.at("mediaFilePath").get_to(context.mediaFilePath);
      if (j.contains("updateMode")) j.at("updateMode").get_to(context.updateMode);
      if (j.contains("effectInfo")) j.at("effectInfo").get_to(context.effectInfo);
      else context.noTemplate = true;
    }
  };
}
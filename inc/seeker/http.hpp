/**
@project seeker
@author Tao Zhang
@since 2022/1/1
@version 0.1.2-SNAPSHOT 2022/2/16
*/
#pragma once

#include <iostream>
#include <vector>
#include <map>
#include "seeker/common.h"
#include "spdlog/fmt/fmt.h"


namespace seeker {


// 2022年6月19日
namespace http {


struct contentType {
  inline static const std::string json = "application/json; charset=utf-8";
  inline static const std::string html = "text/html; charset=utf-8";
  inline static const std::string plain = "text/plain; charset=utf-8";
  inline static const std::string javascript = "application/javascript; charset=utf-8";
  inline static const std::string css = "text/css; charset=utf-8";
  inline static const std::string xml = "text/xml; charset=utf-8";
  inline static const std::string xmlApplication = "application/xml; charset=utf-8";
  inline static const std::string gif = "image/gif";
  inline static const std::string png = "image/png";
  inline static const std::string jpg = "image/jpeg";
  inline static const std::string octet_stream = "application/octet-stream";
};

typedef contentType ContentType;

// struct ContentType {
//  inline static std::string json = "application/json; charset=utf-8";
//  inline static std::string html = "text/html; charset=utf-8";
//  inline static std::string plain = "text/plain; charset=utf-8";
//  inline static std::string javascript = "application/javascript; charset=utf-8";
//  inline static std::string css = "text/css; charset=utf-8";
//};


/*
* reference:
* https://developer.mozilla.org/zh-CN/docs/Web/HTTP/Headers/Set-Cookie
* https://developer.mozilla.org/zh-CN/docs/Web/HTTP/Headers/Set-Cookie/SameSite
*/

inline std::string
cleanCookie(const std::string& name,
            const std::string& Path = "/",
            const std::string& Domain = "",
            bool Secure = false,
            bool HttpOnly = false,
            const std::string& SameSite = "Strict"
) {
  std::string cookieContent;
  cookieContent += fmt::format("{}=; ", name);
  cookieContent += Domain.size() > 0 ? fmt::format("Domain={}; ", Domain) : "";
  cookieContent += Path.size() > 0 ? fmt::format("Path={}; ", Path) : "";
  cookieContent += SameSite.size() > 0 ? fmt::format("SameSite={}; ", SameSite) : "";
  cookieContent += Secure ? "Secure; " : "";
  cookieContent += HttpOnly ? "HttpOnly; " : "";
  cookieContent += "Expires=Wed, 30 Aug 1970 00:00:00 GMT; ";
  return cookieContent;
}

inline std::string createCookie(const std::string& name,
                                const std::string& value,
                                const std::string& Path = "/",
                                const std::string& Domain = "",
                                bool Secure = false,
                                bool HttpOnly = false,
                                uint32_t maxAge = 0,
                                const std::string& SameSite = "Strict") {
  std::string cookieContent;
  cookieContent += fmt::format("{}={}; ", name, value);
  cookieContent += Domain.size() > 0 ? fmt::format("Domain={}; ", Domain) : "";
  cookieContent += Path.size() > 0 ? fmt::format("Path={}; ", Path) : "";
  cookieContent += SameSite.size() > 0 ? fmt::format("SameSite={}; ", SameSite) : "";
  cookieContent += Secure ? "Secure; " : "";
  cookieContent += HttpOnly ? "HttpOnly; " : "";
  cookieContent += maxAge > 0 ? fmt::format("Max-Age={}; ", maxAge) : "";
  return cookieContent;
}


inline std::string getCookieValue(const std::string& cookieContent,
                                  const std::string& cookieName) {
  //D_LOG("getCookieValue 0: cookieContent=[{}], cookieName=[{}],", cookieContent, cookieName);

  if (cookieName.size() > 0 && cookieContent.size() > cookieName.size()) {
    std::vector<std::string> cookieVector = seeker::string::split(cookieContent, ";");
    for (const auto& cookieStr : cookieVector) {
      //D_LOG("getCookieValue 1: cookieStr=[{}]", cookieStr);
      std::string cookieString = seeker::string::trim(cookieStr);
      //D_LOG("getCookieValue 1: cookieString=[{}]", cookieString);
      if (seeker::string::startWith(cookieString, cookieName)) {
        //D_LOG("getCookieValue 2: cookieString=[{}] cookieName=[{}]", cookieString, cookieName);
        return cookieString.substr(cookieName.size() + 1);
      }
    }
  }
  return "";
}

inline std::map<std::string, std::string> parseUrlQuery(const std::string& urlQuery) {
  auto kvStrList = seeker::string::split(urlQuery, "&");
  std::map<std::string, std::string> queryMap;
  for (const auto& kvStr : kvStrList) {
    if (kvStr.size() > 0) {
      auto kv = seeker::string::split(kvStr, "=", 2);
      if (kv.size() > 1) {
        queryMap.emplace(kv.at(0), kv.at(1));
      } else {
        queryMap.emplace(kv.at(0), "");
      }
    }
  }
  return queryMap;
}



typedef std::map<std::string, std::string> SessionMap;

inline SessionMap parseSessioString(const std::string& sessionString) {
  // TODO check signature in sessionString
  SessionMap sMap;
  std::vector<std::string> kvVector = seeker::string::split(sessionString, "&");
  for (const auto& kvStr : kvVector) {
    auto kv = seeker::string::split(kvStr, "=", 2);
    if (kv.size() == 2) {
      sMap[kv[0]] = kv[1];
    }
  }
  return sMap;
}

inline std::string createSessionString(const SessionMap& sessionMap) {
  // TODO add signature sessionString
  std::stringstream ss;
  ss << "signatureHere_TODO";
  for (auto& kv : sessionMap) {
    ss << "&" << kv.first << "=" << kv.second;
  }
  //T_LOG("createSessionString: [{}]", ss.str());
  return ss.str();
}


}  // namespace http



}  // namespace seeker

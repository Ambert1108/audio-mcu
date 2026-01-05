// @brief: Sip消息处理单元
// @copyright: Copyright seekloud 2025
// @birth: [Ambert@2025.4.8]
// @version: V0.0.1
// @revision: [Ambert@2025.4.8]

#pragma once

#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <regex>

#include "Config.h"
#include "MediaControlUnit.h"
#include "Message.h"
#include "utils/InvokeTimer.hpp"

#include "seeker/common.h"
#include "seeker/logger.h"
#include "seeker/loggerApi.h"

namespace aom {
  class SipProcessUnit {

  public:
    SipProcessUnit(const std::string& user, const std::string& pass,
      const std::string& dom, const std::string& server,
      int sPort, const std::string& local, int lPort);

    ~SipProcessUnit();

    // 启动Account
    bool start();

    // 停止Account
    void stop();

    // 获取Account信息
    std::string getInfo() const;

  private:
    std::string username;
    std::string password;
    std::string domain;
    std::string serverIp;
    int serverPort;
    std::string localIp;
    int localPort;

    // 状态变量
    std::atomic<bool> registered;
    std::atomic<bool> running;
    std::string callId;
    std::string branch;
    int cseq;
    std::string tag;

    // Socket相关
    int sockfd;
    std::thread registerThread;
    std::thread receiveThread;

    // 锁
    std::mutex socketMutex;
    std::mutex callMutex;

    struct SDPInfo {
      std::string ip;
      int port = 0;
      int payloadType = -1;
      int sampleRate = 0;
      bool isOpus = false;
    };

    MediaControlUnit* mcu = nullptr;

    // 生成唯一的Call-ID
    void generateCallId();

    // 生成Branch参数
    std::string generateBranch();

    // 生成Tag参数
    std::string generateTag();

    // 从消息中提取头部值
    std::string extractHeader(const std::string& message, const std::string& headerName);

    // 获取MESSAGE请求体内容
    std::string extractMessageBody(const std::string& message);

    // 提取From的tag
    std::string extractFromTag(const std::string& fromHeader);

    std::string extractChnlId(const std::string& wholeMsg);

    std::string extractJobId(const std::string& wholeMsg);

    // 解析SDP
    void parseSDP(const std::string& wholeMsg, SDPInfo& info);

    // 初始化Socket
    bool initializeSocket();

    // 构造REGISTER请求
    std::string constructRegisterRequest();

    // 构造SIP响应
    std::string constructSIPResponse(const std::string& statusLine,
      const std::string& via,
      const std::string& from,
      const std::string& to,
      const std::string& callId,
      int cseq,
      const std::string& method,
      const std::string& toTag = "");

    // 构造带SDP的SIP响应
    std::string constructSIPResponseWithSDP(const std::string& statusLine,
      const std::string& via,
      const std::string& from,
      const std::string& to,
      const std::string& callId,
      int cseq,
      const std::string& toTag,
      const std::string& sdp);

    // 发送消息
    bool sendMessage(const std::string& message, const struct sockaddr_in& destAddr);

    // 发送SIP响应
    bool sendSIPResponse(const std::string& statusLine,
      const std::string& via,
      const std::string& from,
      const std::string& to,
      const std::string& callId,
      int cseq,
      const std::string& method,
      const struct sockaddr_in& destAddr,
      const std::string& toTag = "");

    // 发送带SDP的SIP响应
    bool sendSIPResponseWithSDP(const std::string& statusLine,
      const std::string& via,
      const std::string& from,
      const std::string& to,
      const std::string& callId,
      int cseq,
      const struct sockaddr_in& destAddr,
      const std::string& toTag,
      const std::string& sdp);

    // 发送REGISTER请求
    bool sendRegisterRequest();

    // 处理MESSAGE请求
    void handleMESSAGE(const std::string& request, struct sockaddr_in& fromAddr);

    // 处理INVITE请求
    void handleINVITE(const std::string& request, struct sockaddr_in& fromAddr);

    // 处理ACK请求
    void handleACK(const std::string& request, struct sockaddr_in& fromAddr);

    // 处理BYE请求
    void handleBYE(const std::string& request, struct sockaddr_in& fromAddr);

    // 处理接收到的SIP响应
    void handleSIPResponse(const std::string& response, struct sockaddr_in& fromAddr);

    // 处理接收到的SIP请求
    void handleSIPRequest(const std::string& request, struct sockaddr_in& fromAddr);

    // 接收线程函数
    void receiveLoop();

    // 注册保活线程函数
    void registerLoop();
  };
}

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

// SIP Account 类
class SIPAccount {
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

  // 呼叫相关
  std::atomic<bool> inCall;
  std::string currentCallId;
  std::string currentFromTag;
  std::string currentToTag;
  std::string currentVia;
  int currentCSeq;
  struct sockaddr_in currentFromAddr;

  // Socket相关
  int sockfd;
  std::thread registerThread;
  std::thread receiveThread;

  // 锁
  std::mutex socketMutex;
  std::mutex callMutex;

public:
  SIPAccount(const std::string& user, const std::string& pass,
    const std::string& dom, const std::string& server,
    int sPort, const std::string& local, int lPort)
    : username(user), password(pass), domain(dom),
    serverIp(server), serverPort(sPort),
    localIp(local), localPort(lPort),
    registered(false), running(false), cseq(1),
    inCall(false), currentCSeq(0) {
    sockfd = -1;
    generateCallId();
    tag = generateTag();
  }

  ~SIPAccount() {
    stop();
    if (sockfd != -1) {
      close(sockfd);
    }
  }

  // 生成唯一的Call-ID
  void generateCallId() {
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(8)
      << (rand() & 0xFFFFFFFF) << "@" << domain;
    callId = ss.str();
  }

  // 生成Branch参数
  std::string generateBranch() {
    std::stringstream ss;
    ss << "z9hG4bK" << std::hex << std::setfill('0') << std::setw(8)
      << (rand() & 0xFFFFFFFF);
    branch = ss.str();
    return branch;
  }

  // 生成Tag参数
  std::string generateTag() {
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16)
      << (rand() & 0xFFFFFFFF) << (rand() & 0xFFFFFFFF);
    return ss.str();
  }

  // 从消息中提取头部值
  std::string extractHeader(const std::string& message, const std::string& headerName) {
    std::istringstream stream(message);
    std::string line;

    while (std::getline(stream, line)) {
      if (line.find(headerName) == 0) {
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
          std::string value = line.substr(colonPos + 1);
          size_t start = value.find_first_not_of(" \t\r");
          if (start != std::string::npos) {
            size_t end = value.find_last_not_of("\r");
            return value.substr(start, end - start + 1);
          }
        }
      }
    }
    return "";
  }

  // 提取From的tag
  std::string extractFromTag(const std::string& fromHeader) {
    size_t tagPos = fromHeader.find("tag=");
    if (tagPos != std::string::npos) {
      size_t start = tagPos + 4;
      size_t end = fromHeader.find(';', start);
      if (end == std::string::npos) end = fromHeader.length();
      return fromHeader.substr(start, end - start);
    }
    return "";
  }

  // 检查是否是给自己的INVITE
  bool isInviteToMe(const std::string& toHeader) {
    // 匹配格式: To: <sip:username@domain>
    std::regex pattern("sip:" + username + "@" + domain);
    std::smatch match;
    return std::regex_search(toHeader, match, pattern);
  }

  // 初始化Socket
  bool initializeSocket() {
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
      std::cerr << "[" << username << "] 创建socket失败" << std::endl;
      return false;
    }

    // 设置地址重用
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
      std::cerr << "[" << username << "] 设置socket选项失败" << std::endl;
      close(sockfd);
      return false;
    }

    // 绑定到本地IP和端口
    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;

    if (inet_pton(AF_INET, localIp.c_str(), &localAddr.sin_addr) <= 0) {
      std::cerr << "[" << username << "] 无效的本地IP地址: " << localIp << std::endl;
      close(sockfd);
      return false;
    }

    localAddr.sin_port = htons(localPort);

    if (bind(sockfd, (struct sockaddr*)&localAddr, sizeof(localAddr)) < 0) {
      std::cerr << "[" << username << "] 绑定 " << localIp << ":" << localPort << " 失败" << std::endl;
      close(sockfd);
      return false;
    }

    std::cout << "[" << username << "] 成功绑定到: " << localIp << ":" << localPort << std::endl;
    return true;
  }

  // 构造REGISTER请求
  std::string constructRegisterRequest() {
    std::stringstream request;

    generateBranch();

    // SIP REGISTER 请求行
    request << "REGISTER sip:" << domain << ":" << serverPort << " SIP/2.0\r\n";

    // Via头部
    request << "Via: SIP/2.0/UDP " << localIp << ":" << localPort
      << ";branch=" << branch << ";rport\r\n";

    // From头部
    request << "From: <sip:" << username << "@" << domain << ">;tag=" << generateTag() << "\r\n";

    // To头部
    request << "To: <sip:" << username << "@" << domain << ">\r\n";

    // Call-ID头部
    request << "Call-ID: " << callId << "\r\n";

    // CSeq头部
    request << "CSeq: " << cseq++ << " REGISTER\r\n";

    // Max-Forwards
    request << "Max-Forwards: 70\r\n";

    // User-Agent
    request << "Allow: PRACK, INVITE, ACK, BYE, CANCEL, UPDATE, INFO, SUBSCRIBE, NOTIFY, REFER, MESSAGE, OPTIONS\r\n";

    // Expires（注册有效期，单位秒）
    request << "Expires: 300\r\n";  // 5分钟

    // Contact
    request << "Contact: <sip:" << username << "@" << localIp << ":" << localPort << ">;ob\r\n";

    // Content-Length
    request << "Content-Length: 0\r\n";
    request << "\r\n";

    return request.str();
  }

  // 构造SIP响应
  std::string constructSIPResponse(const std::string& statusLine,
    const std::string& via,
    const std::string& from,
    const std::string& to,
    const std::string& callId,
    int cseq,
    const std::string& method,
    const std::string& toTag = "") {
    std::stringstream response;

    response << statusLine << "\r\n";
    response << "Via: " << via << "\r\n";
    response << "From: " << from << "\r\n";

    // To头，如果需要添加tag
    std::string toHeader = to;
    if (!toTag.empty() && toHeader.find("tag=") == std::string::npos) {
      toHeader += ";tag=" + toTag;
    }
    response << "To: " << toHeader << "\r\n";

    response << "Call-ID: " << callId << "\r\n";
    response << "CSeq: " << cseq << " " << method << "\r\n";
    response << "Content-Length: 0\r\n";
    response << "\r\n";

    return response.str();
  }

  // 构造带SDP的SIP响应
  std::string constructSIPResponseWithSDP(const std::string& statusLine,
    const std::string& via,
    const std::string& from,
    const std::string& to,
    const std::string& callId,
    int cseq,
    const std::string& toTag,
    const std::string& sdp) {
    std::stringstream response;

    response << statusLine << "\r\n";
    response << "Via: " << via << "\r\n";
    response << "From: " << from << "\r\n";

    // To头，如果需要添加tag
    std::string toHeader = to;
    if (!toTag.empty() && toHeader.find("tag=") == std::string::npos) {
      toHeader += ";tag=" + toTag;
    }
    response << "To: " << toHeader << "\r\n";

    response << "Call-ID: " << callId << "\r\n";
    response << "CSeq: " << cseq << " INVITE\r\n";

    // SDP相关头部
    response << "Content-Type: application/sdp\r\n";
    response << "Content-Length: " << sdp.length() << "\r\n";
    response << "\r\n";
    response << sdp;

    return response.str();
  }

  // 获取SDP内容
  std::string getSDPContent() {
    std::stringstream sdp;

    // 使用您提供的SDP内容
    sdp << "v=0\r\n";
    sdp << "o=- 3953192465 3953192467 IN IP4 10.1.69.7\r\n";
    sdp << "s=pjmedia\r\n";
    sdp << "c=IN IP4 10.1.69.7\r\n";
    sdp << "b=AS:84\r\n";
    sdp << "t=0 0\r\n";
    sdp << "a=X-nat:0\r\n";
    sdp << "m=audio 62600 RTP/AVP 96\r\n";
    sdp << "a=rtcp:4001 IN IP4 10.1.69.7\r\n";
    sdp << "a=ssrc:766044304 cname:7b8072591a0e9c5a\r\n";
    sdp << "a=rtpmap:96 opus/48000/2\r\n";
    sdp << "a=fmtp:96 0-16\r\n";
    sdp << "a=rtpmap:121 telephone-event/48000\r\n";
    sdp << "a=fmtp:121 0-16\r\n";
    sdp << "a=rtcp-fb:* ccm tmmbr\r\n";
    sdp << "m=video 0 RTP/AVP 96\r\n";
    sdp << "c=IN IP4 10.1.69.7\r\n";
    sdp << "a=rtpmap:96 H264/90000\r\n";
    sdp << "a=fmtp:96 profile-level-id=42801F\r\n";
    sdp << "a=rtcp-fb:96 nack pli\r\n";

    return sdp.str();
  }

  // 发送消息
  bool sendMessage(const std::string& message, const struct sockaddr_in& destAddr) {
    std::lock_guard<std::mutex> lock(socketMutex);

    ssize_t sent = sendto(sockfd, message.c_str(), message.length(), 0,
      (struct sockaddr*)&destAddr, sizeof(destAddr));

    if (sent < 0) {
      std::cerr << "[" << username << "] 发送消息失败" << std::endl;
      return false;
    }

    return true;
  }

  // 发送SIP响应
  bool sendSIPResponse(const std::string& statusLine,
    const std::string& via,
    const std::string& from,
    const std::string& to,
    const std::string& callId,
    int cseq,
    const std::string& method,
    const struct sockaddr_in& destAddr,
    const std::string& toTag = "") {

    std::string response = constructSIPResponse(statusLine, via, from, to,
      callId, cseq, method, toTag);

    std::cout << "[" << username << "] 发送响应: " << statusLine << std::endl;

    return sendMessage(response, destAddr);
  }

  // 发送带SDP的SIP响应
  bool sendSIPResponseWithSDP(const std::string& statusLine,
    const std::string& via,
    const std::string& from,
    const std::string& to,
    const std::string& callId,
    int cseq,
    const struct sockaddr_in& destAddr,
    const std::string& toTag) {

    std::string sdp = getSDPContent();
    std::string response = constructSIPResponseWithSDP(statusLine, via, from, to,
      callId, cseq, toTag, sdp);

    std::cout << "[" << username << "] 发送响应: " << statusLine << std::endl;
    std::cout << "[" << username << "] SDP内容:\n" << sdp << std::endl;

    return sendMessage(response, destAddr);
  }

  // 发送REGISTER请求
  bool sendRegisterRequest() {
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);

    if (inet_pton(AF_INET, serverIp.c_str(), &serverAddr.sin_addr) <= 0) {
      std::cerr << "[" << username << "] 无效的服务器IP地址: " << serverIp << std::endl;
      return false;
    }

    std::string request = constructRegisterRequest();

    std::cout << "[" << username << "] 发送REGISTER请求到 "
      << serverIp << ":" << serverPort << std::endl;

    return sendMessage(request, serverAddr);
  }

  // 处理MESSAGE请求
  void handleMESSAGE(const std::string& request, struct sockaddr_in& fromAddr) {
    std::cout << "[" << username << "] 收到MESSAGE请求" << std::endl;

    // 提取必要头部
    std::string callId = extractHeader(request, "Call-ID");
    std::string from = extractHeader(request, "From");
    std::string to = extractHeader(request, "To");
    std::string cseqStr = extractHeader(request, "CSeq");
    std::string via = extractHeader(request, "Via");

    int cseq = 1;
    if (!cseqStr.empty()) {
      cseq = std::stoi(cseqStr.substr(0, cseqStr.find(' ')));
    }

    // 构建200 OK响应
    std::cout << "[" << username << "] 回复200 OK" << std::endl;
    sendSIPResponse("SIP/2.0 200 OK", via, from, to, callId, cseq, "MESSAGE", fromAddr, tag);
  }

  // 处理INVITE请求
  void handleINVITE(const std::string& request, struct sockaddr_in& fromAddr) {
    std::lock_guard<std::mutex> lock(callMutex);

    if (inCall) {
      std::cout << "[" << username << "] 正在通话中，拒绝新的INVITE" << std::endl;
      // 发送486 Busy Here
      std::string via = extractHeader(request, "Via");
      std::string from = extractHeader(request, "From");
      std::string to = extractHeader(request, "To");
      std::string callId = extractHeader(request, "Call-ID");
      std::string cseqStr = extractHeader(request, "CSeq");
      int cseq = std::stoi(cseqStr.substr(0, cseqStr.find(' ')));

      sendSIPResponse("SIP/2.0 486 Busy Here", via, from, to, callId, cseq, "INVITE", fromAddr, tag);
      return;
    }

    // 提取头部信息
    std::string via = extractHeader(request, "Via");
    std::string from = extractHeader(request, "From");
    std::string to = extractHeader(request, "To");
    std::string callId = extractHeader(request, "Call-ID");
    std::string cseqStr = extractHeader(request, "CSeq");
    int cseq = std::stoi(cseqStr.substr(0, cseqStr.find(' ')));

    // 提取From的tag
    currentFromTag = extractFromTag(from);

    // 保存呼叫信息
    currentCallId = callId;
    currentVia = via;
    currentCSeq = cseq;
    currentFromAddr = fromAddr;
    inCall = true;

    std::cout << "[" << username << "] 收到INVITE请求，呼叫ID: " << callId << std::endl;
    std::cout << "[" << username << "] 来自: " << from << std::endl;
    std::cout << "[" << username << "] 发往: " << to << std::endl;

    // 发送100 Trying
    std::cout << "[" << username << "] 发送100 Trying" << std::endl;
    sendSIPResponse("SIP/2.0 100 Trying", via, from, to, callId, cseq, "INVITE", fromAddr);

    // 等待1秒后发送180 Ringing
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::cout << "[" << username << "] 发送180 Ringing" << std::endl;
    sendSIPResponse("SIP/2.0 180 Ringing", via, from, to, callId, cseq, "INVITE", fromAddr);

    // 等待2秒后发送200 OK
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    std::cout << "[" << username << "] 发送200 OK" << std::endl;
    sendSIPResponseWithSDP("SIP/2.0 200 OK", via, from, to, callId, cseq, fromAddr, tag);

    std::cout << "[" << username << "] 等待ACK..." << std::endl;
  }

  // 处理ACK请求
  void handleACK(const std::string& request, struct sockaddr_in& fromAddr) {
    std::lock_guard<std::mutex> lock(callMutex);

    std::string callId = extractHeader(request, "Call-ID");

    if (inCall && callId == currentCallId) {
      std::cout << "[" << username << "] 收到ACK，会话已建立" << std::endl;
      std::cout << "[" << username << "] 通话建立完成" << std::endl;
    }
    else {
      std::cout << "[" << username << "] 收到不匹配的ACK，忽略" << std::endl;
    }
  }

  // 处理BYE请求
  void handleBYE(const std::string& request, struct sockaddr_in& fromAddr) {
    std::lock_guard<std::mutex> lock(callMutex);

    if (!inCall) {
      std::cout << "[" << username << "] 收到BYE请求，但不在通话中" << std::endl;
      // 发送481 Call Leg/Transaction Does Not Exist
      std::string via = extractHeader(request, "Via");
      std::string from = extractHeader(request, "From");
      std::string to = extractHeader(request, "To");
      std::string callId = extractHeader(request, "Call-ID");
      std::string cseqStr = extractHeader(request, "CSeq");
      int cseq = std::stoi(cseqStr.substr(0, cseqStr.find(' ')));

      sendSIPResponse("SIP/2.0 481 Call Leg/Transaction Does Not Exist",
        via, from, to, callId, cseq, "BYE", fromAddr, tag);
      return;
    }

    // 提取头部信息
    std::string callId = extractHeader(request, "Call-ID");
    std::string via = extractHeader(request, "Via");
    std::string from = extractHeader(request, "From");
    std::string to = extractHeader(request, "To");
    std::string cseqStr = extractHeader(request, "CSeq");
    int cseq = std::stoi(cseqStr.substr(0, cseqStr.find(' ')));

    // 检查Call-ID是否匹配当前通话
    if (callId != currentCallId) {
      std::cout << "[" << username << "] BYE请求的Call-ID不匹配，忽略" << std::endl;
      return;
    }

    std::cout << "[" << username << "] 收到BYE请求，结束通话" << std::endl;

    // 发送200 OK响应
    std::cout << "[" << username << "] 发送200 OK响应" << std::endl;
    sendSIPResponse("SIP/2.0 200 OK", via, from, to, callId, cseq, "BYE", fromAddr, tag);

    // 重置通话状态
    inCall = false;
    currentCallId = "";
    currentFromTag = "";
    currentToTag = "";
    currentVia = "";
    currentCSeq = 0;

    std::cout << "[" << username << "] 通话已结束" << std::endl;
  }

  // 处理接收到的SIP响应
  void handleSIPResponse(const std::string& response, struct sockaddr_in& fromAddr) {
    // 检查是否为REGISTER的响应
    if (response.find("SIP/2.0 200 OK") == 0) {
      std::string cseq = extractHeader(response, "CSeq");
      if (cseq.find("REGISTER") != std::string::npos) {
        std::cout << "[" << username << "] REGISTER成功!" << std::endl;
        registered = true;
      }
    }
    else if (response.find("SIP/2.0 401 Unauthorized") == 0) {
      std::cout << "[" << username << "] 收到401 Unauthorized (需要认证)" << std::endl;
      // 这里可以添加认证逻辑
    }
    else if (response.find("SIP/2.0") == 0) {
      // 其他SIP响应
      std::cout << "[" << username << "] 收到响应: " << response.substr(0, response.find("\r\n")) << std::endl;
    }
  }

  // 处理接收到的SIP请求
  void handleSIPRequest(const std::string& request, struct sockaddr_in& fromAddr) {
    // 提取请求方法
    size_t firstSpace = request.find(' ');
    if (firstSpace == std::string::npos) return;

    std::string method = request.substr(0, firstSpace);

    if (method == "INVITE") {
      std::string toHeader = extractHeader(request, "To");
      //if (isInviteToMe(toHeader)) {
      handleINVITE(request, fromAddr);
      //}
      //else {
      //  std::cout << "[" << username << "] INVITE不是给我的，忽略" << std::endl;
      //}
    }
    else if (method == "ACK") {
      handleACK(request, fromAddr);
    }
    else if (method == "BYE") {
      handleBYE(request, fromAddr);
    }
    else if (method == "MESSAGE") {
      handleMESSAGE(request, fromAddr);
    }
    else {
      std::cout << "[" << username << "] 收到无法处理的SIP请求: " << method << std::endl;
    }
  }

  // 接收线程函数
  void receiveLoop() {
    char buffer[4096];
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);

    std::cout << "[" << username << "] 开始接收消息..." << std::endl;

    while (running) {
      memset(buffer, 0, sizeof(buffer));
      memset(&clientAddr, 0, sizeof(clientAddr));

      // 设置接收超时（1秒），以便可以检查running标志
      struct timeval tv;
      tv.tv_sec = 1;
      tv.tv_usec = 0;
      setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

      ssize_t bytesReceived = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
        (struct sockaddr*)&clientAddr, &clientLen);

      if (bytesReceived > 0) {
        buffer[bytesReceived] = '\0';

        // 判断是请求还是响应
        if (strstr(buffer, "SIP/2.0") == buffer) {
          // 这是响应（以"SIP/2.0"开头）
          handleSIPResponse(buffer, clientAddr);
        }
        else {
          // 这是请求
          handleSIPRequest(buffer, clientAddr);
        }
      }
      else if (bytesReceived < 0) {
        // 超时或其他错误，继续循环
        continue;
      }
    }
  }

  // 注册保活线程函数
  void registerLoop() {
    // 初始等待1秒，让接收线程先启动
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "[" << username << "] 开始注册保活..." << std::endl;

    while (running) {
      if (sendRegisterRequest()) {
        std::cout << "[" << username << "] 注册请求已发送，等待1小时..." << std::endl;
      }
      else {
        std::cerr << "[" << username << "] 发送注册请求失败" << std::endl;
      }

      // 等待1小时（3600秒）
      for (int i = 0; i < 3600 && running; i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }
  }

  // 启动Account
  bool start() {
    if (!initializeSocket()) {
      return false;
    }

    running = true;
    registered = false;
    inCall = false;

    // 启动接收线程
    receiveThread = std::thread(&SIPAccount::receiveLoop, this);

    // 启动注册线程
    registerThread = std::thread(&SIPAccount::registerLoop, this);

    std::cout << "[" << username << "] Account 已启动" << std::endl;
    return true;
  }

  // 停止Account
  void stop() {
    std::cout << "[" << username << "] 正在停止..." << std::endl;

    running = false;

    if (receiveThread.joinable()) {
      receiveThread.join();
    }

    if (registerThread.joinable()) {
      registerThread.join();
    }

    std::cout << "[" << username << "] Account 已停止" << std::endl;
  }

  // 获取Account信息
  std::string getInfo() const {
    std::stringstream ss;
    ss << "用户名: " << username << "\n"
      << "域: " << domain << "\n"
      << "服务器: " << serverIp << ":" << serverPort << "\n"
      << "本地绑定: " << localIp << ":" << localPort << "\n"
      << "注册状态: " << (registered ? "已注册" : "未注册") << "\n"
      << "通话状态: " << (inCall ? "通话中" : "空闲");
    return ss.str();
  }
};

// 主程序
int main() {
  std::cout << "=== SIP Server ===" << std::endl;
  std::cout << "初始化随机数种子..." << std::endl;
  srand(time(nullptr));

  // 创建并启动faust账号
  // 根据实际Kamailio服务器配置调整参数
  SIPAccount faust("faust", "password123", "10.1.63.112",
    "10.1.63.112", 51255, "10.1.69.7", 54325);

  // 显示账号信息
  std::cout << "\n账号信息:\n" << faust.getInfo() << std::endl;

  // 启动账号
  std::cout << "\n启动账号..." << std::endl;
  if (!faust.start()) {
    std::cerr << "启动账号失败" << std::endl;
    return 1;
  }

  // 主循环
  std::cout << "\n=== 程序运行中 ===" << std::endl;
  std::cout << "等待SIP消息..." << std::endl;
  std::cout << "按Enter键停止程序..." << std::endl;

  // 等待用户输入以停止程序
  std::cin.get();

  // 停止账号
  std::cout << "\n正在停止账号..." << std::endl;
  faust.stop();

  std::cout << "程序结束" << std::endl;
  return 0;
}
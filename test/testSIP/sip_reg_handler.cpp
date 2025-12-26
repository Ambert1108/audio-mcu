#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <sstream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>

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

  // Socket相关
  int sockfd;
  std::thread registerThread;
  std::thread receiveThread;

  // 锁
  std::mutex socketMutex;

public:
  SIPAccount(const std::string& user, const std::string& pass,
    const std::string& dom, const std::string& server,
    int sPort, const std::string& local, int lPort)
    : username(user), password(pass), domain(dom),
    serverIp(server), serverPort(sPort),
    localIp(local), localPort(lPort),
    registered(false), running(false), cseq(1) {
    sockfd = -1;
    generateCallId();
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
    ss << std::hex << std::setfill('0') << std::setw(8)
      << (rand() & 0xFFFFFFFF);
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

    if (localIp.empty() || localIp == "0.0.0.0" || localIp == "any") {
      localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else {
      if (inet_pton(AF_INET, localIp.c_str(), &localAddr.sin_addr) <= 0) {
        std::cerr << "[" << username << "] 无效的本地IP地址: " << localIp << std::endl;
        close(sockfd);
        return false;
      }
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

  // 发送REGISTER请求
  bool sendRegisterRequest() {
    std::lock_guard<std::mutex> lock(socketMutex);

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
    std::cout << "[" << username << "] 请求内容:\n" << request << std::endl;

    ssize_t sent = sendto(sockfd, request.c_str(), request.length(), 0,
      (struct sockaddr*)&serverAddr, sizeof(serverAddr));

    if (sent < 0) {
      std::cerr << "[" << username << "] 发送REGISTER失败" << std::endl;
      return false;
    }

    std::cout << "[" << username << "] REGISTER请求已发送 (" << sent << " 字节)" << std::endl;
    return true;
  }

  // 处理接收到的SIP响应
  void handleSIPResponse(const std::string& response, struct sockaddr_in& fromAddr) {
    std::cout << "[" << username << "] 收到SIP响应" << std::endl;

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

    // 打印响应来源
    char fromIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &fromAddr.sin_addr, fromIP, sizeof(fromIP));
    std::cout << "[" << username << "] 来自: " << fromIP << ":" << ntohs(fromAddr.sin_port) << std::endl;
  }

  // 处理接收到的SIP请求
  void handleSIPRequest(const std::string& request, struct sockaddr_in& fromAddr) {
    std::cout << "[" << username << "] 收到SIP请求" << std::endl;

    if (request.find("MESSAGE sip:") != std::string::npos ||
      request.find("MESSAGE ") != std::string::npos) {

      std::cout << "[" << username << "] 检测到SIP MESSAGE请求" << std::endl;

      // 提取必要头部
      std::string callId = extractHeader(request, "Call-ID");
      std::string from = extractHeader(request, "From");
      std::string to = extractHeader(request, "To");
      std::string cseq = extractHeader(request, "CSeq");
      std::string via = extractHeader(request, "Via");

      // 构建200 OK响应
      std::stringstream response;
      response << "SIP/2.0 200 OK\r\n";
      response << "Via: " << via << "\r\n";
      response << "From: " << from << "\r\n";
      response << "To: " << to;

      // 添加tag（如果还没有）
      if (to.find("tag=") == std::string::npos) {
        response << ";tag=" << generateTag();
      }
      response << "\r\n";

      response << "Call-ID: " << callId << "\r\n";
      response << "CSeq: " << cseq << "\r\n";
      response << "Content-Length: 0\r\n";
      response << "\r\n";

      // 发送响应
      std::lock_guard<std::mutex> lock(socketMutex);
      sendto(sockfd, response.str().c_str(), response.str().length(), 0,
        (struct sockaddr*)&fromAddr, sizeof(fromAddr));

      std::cout << "[" << username << "] 已发送200 OK响应" << std::endl;
    }
    // 可以添加其他SIP方法的处理
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
        std::cout << "[" << username << "] 注册请求已发送，等待5分钟..." << std::endl;
      }
      else {
        std::cerr << "[" << username << "] 发送注册请求失败" << std::endl;
      }

      // 等待5分钟（300秒）
      for (int i = 0; i < 300 && running; i++) {
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
      << "注册状态: " << (registered ? "已注册" : "未注册");
    return ss.str();
  }
};

// Account管理器
class SIPAccountManager {
private:
  std::map<std::string, SIPAccount*> accounts;
  std::mutex accountsMutex;
  bool running;

public:
  SIPAccountManager() : running(false) {}

  ~SIPAccountManager() {
    stopAll();
  }

  // 添加Account
  bool addAccount(const std::string& name, const std::string& user, const std::string& pass,
    const std::string& domain, const std::string& server, int serverPort,
    const std::string& local = "0.0.0.0", int localPort = 0) {
    std::lock_guard<std::mutex> lock(accountsMutex);

    if (accounts.find(name) != accounts.end()) {
      std::cerr << "Account '" << name << "' 已存在" << std::endl;
      return false;
    }

    // 如果未指定本地端口，随机分配一个
    if (localPort == 0) {
      localPort = 5060 + accounts.size(); // 简单分配策略
    }

    SIPAccount* account = new SIPAccount(user, pass, domain, server,
      serverPort, local, localPort);

    if (running) {
      if (!account->start()) {
        delete account;
        return false;
      }
    }

    accounts[name] = account;
    std::cout << "Account '" << name << "' 已添加" << std::endl;
    return true;
  }

  // 启动所有Account
  void startAll() {
    std::lock_guard<std::mutex> lock(accountsMutex);
    running = true;

    for (auto& pair : accounts) {
      std::cout << "启动Account: " << pair.first << std::endl;
      if (!pair.second->start()) {
        std::cerr << "启动Account '" << pair.first << "' 失败" << std::endl;
      }
    }
  }

  // 停止所有Account
  void stopAll() {
    std::lock_guard<std::mutex> lock(accountsMutex);
    running = false;

    for (auto& pair : accounts) {
      std::cout << "停止Account: " << pair.first << std::endl;
      pair.second->stop();
      delete pair.second;
    }

    accounts.clear();
  }

  // 显示所有Account信息
  void showAccounts() {
    std::lock_guard<std::mutex> lock(accountsMutex);

    std::cout << "\n=== Account列表 (" << accounts.size() << " 个) ===" << std::endl;
    for (const auto& pair : accounts) {
      std::cout << "\n[" << pair.first << "]\n"
        << pair.second->getInfo() << "\n" << std::endl;
    }
  }

  // 获取Account数量
  size_t count() const {
    return accounts.size();
  }
};

// 主程序
int main() {
  std::cout << "=== SIP Account 管理器 ===" << std::endl;
  std::cout << "初始化随机数种子..." << std::endl;
  srand(time(nullptr));

  SIPAccountManager manager;

  // 示例：添加一些Account
  // 格式：账号名, 用户名, 密码, 域, 服务器IP, 服务器端口, 本地IP, 本地端口
  manager.addAccount("alice", "alice", "password123", "10.1.63.112",
    "10.1.63.112", 51255, "10.1.69.7", 30022);

  //manager.addAccount("bob", "bob", "password456", "10.1.63.112",
  //  "10.1.63.112", 51255, "10.1.69.7", 30022);

  // 显示添加的Account
  manager.showAccounts();

  // 启动所有Account
  std::cout << "\n启动所有Account..." << std::endl;
  manager.startAll();

  // 主循环
  std::cout << "\n=== 主程序运行中 ===" << std::endl;
  std::cout << "管理 " << manager.count() << " 个Account" << std::endl;
  std::cout << "按Enter键停止程序..." << std::endl;

  // 等待用户输入以停止程序
  std::cin.get();

  // 停止所有Account
  std::cout << "\n正在停止所有Account..." << std::endl;
  manager.stopAll();

  std::cout << "程序结束" << std::endl;
  return 0;
}
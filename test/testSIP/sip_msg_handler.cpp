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

class SIPMessageHandler {
private:
  int sockfd;
  std::string ip;
  int port;
  bool running;

public:
  SIPMessageHandler(const std::string& listenIp, int listenPort) :
    ip(listenIp), port(listenPort), running(false) {
    sockfd = -1;
  }

  ~SIPMessageHandler() {
    if (sockfd != -1) {
      close(sockfd);
    }
  }

  // 初始化UDP socket
  bool initialize() {
    // 创建UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
      std::cerr << "创建socket失败" << std::endl;
      return false;
    }

    // 设置地址重用
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
      std::cerr << "设置socket选项失败" << std::endl;
      close(sockfd);
      return false;
    }

    // 绑定到指定IP和端口（修改这里）
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;

    // 使用指定IP地址
    if (ip.empty() || ip == "0.0.0.0" || ip == "any") {
      serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
      std::cout << "绑定到所有网络接口" << std::endl;
    }
    else {
      // 将字符串IP转换为网络字节序
      if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "无效的IP地址: " << ip << std::endl;
        close(sockfd);
        return false;
      }
      std::cout << "绑定到指定IP: " << ip << std::endl;
    }

    serverAddr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
      std::cerr << "绑定 " << ip << ":" << port << " 失败" << std::endl;
      close(sockfd);
      return false;
    }

    std::cout << "成功绑定到UDP地址 -> " << ip << ":" << port << std::endl;
    return true;
  }

  // 从SIP消息中提取头部值
  std::string extractHeader(const std::string& message, const std::string& headerName) {
    std::istringstream stream(message);
    std::string line;

    while (std::getline(stream, line)) {
      // 查找头部
      if (line.find(headerName) == 0) {
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
          // 返回冒号后面的值（去掉前导空格）
          std::string value = line.substr(colonPos + 1);
          // 去除前导空格和尾随的\r
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

  // 生成Branch参数（用于Via头部）
  std::string generateBranch() {
    std::stringstream ss;
    ss << "z9hG4bK" << std::hex << std::setfill('0') << std::setw(8)
      << (rand() & 0xFFFFFFFF);
    return ss.str();
  }

  // 生成Tag参数
  std::string generateTag() {
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(8)
      << (rand() & 0xFFFFFFFF);
    return ss.str();
  }

  // 构造200 OK响应
  std::string construct200OK(const std::string& request) {
    std::stringstream response;

    // 提取必要的头部信息
    std::string callId = extractHeader(request, "Call-ID");
    std::string from = extractHeader(request, "From");
    std::string to = extractHeader(request, "To");
    std::string cseq = extractHeader(request, "CSeq");
    std::string via = extractHeader(request, "Via");

    // 生成时间戳
    std::time_t now = std::time(nullptr);
    std::string timestamp = std::to_string(now);

    // 生成tag（如果需要）
    std::string toTag = extractHeader(request, "tag");
    if (toTag.empty()) {
      toTag = generateTag();
    }

    // 处理Via头部，添加received和rport
    std::string viaWithParams = via;
    size_t viaEnd = viaWithParams.find(';');
    if (viaEnd != std::string::npos) {
      // 检查是否已有branch参数
      if (viaWithParams.find("branch=", viaEnd) == std::string::npos) {
        viaWithParams.insert(viaEnd, ";branch=" + generateBranch());
      }
    }

    // 构建响应消息
    response << "SIP/2.0 200 OK\r\n";
    response << "Via: " << viaWithParams << "\r\n";
    response << "From: " << from << "\r\n";

    // 检查To头部是否已有tag
    if (to.find("tag=") == std::string::npos) {
      response << "To: " << to << ";tag=" << toTag << "\r\n";
    }
    else {
      response << "To: " << to << "\r\n";
    }

    response << "Call-ID: " << callId << "\r\n";
    response << "CSeq: " << cseq << "\r\n";

    // 添加其他必要的头部
    response << "Server: SIPMessageHandler/1.0\r\n";
    response << "Content-Length: 0\r\n";
    response << "\r\n";  // 空行表示头部结束

    return response.str();
  }

  // 处理接收到的SIP消息
  void handleSIPMessage(const std::string& message, struct sockaddr_in& clientAddr) {
    std::cout << "\n=== 收到SIP消息 ===" << std::endl;
    std::cout << message << std::endl;
    std::cout << "=== 消息结束 ===" << std::endl;

    // 检查是否为MESSAGE请求
    if (message.find("MESSAGE sip:") != std::string::npos ||
      message.find("MESSAGE ") != std::string::npos) {

      std::cout << "检测到SIP MESSAGE请求" << std::endl;

      // 构建200 OK响应
      std::string response = construct200OK(message);

      std::cout << "\n=== 发送200 OK响应 ===" << std::endl;
      std::cout << response << std::endl;
      std::cout << "=== 响应结束 ===" << std::endl;

      // 发送响应
      sendto(sockfd, response.c_str(), response.length(), 0,
        (struct sockaddr*)&clientAddr, sizeof(clientAddr));

      std::cout << "已发送200 OK响应到 "
        << inet_ntoa(clientAddr.sin_addr) << ":"
        << ntohs(clientAddr.sin_port) << std::endl;
    }
    else {
      std::cout << "不是SIP MESSAGE请求，忽略" << std::endl;
    }
  }

  // 开始监听
  void startListening() {
    running = true;
    char buffer[4096];
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);

    std::cout << "开始监听SIP消息..." << std::endl;
    std::cout << "按Ctrl+C停止" << std::endl;

    while (running) {
      memset(buffer, 0, sizeof(buffer));
      memset(&clientAddr, 0, sizeof(clientAddr));

      // 接收数据
      ssize_t bytesReceived = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
        (struct sockaddr*)&clientAddr, &clientLen);

      if (bytesReceived > 0) {
        buffer[bytesReceived] = '\0';

        // 输出客户端信息
        std::cout << "\n收到来自 "
          << inet_ntoa(clientAddr.sin_addr) << ":"
          << ntohs(clientAddr.sin_port)
          << " 的消息，长度: " << bytesReceived << " 字节" << std::endl;

        // 处理SIP消息
        handleSIPMessage(buffer, clientAddr);
      }
      else if (bytesReceived < 0) {
        std::cerr << "接收数据出错" << std::endl;
        break;
      }
    }
  }

  // 停止监听
  void stopListening() {
    running = false;
  }
};

// 用法示例
int main(int argc, char* argv[]) {
  std::string ip = "0.0.0.0";  // 默认绑定所有接口
  int port = 5060;  // SIP默认端口

  // 解析命令行参数
  if (argc > 1) {
    ip = argv[1];
  }
  if (argc > 2) {
    port = atoi(argv[2]);
  }

  if (port <= 0 || port > 65535) {
    std::cerr << "无效的端口号: " << port << std::endl;
    std::cerr << "用法: " << argv[0] << " [IP地址] [端口号]" << std::endl;
    std::cerr << "示例: " << argv[0] << " 192.168.1.100 5060" << std::endl;
    std::cerr << "示例: " << argv[0] << " 0.0.0.0 5060 (绑定所有接口)" << std::endl;
    std::cerr << "示例: " << argv[0] << " 127.0.0.1 5060 (仅本地)" << std::endl;
    return 1;
  }

  // 初始化随机数种子
  srand(time(nullptr));

  SIPMessageHandler handler(ip, port);

  if (!handler.initialize()) {
    std::cerr << "初始化失败" << std::endl;
    return 1;
  }

  try {
    handler.startListening();
  }
  catch (const std::exception& e) {
    std::cerr << "发生异常: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
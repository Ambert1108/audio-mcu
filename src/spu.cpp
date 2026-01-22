#include "SipProcesserUnit.h"

namespace aom {
  SipProcessUnit::SipProcessUnit(const std::string& user, const std::string& pass,
    const std::string& dom, const std::string& server,
    int sPort, const std::string& local, int lPort)
    : username(user), password(pass), domain(dom),
    serverIp(server), serverPort(sPort),
    localIp(local), localPort(lPort),
    registered(false), running(false), cseq(7343) {
    sockfd = -1;
    generateCallId();
    tag = generateTag();
    mcu = MediaControlUnit::getInstance();
  }

  SipProcessUnit::~SipProcessUnit() {
    stop();
    if (sockfd != -1) {
      close(sockfd);
    }
    if (mcu) {
      MediaControlUnit::giveInstance(mcu);
    }
  }

  bool SipProcessUnit::start() {
    if (!initializeSocket()) {
      return false;
    }

    running = true;
    registered = false;

    // 启动接收线程
    receiveThread = std::thread(&SipProcessUnit::receiveLoop, this);

    // 启动注册线程
    registerThread = std::thread(&SipProcessUnit::registerLoop, this);
    W_LOG("Account {} is started", username);
    //receiveThread.join();
    return true;
  }

  void SipProcessUnit::stop() {
    W_LOG("Account {} is stoping", username);

    running = false;

    if (receiveThread.joinable()) {
      receiveThread.join();
    }

    if (registerThread.joinable()) {
      registerThread.join();
    }

    W_LOG("Account {} is stoped", username);
  }

  std::string SipProcessUnit::getInfo() const {
    std::stringstream ss;
    ss << "user: " << username << "\n"
      << "domain " << domain << "\n"
      << "server: " << serverIp << ":" << serverPort << "\n"
      << "local bind: " << localIp << ":" << localPort << "\n"
      << "register status: " << (registered ? "yes" : "no");
    return ss.str();
  }

  void SipProcessUnit::generateCallId() {
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(8)
      << (rand() & 0xFFFFFFFF) << "@" << domain;
    callId = ss.str();
  }

  std::string SipProcessUnit::generateBranch() {
    std::stringstream ss;
    ss << "z9hG4bK" << std::hex << std::setfill('0') << std::setw(8)
      << (rand() & 0xFFFFFFFF);
    branch = ss.str();
    return branch;
  }

  std::string SipProcessUnit::generateTag() {
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16)
      << (rand() & 0xFFFFFFFF) << (rand() & 0xFFFFFFFF);
    return ss.str();
  }

  std::string SipProcessUnit::extractHeader(const std::string& message, const std::string& headerName) {
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

  std::string SipProcessUnit::extractMessageBody(const std::string& message) {
    size_t body_start = message.find("\r\n\r\n");
    if (body_start != std::string::npos) {
      return message.substr(body_start + 4); // 跳过两个CRLF
    }
    return "";
  }

  std::string SipProcessUnit::extractFromTag(const std::string& fromHeader) {
    size_t tagPos = fromHeader.find("tag=");
    if (tagPos != std::string::npos) {
      size_t start = tagPos + 4;
      size_t end = fromHeader.find(';', start);
      if (end == std::string::npos) end = fromHeader.length();
      return fromHeader.substr(start, end - start);
    }
    return "";
  }

  std::string SipProcessUnit::extractChnlId(const std::string& wholeMsg) {
    // 查找 "From:" 行
    size_t fromPos = wholeMsg.find("From:");
    if (fromPos == std::string::npos) {
      return "";  // 没有找到 From 头
    }

    // 查找 "sip:" 或 "SIP:"
    size_t sipPos = wholeMsg.find("sip:", fromPos);
    if (sipPos == std::string::npos) {
      sipPos = wholeMsg.find("SIP:", fromPos);
      if (sipPos == std::string::npos) {
        return "";  // 没有找到 SIP URI
      }
    }

    // 提取 "sip:user@domain" 部分
    size_t atPos = wholeMsg.find("@", sipPos);
    if (atPos == std::string::npos) {
      return "";  // 无效的 SIP URI（没有 @）
    }

    // 提取用户名部分（sip: 之后，@ 之前）
    size_t userStart = sipPos + 4;  // "sip:" 占 4 字符
    std::string username = wholeMsg.substr(userStart, atPos - userStart);
    return username;
  }

  std::string SipProcessUnit::extractJobId(const std::string& wholeMsg) {
    // 查找 "To:" 行
    size_t toPos = wholeMsg.find("To:");
    if (toPos == std::string::npos) {
      return "";  // 没有找到 To 头
    }

    // 查找 "sip:" 或 "SIP:"
    size_t sipPos = wholeMsg.find("sip:", toPos);
    if (sipPos == std::string::npos) {
      sipPos = wholeMsg.find("SIP:", toPos);
      if (sipPos == std::string::npos) {
        return "";  // 没有找到 SIP URI
      }
    }

    // 提取 "sip:user@domain" 部分
    size_t atPos = wholeMsg.find("@", sipPos);
    if (atPos == std::string::npos) {
      return "";  // 无效的 SIP URI（没有 @）
    }

    // 提取用户名部分（sip: 之后，@ 之前）
    size_t userStart = sipPos + 4;  // "sip:" 占 4 字符
    std::string username = wholeMsg.substr(userStart, atPos - userStart);
    return username;
  }

  void SipProcessUnit::parseSDP(const std::string& wholeMsg, SDPInfo& info) {
    size_t sdp_start = wholeMsg.find("\r\n\r\n");
    if (sdp_start == std::string::npos) {
      E_LOG("find sdp failed");
    }
    std::string sdp = wholeMsg.substr(sdp_start + 4); // 跳过头部和空行
    D_LOG("Offer SDP:\n{}", sdp);
    std::istringstream iss(sdp);
    std::string line;

    size_t c_pos = sdp.find("c=");
    while (c_pos != std::string::npos) {
      // 确保c=行是独立的一行（前面为换行或位于开头）
      if (c_pos == 0 || (sdp[c_pos - 1] == '\n' && (c_pos == 1 || sdp[c_pos - 2] == '\r'))) {
        size_t end_line = sdp.find("\r\n", c_pos);
        std::string c_line = sdp.substr(c_pos, end_line - c_pos);
        D_LOG("c: {}", c_line);

        // 分割c=行的内容
        std::istringstream iss(c_line);
        std::vector<std::string> parts;
        std::string part;
        while (iss >> part) {
          parts.push_back(part);
        }

        if (parts.size() >= 3) {
          std::string addressType = parts[1]; // 如IP4或IP6
          std::string connectionAddress = parts[2];

          // 处理地址中的附加信息（如/后的内容）
          size_t slash_pos = connectionAddress.find('/');
          if (slash_pos != std::string::npos) {
            connectionAddress = connectionAddress.substr(0, slash_pos);
          }

          info.ip = connectionAddress;
          D_LOG("dst ip is {}", info.ip);
          break; // 提取第一个有效的IP后退出
        }
        else {
          E_LOG("c line is error: {}", c_line);
        }
      }
      // 继续查找下一个c=行
      c_pos = sdp.find("c=", c_pos + 1);
    }

    // 解析音频端口
    size_t audio_pos = sdp.find("m=audio");
    if (audio_pos != std::string::npos) {
      size_t port_end = sdp.find(" ", audio_pos + 8); // "m=audio "共8字符
      std::string audioPort = sdp.substr(audio_pos + 8, port_end - (audio_pos + 8));
      info.port = std::atoi(audioPort.c_str());
      D_LOG("audio port is {}", info.port);
    }

    while (std::getline(iss, line)) {
      // 解析Opus编码参数（a=rtpmap行）
      if (line.find("a=rtpmap:") != std::string::npos && line.find("opus/") != std::string::npos) {
        info.isOpus = true;
        size_t colonPos = line.find(':');
        size_t slashPos = line.find('/');
        if (colonPos != std::string::npos && slashPos != std::string::npos) {
          info.payloadType = std::stoi(line.substr(colonPos + 1, slashPos - colonPos - 1));
          D_LOG("get pt is {}", info.payloadType);
          size_t ratePos = line.find('/', slashPos + 1);
          if (ratePos != std::string::npos) {
            info.sampleRate = std::stoi(line.substr(slashPos + 1, ratePos - slashPos - 1));
            D_LOG("get sampleRate is {}", info.sampleRate);
          }
        }
      }
    }
  }

  bool SipProcessUnit::initializeSocket() {
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
      E_LOG("sip mode: create socket failed");
      return false;
    }

    // 设置地址重用
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
      E_LOG("sip mode: setting socket option failed");
      close(sockfd);
      return false;
    }

    // 绑定到本地IP和端口
    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;

    if (inet_pton(AF_INET, localIp.c_str(), &localAddr.sin_addr) <= 0) {
      E_LOG("sip mode: invaild local ip {}", localIp);
      close(sockfd);
      return false;
    }

    localAddr.sin_port = htons(localPort);

    if (bind(sockfd, (struct sockaddr*)&localAddr, sizeof(localAddr)) < 0) {
      E_LOG("sip mode: bind {}:{} failed", localIp, localPort);
      close(sockfd);
      return false;
    }


    W_LOG("sip mode: bind {}:{} success", localIp, localPort);
    return true;
  }

  std::string SipProcessUnit::constructRegisterRequest() {
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

  std::string SipProcessUnit::constructSIPResponse(const std::string& statusLine,
    const std::string& via,
    const std::string& from,
    const std::string& to,
    const std::string& callId,
    int cseq,
    const std::string& method,
    const std::string& toTag) {
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

  std::string SipProcessUnit::constructSIPResponseWithSDP(const std::string& statusLine,
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

  bool SipProcessUnit::sendMessage(const std::string& message, const struct sockaddr_in& destAddr) {
    std::lock_guard<std::mutex> lock(socketMutex);

    ssize_t sent = sendto(sockfd, message.c_str(), message.length(), 0,
      (struct sockaddr*)&destAddr, sizeof(destAddr));

    if (sent < 0) {
      E_LOG("sip mode: send sip message failed");
      return false;
    }
    I_LOG("send:\n{}", message);
    return true;
  }

  bool SipProcessUnit::sendSIPResponse(const std::string& statusLine,
    const std::string& via,
    const std::string& from,
    const std::string& to,
    const std::string& callId,
    int cseq,
    const std::string& method,
    const struct sockaddr_in& destAddr,
    const std::string& toTag) {

    std::string response = constructSIPResponse(statusLine, via, from, to,
      callId, cseq, method, toTag);

    return sendMessage(response, destAddr);
  }

  bool SipProcessUnit::sendSIPResponseWithSDP(const std::string& statusLine,
    const std::string& via,
    const std::string& from,
    const std::string& to,
    const std::string& callId,
    int cseq,
    const struct sockaddr_in& destAddr,
    const std::string& toTag,
    const std::string& sdp) {
    std::string response = constructSIPResponseWithSDP(statusLine, via, from, to,
      callId, cseq, toTag, sdp);

    return sendMessage(response, destAddr);
  }

  bool SipProcessUnit::sendRegisterRequest() {
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);

    if (inet_pton(AF_INET, serverIp.c_str(), &serverAddr.sin_addr) <= 0) {
      E_LOG("sip mode: invaild server ip {}", serverIp);
      return false;
    }

    std::string request = constructRegisterRequest();

    return sendMessage(request, serverAddr);
  }

  void SipProcessUnit::handleMESSAGE(const std::string& request, struct sockaddr_in& fromAddr) {
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
    sendSIPResponse("SIP/2.0 200 OK", via, from, to, callId, cseq, "MESSAGE", fromAddr, tag);

    std::string message_body = extractMessageBody(request);
    std::string userName = message_body;
    if (userName.size() > 11) {
      userName = userName.substr(userName.length() - 11, 11);
    }
    std::regex pattern1("audio(\\d{6})");
    std::regex pattern2("close(\\d{6})");
    std::smatch matches;
    I_LOG("message_body={}, userName={}", message_body, userName);
    if (std::regex_match(userName, matches, pattern1)) {
      CreateJobContext createCtx;
      createCtx.jobId = matches[1];
      createCtx.url = "empty";
      I_LOG("debug: create mpu, jobId={}", createCtx.jobId);
      mcu->createMpu(createCtx);
    }
    else if (std::regex_match(userName, matches, pattern2)) {
      std::string jobId = matches[1];
      I_LOG("debug: end mpu");
      mcu->endMpu(jobId);
    }
    else {
      E_LOG("sip mode: match message {} failed", userName);
    }
  }

  void SipProcessUnit::handleINVITE(const std::string& request, struct sockaddr_in& fromAddr) {
    std::lock_guard<std::mutex> lock(callMutex);

    // 提取头部信息
    std::string via = extractHeader(request, "Via");
    std::string from = extractHeader(request, "From");
    std::string to = extractHeader(request, "To");
    std::string callId = extractHeader(request, "Call-ID");
    std::string cseqStr = extractHeader(request, "CSeq");
    int cseq = std::stoi(cseqStr.substr(0, cseqStr.find(' ')));

    // 发送100 Trying
    sendSIPResponse("SIP/2.0 100 Trying", via, from, to, callId, cseq, "INVITE", fromAddr);

    std::string jobId = extractJobId(request);

    std::regex pattern("audio(\\d{6})");
    std::smatch match;
    if (std::regex_match(jobId, match, pattern)) {
      jobId = match[1];
    }
    else {
      E_LOG("sip mode: match {} failed!", jobId);
      mcu->setJoinErr();
      return;
    }

    if (!mcu->checkJob(jobId)) {
      E_LOG("sip mode: jobId {} not found", jobId);
      mcu->setJoinErr();
      return;
    }

    //取出INVITE中的From作为channelId
    std::string chnlId = extractChnlId(request);

    //在SDP中取出payloadType、codecType、inSampleRate、outSampleRate、dstIp、dstPort
    SDPInfo info;
    parseSDP(request, info);
    I_LOG("sip mode: jobId={} chnlId={} dstIp={} dstPort={} pt={} samplerate={} opus={}",
      jobId, chnlId, info.ip, info.port, info.payloadType, info.sampleRate, info.isOpus);
    AddChnlContext addCtx;
    ListenAddr addr;
    addCtx.jobId = jobId;
    addCtx.chnlId = chnlId;
    addCtx.dstIp = info.ip;
    addCtx.dstPort = info.port;
    if (info.isOpus) {
      addCtx.codecType = 2;
      addCtx.payloadType = info.payloadType;
      addCtx.inSampleRate = info.sampleRate;
      addCtx.outSampleRate = info.sampleRate;
    }
    else {
      //PCMA
      addCtx.codecType = 1;
      addCtx.payloadType = 8;
      addCtx.inSampleRate = 8000;
      addCtx.outSampleRate = 8000;
    }

    sendSIPResponse("SIP/2.0 180 Ringing", via, from, to, callId, cseq, "INVITE", fromAddr);

    if (!mcu->addChnl(addCtx, addr)) {
      E_LOG("sip mode: jobId={} add channel {} faild", jobId, chnlId);

      return;
    }
    MicCtrlContext ctx;
    ctx.jobId = jobId;
    ctx.channelId = chnlId;
    mcu->openMic(ctx);

    std::string sdp =
      "v=0\r\n"
      "o=- 3953192465 3953192466 IN IP4 " + addr.ip + "\r\n"
      "s=pjmedia\r\n"
      "c=IN IP4 " + addr.ip + "\r\n"
      "b=AS:84\r\n"
      "t=0 0\r\n"
      "a=X-nat:0\r\n"
      "m=audio " + std::to_string(addr.port) + " RTP/AVP 8\r\n"
      "a=rtcp:4001 IN IP4 " + addr.ip + "\r\n"
      "a=ssrc:766044304 cname:7b8072591a0e9c5a\r\n"
      "a=rtpmap:8 PCMA/8000\r\n"
      "a=fmtp:8 0-16\r\n"
      "a=rtpmap:121 telephone-event/8000\r\n"
      "a=fmtp:121 0-16\r\n"
      "a=rtcp-fb:* ccm tmmbr\r\n"
      "m=video 0 RTP/AVP 96\r\n"
      "c=IN IP4 " + addr.ip + "\r\n"
      "a=rtpmap:96 H264/90000\r\n"
      "a=fmtp:96 profile-level-id=42801F\r\n"
      "a=rtcp-fb:96 nack pli\r\n";

    sendSIPResponseWithSDP("SIP/2.0 200 OK", via, from, to, callId, cseq, fromAddr, tag, sdp);
  }

  void SipProcessUnit::handleACK(const std::string& request, struct sockaddr_in& fromAddr) {
    std::string jobId = extractJobId(request);

    std::regex pattern("audio(\\d{6})");
    std::smatch match;
    if (std::regex_match(jobId, match, pattern)) {
      jobId = match[1];
    }
    else {
      E_LOG("sip mode: match {} failed!", jobId);
      mcu->setJoinErr();
      return;
    }

    if (!mcu->checkJob(jobId)) {
      E_LOG("sip mode: jobId {} not found", jobId);
      mcu->setJoinErr();
      return;
    }

    //取出INVITE中的From作为channelId
    std::string chnlId = extractChnlId(request);
    I_LOG("sip mode: session is make up, jobId={}, chnlId={}", jobId, chnlId);
  }

  void SipProcessUnit::handleBYE(const std::string& request, struct sockaddr_in& fromAddr) {
    std::lock_guard<std::mutex> lock(callMutex);

    // 提取头部信息
    std::string callId = extractHeader(request, "Call-ID");
    std::string via = extractHeader(request, "Via");
    std::string from = extractHeader(request, "From");
    std::string to = extractHeader(request, "To");
    std::string cseqStr = extractHeader(request, "CSeq");
    int cseq = std::stoi(cseqStr.substr(0, cseqStr.find(' ')));

    // 发送200 OK响应
    sendSIPResponse("SIP/2.0 200 OK", via, from, to, callId, cseq, "BYE", fromAddr, tag);

    std::string jobId = extractJobId(request);

    std::regex pattern("audio(\\d{6})");
    std::smatch match;
    if (std::regex_match(jobId, match, pattern)) {
      jobId = match[1];
    }
    else {
      E_LOG("sip mode: match {} failed!", jobId);
      mcu->setLeaveErr();
      return;
    }
    std::string chnlId = extractChnlId(request);
    I_LOG("sip mode: jobId={} start remove chnlId={}", jobId, chnlId);
    RemoveChnlContext ctx(jobId, chnlId);
    mcu->removeChnl(ctx);

    I_LOG("sip mode: session is call dump, jobId={}, chnlId={}", jobId, chnlId);
  }

  void SipProcessUnit::handleSIPResponse(const std::string& response, struct sockaddr_in& fromAddr) {
    // 检查是否为REGISTER的响应
    if (response.find("SIP/2.0 200 OK") == 0) {
      std::string cseq = extractHeader(response, "CSeq");
      if (cseq.find("REGISTER") != std::string::npos) {
        I_LOG("sip mode: register success");
        registered = true;
      }
    }
    else if (response.find("SIP/2.0 401 Unauthorized") == 0) {
      W_LOG("sip mode: need authorized");
      // 这里可以添加认证逻辑
    }
  }

  void SipProcessUnit::handleSIPRequest(const std::string& request, struct sockaddr_in& fromAddr) {
    // 提取请求方法
    size_t firstSpace = request.find(' ');
    if (firstSpace == std::string::npos) return;

    std::string method = request.substr(0, firstSpace);

    if (method == "INVITE") {
      handleINVITE(request, fromAddr);
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
      W_LOG("sip mode: {} method is not support", method);
    }
  }

  void SipProcessUnit::receiveLoop() {
    char buffer[4096];
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);

    I_LOG("sip mode: receive loop is start working");
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
        I_LOG("receive:\n{}", buffer);
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

  void SipProcessUnit::registerLoop() {
    // 初始等待1秒，让接收线程先启动
    std::this_thread::sleep_for(std::chrono::seconds(1));

    I_LOG("sip mode: register keep loop is start working");

    while (running) {
      if (!sendRegisterRequest()) {
        E_LOG("sip mode: send request REGISTER failed!");
      }

      // 等待5分钟（300秒）
      for (int i = 0; i < 300 && running; i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }
  }
}
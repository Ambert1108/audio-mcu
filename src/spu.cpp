#include "SipProcesserUnit.h"

namespace aom {
  SipCall::SipCall(bool opus, Account& acc, int call_id) : Call(acc, call_id), isOpus(opus) {
    //pj_caching_pool_init(&cp, NULL, 0);
    //pool = pj_pool_create(&cp.factory, "answerSdp", 4096, 4096, NULL);
    //if (!pool) {
    //  E_LOG("create pool failed");
    //}
  }

  SipCall::~SipCall() {
    //if (pool) {
    //  pj_pool_release(pool);
    //}
    account = nullptr;
  }

  void SipCall::registerSipAccount(SipAccount* val) { account = val; }

  void SipCall::setId(const std::string& id) { chnlId = id; }

  void SipCall::setPort(port_t val) {
    listenPort = val;
  }

  void SipCall::onCallState(OnCallStateParam& prm) {
    CallInfo ci = getInfo();
    I_LOG("[SC:{}] status change, current code {} from {}", chnlId, ci.lastReason, ci.remoteUri);

    if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
      SipRxData rdata = prm.e.body.tsxState.src.rdata;
      if (account) {
        account->closeChannel(rdata.wholeMsg);
      }
      else {
        E_LOG("[SC:{}] SipAccount already destory, can't use closeChannel", chnlId);
      }
    }
    if (ci.lastStatusCode == PJSIP_SC_REQUEST_UPDATED) {
      I_LOG("[SC:{}] Receive UPDATE request, start process", chnlId);
      //std::string msg = prm.e.body.tsxState.src.rdata.wholeMsg;
      //account->updateChannelDestition(msg);
    }
    //W_LOG("[DEBUG] Request:{}", prm.e.body.tsxState.src.rdata.info);
  }

  void SipCall::onCallTsxState(OnCallTsxStateParam& prm) {
    std::string sendMsg = prm.e.body.tsxState.src.tdata.wholeMsg;
    std::string recvMsg = prm.e.body.tsxState.src.rdata.wholeMsg;
    if(!sendMsg.empty()) I_LOG("Send Msg\n{}", sendMsg);
    if (!recvMsg.empty()) {
      I_LOG("Recv Msg\n{}", recvMsg);
      std::string method = prm.e.body.tsxState.tsx.method;
      W_LOG("Debug: Recv Method:{}", method);
      if (method == "UPDATE") {
        I_LOG("[SC:{}] Receive UPDATE request, start process", chnlId);

        if (!account->updateChannelDestition(recvMsg)) {
          E_LOG("[SC] update channel {} faild", chnlId);
        }
      }
    }
  }

  void SipCall::onCallSdpCreated(pj::OnCallSdpCreatedParam& prm) {
    I_LOG("[SC:{}] raw sdp:\n{}", chnlId, prm.sdp.wholeSdp);

    // 生成sdp
    std::string newSdp;
    auto ait = prm.sdp.wholeSdp.find("m=audio");
    auto vit = prm.sdp.wholeSdp.find("m=video");
    if (isOpus) {
      newSdp =
        "v=0\r\n"
        "o=- 3953192465 3953192466 IN IP4 " + listenIp + "\r\n"
        "s=pjmedia\r\n"
        "c=IN IP4 " + listenIp + "\r\n"
        "b=AS:84\r\n"
        "t=0 0\r\n"
        "a=X-nat:0\r\n"
        "m=audio " + std::to_string(listenPort) + " RTP/AVP 96\r\n"
        "a=rtcp:4001 IN IP4 " + listenIp + "\r\n"
        "a=ssrc:766044304 cname:7b8072591a0e9c5a\r\n"
        "a=rtpmap:96 opus/48000/2\r\n"
        "a=fmtp:96 0-16\r\n"
        "a=rtpmap:121 telephone-event/48000\r\n"
        "a=fmtp:121 0-16\r\n"
        "a=a=rtcp-fb:* ccm tmmbr\r\n";
    }
    else {
      newSdp =
        "v=0\r\n"
        "o=- 3953192465 3953192466 IN IP4 " + listenIp + "\r\n"
        "s=pjmedia\r\n"
        "c=IN IP4 " + listenIp + "\r\n"
        "b=AS:84\r\n"
        "t=0 0\r\n"
        "a=X-nat:0\r\n"
        "m=audio " + std::to_string(listenPort) + " RTP/AVP 8\r\n"
        "a=rtcp:4001 IN IP4 " + listenIp + "\r\n"
        "a=ssrc:766044304 cname:7b8072591a0e9c5a\r\n"
        "a=rtpmap:8 PCMA/8000\r\n"
        "a=fmtp:8 0-16\r\n"
        "a=rtpmap:121 telephone-event/8000\r\n"
        "a=fmtp:121 0-16\r\n"
        "a=a=rtcp-fb:* ccm tmmbr\r\n";
    }
    
    if (ait != std::string::npos && vit != std::string::npos) {
      // 存在audio和video
      newSdp +=
        "m=video 0 RTP/AVP 96\r\n"
        "c=IN IP4 47.93.119.6";
        "a=rtpmap:96 H264/90000\r\n"
        "a=fmtp:96 profile-level-id=42801F\r\n"
        "a=rtcp-fb:96 nack pli\r\n";
    }

    prm.sdp.wholeSdp = newSdp;

    I_LOG("[SC:{}] modify sdp:\n{}", chnlId, prm.sdp.wholeSdp);
  }

  SipAccount::SipAccount() {
    mcu = MediaControlUnit::getInstance();
  }

  SipAccount::~SipAccount() {
    this->shutdown();
    if (mcu) {
      MediaControlUnit::giveInstance(mcu);
    }
    I_LOG("[SA:{}] destruct finish", jobId);
  }

  bool SipAccount::closeChannel(const std::string& msg) {
    std::string jobId = extractJobId(msg);

    std::regex pattern("audio(\\d{6})");
    std::smatch match;
    if (std::regex_match(jobId, match, pattern)) {
      jobId = match[1];
    }
    std::string chnlId = extractChnlId(msg);
    I_LOG("[SA:{}->{}]start remove channel", jobId, chnlId);
    RemoveChnlContext ctx(jobId, chnlId);
    mcu->removeChnl(ctx);
    W_LOG("[SA:{}->{}] remove step: remove channel finish", jobId, chnlId);

    {
      std::lock_guard<std::mutex> lck(listLocker);
      auto it = callList.find(chnlId);
      if (it == callList.end()) {
        E_LOG("find channel id:{} in call list failed", chnlId);
        return false;
      }
      //CallOpParam prm;
      //prm.statusCode = PJSIP_SC_OK;
      //it->second->answer(prm);
      it->second.reset();
      callList.erase(it);
    }
    I_LOG("[SA:{}->{}] remove step: remove call finish", jobId, chnlId);

    return true;
  }

  bool SipAccount::updateChannelDestition(const std::string& msg) {
    std::string jobId = extractJobId(msg);

    std::regex pattern("audio(\\d{6})");
    std::smatch match;
    if (std::regex_match(jobId, match, pattern)) {
      jobId = match[1];
    }
    std::string chnlId = extractChnlId(msg);
    I_LOG("[SA:{}->{}]start update channel destition", jobId, chnlId);

    SDPInfo info;
    parseSDP(msg, info);
    I_LOG("[SA:{}->{}] get dst ip is {}, port is {}, pt is {}, samplerate is {}, opus is {}",
      jobId, chnlId, info.ip, info.port, info.payloadType, info.sampleRate, info.isOpus);

    UpdateContext ctx(jobId, chnlId, info.ip, info.port);
    if (info.isOpus) {
      ctx.codecType = 2;
    }
    else {
      ctx.codecType = 1;
    }

    if (!mcu->updateDestition(ctx)) {
      return false;
    }
    return true;
  }

  void SipAccount::setRemoveCallListCallback(RemoveCallList func) {
    callback = func;
  }

  void SipAccount::setUnregistering(bool val) {
    isUnregistering = val;
  }

  void SipAccount::onRegState(OnRegStateParam& prm) {
    AccountInfo ai = getInfo();
    if (ai.regIsActive) {
      jobId = extractJobId("To: " + ai.uri);
      I_LOG("[SA:{}] register {} success, code={}, reason={}", jobId, ai.uri, prm.code, prm.reason);
    }
    else {
      if (isUnregistering) {
        if (prm.code >= 200 && prm.code < 300) {
          I_LOG("[SA:{}] receive unregister {}, reason={}", jobId, ai.uri, prm.reason);
          isUnregistering = false;
          callback(jobId);
        }
        else {
          I_LOG("[SA:{}] unregister {} failed, reason={}", jobId, ai.uri, prm.reason);
        }
      }
      else E_LOG("[SA:{}] register {} failed, code={}, reason={}", jobId, ai.uri, prm.code, prm.reason);
    }
  }

  void SipAccount::onIncomingCall(OnIncomingCallParam& iprm) {
    std::string msg = iprm.rdata.wholeMsg;
    W_LOG("Receive Call \n{}", msg);
    //取出INVITE中的From作为channelId
    std::string jobId = extractJobId(msg);

    std::regex pattern("audio(\\d{6})");
    std::smatch match;
    if (std::regex_match(jobId, match, pattern)) {
      jobId = match[1];
    }

    if (!mcu->checkJob(jobId)) {
      E_LOG("[SA] jobId {} not found", jobId);
      return;
    }

    std::string chnlId = extractChnlId(msg);
    I_LOG("[SA] get jobId {} and chnlId {}", jobId, chnlId);
    std::string dstIp;
    int dstPort;
    //取出INVITE中的SDP
    //在SDP中取出payloadType、codecType、inSampleRate、outSampleRate、dstIp、dstPort
    SDPInfo info;
    parseSDP(msg, info);
    I_LOG("[SA:{}->{}] get dst ip is {}, port is {}, pt is {}, samplerate is {}, opus is {}",
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

    auto call = std::make_unique<SipCall>(info.isOpus, *this, iprm.callId);
    call->registerSipAccount(this);
    CallInfo ci = call->getInfo();

    CallOpParam answer_prm;
    answer_prm.statusCode = PJSIP_SC_RINGING;
    call->answer(answer_prm);

    if (!mcu->addChnl(addCtx, addr)) {
      E_LOG("[SA:{}->{}] add channel faild", jobId, chnlId);
      answer_prm.statusCode = PJSIP_SC_BAD_REQUEST;
      call->answer(answer_prm);
    }
    MicCtrlContext ctx;
    ctx.jobId = jobId;
    ctx.channelId = chnlId;
    mcu->openMic(ctx);
    call->setPort(addr.port);
    answer_prm.statusCode = PJSIP_SC_OK;
    call->answer(answer_prm);
    std::lock_guard<std::mutex> lck(listLocker);
    callList.emplace(chnlId, std::move(call));
  }

  std::string SipAccount::extractChnlId(const std::string& wholeMsg) {
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

  std::string SipAccount::extractJobId(const std::string& wholeMsg) {
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

  void SipAccount::parseSDP(const std::string& wholeMsg, SDPInfo& info) {
    size_t sdp_start = wholeMsg.find("\r\n\r\n");
    if (sdp_start == std::string::npos) {
      E_LOG("find sdp failed");
    }
    std::string sdp = wholeMsg.substr(sdp_start + 4); // 跳过头部和空行
    I_LOG("Offer SDP:\n{}", sdp);
    std::istringstream iss(sdp);
    std::string line;

    size_t c_pos = sdp.find("c=");
    while (c_pos != std::string::npos) {
      // 确保c=行是独立的一行（前面为换行或位于开头）
      if (c_pos == 0 || (sdp[c_pos - 1] == '\n' && (c_pos == 1 || sdp[c_pos - 2] == '\r'))) {
        size_t end_line = sdp.find("\r\n", c_pos);
        std::string c_line = sdp.substr(c_pos, end_line - c_pos);
        I_LOG("c: {}", c_line);

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
          I_LOG("dst ip is {}", info.ip);
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
      I_LOG("audio port is {}", info.port);
    }

    while (std::getline(iss, line)) {
      // 解析Opus编码参数（a=rtpmap行）
      if (line.find("a=rtpmap:") != std::string::npos && line.find("opus/") != std::string::npos) {
        info.isOpus = true;
        size_t colonPos = line.find(':');
        size_t slashPos = line.find('/');
        if (colonPos != std::string::npos && slashPos != std::string::npos) {
          info.payloadType = std::stoi(line.substr(colonPos + 1, slashPos - colonPos - 1));
          I_LOG("get pt is {}", info.payloadType);
          size_t ratePos = line.find('/', slashPos + 1);
          if (ratePos != std::string::npos) {
            info.sampleRate = std::stoi(line.substr(slashPos + 1, ratePos - slashPos - 1));
            I_LOG("get sampleRate is {}", info.sampleRate);
          }
        }
      }
    }
  }

  SipProcessUnit::SipProcessUnit(std::string targetIp, port_t targetPort)
  : ip(targetIp), port(targetPort) {
    fn = std::bind(&SipProcessUnit::removeCallFromList, this, std::placeholders::_1);
  }

  SipProcessUnit::~SipProcessUnit() {
    this->shutdown();
    MediaControlUnit::giveInstance(mcu);
    acList.clear();
    ep.libDestroy();
  }

  void SipProcessUnit::open() {
    ep.libCreate();
    pj::EpConfig epCfg;
    epCfg.uaConfig.maxCalls = 16;
    epCfg.logConfig.level = 4;
    //epCfg.logConfig.writer = &logger;
    ep.libInit(epCfg);

    pj_status_t status = pjsua_set_null_snd_dev();
    if (status != PJ_SUCCESS) {
      I_LOG("close audio dev failed:{}", status);
      ep.libDestroy();
      return;
    }

    tcfg.port = sipPort;
    ep.transportCreate(PJSIP_TRANSPORT_UDP, tcfg);
    I_LOG("UDP transport create success, port is {}", tcfg.port);

    ep.libStart();
    I_LOG("start PJSUA2 module");

    mcuAcCfg.idUri = "sip:" + user + "@" + ip + ":" + std::to_string(port);
    mcuAcCfg.regConfig.registrarUri = "sip:" + ip + ":" + std::to_string(port);
    mcuCred = AuthCredInfo("digest", "*", user, 0, pwd);
    mcuAcCfg.sipConfig.authCreds.push_back(mcuCred);
    mcuAcCfg.mediaConfig.useLoopMedTp = true;
    create(mcuAcCfg);
    I_LOG("Sip Process Unit Register uri sip:{}@{}:{}", user, ip, port);
    mcu = MediaControlUnit::getInstance();
    isRunning = true;
    run();
  }

  void SipProcessUnit::run() {
    I_LOG("Sip Process Unit Start listen");

    while (isRunning) {
      ep.libHandleEvents(100);
    }

    E_LOG("Sip Process Unit listen Failed");
  }

  void SipProcessUnit::registerAccount(std::string userName) {
    AccountConfig acfg;
    acfg.idUri = "sip:" + userName + "@" + ip + ":" + std::to_string(port);
    acfg.regConfig.registrarUri = "sip:" + ip + ":" + std::to_string(port);
    AuthCredInfo cred("digest", "*", userName, 0, pwd);
    acfg.sipConfig.authCreds.push_back(cred);
    std::unique_ptr<SipAccount> acc = std::make_unique<SipAccount>();
    acc->create(acfg);
    acc->setRemoveCallListCallback(fn);
    acList.emplace(userName, std::move(acc));
    I_LOG("[SPU] register {} success", userName);
  }

  void SipProcessUnit::unregisterAccount(std::string userName) {
    std::string id = "audio" + userName.substr(5);
    auto it = acList.find(id);
    if (it == acList.end()) {
      E_LOG("[SPU] find {} from account list failed", id);
      return;
    }
    it->second->setRegistration(false);
    it->second->setUnregistering(true);
    I_LOG("[SPU] unregister {} start", userName);
  }

  void SipProcessUnit::removeCallFromList(std::string userName) {
    std::string id = "audio" + userName.substr(5);
    auto it = acList.find(id);
    if (it == acList.end()) {
      E_LOG("[SPU] find {} from account list failed", id);
      return;
    }
    acList.erase(id);
    W_LOG("[SPU] unregister {} success", userName);
  }
  
  void SipProcessUnit::onRegState(OnRegStateParam& prm) {
    AccountInfo ai = getInfo();
    if (ai.regIsActive) {
      I_LOG("[SPU] Register MCU Account:{} success, code={}, reason={}", ai.uri, prm.code, prm.reason);
    }
    else {
      E_LOG("[SPU] Register MCU Account:{} failed, code={}, reason={}", ai.uri, prm.code, prm.reason);
    }
  }

  void SipProcessUnit::onInstantMessage(OnInstantMessageParam& prm) {
    I_LOG("Recv Msg\n{}", prm.msgBody);
    std::string userName = prm.msgBody;
    if (userName.size() > 11) {
      userName = userName.substr(userName.length() - 11, 11);
    }
    std::regex pattern("audio(\\d{6})");
    std::regex pattern2("close(\\d{6})");
    std::smatch match, match2;

    if (std::regex_match(userName, match, pattern)) {
      registerAccount(userName);
      CreateJobContext createCtx;
      createCtx.jobId = match[1];
      createCtx.url = "empty";
      mcu->createMpu(createCtx);
    }
    else if (std::regex_match(userName, match2, pattern2)) {
      unregisterAccount(userName);
      std::string jobId = match2[1];
      mcu->endMpu(jobId);
    }
    else {
      W_LOG("[SPU] jobId {} regex failed", userName);
    }
  }
}
#include "SipProcesserUnit.h"

namespace aom {
  SipProcessUnit::SipProcessUnit(std::string targetIp, port_t targetPort)
  : ip(targetIp), port(targetPort) {}

  SipProcessUnit::~SipProcessUnit() {
    MediaControlUnit::giveInstance(mcu);
    for (auto& each : acList) {
      each->shutdown();
    }
    ep.libDestroy();
  }

  void SipProcessUnit::open() {
    ep.libCreate();
    epCfg.logConfig.level = 2;
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
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
    acList.emplace_back(std::move(acc));
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
    I_LOG("From: {}\nTo: {}\nBody: {}", prm.fromUri, prm.toUri, prm.msgBody);
    std::string userName = prm.msgBody;
    if (userName.size() > 11) {
      userName = userName.substr(userName.length() - 11, 11);
    }
    std::regex pattern("audio(\\d{6})");
    std::smatch match;

    if (std::regex_match(userName, match, pattern)) {
      registerAccount(userName);
      CreateJobContext createCtx;
      createCtx.jobId = match[1];
      createCtx.url = "empty";
      mcu->createMpu(createCtx);
    }
    else {
      W_LOG("jobId {} regex failed", userName);
    }
  }
}
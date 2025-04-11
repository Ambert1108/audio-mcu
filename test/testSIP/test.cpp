#include <iostream>
#include <pjsua2.hpp>
#include <csignal>
#include <memory>
#include <string>

#include "seeker/logger.h"
#include "seeker/loggerApi.h"

using namespace pj;

volatile std::sig_atomic_t isRunning = 1;

// 配置常量
const std::string SERVER_IP = "10.1.63.110";
const int SERVER_PORT = 5060;
const int SIP_PORT = 54321;
const std::string USERNAME = "faust";
const std::string PASSWORD = "123456";
const std::string TARGET_NUMBER = "zzx_call"; // 新增被叫号码常量

std::string getSipHeader(const SipHeaderVector& vec) {
  if (vec.empty()) {
    E_LOG("Sip Header vec is empty");
    return {};
  }
  std::string header{};
  for (const auto& each : vec) {
    header += each.hName + ": " + each.hValue + "\n";
  }
  return header;
}

// 前置声明
class MyAccount;

// 自定义Call类
class MyCall : public Call {
public:
  MyCall(Account& acc, int call_id = PJSUA_INVALID_ID) : Call(acc, call_id) {}

  void onCallState(OnCallStateParam& prm) override {
    CallInfo ci = getInfo();
    I_LOG("Call status change, current code {} from {}", ci.lastReason, ci.remoteUri);

    if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
      delete this; // 自动清理资源
    }
  }

  void onCallSdpCreated(OnCallSdpCreatedParam& prm) override {
    //if (prm.remSdp.wholeSdp.empty()) {
    //  I_LOG("local offer\n{}", prm.sdp.wholeSdp);
    //  std::string newSdp =
    //    "v=0\r\n"
    //    "o=alice 2890844526 2890844526 IN IP4 192.168.1.2\r\n"
    //    "s=-\r\n"
    //    "c=IN IP4 192.168.1.2\r\n"
    //    "t=0 0\r\n"
    //    "m=audio 4000 RTP/AVP 0\r\n";
    //  //prm.sdp.wholeSdp = newSdp;
    //}
    //else {
    //  I_LOG("local answer\n{}", prm.remSdp.wholeSdp);
    //}
  }
};

// 自定义Account类
class MyAccount : public Account {
public:
  virtual void onRegState(OnRegStateParam& prm) override {
    AccountInfo ai = getInfo();
    if (ai.regIsActive) {
      I_LOG("register {} success, code={}, reason={}", ai.uri, prm.code, prm.reason);
    }
    else {
      E_LOG("register {{} failed, code={}, reason={}", ai.uri, prm.code, prm.reason);
    }
  }

  void makeCall(const std::string& targetUri) {
    AccountInfo ai = getInfo();
    if (!ai.regIsActive) {
      E_LOG("unregister, call failed");
      return;
    }
    call = new MyCall(*this); // 使用默认的call_id
    CallOpParam prm(true);

    try {
      call->makeCall(targetUri, prm);
      I_LOG("is Calling {}", targetUri);
    }
    catch (Error& err) {
      E_LOG("Call failed:{}", err.info());
      delete call;
    }
  }

  void answerCall() {
    CallOpParam answer_prm;
    answer_prm.statusCode = PJSIP_SC_OK;
    call->answer(answer_prm);
    I_LOG("answer response\n{}", getSipHeader(answer_prm.txOption.headers));
  }

  virtual void onIncomingCall(OnIncomingCallParam& iprm) override {
    call = new MyCall(*this, iprm.callId);
    CallInfo ci = call->getInfo();
    W_LOG("Receive Call from {}\n{}", ci.remoteUri, iprm.rdata.wholeMsg);

    CallOpParam ring_prm;
    ring_prm.statusCode = PJSIP_SC_RINGING;
    call->answer(ring_prm);
    I_LOG("answer response\nreason:{}\nsdp:{}\nstatusCode:{}\ntargetUri:{}\nmsgBody:{}", ring_prm.reason, ring_prm.sdp.wholeSdp, ring_prm.statusCode,
      ring_prm.txOption.targetUri, ring_prm.txOption.msgBody);
  }

private:
  Call* call = nullptr;
};

class SipProcessUnit {
public:
  SipProcessUnit(std::string targetIp, uint16_t targetPort)
    : ip(targetIp), port(targetPort) {
  }
  ~SipProcessUnit() {
    if (mcuAccount) mcuAccount->shutdown();
    for (auto& each : acList) {
      each->shutdown();
    }
    ep.libDestroy();
  }

  void open() {
    ep.libCreate();
    epCfg.logConfig.level = 4;
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
    mcuAcCfg.callConfig.timerMinSESec = 90;
    mcuAcCfg.callConfig.timerMinSESec = 1800;
    mcuCred = AuthCredInfo("digest", "*", user, 0, pwd);
    mcuAcCfg.sipConfig.authCreds.push_back(mcuCred);
    mcuAccount = std::make_unique<MyAccount>();
    mcuAccount->create(mcuAcCfg);
    I_LOG("Sip Process Unit Register uri sip:{}@{}:{}", user, ip, port);
    isRunning = true;
    run();
  }

private:
  std::string ip;
  uint16_t port;
  uint16_t sipPort = SIP_PORT;
  std::string user = USERNAME;
  std::string pwd = PASSWORD;

  pj::Endpoint ep;
  pj::EpConfig epCfg;
  pj::TransportConfig tcfg;

  AccountConfig mcuAcCfg;
  AuthCredInfo mcuCred;
  std::unique_ptr<MyAccount> mcuAccount;
  std::vector<std::unique_ptr<MyAccount>> acList{};
  bool isRunning{ false };

  void run() {
    I_LOG("Sip Process Unit Start listen");
    while (isRunning) {
      ep.libHandleEvents(100);
    }
    E_LOG("Sip Process Unit listen Failed");
  }
  void registerAccount(std::string userName) {
    AccountConfig acfg;
    acfg.idUri = "sip:" + userName + "@" + ip + ":" + std::to_string(port);
    acfg.regConfig.registrarUri = "sip:" + ip + ":" + std::to_string(port);
    AuthCredInfo cred("digest", "*", userName, 0, pwd);
    acfg.sipConfig.authCreds.push_back(cred);
    std::unique_ptr<MyAccount> acc = std::make_unique<MyAccount>();
    acc->create(acfg);
    acList.emplace_back(std::move(acc));
  }
};

int main() {
  //1.
 std::unique_ptr<SipProcessUnit> spu = std::make_unique<SipProcessUnit>(SERVER_IP, SERVER_PORT);
 spu->open();

  //2.
  //Endpoint ep;
  //try {
  //  ep.libCreate();
  //  EpConfig ep_cfg;
  //  ep_cfg.logConfig.level = 4;
  //  ep.libInit(ep_cfg);
  //
  //  pj_status_t status = pjsua_set_null_snd_dev();
  //  if (status != PJ_SUCCESS) {
  //    std::cerr << "禁用音频设备失败: " << status << std::endl;
  //    ep.libDestroy();
  //    return 1;
  //  }
  //
  //  TransportConfig tcfg;
  //  tcfg.port = SIP_PORT;
  //  ep.transportCreate(PJSIP_TRANSPORT_UDP, tcfg);
  //  I_LOG("UDP transport create success, port is {}", tcfg.port);
  //
  //  ep.libStart();
  //  I_LOG("start PJSUA2 module");
  //
  //  AccountConfig acfg;
  //  acfg.idUri = "sip:" + USERNAME + "@" + SERVER_IP + ":" + std::to_string(SERVER_PORT);
  //  acfg.regConfig.registrarUri = "sip:" + SERVER_IP + ":" + std::to_string(SERVER_PORT);
  //  AuthCredInfo cred("digest", "*", USERNAME, 0, PASSWORD);
  //  acfg.sipConfig.authCreds.push_back(cred);
  //
  //  MyAccount acc;
  //  acc.create(acfg);
  //
  //  AccountConfig acfg2;
  //  acfg2.idUri = "sip:hello@" + SERVER_IP + ":" + std::to_string(SERVER_PORT);
  //  acfg2.regConfig.registrarUri = "sip:" + SERVER_IP + ":" + std::to_string(SERVER_PORT);
  //  AuthCredInfo cred2("digest", "*", "hello", 0, PASSWORD);
  //  acfg2.sipConfig.authCreds.push_back(cred2);
  //
  //  MyAccount acc2;
  //  acc2.create(acfg2);
  //
  //  while (isRunning) {
  //    char option[10];
  //
  //    puts("Press 'h' to hangup all calls, 'q' to quit");
  //    if (fgets(option, sizeof(option), stdin) == NULL) {
  //      puts("EOF while reading stdin, will quit now..");
  //      break;
  //    }
  //    if (option[0] == 'c')
  //      acc.makeCall("sip:" + TARGET_NUMBER + "@" + SERVER_IP + ":" + std::to_string(SERVER_PORT));
  //
  //    if (option[0] == 'a')
  //      acc.answerCall();
  //
  //    if (option[0] == 'q')
  //      break;
  //
  //    if (option[0] == 'h')
  //      pjsua_call_hangup_all();
  //    ep.libHandleEvents(100);
  //  }
  //
  //  I_LOG("Clean up resource");
  //  acc.shutdown();
  //  ep.libDestroy();
  //}
  //catch (Error& err) {
  //  E_LOG("Catch exception:{}", err.info());
  //  ep.libDestroy();
  //  return 1;
  //}
  return 0;
}
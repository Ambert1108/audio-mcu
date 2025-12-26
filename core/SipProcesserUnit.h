// @brief: Sip消息处理单元
// @copyright: Copyright seekloud 2025
// @birth: [Ambert@2025.4.8]
// @version: V0.0.1
// @revision: [Ambert@2025.4.8]

#pragma once

#define PJSUA_MEDIA_HAS_PJMEDIA       0

#include <pjsua2.hpp>
extern "C" {
#include "pjsip.h"
#include "pjsua.h"
}

#include "Config.h"
#include "MediaControlUnit.h"
#include "Message.h"
#include "utils/InvokeTimer.hpp"

#include "seeker/common.h"
#include "seeker/logger.h"
#include "seeker/loggerApi.h"

namespace aom {
  using namespace pj;

  class SipLogger : public LogWriter {
  public:
    void write(const pj::LogEntry& entry) override {
      if (entry.level <= 2) {  // 过滤SIP消息日志
        I_LOG("SIP Message\n{}", entry.msg);
      }
    }
  };

  class SipAccount;

  class SipCall : public Call {
  public:
    SipCall(bool opus, Account& acc, int call_id = PJSUA_INVALID_ID);

    ~SipCall();

    void registerSipAccount(SipAccount* val);

    void setId(const std::string& jobId, const std::string& chnlId);

    void setPort(port_t val);

    void onCallState(OnCallStateParam& prm) override;

    void onCallTsxState(OnCallTsxStateParam& prm) override;

    void onCallSdpCreated(pj::OnCallSdpCreatedParam& prm) override;

  private:
    SipAccount* account = nullptr;
    //pj_caching_pool cp;
    //pj_pool_t* pool = nullptr;
    const std::string listenIp = seeker::IniConfig::Get("media", "nat_ip", "0.0.0.0");
    port_t listenPort = -1;
    std::string jobId{};
    std::string chnlId{};
    bool isOpus = false;
  };

  struct SDPInfo {
    std::string ip;
    int port = 0;
    int payloadType = -1;
    int sampleRate = 0;
    bool isOpus = false;
  };

  typedef std::function<void(std::string userName)> RemoveCallList;

  // 自定义Account类
  class SipAccount : public Account {
  public:
    SipAccount();

    ~SipAccount();

    bool closeChannel(const std::string& msg);

    bool closeChannel(const std::string& jobId, const std::string& chnlId);

    bool updateChannelDestition(const std::string& msg);

    void setRemoveCallListCallback(RemoveCallList func);

    void setUnregistering(bool val);

    void setJoinErr();
    void setLeaveErr();

    virtual void onRegState(OnRegStateParam& prm) override;

    virtual void onIncomingCall(OnIncomingCallParam& iprm) override;

  private:
    mutable std::mutex listLocker{};
    std::map<std::string, std::unique_ptr<SipCall>> callList{};
    MediaControlUnit* mcu = nullptr;
    std::string jobId{};
    RemoveCallList callback = nullptr;
    bool isUnregistering = false;

    std::string extractChnlId(const std::string& wholeMsg);

    std::string extractJobId(const std::string& wholeMsg);

    void parseSDP(const std::string& wholeMsg, SDPInfo& info);
  };

  using RemoveCallFunc = RemoveCallList;
  class SipProcessUnit : public SipAccount {
  public:
    SipProcessUnit(std::string targetIp, port_t targetPort);
    ~SipProcessUnit();
    void open();

  private:
    std::string ip;
    port_t port;
    port_t sipPort = seeker::IniConfig::GetInteger("main", "sip_port", 54321);
    std::string user = seeker::IniConfig::Get("main", "user", "faust");
    std::string pwd = seeker::IniConfig::Get("main", "pwd", "123456");

    pj::Endpoint ep;
    pj::EpConfig epCfg;
    pj::TransportConfig tcfg;
    SipLogger logger;

    AccountConfig mcuAcCfg;
    AuthCredInfo mcuCred;
    std::map<std::string, std::unique_ptr<SipAccount>> acList{};
    MediaControlUnit* mcu = nullptr;
    Message msg;
    RemoveCallFunc fn = nullptr;
    bool isRunning { false };

    void run();
    void registerAccount(std::string userName);
    void unregisterAccount(std::string userName);
    void removeCallFromList(std::string userName);

    void onRegState(OnRegStateParam& prm) override;
    void onInstantMessage(OnInstantMessageParam& prm) override;
  };
}

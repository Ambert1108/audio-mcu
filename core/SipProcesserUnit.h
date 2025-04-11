// @brief: Sip消息处理单元
// @copyright: Copyright seekloud 2025
// @birth: [Ambert@2025.4.8]
// @version: V0.0.1
// @revision: [Ambert@2025.4.8]

#pragma once

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

  class SipCall : public Call {
  public:
    SipCall(Account& acc, int call_id = PJSUA_INVALID_ID) : Call(acc, call_id) {
      pj_caching_pool_init(&cp, NULL, 0);
      pool = pj_pool_create(&cp.factory, "answerSdp", 4096, 4096, NULL);
      if (!pool) {
        E_LOG("create pool failed");
      }
    }

    ~SipCall() {
      if (pool) {
        pj_pool_release(pool);
      }
    }

    void setPort(port_t val) {
      listenPort = val;
    }

    void onCallState(OnCallStateParam& prm) override {
      CallInfo ci = getInfo();
      I_LOG("[SipCall] status change, current code {} from {}", ci.lastReason, ci.remoteUri);

      if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
        delete this; // 自动清理资源
      }
    }

    void onCallTsxState(OnCallTsxStateParam& prm) override {
      //I_LOG("Send Msg\n{}", prm.e.body.rxMsg.rdata.wholeMsg);
    }

    void onCallSdpCreated(pj::OnCallSdpCreatedParam& prm) override {
      I_LOG("raw sdp:\n{}", prm.sdp.wholeSdp);

      // 重新组合为完整 SDP 字符串
      std::string newSdp =
        "v=0\r\n"
        "o=- 3953192465 3953192466 IN IP4 10.1.69.7\r\n"
        "s=pjmedia\r\n"
        "c=IN IP4 10.1.69.7\r\n"
        "b=AS:84\r\n"
        "t=0 0\r\n"
        "a=X-nat:0\r\n"
        "m=audio " + std::to_string(listenPort) + " RTP/AVP 8\r\n"
        "a=rtcp:4001 IN IP4 10.1.69.7\r\n"
        "a=ssrc:766044304 cname:7b8072591a0e9c5a\r\n"
        "a=rtpmap:8 PCMA/8000\r\n"
        "a=rtpmap:121 telephone-event/8000\r\n"
        "a=fmtp:121 0-16\r\n"
        "a=a=rtcp-fb:* ccm tmmbr\r\n";

      pj::SdpSession newSdpSession;

      pjmedia_sdp_session* parsedSdp = NULL;
      pj_status_t status = pjmedia_sdp_parse(pool, const_cast<char*>(newSdp.c_str()), newSdp.size(), &parsedSdp);
      if (status != PJ_SUCCESS) {
        I_LOG("status: {}", status);
        I_LOG("parse sdp failed");
        return;
      }

      newSdpSession.fromPj(*parsedSdp);
      prm.sdp = newSdpSession;

      I_LOG("modify sdp:\n{}", prm.sdp.wholeSdp);
    }

  private:
    pj_caching_pool cp;
    pj_pool_t* pool = nullptr;
    port_t listenPort = -1;
  };

  struct SDPInfo {
    std::string ip;
    int port = 0;
    int payloadType = -1;
    int sampleRate = 0;
  };

  // 自定义Account类
  class SipAccount : public Account {
  public:
    SipAccount() {
      mcu = MediaControlUnit::getInstance();
    }

    ~SipAccount() {
      if (mcu) {
         MediaControlUnit::giveInstance(mcu);
      }
    }

    virtual void onRegState(OnRegStateParam& prm) override {
      AccountInfo ai = getInfo();
      if (ai.regIsActive) {
        I_LOG("register {} success, code={}, reason={}", ai.uri, prm.code, prm.reason);
      }
      else {
        E_LOG("register {{} failed, code={}, reason={}", ai.uri, prm.code, prm.reason);
      }
    }

    virtual void onIncomingCall(OnIncomingCallParam& iprm) override {
      std::string msg = iprm.rdata.wholeMsg;
      W_LOG("Receive Call \n{}", msg);
      //取出INVITE中的From作为channelId
      std::string jobId = extractJobId(msg);
      std::regex pattern("audio(\\d{6})");
      std::smatch match;

      if (std::regex_match(jobId, match, pattern)) {
        jobId = match[1];
      }
      std::string chnlId = extractChnlId(msg);
      I_LOG("get jobId {} and chnlId {}", jobId, chnlId);
      auto call = std::make_unique<SipCall>(*this, iprm.callId);
      CallInfo ci = call->getInfo();

      CallOpParam answer_prm;
      answer_prm.statusCode = PJSIP_SC_RINGING;
      call->answer(answer_prm);


      std::string dstIp;
      int dstPort;
      //取出INVITE中的SDP
      //在SDP中取出payloadType、codecType、inSampleRate、outSampleRate、dstIp、dstPort
      SDPInfo info;
      parseSDP(msg, info);
      I_LOG("get dst ip is {}, port is {}, pt is {}, samplerate is {}", 
        info.ip, info.port, info.payloadType, info.sampleRate);
      AddChnlContext addCtx;
      ListenAddr addr;
      addCtx.jobId = jobId;
      addCtx.chnlId = chnlId;
      addCtx.codecType = 1;
      addCtx.dstIp = info.ip;
      addCtx.dstPort = info.port;
      //addCtx.payloadType = info.payloadType;
      //addCtx.inSampleRate = info.sampleRate;
      //addCtx.outSampleRate = info.sampleRate;

      //暂时写死为PCMA
      addCtx.payloadType = 8;
      addCtx.inSampleRate = 8000;
      addCtx.outSampleRate = 8000;
      if (!mcu->addChnl(addCtx, addr)) {
        E_LOG("[SPU {}->{}] add channel faild", jobId, chnlId);
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
      callList.emplace(chnlId, std::move(call));
    }

  private:
    std::map<std::string, std::unique_ptr<SipCall>> callList{};
    MediaControlUnit* mcu = nullptr;

    std::string extractChnlId(const std::string& wholeMsg) {
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

    std::string extractJobId(const std::string& wholeMsg) {
      // 查找 "From:" 行
      size_t fromPos = wholeMsg.find("To:");
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

    void parseSDP(const std::string& wholeMsg, SDPInfo& info) {
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
  };

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
    std::vector<std::unique_ptr<SipAccount>> acList{};
    MediaControlUnit* mcu = nullptr;
    Message msg;
    bool isRunning { false };

    void run();
    void registerAccount(std::string userName);

    void onRegState(OnRegStateParam& prm) override;
    void onInstantMessage(OnInstantMessageParam& prm) override;
  };
}

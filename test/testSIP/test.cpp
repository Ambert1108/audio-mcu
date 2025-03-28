#include <pjsua2.hpp>
#include <iostream>
#include <unistd.h>
#include <termios.h>
#include <cerrno>
#include <cstdlib>

using namespace pj;

// Subclass to extend the Account and get notifications etc.
class MyAccount : public Account {
public:
  virtual void onRegState(OnRegStateParam& prm) {
    AccountInfo ai = getInfo();
    std::cout << (ai.regIsActive ? "*** Register:" : "*** Unregister:")
      << " code=" << prm.code << std::endl;
  }
};

// 设置终端为非规范模式并关闭回显
void setNonCanonicalMode(bool enable) {
  static struct termios oldt, newt;
  if (enable) {
    tcgetattr(STDIN_FILENO, &oldt);  // 保存当前终端设置
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO); // 关闭规范模式和回显
    newt.c_cc[VMIN] = 0;   // 读取最小字符数（非阻塞模式）
    newt.c_cc[VTIME] = 0;  // 超时时间（立即返回）
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  }
  else {
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); // 恢复原始终端设置
  }
}

int main()
{
  bool isRunning = false;
  Endpoint ep;

  ep.libCreate();

  // Initialize endpoint
  EpConfig ep_cfg;
  ep.libInit(ep_cfg);

  // Create SIP transport. Error handling sample is shown
  TransportConfig tcfg;
  tcfg.port = 5060;
  try {
    ep.transportCreate(PJSIP_TRANSPORT_UDP, tcfg);
  }
  catch (Error& err) {
    std::cout << err.info() << std::endl;
    return 1;
  }

  // Start the library (worker threads etc)
  ep.libStart();
  std::cout << "*** PJSUA2 STARTED ***" << std::endl;

  // Configure an AccountConfig
  AccountConfig acfg;
  acfg.idUri = "sip:changjinglu@10.1.63.111:5060";
  acfg.regConfig.registrarUri = "sip:10.1.63.111:5060";
  AuthCredInfo cred("digest", "*", "changjinglu", 0, "123456");
  acfg.sipConfig.authCreds.push_back(cred);

  setNonCanonicalMode(true); // 进入非规范模式

  while (!isRunning) {
    char c;
    int bytesRead = read(STDIN_FILENO, &c, 1); // 尝试读取一个字符
    if (bytesRead == 1) {
      if (c == 's') {
        isRunning = true;
      }
    }
    pj_thread_sleep(10);
  }

  // Create the account
  MyAccount* acc = new MyAccount;
  acc->create(acfg);

  // Here we don't have anything else to do..
  while (isRunning) {
    char c;
    int bytesRead = read(STDIN_FILENO, &c, 1); // 尝试读取一个字符
    if (bytesRead == 1) {
      if (c == 'q') {
        isRunning = false;
      }
    }
    pj_thread_sleep(10);
  }


  // Delete the account. This will unregister from server
  delete acc;

  // This will implicitly shutdown the library
  return 0;
}
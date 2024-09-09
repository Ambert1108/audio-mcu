/**
@project rtpTrs
@author Tao Zhang
@since 2023/3/13
@version 0.2.2 2023/6/15
*/



#pragma once

#include "config.h"

#include "seeker/common.h"

#include "Rtp.hpp"

#include "RtpPipe.hpp"

#include "Rtcp.hpp"

#include "UdpReceiver.hpp"
#include "UdpSender.hpp"

#include "asio.hpp"

#include <vector>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <chrono>
#include <condition_variable>

namespace seeker {

namespace rtp {

using RawData = std::vector<uint8_t>;
using Endpoint = asio::ip::udp::endpoint;
using udp = asio::ip::udp;

namespace {


void keepSocketIo(const asio::error_code& err, asio::steady_timer* timer, int intervalInMs) {
  if (err) {
    W_LOG("RtpTransceiver::keepSocketIo ABORT: {}", err.message());
    return;
  }
  D_LOG("keepSocketIo is wake up.");
  timer->expires_from_now(std::chrono::milliseconds(intervalInMs));
  auto func = std::bind(keepSocketIo, std::placeholders::_1, timer, intervalInMs);
  timer->async_wait(func);
}


}  // namespace



template <typename DATA>
class OneOnePipe {
 private:
  const uint16_t capacity;
  DATA* internalArray;
  std::atomic<uint16_t> headPos;
  std::atomic<uint16_t> tailPos;

 public:
  OneOnePipe(uint16_t c)
      : capacity(c), internalArray(new DATA[capacity]), headPos(0), tailPos(0) {
    // memset(internalArray, 0, capacity * sizeof(DATA));
  }

  OneOnePipe(const OneOnePipe& o) = delete;
  OneOnePipe& operator=(const OneOnePipe& o) = delete;

  ~OneOnePipe() {
    if (internalArray != nullptr) {
      delete[] internalArray;
      internalArray = nullptr;
    }
  }


  // only producer thread
  bool enqueue(DATA&& input) {
    uint16_t head = headPos.load();
    uint16_t tail = tailPos.load();
    uint16_t nextTail = nextPos(tail);
    if (nextTail == head) {  // full
      return false;
    } else {
      internalArray[tail] = std::move(input);
      tailPos.store(nextTail);
      return true;
    }
  }

  // only consumer thread
  bool dequeue(DATA& output) {
    uint16_t head = headPos.load();
    uint16_t tail = tailPos.load();
    if (head == tail) {  // empty
      return false;
    } else {
      output = std::move(internalArray[head]);
      uint16_t nextHead = nextPos(head);
      headPos.store(nextHead);
      return true;
    }
  }

  inline bool empty() {
    // consumer and producer
    return tailPos == headPos;
  }

  inline bool full() {  // 最多容纳 capacity - 1 个元素
    uint16_t head = headPos.load();
    uint16_t nextTail = nextPos(tailPos.load());
    return nextTail == head;
  }

  inline uint8_t size() {
    uint16_t head = headPos.load();
    uint16_t tail = tailPos.load();
    if (tail >= head) {
      return tail - head;
    } else {
      return (capacity - head) + tail;
    }
  }


  // only consumer thread
  void dequeueAll(std::vector<DATA>& outputs) {
    uint16_t head = headPos.load();
    uint16_t tail = tailPos.load();
    while (head != tail) {
      outputs.emplace_back(std::move(internalArray[head]));
      head = nextPos(head);
    }
    headPos.store(head);
  }

  // only consumer thread
  void dequeueAll(std::deque<DATA>& outputs) {
    uint16_t head = headPos.load();
    uint16_t tail = tailPos.load();
    while (head != tail) {
      outputs.emplace_back(std::move(internalArray[head]));
      head = nextPos(head);
    }
    headPos.store(head);
  }


 private:
  inline uint16_t nextPos(uint16_t pos) {
    int rst = pos + 1;
    return static_cast<uint16_t>(rst % capacity);
  }
};

template <typename T>
class Received {
 public:
  Received() : element(), from(){};

  Received(T&& element, const Endpoint& from) : element(std::move(element)), from(from){};

  Received(const Received& o) = delete;

  Received& operator=(const Received& o) noexcept = delete;

  Received& operator=(Received&& o) noexcept {
    element = std::move(o.element);
    from = std::move(o.from);
    return *this;
  }

  Received(Received&& o) noexcept : Received(std::move(o.element), o.from){};

  T element;
  Endpoint from;
};



class RtpTransceiver;

class Notifier {
 public:
  void notify() { condition.notify_all(); }

  bool waitNotify(int ms) {
    std::unique_lock<std::mutex> lk{mx};
    auto rst = condition.wait_for(lk, std::chrono::milliseconds(ms));
    if (rst == std::cv_status::timeout) {
      return false;
    } else {
      return true;
    }
  }

 protected:
  std::mutex mx;
  std::condition_variable condition;
};

struct StreamKey {
  uint32_t ssrc;
  uint8_t pt;
  StreamKey(uint32_t ssrc, uint8_t pt) : ssrc(ssrc), pt(pt){};

  // 作为 key 必须包含`==`函数
  bool operator==(const StreamKey& o) const { return ssrc == o.ssrc && pt == o.pt; }


  bool operator!=(const StreamKey& o) const { return ssrc != o.ssrc || pt != o.pt; }
};

struct StreamKeyHash {
  std::size_t operator()(const StreamKey& myKey) const {
    uint64_t target = myKey.ssrc;
    target = (target << 8) | myKey.pt;
    return std::hash<uint64_t>()(target);
  }
};



class RtpTransceiver {
 private:  // static private
  inline static std::atomic<bool> isInited{false};
  inline static std::mutex masterMx{};
  inline static asio::io_context socketIoCtx{};
  inline static std::vector<std::thread> socketThreads{};
  inline static asio::steady_timer socketIoKeeper{socketIoCtx};

  inline static int maxPort;
  inline static int portRange;

  class RtpProcessor : public UdpProcessor {
   public:
    RtpProcessor(RtpTransceiver& parent) : p(parent){};

    inline bool pushUdp(RawData&& udp, const udp::endpoint& from) override;

   private:
    RtpTransceiver& p;
  };


  class RtcpProcessor : public UdpProcessor {
   public:
    RtcpProcessor(RtpTransceiver& parent) : p(parent){};
    inline bool pushUdp(RawData&& udp, const udp::endpoint& from) override;

   private:
    RtpTransceiver& p;
  };



 public:  // static API
          /*
           * 初始化网络监听线程池
           *  - `threadNum`: 内部监听网络的线程数量.
           *  - init函数为静态函数, 需要在创建任何`RtpTranserver`对象之前, 全局调用一次,
           * 也仅能调用一次.
           */
  inline static void init(int threadCount = 4) {
    if (!socketThreads.empty() || isInited) {
      throw std::logic_error("RtpTransceiver init: !socketThreads.empty() || isInited");
    }

    std::lock_guard<std::mutex> lg{masterMx};
    if (!socketThreads.empty() || isInited) {
      throw std::logic_error("RtpTransceiver init: !socketThreads.empty() || isInited");
    }

    std::error_code OK;
    keepSocketIo(OK, &socketIoKeeper, 1000 * 30);

    for (int i = 0; i < threadCount; i++) {
      std::thread t([&]() {
        try {
          socketIoCtx.run();
        } catch (std::exception& ex) {
          std::cerr << "It should never happend !!!!" << std::endl;
        }
        W_LOG("traceId[{}] socketThreads stopped.");
      });
      socketThreads.emplace_back(std::move(t));
    }


    isInited.store(true);
  }

  /*
   * 关闭整个 RtpTransceiver 系统
   * 调用该方法前，必须对所有 rtpTransceiver 对象调用 close, 并进行析构。
   * 该方法应该仅在程序结束时调用
   */
  inline static void shutdown() {
    if (!isInited.load()) {
      throw std::logic_error("RtpTransceiver shutdown: isInited is false");
    }
    std::lock_guard<std::mutex> lg{masterMx};
    if (!isInited.load()) {
      throw std::logic_error("RtpTransceiver shutdown: isInited is false");
    }


    socketIoKeeper.cancel();
    socketIoCtx.stop();

    std::hash<std::thread::id> hasher;

    for (auto& t : socketThreads) {
      int64_t id = hasher(t.get_id());
      W_LOG("socketThreads join: {}", id);
      t.join();
    }

    socketThreads.clear();

    isInited.store(false);
  }



  inline static Endpoint makeEndpoint(const std::string& ip, const uint16_t port) {
    return Endpoint{asio::ip::make_address(ip), port};
  }

 public:  // API
          /*
           * 创建`RtpTranserver`对象
           * - `traceId`: 日志打印时的跟踪标识, 对实际功能没有影响, 也没有任何限制.
           * - `reorderSize`: rtp包乱序重排的范围, 即超过此范围的乱序包, 将被丢弃.
           * - `leadingReorderSize`: 对于每路流的开头RTP包的重排个数, 这些包将被缓存,
           * 当到达该数目之后才会真正进行处理
           */
  inline RtpTransceiver(const std::string& traceId, uint16_t reorderSize,
                        uint16_t leadingReorderSize = 0);

  RtpTransceiver(const RtpTransceiver& o) = delete;

  RtpTransceiver& operator=(const RtpTransceiver& o) = delete;

  ~RtpTransceiver() { close(); }

  /*
   * 绑定本地UDP地址(IP和端口)
   * - 返回值为0, 表示成功, 其他则表示绑定失败.
   * - `localIp`: 要绑定的本地IP地址, 若要监听全部地址则设置为"0.0.0.0"
   * - `localPort`: 要绑定的UDP端口
   * - `sendOnly`: 如果为 true, 则将跳过所有接收流程，不提供接收能力
   * - `enableRtcp`: 如果为 true, 则启动Rtcp收发能力
   * - 绑定成功之后, `transceiver`就自动开始接收绑定端口发来的数据了.
   */
  inline int open(const std::string& localIp, uint16_t localPort, bool sendOnly = false);



  /*
   * 绑定本地UDP地址(IP和端口)
   * - 返回值为0, 表示成功, 其他则表示绑定失败.
   * - `localIp`: 要绑定的本地IP地址, 若要监听全部地址则设置为"0.0.0.0"
   * - `localPort`: 要绑定的UDP端口
   * - `sendOnly`: 如果为 true, 则将跳过所有接收流程，不提供接收能力
   * - `localRtcpPort`: 如果为 localRtcpPort > 0, 则启动Rtcp收发能力
   * - 绑定成功之后, `transceiver`就自动开始接收绑定端口发来的数据了.
   */
  inline int open2(const std::string& localIp, uint16_t localRtpPort,
                   uint16_t localRtcpPort = 0, bool sendOnly = false);


  inline void setRtpNotifier(std::shared_ptr<Notifier> notifier) { rtpNotifier = notifier; };

  inline void setRtcpNotifier(std::shared_ptr<Notifier> notifier) { rtcpNotifier = notifier; };

  inline void setUdpNotifier(std::shared_ptr<Notifier> notifier) { udpNotifier = notifier; };


  inline void setUnkonwnMsgNotifier(std::shared_ptr<Notifier> notifier) {
    setUdpNotifier(notifier);
  };

  inline void setDestination(const std::string& ip, uint16_t port) {
    dstAddrs.clear();
    addDestination(ip, port);
  }

  inline void addDestination(const std::string& ip, uint16_t port);

  inline void removeDestination(const std::string& ip, uint16_t port);

  // 2023年9月23日
  inline void removeAllDestinations();

  inline std::vector<std::pair<std::string, uint16_t>> listDestinations() const;


  // 2023年5月1日
  inline size_t destinationCount() const { return dstAddrs.size(); }

  /*
   * 发送RTP包
   * - `rtps`: 待发送的若干个RTP包组成的队列, 函数返回时`rtps`队列将为空.
   * - 发送的目的地址, 需要在调用`sendRtp()`函数前, 通过调用`void setDestination(string ip,
   * uint16_t port)`函数指定, 若目的地址不发生改变, 只需要指定一次即可.
   */
  inline void sendRtp(std::deque<Rtp>& rtps);

  /*
   * 发送RTP包, 绕开已经设置的destination, 指定目的地址发送RTP包
   */
  inline void sendRtp(std::deque<Rtp>& rtps, const std::string& dstIp, uint16_t dstPort);

  /*
   * 发送RTP包, 绕开已经设置的destination, 指定目的地址发送RTP包
   */
  inline void sendRtp(std::deque<Rtp>& rtps, const Endpoint& dst);

  /*
   * 发送RTP包, 绕开已经设置的destination, 指定多个目的地址发送RTP包
   */
  inline void sendRtp(std::deque<Rtp>& rtps, const std::vector<Endpoint>& multiAddrs);

  /*
   * 发送UDP包, 绕开已经设置的destination, 指定目的地址发送UDP包
   */
  inline void sendUdp(std::deque<std::vector<uint8_t>>& udps, const std::string& dstIp,
                      uint16_t dstPort);

  inline void sendUdp(std::deque<std::vector<uint8_t>>& udps, const Endpoint& dst);


  /*
   * 发送RTCP包, 绕开已经设置的 destination, 指定目的地址发送RTCP包
   */
  inline void sendRtcp(std::deque<Rtcp>& rtcp, const std::string& dstIp, uint16_t dstPort);

  inline void sendRtcp(std::deque<Rtcp>& rtcp, const Endpoint& dst);

  /*
   * 关闭`transceiver`
   * - 该函数为阻塞函数, 会等待`transceiver`内部的`socket`资源以及工作线程,
   * 结束之后才会进行返回
   * - 经过测试`close`函数耗时在`2ms`左右
   */
  inline void close();

  // 2023年12月25日
  inline void resetStreams() {
    std::scoped_lock lg(pipeMapMx, pipeNeedToRemoveMx);
    for (auto& it : rtpPipeMap) {
      auto& key = it.first;
      D_LOG("traceId[{}] resetStreams rtpPIpe ssrc={} pt={}", traceId_, key.ssrc, key.pt);
      rtpPipeNeedToRemove.push_back(key);
    }
  }


  // 2024年1月16日
  inline void resetStream(const StreamKey& streamKey) {
    D_LOG("traceId[{}] resetStream rtpPIpe ssrc={} pt={}", traceId_, streamKey.ssrc,
          streamKey.pt);
    std::lock_guard<std::mutex> lg{pipeNeedToRemoveMx};
    rtpPipeNeedToRemove.push_back(streamKey);
  }

  // 2023年12月25日
  inline void resetStream(uint32_t ssrc, uint8_t pt) {
    StreamKey key{ssrc, pt};
    resetStream(key);
  }


  /*
   * 获取接收到的RTP数据
   * - `queue`: `transceiver`接收到的RTP包, 将全部填充到`queue`中.
   * - 传入的`queue`必须为空队列, 即`queue.empty() == true`
   */
  inline void receiveRtp(std::deque<Received<Rtp>>& queue);

  /*
   * 获取接收到的RTCP数据
   * - `queue`: `transceiver`接收到的RTCP包, 将全部填充到`queue`中.
   * - 传入的`queue`必须为空队列, 即`queue.empty() == true`
   */
  inline void receiveRtcp(std::deque<Received<Rtcp>>& queue);


  /*
   * 对于RTP解析失败的包
   * - `queue`: `transceiver`接收到的UDP中, 不符合RTP规范的数据包, 将全部填充到`queue`中.
   * - 传入的`queue`必须为空队列, 即`queue.empty() == true`
   * - 不符合`RTP规范`的含义: 可能是无法成功解析出RTP header,
   * - 在recvUdpOnly模式下, 会跳过RTP解析过程, 所有的Udp包都会直接在这个队列
   */
  inline void receiveUdp(std::deque<Received<RawData>>& queue);

  // deprecated
  // 与 receiveUdp 功能完全相同, 底层直接调用 receiveUdp 函数, 请使用 receiveUdp.
  inline void receiveUnknownMsg(std::deque<Received<RawData>>& queue) { receiveUdp(queue); }


  // inline void setRecvPayloadType(uint16_t pt) { pt_for_recv_ = pt; }


  // deprecated
  // inline void setRecvSsrcAndPayloadType(int64_t ssrc, uint16_t pt) {
  //  ssrc_for_recv_ = ssrc;
  //  pt_for_recv_ = pt;
  //  firstRtpGot = true;
  //}

  // deprecated
  // inline void setSkipSsrcChcek(bool v) { skipSsrcCheck = v; };

  // deprecated
  // inline void setSkipPtChcek(bool v) { skipPtCheck = v; };

  inline size_t udpQueueSize() {
    std::lock_guard<std::mutex> lg{recvUdpQueueMx};
    return recvUdpQueue.size();
  }


  inline size_t rtpQueueSize() {
    std::lock_guard<std::mutex> lg{recvRtpQueueMx};
    return recvRtpQueue.size();
  }

  inline size_t unknownMsgQueueSize() { return udpQueueSize(); }

  inline std::vector<StreamKey> getStreams() {
    std::vector<StreamKey> keys{};
    {
      std::lock_guard<std::mutex> lg{pipeMapMx};
      keys.reserve(rtpPipeMap.size());
      for (auto& kv : rtpPipeMap) {
        keys.push_back(kv.first);
      }
    }
    return keys;
  }

  inline SeqCounter getSeqCounter(const StreamKey& streamKey) {
    RtpPipe* currentPipe = nullptr;
    {
      std::lock_guard<std::mutex> lg{pipeMapMx};
      auto it = rtpPipeMap.find(streamKey);
      if (it != rtpPipeMap.end()) {
        currentPipe = &it->second;
      }
    }

    if (currentPipe == nullptr) {
      return SeqCounter();
    } else {
      return currentPipe->getSeqCounter();
    }
  }


  inline SeqCounter getSeqCounter(uint32_t ssrc, uint8_t pt) {
    StreamKey steamKey{ssrc, pt};
    return getSeqCounter(steamKey);
  }


  inline StreamStatus getStreamStatus(const StreamKey& streamKey) {
    RtpPipe* currentPipe = nullptr;
    {
      std::lock_guard<std::mutex> lg{pipeMapMx};
      auto it = rtpPipeMap.find(streamKey);
      if (it != rtpPipeMap.end()) {
        currentPipe = &it->second;
      }
    }

    if (currentPipe == nullptr) {
      return StreamStatus();
    } else {
      return currentPipe->getStatus();
    }
  }


  inline StreamStatus getStreamStatus(uint32_t ssrc, uint8_t pt) {
    StreamKey streamKey{ssrc, pt};
    return getStreamStatus(streamKey);
  }

  // deprecated
  // inline SeqCounter getSeqCounter() {
  //  std::lock_guard<std::mutex> lg{pipeMx};
  //  return rtpPipe.getSeqCounter();
  //}

  // deprecated
  // inline StreamStatus getStreamStatus() {
  //  std::lock_guard<std::mutex> lg{pipeMx};
  //  return rtpPipe.getStatus();
  //}

  // 2023年5月1日
  inline bool isBinding() const { return isBinded; }

  // 2023年5月1日
  inline std::string getLocalRtpAddr() {
    if (isBinded) {
      return rtpSocket_.local_endpoint().address().to_string();
    } else {
      throw std::logic_error("not binded.");
    }
  }

  // 2023年5月1日
  inline uint16_t getLocalRtpPort() {
    if (isBinded) {
      return rtpSocket_.local_endpoint().port();
    } else {
      throw std::logic_error("not binded.");
    }
  }

  // 2023年7月24日
  inline std::string getLocalRtcpAddr() {
    if (isBinded && enableRtcp_) {
      return rtcpSocket_.local_endpoint().address().to_string();
    } else {
      throw std::logic_error("not binded.");
    }
  }

  // 2023年7月24日
  inline uint16_t getLocalRtcpPort() {
    if (isBinded && enableRtcp_) {
      return rtcpSocket_.local_endpoint().port();
    } else {
      throw std::logic_error("not binded.");
    }
  }


  void setCongestionControl(bool enable) { rtpSender->setCongestionControl(enable); };

  // 2023年6月12日
  /*
   * maxBytesPerSeoncd: 每秒发送的最多字节数.
   *   - 当设置maxBytesPerSeoncd 为0时, 即不对带宽进行限制
   *   - 如果从来就没有调用`setBandwidth`函数, 默认情况下不对带宽进行限制.
   * bandwidthControlUnitInMs: 重新计算当前带宽的单位时间, 即达到带宽上限.
   *   - 达到带宽上限后, 在当前单位时间内就不再发送RTP, 需要在下一个单位时间中才会解开发送限制.
   *   - 该值设置越小, 造成的时延也就越少, 但是会带来更多的计算量(具体影响未做定量分析)
   *   - 推荐设置在20-50之间, 另外尽量保障该值是1000的因数.
   */
  void setBandwidth(size_t maxBytesPerSecond, size_t bandwidthControlUnitInMs = 25) {
    rtpSender->setBandwidth(maxBytesPerSecond, bandwidthControlUnitInMs);
  }

  // 2023年6月8日
  void setRecvUdpOnly(bool enable) {
    if (sendOnly_ && enable) {
      std::string msg = fmt::format(
          "{}, rtpTransceiver setRecvUdpOnly error: rtpTrs is sendOnly.", traceId_);
    }

    if (enableRtcp_ && enable) {
      std::string msg = fmt::format(
          "{}, rtpTransceiver setRecvUdpOnly error: rtpTrs is enableRtcp.", traceId_);
      E_LOG(msg);
      throw std::logic_error(msg);
    }

    recvUdpOnly = enable;
  };

  const std::string traceId_;

 private:
  bool sendOnly_ = false;
  bool enableRtcp_ = false;


  // deprecated
  // int pt_for_recv_ = -1;
  // deprecated
  // uint32_t ssrc_for_recv_{0};

  // deprecated
  // bool skipSsrcCheck = false;
  // deprecated
  // bool skipPtCheck = false;


  // deprecated
  // bool firstRtpGot{false};

  std::shared_ptr<Notifier> rtpNotifier{nullptr};
  std::shared_ptr<Notifier> rtcpNotifier{nullptr};
  std::shared_ptr<Notifier> udpNotifier{nullptr};

  udp::socket rtpSocket_;
  udp::socket rtcpSocket_;

  const uint16_t reorderSize_;
  const uint16_t leadingReorderSize_;

  bool recvUdpOnly;

  RtpProcessor rtpProcessor;
  RtcpProcessor rtcpProcessor;


  std::mutex pipeMapMx{};
  std::unordered_map<StreamKey, RtpPipe, StreamKeyHash> rtpPipeMap{};


  std::mutex pipeNeedToRemoveMx{};
  std::deque<StreamKey> rtpPipeNeedToRemove{};


  std::thread recvWorkerThread;
  std::condition_variable recvBuffCv;
  std::mutex recvBuffMx{};
  bool stopRecvWorker = true;
  // std::deque<Received<RawData>> recvBuffQueue{};
  // OneOnePipe<Received<RawData>> recvBuffQueue{60};

  OneOnePipe<Received<RawData>> rtpRecvBuffQueue{200};
  OneOnePipe<Received<RawData>> rtcpRecvBuffQueue{200};

  std::mutex recvRtpQueueMx{};
  std::atomic<bool> recvRtpQueueEmpty = true;
  std::deque<Received<Rtp>> recvRtpQueue{};

  std::mutex recvRtcpQueueMx{};
  std::atomic<bool> recvRtcpQueueEmpty = true;
  std::deque<Received<Rtcp>> recvRtcpQueue{};

  std::mutex recvUdpQueueMx{};
  std::atomic<bool> recvUdpQueueEmpty = true;
  std::deque<Received<RawData>> recvUdpQueue{};

  // std::unique_ptr<Endpoint> dstAddr{nullptr};

  std::vector<Endpoint> dstAddrs{};

  // deprecated
  // Endpoint localRtpEndpoint{};

  std::atomic<bool> isBinded{false};
  std::mutex bindingMx;
  UdpReceiver* rtpReceiver{nullptr};
  UdpReceiver* rtcpReceiver{nullptr};
  UdpSender* rtpSender{nullptr};
  UdpSender* rtcpSender{nullptr};

  uint16_t lastSeq{0};  // only for debug

  inline void recvWorker();

  inline void recvWorker2();

  // inline void pushUdp(RawData&& udp, const udp::endpoint& from) override;


  // FOR temp debug
  // deprecated
  uint32_t TEMP_TOTAL_JUMP = 0;
  // deprecated
  uint32_t TEMP_TOTAL_COUNT = 0;
  // deprecated
  uint32_t TEMP_PUSH_TIME = 0;
};


RtpTransceiver::RtpTransceiver(const std::string& traceId, uint16_t reorderSize,
                               uint16_t leadingReorderSize)
    : traceId_(traceId),
      rtpSocket_(socketIoCtx),
      rtcpSocket_(socketIoCtx),
      reorderSize_(reorderSize),
      leadingReorderSize_(leadingReorderSize),
      recvUdpOnly(false),
      rtpProcessor(*this),
      rtcpProcessor(*this) {
  if (!isInited) {
    std::lock_guard<std::mutex> lg{masterMx};
    if (!socketThreads.empty() || isInited) {
      return;
    } else {
      init(4);
    }
  }
}


int RtpTransceiver::open(const std::string& localIp, uint16_t localPort, bool sendOnly) {
  return open2(localIp, localPort, 0, sendOnly);
}

int RtpTransceiver::open2(const std::string& localIp, uint16_t localRtpPort,
                          uint16_t localRtcpPort, bool sendOnly) {
  if (isBinded.load()) {
    throw std::logic_error("rtpTransceiver open: already binded." + traceId_);
  }

  if (localRtpPort == localRtcpPort) {
    throw std::logic_error(
        "rtpTransceiver open: localRtpPort == localRtcpPort is not supported." + traceId_);
  }

  std::lock_guard<std::mutex> lg{bindingMx};
  if (isBinded.load()) {
    throw std::logic_error("rtpTransceiver bind: already binded." + traceId_);
  }


  asio::ip::udp::endpoint rtpEndpoint{asio::ip::make_address(localIp), localRtpPort};

  // asio::ip::udp::resolver resolver(rtpSocket_.get_executor());
  // asio::ip::udp::endpoint rtpEndpoint =
  //     *resolver.resolve(localIp, std::to_string(localRtpPort)).begin();

  std::error_code ec;

  rtpSocket_.open(rtpEndpoint.protocol(), ec);

  if (ec) {
    if (ec) W_LOG("traceId[{}], rtp socket open error: {}", traceId_, ec.message());
    return -1;
  }

  // rtpSocket_.set_option(asio::ip::udp::socket::reuse_address(true));
  rtpSocket_.bind(rtpEndpoint, ec);
  if (ec) {
    W_LOG(
        "traceId[{}], rtp socket bind error value={} message={}, rtpEndpoint addr={} port={} "
        "is_v4={} "
        "is_v6={} ",
        traceId_, ec.value(), ec.message(), rtpEndpoint.address().to_string(),
        rtpEndpoint.port(), rtpEndpoint.address().is_v4(), rtpEndpoint.address().is_v6());
    ec.clear();
    rtpSocket_.close(ec);
    if (ec) W_LOG("traceId[{}], rtp socket close: {}", traceId_, ec.message());
    return -2;
  }

  rtpSender = new UdpSender(socketIoCtx, rtpSocket_, traceId_ + "_rtp");

  // localRtpEndpoint = localAddr;
  sendOnly_ = sendOnly;
  enableRtcp_ = (localRtcpPort > 0);


  if (enableRtcp_) {
    Endpoint rtcpEndpoint{asio::ip::make_address(localIp), localRtcpPort};
    std::error_code ec;
    rtcpSocket_.open(rtcpEndpoint.protocol(), ec);
    if (ec) {
      if (ec) W_LOG("traceId[{}], rtcp socket open error: {}", traceId_, ec.message());
      return -1;
    }

    rtcpSocket_.bind(rtcpEndpoint, ec);

    if (ec) {
      if (ec) W_LOG("traceId[{}], rtcp socket bind error: {}", traceId_, ec.message());
      ec.clear();
      rtcpSocket_.close(ec);
      if (ec) W_LOG("traceId[{}], rtcp socket close: {}", traceId_, ec.message());
      ec.clear();
      rtpSocket_.close(ec);
      if (ec) W_LOG("traceId[{}], rtp socket close: {}", traceId_, ec.message());
      return -3;
    }

    rtcpSender = new UdpSender(socketIoCtx, rtcpSocket_, traceId_ + "_rtcp");
  }



  if (!sendOnly_) {
    rtpReceiver = new UdpReceiver(rtpSocket_, &rtpProcessor, traceId_ + "_rtp");
    rtpReceiver->start();

    if (enableRtcp_) {
      rtcpReceiver = new UdpReceiver(rtcpSocket_, &rtcpProcessor, traceId_ + "_rtcp");
      rtcpReceiver->start();
    }

    stopRecvWorker = false;
    std::thread worker{&RtpTransceiver::recvWorker, this};
    recvWorkerThread = std::move(worker);
  }

  isBinded.store(true);
  return 0;
}


void RtpTransceiver::addDestination(const std::string& ip, uint16_t port) {
  Endpoint target{asio::ip::make_address(ip), port};
  for (auto it = dstAddrs.begin(); it != dstAddrs.end(); it++) {
    if (it->address().to_string() == target.address().to_string() &&
        (uint16_t)it->port() == port) {
      return;
    }
  }
  dstAddrs.emplace_back(target);
}

void RtpTransceiver::removeDestination(const std::string& ip, uint16_t port) {
  for (auto it = dstAddrs.begin(); it != dstAddrs.end();) {
    if (it->address().to_string() == ip && (uint16_t)it->port() == port) {
      it = dstAddrs.erase(it);
    } else {
      it++;
    }
  }
}

void RtpTransceiver::removeAllDestinations() { dstAddrs.clear(); }

std::vector<std::pair<std::string, uint16_t>> RtpTransceiver::listDestinations() const {
  std::vector<std::pair<std::string, uint16_t>> ipAndPortVector;
  for (auto& dst : dstAddrs) {
    ipAndPortVector.emplace_back(dst.address().to_string(), dst.port());
  }
  return ipAndPortVector;
}

void RtpTransceiver::sendRtp(std::deque<Rtp>& rtps, const std::vector<Endpoint>& multiAddrs) {
  size_t targetSize = multiAddrs.size();
  if (targetSize == 0) {
    throw std::logic_error("RtpTransceiver sendRtp: targetSize is 0. traceId=" + traceId_);
  }

  std::queue<std::vector<uint8_t>> dataQueue;
  while (!rtps.empty()) {
    dataQueue.emplace(rtps.front().takeRawData());
    rtps.pop_front();
  }
  if (targetSize == 1) {
    rtpSender->send(std::move(dataQueue), multiAddrs.at(0));
  } else {
    rtpSender->send(std::move(dataQueue), multiAddrs);
  }
}

void RtpTransceiver::sendRtp(std::deque<Rtp>& rtps) { sendRtp(rtps, dstAddrs); }


/*
 * 发送RTP包, 绕开已经设置的destination, 指定目的地址发送RTP包
 */
inline void RtpTransceiver::sendRtp(std::deque<Rtp>& rtps, const Endpoint& dst) {
  std::queue<std::vector<uint8_t>> dataQueue;
  while (!rtps.empty()) {
    dataQueue.emplace(rtps.front().takeRawData());
    rtps.pop_front();
  }
  rtpSender->send(std::move(dataQueue), dst);
}

/*
 * 发送RTP包, 绕开已经设置的destination, 指定目的地址发送RTP包
 */
void RtpTransceiver::sendRtp(std::deque<Rtp>& rtps, const std::string& dstIp,
                             uint16_t dstPort) {
  sendRtp(rtps, Endpoint{asio::ip::make_address(dstIp), dstPort});
}

// 2023年5月19日
inline void RtpTransceiver::sendUdp(std::deque<std::vector<uint8_t>>& udps,
                                    const std::string& dstIp, uint16_t dstPort) {
  std::queue<std::vector<uint8_t>> dataQueue;
  while (!udps.empty()) {
    dataQueue.emplace(std::move(udps.front()));
    udps.pop_front();
  }

  rtpSender->send(std::move(dataQueue), Endpoint{asio::ip::make_address(dstIp), dstPort});
}

// 2023年6月8日
inline void RtpTransceiver::sendUdp(std::deque<std::vector<uint8_t>>& udps,
                                    const Endpoint& dst) {
  std::queue<std::vector<uint8_t>> dataQueue;
  while (!udps.empty()) {
    dataQueue.emplace(std::move(udps.front()));
    udps.pop_front();
  }
  rtpSender->send(std::move(dataQueue), dst);
}


// 2023年7月24日
inline void RtpTransceiver::sendRtcp(std::deque<Rtcp>& rtcps, const std::string& dstIp,
                                     uint16_t dstPort) {
  std::queue<std::vector<uint8_t>> dataQueue;
  while (!rtcps.empty()) {
    dataQueue.emplace(rtcps.front().takeRawData());
    rtcps.pop_front();
  }
  rtcpSender->send(std::move(dataQueue), Endpoint{asio::ip::make_address(dstIp), dstPort});
}

// 2023年7月24日
inline void RtpTransceiver::sendRtcp(std::deque<Rtcp>& rtcps, const Endpoint& dst) {
  std::queue<std::vector<uint8_t>> dataQueue;
  while (!rtcps.empty()) {
    dataQueue.emplace(rtcps.front().takeRawData());
    rtcps.pop_front();
  }
  rtcpSender->send(std::move(dataQueue), dst);
}

// void RtpTransceiver::sendRtp(std::deque<Rtp>& rtps) {
//   if (dstAddr == nullptr) {
//     throw std::logic_error("RtpTransceiver sendRtp: dstAddr == nullptr. " + traceId_);
//   }
//
//   std::queue<std::vector<uint8_t>> dataQueue;
//   while (!rtps.empty()) {
//     dataQueue.emplace(rtps.front().takeRawData());
//     rtps.pop_front();
//   }
//
//   rtpSender->send(std::move(dataQueue), *dstAddr);
// }

void RtpTransceiver::close() {
  if (!isBinded.load()) {
    return;
  }
  std::lock_guard<std::mutex> lg{bindingMx};
  if (!isBinded.load()) {
    return;
  }


  isBinded.store(false);

  // rtpPipe.resetPipe();

  if (rtpReceiver != nullptr) {
    rtpReceiver->close();
  }

  if (rtpSender != nullptr) {
    rtpSender->close();
  }

  if (rtcpReceiver != nullptr) {
    rtcpReceiver->close();
  }

  if (rtcpSender != nullptr) {
    rtcpSender->close();
  }

  {
    std::lock_guard<std::mutex> lg{recvBuffMx};
    stopRecvWorker = true;
  }

  recvBuffCv.notify_all();

  std::error_code ec;

  ec.clear();
  rtpSocket_.cancel(ec);
  if (ec) {
    D_LOG("traceId[{}], RtpTransceiver close error: socket cancel failed: {}", traceId_,
          ec.message());
  }

  ec.clear();
  rtcpSocket_.cancel(ec);
  if (ec) {
    D_LOG("traceId[{}], RtpTransceiver close error: socket cancel failed: {}", traceId_,
          ec.message());
  }


  if (rtpReceiver != nullptr) {
    size_t failTime = 0;
    while (!rtpReceiver->isStopped() && failTime < 3000) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      failTime += 1;
    }

    if (failTime >= 300) {
      E_LOG("traceId[{}], rtpReceiver.isStopped success failTime=[{}]", traceId_, failTime);
    } else if (failTime >= 5) {
      W_LOG("traceId[{}], rtpReceiver.isStopped success failTime=[{}]", traceId_, failTime);
    } else {
      D_LOG("traceId[{}], rtpReceiver.isStopped success failTime=[{}]", traceId_, failTime);
    }

    if (rtpReceiver->isStopped()) {
      delete rtpReceiver;
    } else {
      E_LOG("traceId[{}], rtpReceiver.isStopped failed. failTime=[{}]", traceId_, failTime);
    }
  }

  if (rtcpReceiver != nullptr) {
    size_t failTime = 0;
    while (!rtcpReceiver->isStopped() && failTime < 3000) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      failTime += 1;
    }

    if (failTime >= 3000) {
      E_LOG("traceId[{}], rtcpReceiver.isStopped failed failTime=[{}]", traceId_, failTime);
    } else if (failTime >= 300) {
      E_LOG("traceId[{}], rtcpReceiver.isStopped success failTime=[{}]", traceId_, failTime);
    } else if (failTime >= 5) {
      W_LOG("traceId[{}], rtcpReceiver.isStopped success failTime=[{}]", traceId_, failTime);
    } else {
      D_LOG("traceId[{}], rtcpReceiver.isStopped success failTime=[{}]", traceId_, failTime);
    }

    if (rtcpReceiver->isStopped()) {
      delete rtcpReceiver;
    } else {
      E_LOG("traceId[{}], rtcpReceiver.isStopped failed. failTime=[{}]", traceId_, failTime);
    }
  }


  if (rtpSender != nullptr) {
    size_t failTime = 0;
    while (!rtpSender->isStopped() && failTime < 3000) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      failTime += 1;
    }

    if (failTime >= 300) {
      E_LOG("traceId[{}], rtpSender.isStopped failTime[{}]", traceId_, failTime);
    } else if (failTime >= 20) {
      W_LOG("traceId[{}], rtpSender.isStopped failTime[{}]", traceId_, failTime);
    } else if (failTime >= 5) {
      I_LOG("traceId[{}], rtpSender.isStopped failTime[{}]", traceId_, failTime);
    } else {
      D_LOG("traceId[{}], rtpSender.isStopped failTime[{}]", traceId_, failTime);
    }

    if (rtpSender->isStopped()) {
      delete rtpSender;
    } else {
      E_LOG("traceId[{}], rtpSender.isStopped failed. failTime=[{}]", traceId_, failTime);
    }
  }

  if (rtcpSender != nullptr) {
    size_t failTime = 0;
    while (!rtcpSender->isStopped() && failTime < 3000) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      failTime += 1;
    }

    if (failTime >= 300) {
      E_LOG("traceId[{}], rtcpSender.isStopped failTime[{}]", traceId_, failTime);
    } else if (failTime >= 20) {
      W_LOG("traceId[{}], rtcpSender.isStopped failTime[{}]", traceId_, failTime);
    } else if (failTime >= 5) {
      I_LOG("traceId[{}], rtcpSender.isStopped failTime[{}]", traceId_, failTime);
    } else {
      D_LOG("traceId[{}], rtcpSender.isStopped failTime[{}]", traceId_, failTime);
    }

    if (rtcpSender->isStopped()) {
      delete rtcpSender;
    } else {
      E_LOG("traceId[{}], rtcpSender.isStopped failed. failTime=[{}]", traceId_, failTime);
    }
  }


  ec.clear();
  rtpSocket_.close(ec);
  if (ec) {
    D_LOG("traceId[{}], Transporter Destroy, rtpSocket_ close error: {}", traceId_,
          ec.message());
  }

  ec.clear();
  rtcpSocket_.close(ec);
  if (ec) {
    D_LOG("traceId[{}], Transporter Destroy, rtcpSocket_ close error: {}", traceId_,
          ec.message());
  }

  if (!sendOnly_) {
    if (recvWorkerThread.joinable()) {
      recvWorkerThread.join();
      D_LOG("traceId[{}], recvWorkerThread finished.", traceId_);
    } else {
      E_LOG("traceId[{}], recvWorkerThread is not joinable!", traceId_);
    }
  }


  sendOnly_ = false;

  {
    std::lock_guard<std::mutex> lg{pipeMapMx};
    rtpPipeMap.clear();
  }

  D_LOG("traceId[{}], RtpTransceiver::close finished.", traceId_);
}

// void cancel();    // need it?
// void shutdown();  // need it?



void RtpTransceiver::receiveRtp(std::deque<Received<Rtp>>& queue) {
  if (!queue.empty()) {
    std::string msg = fmt::format("traceId[{}], receiveRtp: queue must be empty!", traceId_);
    E_LOG(msg);
    throw std::logic_error(msg);
  }

  if (sendOnly_) {
    std::string msg = fmt::format("traceId[{}], receiveRtp: sendOnly.", traceId_);
    E_LOG(msg);
    throw std::logic_error(msg);
  }

  if (recvRtpQueueEmpty) {
    return;
  }

  {
    std::lock_guard<std::mutex> lg{recvRtpQueueMx};
    if (!recvRtpQueue.empty()) {
      recvRtpQueue.swap(queue);
      recvRtpQueueEmpty = true;
    }
  }
}


void RtpTransceiver::receiveRtcp(std::deque<Received<Rtcp>>& queue) {
  if (!queue.empty()) {
    std::string msg = fmt::format("traceId[{}], receiveRtcp: queue must be empty!", traceId_);
    E_LOG(msg);
    throw std::logic_error(msg);
  }

  if (sendOnly_) {
    std::string msg = fmt::format("traceId[{}], receiveRtcp: sendOnly.", traceId_);
    E_LOG(msg);
    throw std::logic_error(msg);
  }

  if (recvRtcpQueueEmpty) {
    return;
  }

  {
    std::lock_guard<std::mutex> lg{recvRtcpQueueMx};
    if (!recvRtcpQueue.empty()) {
      recvRtcpQueue.swap(queue);
      recvRtcpQueueEmpty = true;
    }
  }
}


void RtpTransceiver::receiveUdp(std::deque<Received<RawData>>& queue) {
  if (!queue.empty()) {
    std::string msg = fmt::format("traceId[{}], receiveUdp: queue must be empty!", traceId_);
    E_LOG(msg);
    throw std::logic_error(msg);
  }

  if (sendOnly_) {
    std::string msg = fmt::format("traceId[{}], receiveUdp: sendOnly.", traceId_);
    E_LOG(msg);
    throw std::logic_error(msg);
  }

  if (recvUdpQueueEmpty) {
    return;
  }

  {
    std::lock_guard<std::mutex> lg{recvUdpQueueMx};
    if (!recvUdpQueue.empty()) {
      recvUdpQueue.swap(queue);
      recvUdpQueueEmpty = true;
    }
  }
}

// backup
void RtpTransceiver::recvWorker2() {
  std::deque<Received<RawData>> workingQueue{};
  std::deque<RawData> tmpQueue{};
  while (true) {
    // base lock dequeue
    //{
    //  std::unique_lock<std::mutex> lk(recvBuffMx);
    //  recvBuffCv.wait_for(lk, std::chrono::milliseconds(1024),
    //                      [this] { return stopRecvWorker || !recvBuffQueue.empty(); });
    //
    //  if (stopRecvWorker) {
    //    D_LOG("traceId[{}], recvWorker got stop sign", traceId_);
    //    break;
    //  } else if (!recvBuffQueue.empty()) {
    //    // D_LOG("recvWorker working.");
    //    workingQueue.swap(recvBuffQueue);
    //  } else {
    //    // D_LOG("recvWorker continue wait.");
    //    continue;
    //  }
    //}


    // OneOnePipe
    rtpRecvBuffQueue.dequeueAll(workingQueue);
    if (workingQueue.size() == 0) {
      std::unique_lock<std::mutex> lk(recvBuffMx);
      recvBuffCv.wait_for(lk, std::chrono::milliseconds(1024));
      if (stopRecvWorker) {
        break;
      } else {
        rtpRecvBuffQueue.dequeueAll(workingQueue);
      }
    }


    // deal with workingQueue

    bool gotRtp = false;
    bool gotUnknonMsg = false;


    // TODO: 可以考虑根据streamKey, 将不同stream的rtp放到不同的workingQueue中进行分别处理
    // 这样就不需要每个rtp包都获取一次 pipe 了.
    // 但是, 如果udp包接收很快, 每次进来的 udp 队列也就一两个包, 好像就没有必要这样搞.

    uint32_t lastSsrc = 0;
    uint8_t lastPt = 0;
    RtpPipe* currentPipe = nullptr;


    while (!workingQueue.empty()) {
      Received<RawData>& front = workingQueue.front();
      RawData& udp = front.element;
      Endpoint& from = front.from;

      if (!recvUdpOnly && Rtp::isValidRtp(udp)) {
        uint32_t ssrc = getSsrc(udp.data());
        uint8_t pt = getPayloadType(udp.data());
        StreamKey currentStream{ssrc, pt};

        if (currentPipe != nullptr && currentStream.ssrc == lastSsrc &&
            currentStream.pt == lastPt) {
          // no need update currentPipe pointer.
        } else {
          std::lock_guard<std::mutex> lk{pipeMapMx};
          auto it = rtpPipeMap.find(currentStream);
          if (it != rtpPipeMap.end()) {
            currentPipe = &it->second;
          } else {
            std::string pipeTraceId = traceId_ + "_" + std::to_string(currentStream.ssrc);
            auto newIt = rtpPipeMap.try_emplace(currentStream, pipeTraceId, reorderSize_,
                                                leadingReorderSize_);
            if (newIt.second) {
              currentPipe = &(newIt.first->second);
            } else {
              E_LOG("create RtpPipe error. It should be never happen.");
            }
          }

          lastSsrc = currentStream.ssrc;
          lastPt = currentStream.pt;
        }

        if (currentPipe == nullptr) {
          E_LOG("get currentPipe error. It should be never happen.");
          continue;
        }


        T_LOG("pushUdp  --  1.1  --");

        if (!tmpQueue.empty()) {
          std::logic_error("it shuld never happend: !tmpQueue.empty()");
        }

        currentPipe->reorderRtp(std::move(udp), seeker::time::currentTime(), tmpQueue);

        if (!tmpQueue.empty()) {
          gotRtp = true;
        }

        {
          std::lock_guard<std::mutex> lg{recvRtpQueueMx};
          // TODO if recvRtpQueue is too big, throw some rtps.
          while (!tmpQueue.empty()) {
            RawData& data = tmpQueue.front();
            if (data.size() == 0) {
              throw std::logic_error("pushUdp data.size() == 0, it should never happen.");
            }
            recvRtpQueue.emplace_back(Rtp{std::move(data)}, from);
            tmpQueue.pop_front();
          }
          recvRtpQueueEmpty = recvRtpQueue.empty();
        }
      } else {
        if (udpNotifier) {
          std::lock_guard<std::mutex> lg{recvUdpQueueMx};
          recvUdpQueue.emplace_back(std::move(udp), from);
          gotUnknonMsg = true;
          recvUdpQueueEmpty = false;
        }
      }

      workingQueue.pop_front();
    }

    if ((gotRtp || stopRecvWorker) && rtpNotifier) rtpNotifier->notify();
    if ((gotUnknonMsg || stopRecvWorker) && udpNotifier) udpNotifier->notify();


    // process rtcp queue
  }

  D_LOG("traceId[{}], recvWorker stopped.", traceId_);
}


void RtpTransceiver::recvWorker() {
  std::deque<Received<RawData>> rtpWorkingQueue{};
  std::deque<Received<RawData>> rtcpWorkingQueue{};
  std::deque<RawData> rtpTmpQueue{};
  std::deque<RawData> rtcpTmpQueue{};

  std::deque<StreamKey> pipeToRemove{};

  while (true) {
    // OneOnePipe
    rtpRecvBuffQueue.dequeueAll(rtpWorkingQueue);
    const size_t recvRtpSize = rtpWorkingQueue.size();

    T_LOG("traceId[{}] recvWorker recvRtpSize={}", traceId_, recvRtpSize);

    // deal with workingQueue
    bool gotRtp = false;
    bool gotUdp = false;


    // TODO: 可以考虑根据streamKey, 将不同stream的rtp放到不同的workingQueue中进行分别处理
    // 这样就不需要每个rtp包都获取一次 pipe 了.
    // 但是, 如果udp包接收很快, 每次进来的 udp 队列也就一两个包, 好像就没有必要这样搞.

    uint32_t lastSsrc = 0;
    uint8_t lastPt = 0;
    RtpPipe* currentPipe = nullptr;


    if (recvUdpOnly) {
      if (udpNotifier && recvRtpSize > 0) {
        std::lock_guard<std::mutex> lg{recvUdpQueueMx};
        while (!rtpWorkingQueue.empty()) {
          Received<RawData>& target = rtpWorkingQueue.front();
          recvUdpQueue.emplace_back(std::move(target));
          rtpWorkingQueue.pop_front();
        }
        gotUdp = true;
        recvUdpQueueEmpty = false;
      }
    } else if (reorderSize_ == 0) {
      if (recvRtpSize > 0) {
        std::lock_guard<std::mutex> lg{recvRtpQueueMx};
        while (!rtpWorkingQueue.empty()) {
          Received<RawData>& target = rtpWorkingQueue.front();
          recvRtpQueue.emplace_back(Rtp{std::move(target.element)}, target.from);
          rtpWorkingQueue.pop_front();
        }
        gotRtp = true;
        recvRtpQueueEmpty = false;
      }
    } else {
      while (!rtpWorkingQueue.empty()) {
        Received<RawData>& front = rtpWorkingQueue.front();
        RawData& udp = front.element;
        Endpoint& from = front.from;

        if (!Rtp::isValidRtp(udp)) {
          if (udpNotifier) {
            std::lock_guard<std::mutex> lg{recvUdpQueueMx};
            recvUdpQueue.emplace_back(std::move(udp), from);
            gotUdp = true;
            recvUdpQueueEmpty = false;
          }
        } else {
          const uint32_t ssrc = getSsrc(udp.data());
          const uint8_t pt = getPayloadType(udp.data());

          // update pip map.
          {
            std::lock_guard<std::mutex> lk{pipeNeedToRemoveMx};
            pipeToRemove.swap(rtpPipeNeedToRemove);
          }

          if (!pipeToRemove.empty()) {
            std::lock_guard<std::mutex> lk{pipeMapMx};
            for (auto& key : pipeToRemove) {
              if (ssrc == key.ssrc && pt == key.pt) {
                currentPipe = nullptr;
              }
              rtpPipeMap.erase(key);
              D_LOG("traceId[{}] erase rtpPIpe from map. ssrc={} pt={}", traceId_, ssrc, pt);
            }
            pipeToRemove.clear();
          }


          if (currentPipe != nullptr && ssrc == lastSsrc && pt == lastPt) {
            // no need update currentPipe pointer.
          } else {
            StreamKey currentStream{ssrc, pt};
            std::lock_guard<std::mutex> lk{pipeMapMx};
            auto it = rtpPipeMap.find(currentStream);
            if (it != rtpPipeMap.end()) {
              currentPipe = &it->second;
            } else {
              std::string pipeTraceId = traceId_ + "_" + std::to_string(currentStream.ssrc);
              auto newIt = rtpPipeMap.try_emplace(currentStream, pipeTraceId, reorderSize_,
                                                  leadingReorderSize_);
              if (newIt.second) {
                currentPipe = &(newIt.first->second);
                D_LOG("traceId[{}] create new RtpPipe ssrc={} pt={}", traceId_, ssrc, pt);
              } else {
                E_LOG("traceId[{}] create RtpPipe error. It should be never happen.",
                      traceId_);
              }
            }

            lastSsrc = currentStream.ssrc;
            lastPt = currentStream.pt;
          }

          if (currentPipe == nullptr) {
            E_LOG("traceId[{}] get currentPipe error. It should be never happen.", traceId_);
            continue;
          }



          if (!rtpTmpQueue.empty()) {
            std::logic_error("it shuld never happend: !tmpQueue.empty()");
          }


          uint16_t seq = getSeq(udp.data());

          RtpState rst = currentPipe->reorderRtp(std::move(udp), seeker::time::currentTime(),
                                                 rtpTmpQueue);
          T_LOG("traceId[{}] pushUdp ssrc={} pt={} seq={} rst={} rtpTmpQueue.size={}",
                traceId_, ssrc, pt, seq, (int)rst, rtpTmpQueue.size());

          if (!rtpTmpQueue.empty()) {
            gotRtp = true;
          }

          {
            std::lock_guard<std::mutex> lg{
                recvRtpQueueMx};  // TODO if rtpTmpQueue is empty, not get lock.
            // TODO if recvRtpQueue is too big, throw some rtps.
            while (!rtpTmpQueue.empty()) {
              RawData& data = rtpTmpQueue.front();
              if (data.size() == 0) {
                throw std::logic_error("pushUdp data.size() == 0, it should never happen.");
              }
              recvRtpQueue.emplace_back(Rtp{std::move(data)}, from);
              rtpTmpQueue.pop_front();
            }
            recvRtpQueueEmpty = recvRtpQueue.empty();
          }
        }

        rtpWorkingQueue.pop_front();
      }
    }


    // process rtcp

    rtcpRecvBuffQueue.dequeueAll(rtcpWorkingQueue);
    const size_t recvRtcpSize = rtcpWorkingQueue.size();

    bool gotRtcp = false;

    while (!rtcpWorkingQueue.empty()) {
      Received<RawData>& front = rtcpWorkingQueue.front();
      RawData& udp = front.element;
      Endpoint& from = front.from;

      if (rtcpNotifier) {
        std::lock_guard<std::mutex> lg{recvRtcpQueueMx};
        recvRtcpQueue.emplace_back(std::move(udp), from);
        gotRtcp = true;
        recvRtcpQueueEmpty = false;
      }
      rtcpWorkingQueue.pop_front();
    }

    if ((gotRtp || stopRecvWorker) && rtpNotifier) rtpNotifier->notify();

    if ((gotRtcp || stopRecvWorker) && rtcpNotifier && rtcpNotifier.get() != rtpNotifier.get())
      rtcpNotifier->notify();

    if ((gotUdp || stopRecvWorker) && udpNotifier && udpNotifier.get() != rtpNotifier.get() &&
        udpNotifier.get() != rtcpNotifier.get())
      udpNotifier->notify();

    if (recvRtpSize == 0 && recvRtcpSize == 0) {
      std::unique_lock<std::mutex> lk(recvBuffMx);
      recvBuffCv.wait_for(lk, std::chrono::milliseconds(100));
      if (stopRecvWorker) {
        break;
      }
    }
  }

  D_LOG("traceId[{}], recvWorker stopped.", traceId_);
}

// void RtpTransceiver::pushUdp(RawData&& udp, const udp::endpoint& from) {
//   if (sendOnly_) {
//     std::string msg = fmt::format("traceId[{}], pushUdp: sendOnly.", traceId_);
//     E_LOG(msg);
//     throw std::logic_error(msg);
//   }
//
//   // tmp queue
//   // bool mark = getMark(udp.data());  // not good.
//   // Received<RawData> recvData{std::move(udp), from};
//   // recvTempQueue.emplace_back(std::move(recvData));
//   // if (recvTempQueue.size() > 5000) {
//   //  recvTempQueue.clear();
//   //}
//   // if (mark) {
//   //   {
//   //     std::lock_guard<std::mutex> lg{recvBuffMx};
//   //     recvBuffQueue.swap(recvTempQueue);
//   //   }
//   //   recvBuffCv.notify_one();
//   // }
//
//   // lock deque
//   //{
//   //  std::lock_guard<std::mutex> lg{recvBuffMx};
//   //  recvBuffQueue.emplace_back(std::move(udp), from);
//   //}
//   // recvBuffCv.notify_one();
//
//
//   // rwq/OneOnePipe
//   Received<RawData> recvData{std::move(udp), from};
//   bool success = recvBuffQueue.enqueue(std::move(recvData));
//   if (!success) {
//     E_LOG("enqueue error! size={}", recvBuffQueue.size());
//   } else {
//     recvBuffCv.notify_one();
//   }
// }


bool RtpTransceiver::RtpProcessor::pushUdp(RawData&& udp, const udp::endpoint& from) {
  if (p.sendOnly_) {
    std::string msg = fmt::format("traceId[{}], pushUdp: sendOnly.", p.traceId_);
    E_LOG(msg);
    throw std::logic_error(msg);
  }

  // rwq/OneOnePipe
  Received<RawData> recvData{std::move(udp), from};
  bool success = p.rtpRecvBuffQueue.enqueue(std::move(recvData));
  if (!success) {
    E_LOG("traceId[{}] rtpRecvBuffQueue enqueue error! size={}", p.traceId_,
          p.rtpRecvBuffQueue.size());
    p.recvBuffCv.notify_one();
    return false;
  } else {
    p.recvBuffCv.notify_one();
    return true;
  }
}

bool RtpTransceiver::RtcpProcessor::pushUdp(RawData&& udp, const udp::endpoint& from) {
  if (p.sendOnly_) {
    std::string msg = fmt::format("traceId[{}], pushUdp: sendOnly.", p.traceId_);
    E_LOG(msg);
    throw std::logic_error(msg);
  }

  const uint16_t udpLen = udp.size();

  const uint8_t* udpData = udp.data();
  uint16_t pos = 0;
  while (pos + 4 <= udpLen) {
    uint16_t rtcpLength = seeker::rtp::getRtcpLength(udpData + pos);
    uint16_t rtcpLengthInBytes = (rtcpLength + 1) * 4;
    if (pos + rtcpLengthInBytes <= udpLen) {
      std::vector<uint8_t> rtcpData{udpData + pos, udpData + pos + rtcpLengthInBytes};
      Received<RawData> recvData{std::move(rtcpData), from};
      bool success = p.rtcpRecvBuffQueue.enqueue(std::move(recvData));
      if (!success) {
        E_LOG("traceId[{}] rtcpRecvBuffQueue enqueue error! size={}", p.traceId_,
              p.rtcpRecvBuffQueue.size());
      }
    } else {
      W_LOG(
          "traceId[{}] rtcp split error. pos={} rtcpLength={} rtcpLengthInBytes={} udpLen={}",
          p.traceId_, pos, rtcpLength, rtcpLengthInBytes, udpLen);
    }
    pos += rtcpLengthInBytes;
  }

  p.recvBuffCv.notify_one();
  return true;


  // Received<RawData> recvData{std::move(udp), from};
  // bool success = p.rtcpRecvBuffQueue.enqueue(std::move(recvData));
  // if (!success) {
  //   E_LOG("rtcpRecvBuffQueue enqueue error! size={}", p.rtcpRecvBuffQueue.size());
  // } else {
  //   p.recvBuffCv.notify_one();
  // }
}

}  // namespace rtp


}  // namespace seeker

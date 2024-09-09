/**
@project rtpTrs
@author Tao Zhang
@since 2023/3/17
@version 0.0.1-SNAPSHOT 2023/4/13
*/


#pragma once

#include "config.h"

#include "seeker/common.h"
#include "seeker/loggerApi.h"

#include "asio.hpp"
#include <string>
#include <vector>
#include <iostream>
#include <queue>
#include <mutex>
#include <tuple>
#include <atomic>
#include <array>

#include <cmath>

class BytesCounter {
  inline static constexpr uint16_t MAX_RANGE = 1024;

  inline static uint16_t add(uint16_t a, uint16_t b) { return (a + b) % MAX_RANGE; };

  inline static uint16_t sub(uint16_t a, uint16_t b) {
    if (a >= b) {
      return a - b;
    } else {
      return a + MAX_RANGE - b;
    }
  };

  uint16_t countRange_ = 0;

 public:
  BytesCounter(uint16_t countRange = 5) : counterVector() { reset(countRange); }

  void reset(uint16_t countRange) {
    if (countRange > MAX_RANGE) {
      std::string msg =
          fmt::format("BytesCounter::reset: countRange_[{}] > MAX_RANGE.", countRange);
      E_LOG(msg);
      throw std::logic_error(msg);
    }

    countRange_ = countRange;
    counterVector.fill(0);
  }

  void update(int64_t ts, uint32_t byteSize) {
    int64_t idx = ts - baseTimestamp;
    if (idx < 0) {
      std::string msg =
          fmt::format("BytesCounter::count: idx[{}] < 0, it should never happen.", idx);
      E_LOG(msg);
      throw std::logic_error(msg);
    }

    if (idx < currIndex) {
      std::string msg =
          fmt::format("BytesCounter::count: idx[{}] < currIndex[{}], it should never happen.",
                      idx, currIndex);
      E_LOG(msg);
      throw std::logic_error(msg);
    }

    if (idx - currIndex >= MAX_RANGE) {
      T_LOG("counter reset idx={} currIndex={}  !!!!!!!!! ", idx, currIndex);
      for (uint16_t i = sub(0, countRange_); i < MAX_RANGE; i++) {
        counterVector[i] = 0;
      }
      baseTimestamp = ts;
      currIndex = 0;
      counterVector[currIndex] = byteSize;
      T_LOG("updateCounter update counterVector[{}]={}", currIndex, counterVector[currIndex]);
      return;
    }

    uint16_t newIndex = static_cast<uint16_t>(idx);
    updateCounter(newIndex, byteSize);
  }


  /*
   *  0      C      L                M
   *  +------+------+----------------+
   *  C: currIndex
   *  L: (C, L] 为 countRange 个元素
   *  M: 最后一个元素的位置
   */

  int64_t count(int64_t ts) {
    uint32_t sum = 0;

    int64_t maxCountedTime = baseTimestamp + currIndex;
    int64_t diff = ts - maxCountedTime;
    T_LOG("BytesCounter countOnly ---  0  --- ts={} maxCountedTime={} df={}", ts,
          maxCountedTime, diff);

    if (diff >= countRange_) {
      return 0;
    } else if (diff >= 0) {
      uint16_t begin = sub(currIndex, (uint16_t)(countRange_ - diff - 1));
      uint16_t end = add(currIndex, 1);
      T_LOG("BytesCounter countOnly ---  1  --- currIndex={} diff={} begin={} end={}",
            currIndex, diff, begin, end);
      for (uint16_t i = begin; i != end; i = add(i, 1)) {
        sum += counterVector.at(i);
        T_LOG("BytesCounter countOnly --1.1-- i={} c={} sum={}", i, counterVector.at(i), sum);
      }
    } else if (-diff < (MAX_RANGE - countRange_)) {
      uint16_t end = add(sub(currIndex, static_cast<uint16_t>(-diff)), 1);
      uint16_t begin = sub(end, countRange_);
      T_LOG("BytesCounter countOnly ---  2  --- currIndex={} diff={} begin={} end={}",
            currIndex, diff, begin, end);
      for (uint16_t i = begin; i != end; i = add(i, 1)) {
        sum += counterVector.at(i);
        T_LOG("BytesCounter countOnly --2.1-- i={} c={} sum={}", i, counterVector.at(i), sum);
      }
    } else {
      return -1;
    }

    return sum;
  }



 private:
  int64_t baseTimestamp = 0;
  // std::vector<uint32_t> counterVector;
  std::array<uint32_t, MAX_RANGE> counterVector;
  uint16_t currIndex = 0;

  void updateCounter(uint16_t newIndex, uint32_t byteSize) {
    T_LOG("updateCounter currIndex={} newIndex={} byteSize={}", currIndex, newIndex, byteSize);

    if (newIndex < currIndex) {
      std::string msg = fmt::format(
          "BytesCounter::count: newIndex[{}] < currIndex[{}], it should never happen.",
          newIndex, currIndex);
      E_LOG(msg);
      throw std::logic_error(msg);
    } else if (newIndex == currIndex) {
      counterVector[currIndex] += byteSize;
    } else {
      for (uint32_t i = currIndex + 1; i <= newIndex; i++) {
        uint16_t idx = i % MAX_RANGE;
        counterVector[idx] = 0;
      }

      if (newIndex >= MAX_RANGE) {
        baseTimestamp += MAX_RANGE;
        currIndex = newIndex % MAX_RANGE;
      } else {
        currIndex = newIndex;
      }
      counterVector[currIndex] += byteSize;
    }
    T_LOG("updateCounter counterVector[{}]={}", currIndex, counterVector[currIndex]);
  }
};

class UdpSender {
  // inline static constexpr uint32_t MAX_RATE_IN_MBYTE = 5;
  // inline static constexpr uint32_t CONTROL_RANGE = 2;
  //
  // inline static constexpr uint32_t MAX_BYTES_IN_CONTROL_RANGE =
  //     MAX_RATE_IN_MBYTE * 1024 * 1024 * CONTROL_RANGE / 1000;

  typedef asio::ip::udp udp;

  static const size_t MAX_WAITING_QUEUE_SIZE = 3000;

 public:
  UdpSender(asio::io_context& ioCtx, udp::socket& sock, const std::string& traceId)
      : traceId_(traceId), timer(ioCtx), socket_(sock), wqMutex(), waitingQueue{} {
    auto endpoint = socket_.local_endpoint();
    D_LOG("{}, UdpSender created: socket={} on {}:{}", traceId_, socket_.is_open(),
          endpoint.address().to_string(), endpoint.port());
    closed = false;
  }

  ~UdpSender() { T_LOG("UdpSender::~UdpSender"); };

  void send(std::queue<std::vector<uint8_t>>&& sendQueue, const udp::endpoint& target) {
    sendTasks.fetch_add(1);
    if (closed) {
      W_LOG("UdpSender::send: UdpSender is closed.");
      sendTasks.fetch_sub(1);
      return;
    }

    if (!sendQueue.empty()) {
      std::lock_guard<std::mutex> lg(wqMutex);
      // std::queue<std::vector<uint8_t>> queue;
      // queue.swap(sendQueue);
      waitingQueue.emplace(std::pair{std::move(sendQueue), target});
    }

    if (!isSending) {
      doSend();
    } else {
      sendTasks.fetch_sub(1);
    }
  };

  void send(std::queue<std::vector<uint8_t>>&& sendQueue,
            const std::vector<udp::endpoint>& targets) {
    sendTasks.fetch_add(1);
    if (closed) {
      W_LOG("UdpSender::send: UdpSender is closed.");
      sendTasks.fetch_sub(1);
      return;
    }

    if (!sendQueue.empty() && !targets.empty()) {
      // get data copy ready.
      std::vector<std::queue<std::vector<uint8_t>>> dataCopys(targets.size());
      while (!sendQueue.empty()) {
        dataCopys[0].emplace(std::move(sendQueue.front()));
        sendQueue.pop();
        const std::vector<uint8_t>& dataSrc = dataCopys[0].back();
        for (size_t i = 1; i < dataCopys.size(); i++) {
          dataCopys[i].emplace(dataSrc);
        }
      }

      {
        std::lock_guard<std::mutex> lg(wqMutex);
        for (size_t i = 0; i < targets.size(); i++) {
          waitingQueue.emplace(std::pair{std::move(dataCopys[i]), targets[i]});
        }
      }
    }

    if (!isSending) {
      doSend();
    } else {
      sendTasks.fetch_sub(1);
    }
  };


  void close() {
    std::lock_guard<std::mutex> lg(wqMutex);
    closed = true;
  };

  bool isStopped() { return closed && !isSending && sendTasks == 0; };

  size_t getSendCount() const { return count; };


  void setBandwidth(size_t maxBytesPerSecond, size_t bandwidthControlUnitInMs) {
    static const size_t bandwidthControlInterverInMs = 1000;

    {
      std::lock_guard<std::mutex> lg(wqMutex);
      maxBytesPerInterval = maxBytesPerSecond;
      bandwidthCalcUnitInMs = bandwidthControlUnitInMs;
      bandwidthCalcIntervalInNumberOfUnit =
          std::ceil(bandwidthControlInterverInMs * 1.0f / bandwidthCalcUnitInMs);
      bytesCounter.reset(bandwidthCalcIntervalInNumberOfUnit);
    }

    T_LOG(
        "setBandwidth: maxBytesPerInterval={} bandwidthCalcUnitInMs={} "
        "bandwidthCalcIntervalInNumberOfUnit={}",
        maxBytesPerInterval, bandwidthCalcUnitInMs, bandwidthCalcIntervalInNumberOfUnit);
  }

  void setCongestionControl(bool value) {
    size_t maxBytesPerSecond = 0;
    if (value) {
      maxBytesPerSecond = 5 * 1024 * 1024;
    }
    setBandwidth(maxBytesPerSecond, 50);
  }


  const std::string traceId_;


 private:
  asio::steady_timer timer;
  udp::socket& socket_;

  std::mutex wqMutex;
  std::atomic<bool> isSending = false;
  std::atomic<bool> closed = true;
  std::atomic<int> sendTasks{0};
  std::queue<std::pair<std::queue<std::vector<uint8_t>>, udp::endpoint>> waitingQueue;

  std::vector<uint8_t> sendBuff;
  udp::endpoint target;

  size_t count = 0;


  size_t bandwidthCalcUnitInMs = 50;
  uint16_t bandwidthCalcIntervalInNumberOfUnit = 20;
  size_t maxBytesPerInterval = 0;


  // bool enableBytesCounter{false};
  // BytesCounter bytesCounter{CONTROL_RANGE};
  BytesCounter bytesCounter{bandwidthCalcIntervalInNumberOfUnit};

  // bool tooFast = false;

  void doSend() {
    // T_LOG("UdpSender::doSend ---  0  ---");
    std::lock_guard<std::mutex> lg(wqMutex);

    if (closed) {
      W_LOG("UdpSender::doSend: UdpSender is closed.");
      sendTasks.fetch_sub(1);
      return;
    }


    if (!waitingQueue.empty()) {
      if (isSending) {
        sendTasks.fetch_sub(1);
        return;
      }
      isSending = true;

      auto& dataQueue = waitingQueue.front().first;

      if (dataQueue.empty()) {
        E_LOG("send empty data, it should never happen.");
        waitingQueue.pop();
        sendTasks.fetch_sub(1);
        return;
      }


      size_t nextPacketSize = dataQueue.front().size();

      // TODO check bytes
      int64_t bandwidthTime = seeker::time::currentTime() / bandwidthCalcUnitInMs;

      size_t byteSentInInterval = 0;
      if (maxBytesPerInterval) {
        byteSentInInterval = bytesCounter.count(bandwidthTime) + nextPacketSize;
      }

      size_t waitingSize = waitingQueue.size();


      if (waitingSize > MAX_WAITING_QUEUE_SIZE) {
        E_LOG("traceId={}, waitingQueue is full waitingQueue size={}. Throw all data in it.",
              traceId_, waitingSize);
        while (!waitingQueue.empty()) {
          waitingQueue.pop();
        }
        sendTasks.fetch_sub(1);
        isSending = false;
        return;
      } else if (waitingSize > 256) {
        E_LOG("traceId={}, Udp waitingQueue is too long:  waitingQueue size={}", traceId_,
              waitingSize);
      } else if (waitingSize > 48) {
        W_LOG("traceId={}, Udp waitingQueue is too long:  waitingQueue size={}", traceId_,
              waitingSize);
      } else if (waitingSize > 24) {
        I_LOG("traceId={}, Udp waitingQueue is too long:  waitingQueue size={}", traceId_,
              waitingSize);
      } else if (waitingSize > 12) {
        D_LOG("traceId={}, Udp waitingQueue is too long:  waitingQueue size={}", traceId_,
              waitingSize);
      }

      if (byteSentInInterval > maxBytesPerInterval) {
        if (waitingSize > 32) {
          W_LOG("traceId={}, bandwidth reach: byteSentInInterval={} bytes, queue size={}",
                traceId_, byteSentInInterval, waitingSize);
        } else if (waitingSize > 16) {
          I_LOG("traceId={}, bandwidth reach: byteSentInInterval={} bytes, queue size={}",
                traceId_, byteSentInInterval, waitingSize);
        } else if (waitingSize > 8) {
          D_LOG("traceId={}, bandwidth reach: byteSentInInterval={} bytes, queue size={}",
                traceId_, byteSentInInterval, waitingSize);
        } else {
          T_LOG("traceId={}, bandwidth reach: byteSentInInterval={} bytes, queue size={}",
                traceId_, byteSentInInterval, waitingSize);
        }

        if (sendTasks.load() > 3) {
          // Too maney sendTasks, skip this send chance.
          // Do nothing,
          sendTasks.fetch_sub(1);
          isSending = false;
        } else {
          timer.expires_from_now(std::chrono::milliseconds(bandwidthCalcUnitInMs / 2));
          timer.async_wait([this](const std::error_code& err) {
            doSendFinish(err, 0);
            sendTasks.fetch_sub(1);
          });
        }

      } else {
        if (waitingSize > 32) {
          W_LOG("traceId={}, bandwidth not reach: byteSentInInterval={} bytes, queue size={}",
                traceId_, byteSentInInterval, waitingSize);
        } else if (waitingSize > 24) {
          I_LOG("traceId={}, bandwidth not reach: byteSentInInterval={} bytes, queue size={}",
                traceId_, byteSentInInterval, waitingSize);
        } else if (waitingSize > 16) {
          D_LOG("traceId={}, bandwidth not reach: byteSentInInterval={} bytes, queue size={}",
                traceId_, byteSentInInterval, waitingSize);
        } else {
          T_LOG("traceId={}, bandwidth not reach: byteSentInInterval={} bytes, queue size={}",
                traceId_, byteSentInInterval, waitingSize);
        }

        auto& data = dataQueue.front();
        auto& currentTarget = waitingQueue.front().second;
        sendBuff.swap(data);
        target = currentTarget;

        if (maxBytesPerInterval) {
          bytesCounter.update(bandwidthTime, sendBuff.size());
        }

        dataQueue.pop();
        if (dataQueue.empty()) {
          waitingQueue.pop();
        }

        socket_.async_send_to(asio::buffer(sendBuff), target,
                              [this](std::error_code ec, std::size_t len) {
                                doSendFinish(ec, len);
                                sendTasks.fetch_sub(1);
                              });
      }
    } else {
      sendTasks.fetch_sub(1);
    }
  };

  void doSendFinish(const asio::error_code& ex, std::size_t len) {
    sendTasks.fetch_add(1);
    isSending = false;

    // The file handle supplied is not valid.
    if (ex) {
      E_LOG("doSendFinish: failed. ec={}, errMsg={}", ex.value(), ex.message());
      sendTasks.fetch_sub(1);
      return;
    }

    if (len > 0) {
      count += 1;
    }

    if (closed) {
      D_LOG("UdpSender::doSendFinish: UdpSender is closed.");
      sendTasks.fetch_sub(1);
      return;
    }

    doSend();
  };
};

/**
@project rtpTrs
@author Tao Zhang
@since 2023/3/17
@version 0.0.1-SNAPSHOT 2023/3/17
*/

#pragma once

#include "config.h"

#include "seeker/loggerApi.h"

#include "asio.hpp"

#include <iostream>
#include <atomic>
#include <vector>

class UdpProcessor {
 public:
  typedef asio::ip::udp udp;
  
  // return false when push failed and input will not modified
  virtual bool pushUdp(std::vector<uint8_t>&& udp, const udp::endpoint& from) = 0;
};

inline constexpr size_t buffCount = 100;


class UdpReceiver {
  typedef asio::ip::udp udp;

  struct RecvBuff {
    std::vector<uint8_t> buff;
    udp::endpoint from;
  };

 public:
  UdpReceiver(udp::socket& sock, UdpProcessor* processor, const std::string& traceId_)
      : traceId_(traceId_), socket_(sock), processor_(processor), recvBuf(2048) {
    auto endpoint = socket_.local_endpoint();
    D_LOG("{}, UdpReceiver created: socket={} on {}:{}", traceId_, socket_.is_open(),
          endpoint.address().to_string(), endpoint.port());
  }


  ~UdpReceiver() { T_LOG("{}, UdpReceiver::~UdpReceiver called.", traceId_); }

  void start() {
    if (!started) {
      D_LOG("{}, UdpReceiver::start", traceId_);
      started = true;
      startReceive();
    }
  };

  void close() { closed = true; };

  bool isStopped() { return closed && recvTasks == 0; }

  size_t getRecvCount() const { return count; }

  const std::string traceId_;

 private:
  std::atomic<bool> started = false;
  std::atomic<bool> closed = false;
  std::atomic<int> recvTasks{0};

  udp::socket& socket_;

  std::mutex pushMx;
  UdpProcessor* processor_;

  udp::endpoint udpFrom;
  std::vector<uint8_t> recvBuf;

  size_t count = 0;

  void startReceive() {
    T_LOG("startReceive  -- 0 --");
    recvTasks.fetch_add(1);
    if (closed) {
      W_LOG("{}, UdpReceiver::startReceive: UdpReceiver is closed.", traceId_);
      recvTasks.fetch_sub(1);
      return;
    }

    socket_.async_receive_from(asio::buffer(recvBuf), udpFrom,
                               [this](std::error_code ec, std::size_t len) {
                                 finishReceive(ec, len);
                                 recvTasks.fetch_sub(1);
                               });
  }


  // void finishReceive(uint8_t idx, const asio::error_code& ex, std::size_t len) {
  void finishReceive(const asio::error_code& ex, std::size_t len) {
#ifdef _WIN32
    if (ex.value() == 995) {
      I_LOG("{}, UdpReceiver ABORT error={}:{} len={} senderEndpoint={}:{}", traceId_,
            ex.value(), ex.message(), len, udpFrom.address().to_string(), udpFrom.port());
      closed = true;
      return;
    }

    if (ex.value() == 10058) {
      I_LOG("{}, UdpReceiver disallowed error={}:{} len={} senderEndpoint={}:{}", traceId_,
            ex.value(), ex.message(), len, udpFrom.address().to_string(), udpFrom.port());
      closed = true;
      return;
    }

    // No connection could be made because the target machine actively refused it
    if (ex.value() == 10061) {
      // W_LOG("UdpReceiver disallowed error={}:{} len={} senderEndpoint={}:{}", ex.value(),
      //       ex.message(), len, udpFrom.address().to_string(), udpFrom.port());
      startReceive();
      return;
    }

    // An existing connection was forcibly closed by the remote host.
    if (ex.value() == 10054) {
      // W_LOG("UdpReceiver disallowed error={}:{} len={} senderEndpoint={}:{}", ex.value(),
      //       ex.message(), len, udpFrom.address().to_string(), udpFrom.port());
      startReceive();
      return;
    }

#else
    if (ex.value() == 125) {
      I_LOG("jobId[{}] UdpReceiver aborted[{}]: {}. senderEndpoint={}:{}", traceId_,
            ex.value(), ex.message(), udpFrom.address().to_string(), udpFrom.port());
      closed = true;
      return;
    }
#endif


    if (ex.value()) {
      W_LOG("{}, ReceiveFinished unhandle error[{}]:{} len={} senderEndpoint={}:{}", traceId_,
            ex.value(), ex.message(), len, udpFrom.address().to_string(), udpFrom.port());
      closed = true;
      return;
    }

    if (closed) {
      T_LOG("{}, UdpReceiver::finishReceive: UdpReceiver is closed.", traceId_);
      return;
    }

    count += 1;

    std::vector<uint8_t> data{recvBuf.begin(), recvBuf.begin() + len};

    processor_->pushUdp(std::move(data), udpFrom);
    startReceive();
  }
};

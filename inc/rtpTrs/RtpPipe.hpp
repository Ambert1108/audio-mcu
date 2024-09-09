/**
@project rtpTrs
@author Tao Zhang
@since 2023/3/18
@version 0.0.1-SNAPSHOT 2023/3/18
*/

#pragma once

#include "config.h"

#include "seeker/loggerApi.h"
#include "Rtp.hpp"

#include <iostream>
#include <vector>
#include <deque>
#include <mutex>

/*
 * rtp 统计
 * jitter 计算方法 https://wiki.wireshark.org/RTP_statistics
 */


namespace seeker {

namespace rtp {

// MAX_RANGE / 2 is the real max value
// diff in [-MAX_RANGE/2, MAX_RANGE/2]
inline int getSeqDistance(uint16_t a, uint16_t b) {
  constexpr static int uint16max = 65535;
  int diff = (int)a - b;
  if (diff > uint16max >> 1) {
    diff -= (uint16max + 1);
  } else if (diff < -(uint16max >> 1)) {
    diff += (uint16max + 1);
  }
  return diff;
}


enum class RtpState {
  OK = 0,
  PACKAGE_LENGTH_ERROR,
  SSRC_ERROR,
  PAYLOAD_TYPE_ERROR,
  SEQ_ERROR,
};

enum class SeqState {
  ok = 0,
  jump,
  late,
  old,
  gap,
  duplicated,
  invalid,
};

using RawData = std::vector<uint8_t>;

struct SeqCounter {
  int lastSeq = -1;

  uint32_t packetCount_ = 0;
  uint32_t packetExpected_ = 0;

  uint32_t seqOk_ = 0;
  uint32_t seqLate_ = 0;
  uint32_t seqOld_ = 0;
  uint32_t seqJump_ = 0;
  uint32_t seqGap_ = 0;
  uint32_t seqDuplicated_ = 0;
  uint32_t seqInvalid_ = 0;

  uint32_t lost() { return packetExpected_ - seqOk_ - seqLate_ - seqGap_ - seqJump_; };

  float lostRate() { return packetExpected_ == 0 ? 0.0f : lost() * 1.0f / packetExpected_; };

  void reset() {
    lastSeq = -1;
    packetCount_ = 0;
    packetExpected_ = 0;
    seqOk_ = 0;
    seqLate_ = 0;
    seqOld_ = 0;
    seqJump_ = 0;
    seqGap_ = 0;
    seqDuplicated_ = 0;
    seqInvalid_ = 0;
  }
};


struct StreamStatus {
  uint32_t ssrc_{0};
  uint8_t payloadType_{0};
  int64_t startAt_{-1};
  int64_t lastRtpArrival_{-1};
  uint32_t dataBytes_{0};
  uint32_t validDataBytes_{0};
  uint32_t udpCount_{0};
  uint32_t validUdpCount_{0};
  uint32_t rtpCount_{0};
  uint32_t markCount_{0};
  uint32_t packageLengthError_{0};


  uint32_t ssrcError_{0};
  uint32_t payloadError_{0};

  void reset() {
    ssrc_ = 0;
    payloadType_ = 0;
    startAt_ = -1;
    lastRtpArrival_ = -1;
    dataBytes_ = 0;
    validDataBytes_ = 0;
    udpCount_ = 0;
    validUdpCount_ = 0;
    rtpCount_ = 0;
    markCount_ = 0;
    packageLengthError_ = 0;
    ssrcError_ = 0;
    payloadError_ = 0;
  }

  int64_t duration() { return lastRtpArrival_ - startAt_; };
};


class RtpPipe {
 private:
  struct LeadingRtp {
    uint16_t seq;
    std::vector<uint8_t> data;
    LeadingRtp(uint16_t s, std::vector<uint8_t>&& d) : seq(s), data(std::move(d)) {}

    bool operator<(const LeadingRtp& o) const {
      uint16_t bigValue = seq;
      uint16_t smallValue = o.seq;
      if (seq < o.seq) {
        bigValue = o.seq;
        smallValue = seq;
      }

      if (bigValue - smallValue > 65000) {
        return o.seq < seq;
      } else {
        return seq < o.seq;
      }
    }
  };


 private:
  const std::string traceId;
  const uint16_t reorderSize;
  const uint16_t arraySize;
  const uint16_t leadingReorderSize;

  std::vector<LeadingRtp> leadingRtpsToReorder{};  // 对开头的若干rtp进行排序

  uint8_t* seqArray;
  RawData* rtpDataArray;

  uint16_t headPos;
  uint16_t tailPos;

  SeqCounter seqCounter{};
  StreamStatus streamStatus{};

  std::mutex pipeMx;


 public:
  RtpPipe(const std::string& traceId, uint16_t reorderSize, uint16_t leadingReorderSize)
      : traceId(traceId),
        reorderSize{reorderSize},
        arraySize{static_cast<uint16_t>(2 * reorderSize + 1)},
        leadingReorderSize{leadingReorderSize},
        seqArray(new uint8_t[arraySize]),
        rtpDataArray(new std::vector<uint8_t>[arraySize]),
        headPos(0),
        tailPos(0) {
    memset(seqArray, 0, arraySize);
  };

  RtpPipe(const RtpPipe& o) = delete;
  RtpPipe& operator=(const RtpPipe& o) = delete;

  RtpPipe(RtpPipe&& o) noexcept
      : traceId(o.traceId),
        reorderSize{o.reorderSize},
        arraySize{o.arraySize},
        leadingReorderSize{o.leadingReorderSize},
        leadingRtpsToReorder{std::move(o.leadingRtpsToReorder)},
        seqArray(o.seqArray),
        rtpDataArray(o.rtpDataArray),
        headPos(o.headPos),
        tailPos(o.tailPos),
        seqCounter(o.seqCounter),
        streamStatus(o.streamStatus) {
    o.seqArray = nullptr;
    o.rtpDataArray = nullptr;
  };

  ~RtpPipe() {
    if (seqArray != nullptr) {
      delete[] seqArray;
      seqArray = nullptr;
    }

    if (rtpDataArray != nullptr) {
      delete[] rtpDataArray;
      rtpDataArray = nullptr;
    }
  }

  // inline uint32_t packetCount() { return seqCounter.packetCount_; };
  // inline uint32_t packetExpected() { return seqCounter.packetExpected_; };
  // inline uint32_t seqOk() { return seqCounter.seqOk_; };
  // inline uint32_t seqLate() { return seqCounter.seqLate_; };
  // inline uint32_t seqOld() { return seqCounter.seqOld_; };
  // inline uint32_t seqGap() { return seqCounter.seqGap_; };
  // inline uint32_t seqDuplicated() { return seqCounter.seqDuplicated_; };
  // inline uint32_t seqInvalid() { return seqCounter.seqInvalid_; };
  // inline uint32_t lost() { return seqCounter.lost(); };
  // inline float lostRate() { return seqCounter.lostRate(); };

  // uint32_t udpCount() { return streamStatus.udpCount_; };
  // uint32_t validUdpCount() { return streamStatus.validUdpCount_; };
  // uint32_t dataBytes() { return streamStatus.dataBytes_; };
  // uint32_t validDataBytes() { return streamStatus.validDataBytes_; };
  // uint32_t markCount() { return streamStatus.markCount_; };
  // int64_t startAt() { return streamStatus.startAt_; };
  // int64_t lastRtpArrival() { return streamStatus.lastRtpArrival_; };
  // int64_t duration() { return streamStatus.lastRtpArrival_ - streamStatus.startAt_; };
  // uint32_t packageLengthError() { return streamStatus.packageLengthError_; };
  // uint32_t ssrcError() { return streamStatus.ssrcError_; };
  // uint32_t payloadError() { return streamStatus.payloadError_; };

  StreamStatus getStatus() {
    std::lock_guard<std::mutex> lk{pipeMx};
    return streamStatus;
  };
  SeqCounter getSeqCounter() {
    std::lock_guard<std::mutex> lk{pipeMx};
    return seqCounter;
  };

  inline RtpState reorderRtp(RawData&& rtpIn, int64_t arrivedTimestamp,
                             std::deque<RawData>& outQueue);

  void resetPipe() {
    std::lock_guard<std::mutex> lk{pipeMx};
    seqCounter.reset();
    streamStatus.reset();
    leadingRtpsToReorder.clear();
    leadingRtpsToReorder.reserve(leadingReorderSize);
    resetPos();
  }


  inline SeqState updateDataForTest(uint16_t seq, std::vector<uint8_t>&& rtpData,
                                    std::deque<RawData>& outQueue) {
    // only for test
    return this->updateData(seq, std::move(rtpData), outQueue);
  }



 private:
  inline SeqState updateData(uint16_t seq, std::vector<uint8_t>&& rtpData,
                             std::deque<RawData>& outQueue);

  int takeRtps(std::deque<RawData>& outQueue, uint16_t forceRange) {
    int rst = 0;
    if (headPos == tailPos) {
      return rst;
    }

    int forwardCount = 0;
    uint16_t origin_tailPos = tailPos;

    uint16_t currentPos = back(headPos, 1);

    T_LOG("takeRtps forceRange={} headPos={} tailPos={} currentPos={}", forceRange, headPos,
          tailPos, currentPos);

    T_LOG("1 valid={} forwardDistance={}", (bool)seqArray[tailPos],
          (int)forwardDistance(currentPos, tailPos));
    // forced get data.
    for (uint16_t i = 0; i < forceRange && headPos != tailPos; i++) {
      if (seqArray[tailPos]) {
        if (rtpDataArray[tailPos].size() == 0) {
          D_LOG("takeRtps forceRange={} headPos={} tailPos={} currentPos={}", forceRange,
                headPos, tailPos, currentPos);
          D_LOG("takeRtps forceRange={} i={} tailPos={} forwardDistance={}", forceRange, i,
                tailPos, forwardDistance(currentPos, tailPos));
          return -1;
        }
        outQueue.emplace_back(std::move(rtpDataArray[tailPos]));
      }
      goForward(tailPos, 1);
      forwardCount += 1;
    }

    // get continue rtpData to outQueue.
    T_LOG("2 valid={} forwardDistance={}", (bool)seqArray[tailPos],
          (int)forwardDistance(currentPos, tailPos));

    bool error = headPos == tailPos;
    // if (error) {
    //   D_LOG(
    //       "A takeRtps forceRange={} headPos={} tailPos={} currentPos={} origin_tailPos={} "
    //       "forwardDistance={} forwardCount={}",
    //       forceRange, headPos, tailPos, currentPos, origin_tailPos,
    //       forwardDistance(currentPos, tailPos), forwardCount);
    // }

    while (seqArray[tailPos] && headPos != tailPos) {
      if (error) {
        D_LOG(
            "B takeRtps forceRange={} headPos={} tailPos={} currentPos={} origin_tailPos={} "
            "forwardDistance={} forwardCount={}",
            forceRange, headPos, tailPos, currentPos, origin_tailPos,
            forwardDistance(currentPos, tailPos), forwardCount);
      }

      if (rtpDataArray[tailPos].size() == 0) {
        D_LOG(
            "C takeRtps forceRange={} headPos={} tailPos={} currentPos={} origin_tailPos={} "
            "forwardDistance={} forwardCount={}",
            forceRange, headPos, tailPos, currentPos, origin_tailPos,
            forwardDistance(currentPos, tailPos), forwardCount);
        return -2;
      }
      outQueue.emplace_back(std::move(rtpDataArray[tailPos]));
      goForward(tailPos, 1);
      forwardCount += 1;
    }

    return rst;
  }


  uint16_t forward(uint16_t pos, uint16_t distance) {
    int rst = (int)pos + distance;
    return static_cast<uint16_t>(rst % arraySize);
  }

  void goForward(uint16_t& pos, uint16_t distance) { pos = forward(pos, distance); }

  uint16_t back(uint16_t pos, uint16_t distance) {
    int padding = 0;
    if (distance > pos) {
      padding = 1 + (distance / arraySize);
    }
    int rst = (int)pos - distance + (padding * arraySize);
    return static_cast<uint16_t>(rst % arraySize);
  }

  void goBack(uint16_t& pos, uint16_t distance) { pos = back(pos, distance); }


  // 以 pos1 领先或并列 pos2 为前提, 返回 pos1 领先 pos2 的距离
  // 返回值一定大于等于零
  uint16_t forwardDistance(uint16_t pos1, uint16_t pos2) {
    if (pos2 > pos1) {
      uint16_t padding = arraySize - pos2;
      return pos1 + padding;
    } else {
      return pos1 - pos2;
    }
  }


  void setSeqState(uint8_t v) {
    seqArray[headPos] = v;
    goForward(headPos, 1);
    seqCounter.packetExpected_ += 1;
  }


  void resetPos() {
    seqCounter.lastSeq = -1;
    headPos = 0;
    tailPos = 0;
    memset(seqArray, 0, arraySize);
  }



  // int getDiff(uint16_t a, uint16_t b, uint16_t MAX) {
  //   int diff = (int)a - b;
  //
  //   if (diff > MAX) {
  //     diff -= MAX;
  //   } else if (diff < -(MAX >> 1)) {
  //     diff += MAX;
  //   }
  //   return diff;
  // }

  uint16_t getPos(int diff) {
    if (diff > reorderSize || diff < -reorderSize) {
      throw std::runtime_error("diff > reorderSize || diff < -reorderSize");
    }

    if (diff == 0) {
      return headPos - 1;
    } else if (diff > 0) {
      int newPos = headPos + diff - 1;
      if (newPos >= arraySize) {
        newPos = newPos - arraySize;
      }
      return newPos;
    } else {
      int newPos = headPos + diff - 1;
      if (newPos < 0) {
        newPos = newPos + arraySize;
      }
      return newPos;
    }
  }
};



inline SeqState RtpPipe::updateData(uint16_t seq, std::vector<uint8_t>&& rtpData,
                                    std::deque<std::vector<uint8_t>>& outQueue) {
  static constexpr int MAX_DIFF = 5000;
  static constexpr int MIN_DIFF = -MAX_DIFF;

  T_LOG("traceId[{}] updateData begin: head={} tail={}  c={} seq={} lastSeq={}", traceId,
        headPos, tailPos, seqCounter.packetCount_, seq, seqCounter.lastSeq);

  seqCounter.packetCount_ += 1;

  if (seqCounter.lastSeq < 0) {
    outQueue.emplace_back(std::move(rtpData));
    setSeqState(1);
    goForward(tailPos, 1);
    seqCounter.seqOk_ += 1;
    seqCounter.lastSeq = seq;
    T_LOG("traceId[{}] updateData begin2: head={} tail={} seq={}", traceId, headPos, tailPos,
          seq);

    return SeqState::ok;
  }



  SeqState state{0};

  int diff = getSeqDistance(seq, seqCounter.lastSeq);
  T_LOG("traceId[{}] in updateData lastSeq={} head={} tail={} diff={}", traceId,
        seqCounter.lastSeq, headPos, tailPos, diff);

  if (diff == 1) {
    state = SeqState::ok;
    if (tailPos == headPos) {
      if (rtpData.size() == 0) {
        std::cout << "updateData -- 0.1 --: rtpData.size()=0 seq=" << (int)seq << std::endl;
        exit(0);
      }
      outQueue.emplace_back(std::move(rtpData));
      setSeqState(1);
      goForward(tailPos, 1);
    } else {
      rtpDataArray[headPos].swap(rtpData);
      T_LOG("traceId[{}] store data 0: pos={} size={}", traceId, headPos,
            rtpDataArray[headPos].size());
      setSeqState(1);

      int d1 = forwardDistance(headPos, tailPos);
      int forceRange = d1 - reorderSize;
      T_LOG("traceId[{}] d1={} forceRange={}", traceId, d1, forceRange);

      if (forceRange > 0) {
        int rst = takeRtps(outQueue, forceRange);
        if (rst < 0) {
          E_LOG("traceId[{}] error: state={} diff={} seq={} lastSeq={} d1={} forceRange={}",
                traceId, (int)state, diff, seq, seqCounter.lastSeq, d1, forceRange);
          exit(0);
        }
      }
    }
  } else if (diff > 1 && diff <= reorderSize) {
    state = SeqState::jump;
    for (int i = 0; i < diff - 1; i++) {
      setSeqState(0);
    }
    rtpDataArray[headPos].swap(rtpData);
    T_LOG("traceId[{}] store data 1: pos={} size={}", traceId, headPos,
          rtpDataArray[headPos].size());
    setSeqState(1);

    int d1 = forwardDistance(headPos, tailPos);
    int forceRange = d1 - reorderSize;
    T_LOG("traceId[{}] d1={} forceRange={}", traceId, d1, forceRange);


    // ONLY FOR DEBUG
    T_LOG("T traceId[{}] jump: state={} diff={} seq={} lastSeq={} d1={} forceRange={}",
          traceId, (int)state, diff, seq, seqCounter.lastSeq, d1, forceRange);


    if (forceRange > 0) {
      int rst = takeRtps(outQueue, forceRange);
      if (rst < 0) {
        E_LOG("traceId[{}] error: state={} diff={} seq={} lastSeq={} d1={} forceRange={}",
              traceId, (int)state, diff, seq, seqCounter.lastSeq, d1, forceRange);
        exit(0);
      }
    }
  } else if (diff > reorderSize && diff < MAX_DIFF) {
    state = SeqState::gap;
    T_LOG("traceId[{}] gap", traceId);

    int rst = takeRtps(outQueue, reorderSize);
    if (rst < 0) {
      E_LOG("traceId[{}] error: state={} diff={} seq={} lastSeq={} reorderSize={}", traceId,
            (int)state, diff, seq, seqCounter.lastSeq, reorderSize);
      exit(0);
    }
    resetPos();

    if (rtpData.size() == 0) {
      std::cout << "updateData -- 0.2 --: rtpData.size()=0 seq=" << (int)seq << std::endl;
      exit(0);
    }
    outQueue.emplace_back(std::move(rtpData));
    setSeqState(1);
    goForward(tailPos, 1);

    seqCounter.packetExpected_ += diff - 1;  // 2023-3-19
  } else if (diff == 0) {
    D_LOG("traceId[{}] SeqState seq={} lastSeq={} diff={}", traceId, seq, seqCounter.lastSeq,
          diff);
    state = SeqState::duplicated;
    D_LOG("traceId[{}] SeqState duplicated1: seq={}", traceId, seq);
  } else if (diff < 0 && diff >= -reorderSize) {
    uint16_t pos = getPos(diff);
    T_LOG("traceId[{}] SeqState seq={} lastSeq={} diff={} pos={}", traceId, seq,
          seqCounter.lastSeq, diff, pos);
    if (seqArray[pos]) {
      state = SeqState::duplicated;
      D_LOG("traceId[{}] SeqState duplicated2: seq={}", traceId, seq);
    } else {
      int inRtpDistance = -diff;
      uint16_t currentPos = back(headPos, 1);
      int tailDistance = headPos == tailPos ? -1 : forwardDistance(currentPos, tailPos);
      T_LOG("traceId[{}] store: inRtpDistance={} tailDistance={}", traceId, inRtpDistance,
            tailDistance);

      if (inRtpDistance <= tailDistance) {
        rtpDataArray[pos].swap(rtpData);
        T_LOG("traceId[{}] late rtp store data 1: pos={} size={}", traceId, pos,
              rtpDataArray[pos].size());
        seqArray[pos] = 1;
        state = SeqState::late;
        T_LOG("traceId[{}] SeqState late: seq={}", traceId, seq);
        takeRtps(outQueue, 0);
        T_LOG("traceId[{}] SeqState late:  --- 2 --- size={}", traceId, outQueue.size());
      } else {
        state = SeqState::old;
        T_LOG("traceId[{}] SeqState old1", traceId);
      }
    }
  } else if (diff < -reorderSize && diff > MIN_DIFF) {
    state = SeqState::old;
    T_LOG("traceId[{}] SeqState old2 ", traceId);
  } else {
    state = SeqState::invalid;
    resetPos();
    // FOR DEBUG
    T_LOG("traceId[{}] error: state={} diff={} seq={} lastSeq={}", traceId, (int)state, diff,
          seq, seqCounter.lastSeq);
  }

  switch (state) {
    case SeqState::ok:
      seqCounter.seqOk_ += 1;
      seqCounter.lastSeq = seq;
      break;
    case SeqState::jump:
      seqCounter.seqJump_ += 1;
      seqCounter.lastSeq = seq;
      break;
    case SeqState::late:
      seqCounter.seqLate_ += 1;
      break;
    case SeqState::old:
      seqCounter.seqOld_ += 1;
      break;
    case SeqState::gap:
      seqCounter.lastSeq = seq;
      seqCounter.seqGap_ += 1;
      break;
    case SeqState::duplicated:
      seqCounter.seqDuplicated_ += 1;
      break;
    case SeqState::invalid:
      seqCounter.seqInvalid_ += 1;
      break;
    default:
      break;
  }

  T_LOG("traceId[{}] updateData end: head={} tail={} state={}", traceId, headPos, tailPos,
        (int)state);
  return state;
}


inline RtpState RtpPipe::reorderRtp(std::vector<uint8_t>&& rtpData, int64_t arrivedTimestamp,
                                    std::deque<std::vector<uint8_t>>& outQueue) {
  std::lock_guard<std::mutex> lk{pipeMx};

  const uint8_t* rtp = rtpData.data();
  const size_t len = rtpData.size();

  streamStatus.dataBytes_ += len;
  streamStatus.udpCount_ += 1;

  if (len < 12) {
    return RtpState::PACKAGE_LENGTH_ERROR;
  }

  if (streamStatus.startAt_ <= 0) {
    streamStatus.startAt_ = arrivedTimestamp;
    streamStatus.ssrc_ = getSsrc(rtp);
    streamStatus.payloadType_ = getPayloadType(rtp);
  }

  if (getPayloadType(rtp) != streamStatus.payloadType_) {
    streamStatus.payloadError_ += 1;
    return RtpState::PAYLOAD_TYPE_ERROR;
  }

  if (getSsrc(rtp) != streamStatus.ssrc_) {
    streamStatus.ssrcError_ += 1;
    return RtpState::SSRC_ERROR;
  }

  streamStatus.lastRtpArrival_ = arrivedTimestamp;

  bool mark = getMark(rtp);
  if (mark) streamStatus.markCount_ += 1;
  streamStatus.rtpCount_ += 1;

  if (streamStatus.rtpCount_ <= leadingReorderSize) {
    // push current rtp to leadingRtps, and return.
    uint16_t seq = rtp::getSeq(rtp);
    leadingRtpsToReorder.emplace_back(seq, std::move(rtpData));
    if (leadingRtpsToReorder.size() == leadingReorderSize) {
      std::sort(leadingRtpsToReorder.begin(), leadingRtpsToReorder.end());
      for (LeadingRtp& leadingRtp : leadingRtpsToReorder) {
        seq = getSeq(leadingRtp.data.data());
        SeqState rst = updateData(seq, std::move(leadingRtp.data), outQueue);
        if (rst != SeqState::invalid && rst != SeqState::duplicated) {
          streamStatus.validDataBytes_ += len;
          streamStatus.validUdpCount_ += 1;
        }
      }
      leadingRtpsToReorder.clear();
    } else if (leadingRtpsToReorder.size() > leadingReorderSize) {
      E_LOG(
          "traceId[{}] rtpPipe error: leadingRtpsToReorder.size [{}] > leadingReorderSize "
          "[{}], it should never happen.",
          traceId, leadingRtpsToReorder.size(), leadingReorderSize);
    }
    return RtpState::OK;
  }


  uint16_t seq = getSeq(rtp);
  SeqState rst = updateData(seq, std::move(rtpData), outQueue);
  T_LOG("traceId[{}] reorderRtp seq={} rst={}", traceId, seq, (int)rst);

  if (rst == SeqState::invalid || rst == SeqState::duplicated) {
    return RtpState::SEQ_ERROR;
  } else {
    streamStatus.validDataBytes_ += len;
    streamStatus.validUdpCount_ += 1;
    return RtpState::OK;
  }
}



}  // namespace rtp
}  // namespace seeker

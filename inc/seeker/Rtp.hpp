/**
@project rtpTrs
@author Tao Zhang
@since 2023/3/13
@version 0.0.1-SNAPSHOT 2023/3/16
*/



#pragma once

#include "config.h"

#include "seeker/common.h"

#include <iostream>
#include <vector>



namespace seeker {

namespace rtp {

inline uint8_t getVersion(const uint8_t* rtpHeader) {
  uint8_t firstByte = rtpHeader[0];
  uint8_t version = (firstByte >> 6) & 0x03;
  return version;
}

inline bool getPadding(const uint8_t* rtpHeader) {
  uint8_t firstByte = rtpHeader[0];
  uint8_t padding = (firstByte >> 5) & 0x01;
  return padding != 0;
}

inline bool getMarker(const uint8_t* rtpHeader) {
  uint8_t secondByte = rtpHeader[1];
  bool marker = (secondByte >> 7) & 0x01;
  return marker;
}


inline int getPayloadType(const uint8_t* rtpHeader) {
  uint8_t secondByte = rtpHeader[1];
  int payloadType = (secondByte >> 0) & 0x7F;
  return payloadType;
}

inline bool getMark(const uint8_t* rtpHeader) {
  uint8_t secondByte = rtpHeader[1];
  bool marker = (secondByte >> 7) & 0x01;
  return marker;
}


inline uint16_t getSeq(const uint8_t* rtpHeader) {
  constexpr uint8_t POSITION = 2;
  uint16_t seqNum{0};
  seeker::bytes::readData(rtpHeader + POSITION, seqNum, false);
  return seqNum;
}


inline size_t getExtensionLength(const uint8_t* rtpHeader) {
  uint8_t firstByte = rtpHeader[0];
  uint8_t csrcCount = (firstByte >> 0) & 0x0F;
  uint8_t extension = (firstByte >> 4) & 0x01;

  size_t extension_length = 0;
  if (extension != 0) {
    // https://www.rfc-editor.org/rfc/rfc3550.html#section-5.3.1
    size_t pos = 12 + csrcCount * sizeof(uint32_t) + 2;
    uint16_t ext32BitCount{0};
    seeker::bytes::readData(rtpHeader + pos, ext32BitCount, false);
    extension_length = (ext32BitCount + 1) << 2;
  }

  return extension_length;
}


inline uint32_t getTimestamp(const uint8_t* rtpHeader) {
  constexpr uint8_t POSITION = 4;
  uint32_t timestamp{0};
  seeker::bytes::readData(rtpHeader + POSITION, timestamp, false);
  return timestamp;
}


inline uint32_t getSsrc(const uint8_t* rtpHeader) {
  constexpr uint8_t POSITION = 8;
  uint32_t ssrc{0};
  seeker::bytes::readData(rtpHeader + POSITION, ssrc, false);
  return ssrc;
}


class Rtp {
  static constexpr uint8_t header_version{2};
  static constexpr uint8_t header_padding{0};
  static constexpr uint8_t header_extension{0};
  static constexpr uint8_t header_csrcCount{0};
  static constexpr size_t HEADER_FIXED_LENGTH = 12;


 public:
  Rtp() : udpBuff(){};
  Rtp(std::vector<uint8_t>&& udp) : udpBuff(std::move(udp)) {}

  Rtp(Rtp&& o) noexcept : udpBuff(std::move(o.udpBuff)){};

  Rtp(const Rtp& o) = delete;
  Rtp& operator=(const Rtp& o) noexcept = delete;
  Rtp& operator=(Rtp&& o) noexcept {
    udpBuff = std::move(o.udpBuff);
    return *this;
  }

  Rtp(int pt, bool marker, uint16_t seq, uint32_t ts, int32_t ssrc,
      const std::vector<uint8_t>& payload) {
    if (pt > 127 || pt < 0)
      throw std::runtime_error("Rtp header error: pt = " + std::to_string(pt));


    udpBuff.resize(HEADER_FIXED_LENGTH + payload.size());

    uint8_t marker_ = marker ? 1 : 0;
    uint8_t payloadType_ = pt;
    uint16_t seqNum_ = seq;
    uint32_t timestamp_ = ts;
    uint32_t ssrc_ = ssrc;

    auto* buf = udpBuff.data();

    uint8_t firstByte{0x00};
    uint8_t secondByte{0x00};

    firstByte =
        header_version << 6 | header_padding << 5 | header_extension << 4 | header_csrcCount;
    secondByte = marker << 7 | payloadType_;

    size_t pos{0};

    seeker::bytes::writeData(buf + pos, firstByte, false);
    pos += sizeof(firstByte);
    seeker::bytes::writeData(buf + pos, secondByte, false);
    pos += sizeof(secondByte);
    seeker::bytes::writeData(buf + pos, seqNum_, false);
    pos += sizeof(seqNum_);
    seeker::bytes::writeData(buf + pos, timestamp_, false);
    pos += sizeof(timestamp_);
    seeker::bytes::writeData(buf + pos, ssrc_, false);
    pos += sizeof(ssrc_);

    if (pos != 12) {
      throw std::logic_error("it should never happen: pos != 12");
    }

    memcpy(buf + pos, payload.data(), sizeof(uint8_t) * payload.size());
    pos += sizeof(uint8_t) * payload.size();
  }


  void reset(std::vector<uint8_t>&& udp) { udpBuff.swap(udp); }

  void cloneRawData(std::vector<uint8_t>& buf) { buf.assign(udpBuff.begin(), udpBuff.end()); }

  std::vector<uint8_t> cloneRawData() { return udpBuff; }

  void takeRawData(std::vector<uint8_t>& buf) { buf.swap(udpBuff); }

  std::vector<uint8_t>&& takeRawData() { return std::move(udpBuff); }

  const std::vector<uint8_t>& getRawData() { return udpBuff; }


  void getPayload(std::vector<uint8_t>& buf) {
    size_t begin = getHeaderLength();
    bool padding = getPadding(udpBuff.data());
    uint8_t paddingLength = 0;
    if (padding) {
      paddingLength = udpBuff[udpBuff.size() - 1];
    }
    size_t end = udpBuff.size() - paddingLength;
    buf.assign(udpBuff.data() + begin, udpBuff.data() + end);
  }

  const uint8_t* getPayload(size_t& len) {
    size_t begin = getHeaderLength();
    bool padding = getPadding(udpBuff.data());
    uint8_t paddingLength = 0;
    if (padding) {
      paddingLength = udpBuff[udpBuff.size() - 1];
    }
    size_t end = udpBuff.size() - paddingLength;

    len = end - begin;
    return udpBuff.data() + begin;
  }

  void getHeader(std::vector<uint8_t>& buf) {
    size_t begin = 0;
    size_t end = getHeaderLength();
    buf.assign(udpBuff.data() + begin, udpBuff.data() + end);
  }

  const uint8_t* getHeader(size_t& len) {
    size_t begin = 0;
    size_t end = getHeaderLength();
    len = end - begin;
    return udpBuff.data() + begin;
  }


  // TODO 设置为静态方法, 不生成 Rtp 对象
  // bool isValid() {
  //  if (udpBuff.size() < HEADER_FIXED_LENGTH) {
  //    return false;
  //  }
  //
  //  uint8_t firstByte = udpBuff.at(0);
  //
  //  uint8_t csrcCount = (firstByte >> 0) & 0x0F;
  //  uint8_t version = (firstByte >> 6) & 0x03;
  //  uint8_t extension = (firstByte >> 4) & 0x01;
  //
  //  if (version != 2) {
  //    return false;
  //  }
  //
  //  if (udpBuff.size() < HEADER_FIXED_LENGTH + csrcCount * sizeof(uint32_t)) {
  //    return false;
  //  }
  //
  //  if (extension != 0) {
  //    if (udpBuff.size() < HEADER_FIXED_LENGTH + csrcCount * sizeof(uint32_t) + 4) {
  //      return false;
  //    }
  //
  //    size_t extensionLen = getExtensionLength(udpBuff.data());
  //
  //    if (udpBuff.size() < HEADER_FIXED_LENGTH + csrcCount * sizeof(uint32_t) + extensionLen)
  //    {
  //      return false;
  //    }
  //  }
  //
  //  return true;
  //}



  size_t getHeaderLength() {
    if (udpBuff.empty()) {
      throw std::runtime_error("udpBuff.empty()");
    }

    uint8_t firstByte = udpBuff.at(0);

    uint8_t csrcCount = (firstByte >> 0) & 0x0F;
    uint8_t version = (firstByte >> 6) & 0x03;
    uint8_t padding = (firstByte >> 5) & 0x01;
    uint8_t extension = (firstByte >> 4) & 0x01;

    if (udpBuff.size() < (HEADER_FIXED_LENGTH + csrcCount * sizeof(uint32_t))) {
      throw std::runtime_error(
          "udpBuff.size() < (HEADER_FIXED_LENGTH + csrcCount * sizeof(uint32_t))");
    }

    size_t extension_length = 0;
    if (extension != 0) {
      // https://www.rfc-editor.org/rfc/rfc3550.html#section-5.3.1

      if (udpBuff.size() < (HEADER_FIXED_LENGTH + csrcCount * sizeof(uint32_t)) + 4) {
        throw std::runtime_error(
            "udpBuff.size() < (HEADER_FIXED_LENGTH + csrcCount * sizeof(uint32_t)) + 4");
      }

      size_t pos = HEADER_FIXED_LENGTH + csrcCount * sizeof(uint32_t) + 2;
      uint16_t ext32BitCount{0};
      seeker::bytes::readData(udpBuff.data() + pos, ext32BitCount, false);
      extension_length = ((size_t)ext32BitCount + 1) << 2;
    }

    return HEADER_FIXED_LENGTH + csrcCount * sizeof(uint32_t) + extension_length;
  };


  int32_t ssrc() { return (int32_t)getSsrc(udpBuff.data()); }

  uint32_t timestamp() { return getTimestamp(udpBuff.data()); };

  int payloadType() { return getPayloadType(udpBuff.data()); };

  bool mark() { return getMarker(udpBuff.data()); };

  uint16_t seq() { return getSeq(udpBuff.data()); };

  size_t length() { return udpBuff.size(); };

  inline static bool isValid(const std::vector<uint8_t>& rtpData);

 private:
  std::vector<uint8_t> udpBuff;
};


bool Rtp::isValid(const std::vector<uint8_t>& rtpData) {
  if (rtpData.size() < HEADER_FIXED_LENGTH) {
    return false;
  }

  uint8_t firstByte = rtpData.at(0);

  uint8_t csrcCount = (firstByte >> 0) & 0x0F;
  uint8_t version = (firstByte >> 6) & 0x03;
  uint8_t extension = (firstByte >> 4) & 0x01;

  if (version != 2) {
    return false;
  }

  if (rtpData.size() < HEADER_FIXED_LENGTH + csrcCount * sizeof(uint32_t)) {
    return false;
  }

  if (extension != 0) {
    if (rtpData.size() < HEADER_FIXED_LENGTH + csrcCount * sizeof(uint32_t) + 4) {
      return false;
    }

    size_t extensionLen = getExtensionLength(rtpData.data());

    if (rtpData.size() < HEADER_FIXED_LENGTH + csrcCount * sizeof(uint32_t) + extensionLen) {
      return false;
    }
  }

  return true;
}


}  // namespace rtp



}  // namespace seeker

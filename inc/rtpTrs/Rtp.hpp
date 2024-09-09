/**
@project rtpTrs
@author Tao Zhang
@since 2023/3/13
@version 0.0.1-SNAPSHOT 2023/10/9
*/



#pragma once

#include "config.h"

#include "seeker/common.h"

#include <iostream>
#include <vector>


/*

https://www.rfc-editor.org/rfc/rfc3550.html

5.1 RTP Fixed Header Fields

   The RTP header has the following format:

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P|X|  CC   |M|     PT      |       sequence number         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           timestamp                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |           synchronization source (SSRC) identifier            |
   +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
   |            contributing source (CSRC) identifiers             |
   |                             ....                              |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

*/


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

// deprecated
inline bool getMarker(const uint8_t* rtpHeader) {
  uint8_t secondByte = rtpHeader[1];
  bool marker = (secondByte >> 7) & 0x01;
  return marker;
}


inline bool getMark(const uint8_t* rtpHeader) {
  uint8_t secondByte = rtpHeader[1];
  bool marker = (secondByte >> 7) & 0x01;
  return marker;
}


inline uint8_t getPayloadType(const uint8_t* rtpHeader) {
  uint8_t secondByte = rtpHeader[1];
  uint8_t payloadType = (secondByte >> 0) & 0x7F;
  return payloadType;
}


inline uint8_t getCsrcCount(const uint8_t* rtpHeader) {
  uint8_t firstByte = rtpHeader[0];
  uint8_t csrcCount = firstByte & 0x07;
  return csrcCount;
}


inline uint16_t getSeq(const uint8_t* rtpHeader) {
  static constexpr uint8_t POSITION = 2;
  uint16_t seqNum{0};
  seeker::bytes::readData(rtpHeader + POSITION, seqNum, false);
  return seqNum;
}


inline void setSeq(uint8_t* rtpHeader, uint16_t seq) {
  static constexpr uint8_t POSITION = 2;
  seeker::bytes::writeData<uint16_t>(rtpHeader + POSITION, seq, false);
}


inline bool hasExtension(const uint8_t* rtpHeader) {
  uint8_t firstByte = rtpHeader[0];
  uint8_t extension = (firstByte >> 4) & 0x01;
  return extension != 0;
}

// include extension header (defined_by_profile + length + header_extension)
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
    extension_length = (static_cast<size_t>(ext32BitCount) + 1) << 2;
  }

  return extension_length;
}


inline uint32_t getTimestamp(const uint8_t* rtpHeader) {
  static constexpr uint8_t POSITION = 4;
  uint32_t timestamp{0};
  seeker::bytes::readData(rtpHeader + POSITION, timestamp, false);
  return timestamp;
}


inline void setTimestamp(uint8_t* rtpHeader, uint32_t ts) {
  static constexpr uint8_t POSITION = 4;
  seeker::bytes::writeData<uint32_t>(rtpHeader + POSITION, ts, false);
}


inline uint32_t getSsrc(const uint8_t* rtpHeader) {
  static constexpr uint8_t POSITION = 8;
  uint32_t ssrc{0};
  seeker::bytes::readData(rtpHeader + POSITION, ssrc, false);
  return ssrc;
}

inline void setSsrc(uint8_t* rtpHeader, uint32_t ssrc) {
  static constexpr uint8_t POSITION = 8;
  seeker::bytes::writeData<uint32_t>(rtpHeader + POSITION, ssrc, false);
}


class Rtp {
  static constexpr uint8_t header_version{2};
  static constexpr uint8_t header_padding{0};
  static constexpr size_t HEADER_FIXED_LENGTH = 12;


  inline static const std::vector<uint8_t> EMPTY_DATA{};

  // const uint8_t header_extension{0};
  // const uint8_t header_csrcCount{0};

 public:
  Rtp() : udpBuff(), isValid_(false){};
  Rtp(std::vector<uint8_t>&& udp)
      : udpBuff(std::move(udp)), isValid_(Rtp::isValidRtp(udpBuff)) {}

  Rtp(Rtp&& o) noexcept : udpBuff(std::move(o.udpBuff)), isValid_(o.isValid_){};

  Rtp(const Rtp& o) : udpBuff(o.udpBuff), isValid_(o.isValid_){};

  Rtp& operator=(const Rtp& o) noexcept = delete;
  Rtp& operator=(Rtp&& o) noexcept {
    udpBuff = std::move(o.udpBuff);
    isValid_ = o.isValid_;
    return *this;
  }

  // Rtp(int pt, bool marker, uint16_t seq, uint32_t ts, int32_t ssrc,
  //     const std::vector<uint8_t>& payload)
  //     : udpBuff(), isValid_(true) {
  //   if (pt > 127 || pt < 0)
  //     throw std::runtime_error("Rtp header error: pt = " + std::to_string(pt));

  //  udpBuff.resize(HEADER_FIXED_LENGTH + payload.size());

  //  uint8_t marker_ = marker ? 1 : 0;
  //  uint8_t payloadType_ = pt;
  //  uint16_t seqNum_ = seq;
  //  uint32_t timestamp_ = ts;
  //  uint32_t ssrc_ = ssrc;

  //  uint8_t* buf = udpBuff.data();

  //  uint8_t firstByte{0x00};
  //  uint8_t secondByte{0x00};

  //  const uint8_t header_extension{0};
  //  const uint8_t header_csrcCount{0};

  //  firstByte =
  //      header_version << 6 | header_padding << 5 | header_extension << 4 | header_csrcCount;
  //  secondByte = marker << 7 | payloadType_;

  //  size_t pos{0};

  //  seeker::bytes::writeData(buf + pos, firstByte, false);
  //  pos += sizeof(firstByte);
  //  seeker::bytes::writeData(buf + pos, secondByte, false);
  //  pos += sizeof(secondByte);
  //  seeker::bytes::writeData(buf + pos, seqNum_, false);
  //  pos += sizeof(seqNum_);
  //  seeker::bytes::writeData(buf + pos, timestamp_, false);
  //  pos += sizeof(timestamp_);
  //  seeker::bytes::writeData(buf + pos, ssrc_, false);
  //  pos += sizeof(ssrc_);

  //  if (pos != 12) {
  //    throw std::logic_error("it should never happen: pos != 12");
  //  }

  //  memcpy(buf + pos, payload.data(), sizeof(uint8_t) * payload.size());
  //  pos += sizeof(uint8_t) * payload.size();
  //  isValid_ = true;
  //}


  Rtp(int pt, bool marker, uint16_t seq, uint32_t ts, uint32_t ssrc,
      const std::vector<uint8_t>& payload, bool hasExtension = false,
      uint16_t extensionDefinedByProfile = 0,
      const std::vector<uint8_t>& extensionData = EMPTY_DATA)
      : udpBuff(), isValid_(true) {
    if (pt > 127 || pt < 0)
      throw std::runtime_error("Rtp header error: pt = " + std::to_string(pt));

    uint16_t extensionLenInWord = 0;
    size_t extensionLenInByte = 0;

    size_t headerLength = 0;

    if (hasExtension) {
      extensionLenInByte = extensionData.size();
      extensionLenInWord = extensionLenInByte >> 2;
      if (extensionLenInByte % 4 != 0) {
        extensionLenInWord += 1;
      }
      headerLength = HEADER_FIXED_LENGTH + (2 + 2 + extensionLenInWord * 4);
    } else {
      headerLength = HEADER_FIXED_LENGTH;
    }

    udpBuff.resize(headerLength + payload.size());

    uint8_t marker_ = marker ? 1 : 0;
    uint8_t payloadType_ = pt;
    uint16_t seqNum_ = seq;
    uint32_t timestamp_ = ts;
    uint32_t ssrc_ = ssrc;

    uint8_t* buf = udpBuff.data();

    uint8_t firstByte{0x00};
    uint8_t secondByte{0x00};

    const uint8_t header_extension = hasExtension ? 1 : 0;
    const uint8_t header_csrcCount{0};

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

    if (hasExtension) {
      seeker::bytes::writeData(buf + pos, extensionDefinedByProfile, false);
      pos += sizeof(extensionDefinedByProfile);

      seeker::bytes::writeData(buf + pos, extensionLenInWord, false);
      pos += sizeof(extensionLenInWord);

      memcpy(buf + pos, extensionData.data(), sizeof(uint8_t) * extensionData.size());
      // last data in header maybe some padding
    }


    pos = headerLength;
    memcpy(buf + pos, payload.data(), sizeof(uint8_t) * payload.size());
    pos += sizeof(uint8_t) * payload.size();
    isValid_ = true;
  }


  void reset(std::vector<uint8_t>&& udp) {
    udpBuff.swap(udp);
    isValid_ = Rtp::isValidRtp(udpBuff);
  }

  void cloneRawData(std::vector<uint8_t>& buf) const {
    buf.assign(udpBuff.begin(), udpBuff.end());
  }

  std::vector<uint8_t> cloneRawData() const { return udpBuff; }

  void takeRawData(std::vector<uint8_t>& buf) {
    isValid_ = false;
    buf.swap(udpBuff);
  }

  std::vector<uint8_t>&& takeRawData() {
    isValid_ = false;
    return std::move(udpBuff);
  }

  const std::vector<uint8_t>& getRawData() { return udpBuff; }

  std::vector<uint8_t>& getMutableRawData() { return udpBuff; }

  // only extension data (without defined_by_profile and extension length)
  void getHeaderExtension(std::vector<uint8_t>& buf) const {
    uint8_t firstByte = udpBuff[0];
    uint8_t csrcCount = (firstByte >> 0) & 0x0F;
    uint8_t extension = (firstByte >> 4) & 0x01;

    if (extension != 0) {
      int begin = HEADER_FIXED_LENGTH + csrcCount * sizeof(uint32_t) + 4;
      size_t len = getExtensionLength(udpBuff.data()) - 4;
      buf.assign(udpBuff.data() + begin, udpBuff.data() + begin + len);
    } else {
      buf.clear();
    }
  }

  void getHeaderExtensionAndProfile(std::vector<uint8_t>& buf,
                                    uint16_t& definedByProfile) const {
    uint8_t firstByte = udpBuff[0];
    uint8_t csrcCount = (firstByte >> 0) & 0x0F;
    uint8_t extension = (firstByte >> 4) & 0x01;

    if (extension != 0) {
      const uint8_t* rtpHeader = udpBuff.data();

      int pos = HEADER_FIXED_LENGTH + csrcCount * sizeof(uint32_t);
      seeker::bytes::readData(rtpHeader + pos, definedByProfile, false);
      pos += sizeof(definedByProfile);

      uint16_t extensionWordLen{0};
      seeker::bytes::readData(rtpHeader + pos, extensionWordLen, false);
      pos += sizeof(extensionWordLen);

      size_t extensionLen = static_cast<size_t>(extensionWordLen) << 2;

      buf.assign(udpBuff.data() + pos, udpBuff.data() + pos + extensionLen);
    } else {
      buf.clear();
    }
  }

  void getPayload(std::vector<uint8_t>& buf) const {
    ensureValid();
    size_t begin = getHeaderLength();
    bool padding = getPadding(udpBuff.data());
    uint8_t paddingLength = 0;
    if (padding) {
      paddingLength = udpBuff[udpBuff.size() - 1];
    }
    size_t end = udpBuff.size() - paddingLength;
    buf.assign(udpBuff.data() + begin, udpBuff.data() + end);
    //std::cout << "getPayload size=" << udpBuff.size()
    //          << " paddingLength=" << (int)paddingLength << " begin=" << begin
    //          << " end=" << end << " payloadSize=" << buf.size() << std::endl;
  }

  const uint8_t* getPayload(size_t& len) const {
    ensureValid();
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

  void getHeader(std::vector<uint8_t>& buf) const {
    ensureValid();
    size_t begin = 0;
    size_t end = getHeaderLength();
    buf.assign(udpBuff.data() + begin, udpBuff.data() + end);
  }

  const uint8_t* getHeader(size_t& len) const {
    ensureValid();
    size_t begin = 0;
    size_t end = getHeaderLength();
    len = end - begin;
    return udpBuff.data() + begin;
  }

  void ensureValid() const {
    if (!isValid()) {
      throw std::logic_error("This RTP is INVALID");
    }
  }

  bool isValid() const { return isValid_; }

  size_t getHeaderLength() const {
    if (udpBuff.empty()) {
      throw std::runtime_error("udpBuff.empty()");
    }

    uint8_t firstByte = udpBuff.at(0);

    uint8_t csrcCount = (firstByte >> 0) & 0x0F;
    // uint8_t version = (firstByte >> 6) & 0x03;
    // uint8_t padding = (firstByte >> 5) & 0x01;
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


  uint32_t ssrc() const { return getSsrc(udpBuff.data()); }

  void updateSsrc(uint32_t ssrc) { return setSsrc(udpBuff.data(), ssrc); };

  uint32_t timestamp() const { return getTimestamp(udpBuff.data()); };

  void updateTimestamp(uint32_t ts) { return setTimestamp(udpBuff.data(), ts); };

  uint8_t payloadType() const { return getPayloadType(udpBuff.data()); };

  bool marker() const { return getMark(udpBuff.data()); };

  uint16_t seq() const { return getSeq(udpBuff.data()); };

  void updateSeq(uint16_t seq) { return setSeq(udpBuff.data(), seq); };

  uint16_t csrcCount() const { return getCsrcCount(udpBuff.data()); };

  bool hasHeaderExtension() const { return hasExtension(udpBuff.data()); };

  size_t length() const { return udpBuff.size(); };

  inline static bool isValidRtp(const std::vector<uint8_t>& rtpData);

 private:
  std::vector<uint8_t> udpBuff;
  bool isValid_;
};


bool Rtp::isValidRtp(const std::vector<uint8_t>& rtpData) {
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

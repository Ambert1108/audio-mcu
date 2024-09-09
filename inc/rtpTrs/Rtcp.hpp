/**
@project rtpTrs
@author Tao Zhang
@since 2023/7/24
@version 0.0.1-SNAPSHOT 2023/10/9
*/
#pragma once

#include "config.h"

#include "seeker/common.h"

#include <iostream>
#include <vector>


/*


https://www.rfc-editor.org/rfc/rfc3550.html

6.4.1 SR: Sender Report RTCP Packet

        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
header |V=2|P|    RC   |   PT=SR=200   |             length            |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                         SSRC of sender                        |
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
sender |              NTP timestamp, most significant word             |
info   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |             NTP timestamp, least significant word             |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                         RTP timestamp                         |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                     sender's packet count                     |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                      sender's octet count                     |
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
report |                 SSRC_1 (SSRC of first source)                 |
block  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  1    | fraction lost |       cumulative number of packets lost       |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |           extended highest sequence number received           |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                      interarrival jitter                      |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                         last SR (LSR)                         |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                   delay since last SR (DLSR)                  |
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
report |                 SSRC_2 (SSRC of second source)                |
block  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  2    :                               ...                             :
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
       |                  profile-specific extensions                  |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

   The sender report packet consists of three sections, possibly
   followed by a fourth profile-specific extension section if defined.
   The first section, the header, is 8 octets long.


https://www.rfc-editor.org/rfc/rfc3550.html#section-6.6
6.6 BYE: Goodbye RTCP Packet

       0                   1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |V=2|P|    SC   |   PT=BYE=203  |             length            |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                           SSRC/CSRC                           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      :                              ...                              :
      +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
(opt) |     length    |               reason for leaving            ...
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

   The BYE packet indicates that one or more sources are no longer
   active.


https://www.rfc-editor.org/rfc/rfc2032
FIR / NACK

5.2.1.  Full INTRA-frame Request (FIR) packet

     0                   1                   2                   3
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |V=2|P|   MBZ   |  PT=RTCP_FIR  |           length              |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                              SSRC                             |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

   This packet indicates that a receiver requires a full encoded image
   in order to either start decoding with an entire image or to refresh
   its image and speed the recovery after a burst of lost packets. The
   receiver requests the source to force the next image in full "INTRA-
   frame" coding mode, i.e. without using differential coding. The
   various fields are defined in the RTP specification [1]. SSRC is the
   synchronization source identifier for the sender of this packet. The
   value of the packet type (PT) identifier is the constant RTCP_FIR
   (192).

   5.2.2.  Negative ACKnowledgements (NACK) packet (rfc2032)

   The format of the NACK packet is as follow:

     0                   1                   2                   3
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |V=2|P|   MBZ   | PT=RTCP_NACK  |           length              |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                              SSRC                             |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |              FSN              |              BLP              |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

https://www.rfc-editor.org/rfc/rfc4585.html
6.1.   Common Packet Format for Feedback Messages

   All FB messages MUST use a common packet format that is depicted in
   Figure 3:

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P|   FMT   |       PT      |          length               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  SSRC of packet sender                        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  SSRC of media source                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   :            Feedback Control Information (FCI)                 :
   :                                                               :

6.2.   Transport Layer Feedback Messages

   Transport layer FB messages are identified by the value RTPFB as RTCP
   message type.

   A single general purpose transport layer FB message is defined in
   this document: Generic NACK.  It is identified by means of the FMT
   parameter as follows:

   0:    unassigned
   1:    Generic NACK
   2-30: unassigned
   31:   reserved for future expansion of the identifier number space

   The following subsection defines the formats of the FCI field for
   this type of FB message.  Further generic feedback messages MAY be
   defined in the future.

   The Feedback Control Information (FCI) field has the following Syntax
   (Figure 4):

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |            PID                |             BLP               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

               Figure 4: Syntax for the Generic NACK message


https://www.rfc-editor.org/rfc/rfc5104#page-42
   Assigned in [RFC4585]:

     1:     Picture Loss Indication (PLI)
     2:     Slice Lost Indication (SLI)
     3:     Reference Picture Selection Indication (RPSI)
     15:    Application layer FB message
     31:    reserved for future expansion of the number space

   Assigned in this memo:

     4:     Full Intra Request (FIR) Command
     5:     Temporal-Spatial Trade-off Request (TSTR)
     6:     Temporal-Spatial Trade-off Notification (TSTN)
     7:     Video Back Channel Message (VBCM)

   Unassigned:

         0: unassigned
      8-14: unassigned
     16-30: unassigned


4.3.1.1.  FIR Message Format

   The Feedback Control Information (FCI) for the Full Intra Request
   consists of one or more FCI entries, the content of which is depicted
   in Figure 4.  The length of the FIR feedback message MUST be set to
   2+2*N, where N is the number of FCI entries.

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                              SSRC                             |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   | Seq nr.       |    Reserved                                   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

         Figure 4 - Syntax of an FCI Entry in the FIR Message


*/

namespace seeker {

namespace rtp {


inline uint8_t getRtcpVersion(const uint8_t* header) {
  uint8_t firstByte = header[0];
  uint8_t version = (firstByte >> 6) & 0x03;
  return version;
}

inline bool getRtcpPadding(const uint8_t* header) {
  uint8_t firstByte = header[0];
  uint8_t padding = (firstByte >> 5) & 0x01;
  return padding != 0;
}

inline uint8_t getRtcpPayloadType(const uint8_t* header) {
  uint8_t secondByte = header[1];
  uint8_t payloadType = (secondByte >> 0) & 0xFF;
  return payloadType;
}

inline uint16_t getRtcpLength(const uint8_t* header) {
  static constexpr uint8_t POSITION = 2;
  uint16_t length{0};
  seeker::bytes::readData(header + POSITION, length, false);
  return length;
}


// for SR / RR / RTPFB / PSFB / BYE
inline uint32_t getRtcpSsrc(const uint8_t* header) {
  static constexpr uint8_t POSITION = 4;
  uint32_t ssrc{0};
  seeker::bytes::readData(header + POSITION, ssrc, false);
  return ssrc;
}


// for SR
inline uint32_t getNtpM4(const uint8_t* header) {
  static constexpr uint8_t POSITION = 8;
  uint32_t ntpM4{0};
  seeker::bytes::readData(header + POSITION, ntpM4, false);
  return ntpM4;
}

// for SR
inline uint32_t getNtpL4(const uint8_t* header) {
  static constexpr uint8_t POSITION = 12;
  uint32_t ntpM4{0};
  seeker::bytes::readData(header + POSITION, ntpM4, false);
  return ntpM4;
}


// for SR
inline uint32_t getSrTs(const uint8_t* header) {
  static constexpr uint8_t POSITION = 16;
  uint32_t ts{0};
  seeker::bytes::readData(header + POSITION, ts, false);
  return ts;
}


// for data
template <typename T>
inline void getData(const uint8_t* pos, uint16_t offset, T& data) {
  seeker::bytes::readData(pos + offset, data, false);
}


inline uint32_t get4Bytes(const uint8_t* pos, uint16_t offset) {
  uint32_t data{0};
  seeker::bytes::readData(pos + offset, data, false);
  return data;
}

inline uint16_t get2Bytes(const uint8_t* pos, uint16_t offset) {
  uint16_t data{0};
  seeker::bytes::readData(pos + offset, data, false);
  return data;
}

inline uint8_t get1Bytes(const uint8_t* pos, uint16_t offset) {
  uint8_t data{0};
  seeker::bytes::readData(pos + offset, data, false);
  return data;
}



enum class RtcpType : uint8_t {
  UNKNOWN = 0,
  // RTCP_FIR = 192,
  // RTCP_NACK = 193,
  SR = 200,
  RR = 201,
  SDES = 202,
  BYE = 203,
  APP = 204,
  RTPFB = 205,
  PSFB = 206,
  XR = 207,
};

inline RtcpType findRtcpType(const std::vector<uint8_t>& rtcpData) {
  uint8_t v = getRtcpVersion(rtcpData.data());
  if (v != 2) {
    return RtcpType::UNKNOWN;
  }

  uint8_t pt = getRtcpPayloadType(rtcpData.data());
  if (pt >= 200 && pt <= 207) {
    return RtcpType{pt};
  } else {
    return RtcpType::UNKNOWN;
  }
}


inline uint8_t getGroup1Bit3to7(const uint8_t* rtcpHeader) {
  uint8_t firstByte = rtcpHeader[0];
  uint8_t v = (firstByte >> 0) & 0x1F;
  return v;
}

inline uint8_t getFmtValue(const uint8_t* rtcpHeader) { return getGroup1Bit3to7(rtcpHeader); }

inline std::vector<uint8_t> getFCI(const uint8_t* rtcpHeader) {
  // TODO
  return std::vector<uint8_t>();
}

enum class FMT : uint8_t {
  UNKNOWN = 0,
  NACK,
  PLI,
  FIR,
};

/*
   https://www.rfc-editor.org/rfc/rfc4585.html
      RTPFB FMT:
      0:    unassigned
      1:    Generic NACK
      2-30: unassigned
      31:   reserved for future expansion of the identifier number space

      PSFB FMT:
      0:     unassigned
      1:     Picture Loss Indication (PLI)
      2:     Slice Loss Indication (SLI)
      3:     Reference Picture Selection Indication (RPSI)
      4-14:  unassigned
      15:    Application layer FB (AFB) message
      16-30: unassigned
      31:    reserved for future expansion of the sequence number space
*/



class Rtcp {
  static constexpr uint8_t header_version{2};
  static constexpr uint8_t header_padding{0};

 public:
  Rtcp() : udpBuff(), rtcpType(RtcpType::UNKNOWN){};

  Rtcp(std::vector<uint8_t>&& udp)
      : udpBuff(std::move(udp)), rtcpType(findRtcpType(udpBuff)) {}

  Rtcp(Rtcp&& o) noexcept : udpBuff(std::move(o.udpBuff)), rtcpType(findRtcpType(udpBuff)){};

  Rtcp(const Rtcp& o) : udpBuff(o.udpBuff), rtcpType(findRtcpType(udpBuff)){};

  Rtcp& operator=(const Rtcp& o) noexcept {
    udpBuff = o.udpBuff;
    rtcpType = findRtcpType(udpBuff);
    return *this;
  }

  Rtcp& operator=(Rtcp&& o) noexcept {
    udpBuff = std::move(o.udpBuff);
    rtcpType = findRtcpType(udpBuff);
    return *this;
  }

  void reset(std::vector<uint8_t>&& udp) {
    udpBuff.swap(udp);
    rtcpType = findRtcpType(udpBuff);
  }

  void cloneRawData(std::vector<uint8_t>& buf) const {
    buf.assign(udpBuff.begin(), udpBuff.end());
  }

  std::vector<uint8_t> cloneRawData() const { return udpBuff; }

  void takeRawData(std::vector<uint8_t>& buf) {
    rtcpType = RtcpType::UNKNOWN;
    buf.swap(udpBuff);
  }

  std::vector<uint8_t>&& takeRawData() {
    rtcpType = RtcpType::UNKNOWN;
    return std::move(udpBuff);
  }

  size_t length() const { return udpBuff.size(); };


  const std::vector<uint8_t>& getRawData() { return udpBuff; }

  void createPLI(uint32_t senderSsrc, uint32_t mediaSsrc) {
    rtcpType = RtcpType::PSFB;
    static constexpr uint16_t packetLength = 12;
    static constexpr uint8_t fmtValue = 1;


    udpBuff.resize(packetLength);

    uint8_t firstByte{0x00};
    uint8_t secondByte{0x00};

    firstByte = header_version << 6 | header_padding << 5 | fmtValue << 0;
    secondByte = static_cast<uint8_t>(RtcpType::PSFB);

    uint8_t* buf = udpBuff.data();
    size_t pos{0};

    seeker::bytes::writeData(buf + pos, firstByte, false);
    pos += sizeof(firstByte);

    seeker::bytes::writeData(buf + pos, secondByte, false);
    pos += sizeof(secondByte);

    static const uint16_t fmtLength = 2;
    seeker::bytes::writeData(buf + pos, fmtLength, false);
    pos += sizeof(fmtLength);

    seeker::bytes::writeData(buf + pos, senderSsrc, false);
    pos += sizeof(senderSsrc);

    seeker::bytes::writeData(buf + pos, mediaSsrc, false);
    pos += sizeof(mediaSsrc);
  };


  void createFIR(uint32_t senderSsrc, uint32_t mediaSsrc, uint8_t commandSeq) {
    rtcpType = RtcpType::PSFB;
    static constexpr uint16_t packetLength = 12 + 4 + 4;
    static constexpr uint8_t fmtValue = 4;  // rfc5104


    udpBuff.resize(packetLength);

    uint8_t firstByte{0x00};
    uint8_t secondByte{0x00};

    firstByte = header_version << 6 | header_padding << 5 | fmtValue << 0;
    secondByte = static_cast<uint8_t>(RtcpType::PSFB);

    uint8_t* buf = udpBuff.data();
    size_t pos{0};

    seeker::bytes::writeData(buf + pos, firstByte, false);
    pos += sizeof(firstByte);

    seeker::bytes::writeData(buf + pos, secondByte, false);
    pos += sizeof(secondByte);

    static const uint16_t fmtLength = 2 + (2 * 1);
    seeker::bytes::writeData(buf + pos, fmtLength, false);
    pos += sizeof(fmtLength);

    seeker::bytes::writeData(buf + pos, senderSsrc, false);
    pos += sizeof(senderSsrc);

    constexpr static int32_t emptyMediaSsrc = 0;
    seeker::bytes::writeData(buf + pos, emptyMediaSsrc, false);
    pos += sizeof(emptyMediaSsrc);

    seeker::bytes::writeData(buf + pos, mediaSsrc, false);
    pos += sizeof(mediaSsrc);

    seeker::bytes::writeData(buf + pos, commandSeq, false);
    pos += sizeof(commandSeq);

    constexpr static int8_t bytePadding = 0;

    seeker::bytes::writeData(buf + pos, bytePadding, false);
    pos += sizeof(bytePadding);
    seeker::bytes::writeData(buf + pos, bytePadding, false);
    pos += sizeof(bytePadding);
    seeker::bytes::writeData(buf + pos, bytePadding, false);
    pos += sizeof(bytePadding);
  };

  RtcpType getType() const { return rtcpType; }

  const std::vector<uint8_t>& getRawData() const { return udpBuff; }

  FMT fmt() const {
    if (rtcpType == RtcpType::RTPFB) {
      uint8_t fmtValue = getFmtValue(udpBuff.data());
      if (fmtValue == 1) {
        return FMT::NACK;
      }
    } else if (rtcpType == RtcpType::PSFB) {
      uint8_t fmtValue = getFmtValue(udpBuff.data());
      if (fmtValue == 1) {
        return FMT::PLI;
      } else if (fmtValue == 4) {
        return FMT::FIR;
      }
    }

    return FMT::UNKNOWN;
  }

  uint32_t PLI_sender_ssrc() const {
    static constexpr uint8_t POSITION = 4;
    if (udpBuff.size() != 12) {
      throw std::logic_error("PLI_sender_ssrc error: package length = " +
                             std::to_string(udpBuff.size()));
    }

    const uint8_t* rawData = udpBuff.data();
    uint32_t ssrc{0};
    seeker::bytes::readData(rawData + POSITION, ssrc, false);
    return ssrc;
  }

  uint32_t PLI_media_ssrc() const {
    static constexpr uint8_t POSITION = 8;
    if (udpBuff.size() != 12) {
      throw std::logic_error("PLI_media_ssrc error: package length = " +
                             std::to_string(udpBuff.size()));
    }

    const uint8_t* rawData = udpBuff.data();
    uint32_t ssrc{0};
    seeker::bytes::readData(rawData + POSITION, ssrc, false);
    return ssrc;
    return 0;
  }

  uint32_t FIR_sender_ssrc() const {
    static constexpr uint8_t POSITION = 4;
    if (udpBuff.size() < 12 + 4 + 4) {
      throw std::logic_error("FIR_sender_ssrc error: package length = " +
                             std::to_string(udpBuff.size()));
    }

    const uint8_t* rawData = udpBuff.data();
    uint32_t ssrc{0};
    seeker::bytes::readData(rawData + POSITION, ssrc, false);
    return ssrc;
  }

  uint32_t FIR_media_ssrc() const {
    static constexpr uint8_t POSITION = 12;
    if (udpBuff.size() < 12 + 4 + 4) {
      throw std::logic_error("FIR_media_ssrc error: package length = " +
                             std::to_string(udpBuff.size()));
    }

    const uint8_t* rawData = udpBuff.data();
    uint32_t ssrc{0};
    seeker::bytes::readData(rawData + POSITION, ssrc, false);
    return ssrc;
  }

  uint16_t FIR_seq() const {
    static constexpr uint8_t POSITION = 12 + 4;
    if (udpBuff.size() < 12 + 4 + 4) {
      throw std::logic_error("FIR_seq error: package length = " +
                             std::to_string(udpBuff.size()));
    }

    const uint8_t* rawData = udpBuff.data();
    uint8_t seq{0};
    seeker::bytes::readData(rawData + POSITION, seq, false);
    return seq;
  }

  uint32_t SR_ssrc() const {
    const uint8_t* rawData = udpBuff.data();
    return getRtcpSsrc(rawData);
  }

  // NTP timestamp, most significant word
  uint32_t SR_ntp_M4() const {
    const uint8_t* rawData = udpBuff.data();
    return getNtpM4(rawData);
  }

  // NTP timestamp, least significant word
  uint32_t SR_ntp_L4() const {
    const uint8_t* rawData = udpBuff.data();
    return getNtpL4(rawData);
  }

  // RTP timestamp
  uint32_t SR_ts() const {
    const uint8_t* rawData = udpBuff.data();
    return getSrTs(rawData);
  }

  uint8_t BYE_source_count() const { return getGroup1Bit3to7(udpBuff.data()); }

  std::vector<uint32_t> BYE_ssrc_list(uint8_t sc) const {
    static constexpr uint8_t POSITION = 4;
    std::vector<uint32_t> ssrcList;
    ssrcList.reserve(sc);

    const uint8_t* data = udpBuff.data();
    for (uint8_t i = 0; i < sc; i++) {
      uint32_t ssrc;
      int offset = i * 4 + POSITION;
      seeker::bytes::readData(data + offset, ssrc, false);
      ssrcList.emplace_back(ssrc);
    }
    return ssrcList;
  }

  std::vector<uint32_t> BYE_ssrc_list() const {
    uint8_t sc = BYE_source_count();
    return BYE_ssrc_list(sc);
  }

 private:
  std::vector<uint8_t> udpBuff;
  RtcpType rtcpType;
};



}  // namespace rtp



}  // namespace seeker

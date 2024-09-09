// @brief: 音频数据处理通道
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.9.9]
// @version: V0.0.1
// @revision: [Ambert@2024.9.9]

#pragma once

#include "Config.h"
#include "MediaType.h"

#include "rtpTrs/rtpTrs.hpp"
#include "AudioEngine23/AudioEngine23.hpp"
#include "utils/InvokeTimer.hpp"

#include <deque>
#include <vector>
#include <functional>

namespace aom {
	struct ChnlData {
		uint32_t readPktNum = 0;
		uint32_t sendPktNum = 0;
		uint32_t sendPayloadNum = 0;

		int64_t firstKeyFrameTime = -1;

		uint32_t readTimeOutNum = 0;
		uint32_t procTimeOutNum = 0;
		uint32_t sendTimeOutNum = 0;

		uint16_t lastSendSeq = 0;

		uint32_t decodeCount = 0;
		uint32_t renderCount = 0;
		uint32_t encodeCount = 0;

		port_t audioPort = 0;
	};

	using namespace seeker::rtp;
	typedef std::unique_ptr<AudioEngine23::Decoder> Decoder;
	typedef std::unique_ptr<AudioEngine23::Encoder> Encoder;
	typedef std::unique_ptr<AudioEngine23::Demuxer> Demuxer;
	typedef std::unique_ptr<AudioEngine23::Muxer> Muxer;
	typedef std::unique_ptr<RtpTransceiver> RtpTrxer;
	typedef std::shared_ptr<Notifier> RtpNotifier;
	typedef std::queue<std::vector<uint8_t>> NaluBuffer;

	class AudioPorcessChnl {
	private:
		std::string jobId;
		Demuxer demuxer = nullptr;
		Decoder decoder = nullptr;
		Encoder encoder = nullptr;
		Muxer muxer = nullptr;


		int setDemuxer();
		int setMuxer();
		int setDecoder();
		int setEncoder();
	};

	using UniqueAPC = std::unique_ptr<AudioPorcessChnl>;
}
#include "AudioPorcessChnl.h"

namespace aom {
	AudioPorcessChnl::AudioPorcessChnl(const std::string& id, Point listen, Point dst, double interval)
		: chnlId(id), listenPoint(listen), dstPoint(dst), timeInterval(interval), decoder(nullptr),
		notifier(nullptr), switcher(nullptr) {
		srcBuffer.reserve(30000);
	}

	AudioPorcessChnl::~AudioPorcessChnl() {
		close();
	}

	bool AudioPorcessChnl::open(int codecType, int inputRate, int outputRate, int bitrate, int payloadType) {
		status << TaskStatusType::run;
		if (setDecoder(inputRate) != 0) return false;
		this->payloadType = payloadType;
		//随机设置ssrc
		ssrc = rand() % 9000000 + 1000000 + (int32_t)seeker::time::currentTime();
		work1Th = std::thread{ &AudioPorcessChnl::workingLoop, this };
		I_LOG("[apc::open->{}] channel open success. codecType:{}, inputRate:{}, "
			"outputRate:{}, bitrate:{}, pt:{}", chnlId, codecType, inputRate, outputRate, bitrate, payloadType);
		return true;
	}

	void AudioPorcessChnl::close() {
		if (status.getStatus() == TaskStatusType::end) return;
		status << TaskStatusType::down;
		if (work1Th.joinable()) work1Th.join();
		status << TaskStatusType::end;
	}

	size_t AudioPorcessChnl::getLength() {
		lockGuard lck(srcBufLocker);
		return srcBuffer.size();
	}

	void AudioPorcessChnl::getBuffer(std::vector<int16_t>& dst, size_t length) {
		lockGuard lck(srcBufLocker);
		if (srcBuffer.empty()) {
			D_LOG("[apc::getBuffer->{}] get src buffer is empty, size {}", chnlId, srcBuffer.size());
			return;
		}
		D_LOG("length={}", length);
		dst.assign(srcBuffer.begin(), srcBuffer.begin() + length);
		srcBuffer.erase(srcBuffer.begin(), srcBuffer.begin() + length);
	}

	void AudioPorcessChnl::setMicType(int val) { micType.store(val); }

	TaskStatusType AudioPorcessChnl::getStatus() const { return status.getStatus(); }

	void AudioPorcessChnl::sendRtp(std::vector<uint8_t> payload, uint32_t ts) {
		seeker::rtp::Rtp rtpPacket = seeker::rtp::Rtp(payloadType, 1, seqNum++, ts, ssrc, payload);
		std::deque<Rtp> sendQueue{ std::move(rtpPacket) };
		switcher->sendRtp(sendQueue);
	}

	void AudioPorcessChnl::workingLoop() {
		try {
			//设置视频RTP接收器，让接收器绑定收流地址并设置发流地址
			switcher = std::make_unique<RtpTransceiver>(chnlId, 32);
			if (switcher->open(listenPoint.ip, listenPoint.port) != 0) {
				E_LOG("[apc::recvAndDec->{}] rtpTrs bind video recv ip={}, port={} failed.",
					chnlId, listenPoint.ip, listenPoint.port);
				status << TaskStatusType::exce;
				return;
			}
			switcher->setDestination(dstPoint.ip, dstPoint.port);

			//设置视频RTP接收唤醒器
			notifier = std::make_shared<Notifier>();
			switcher->setRtpNotifier(notifier);
		}
		catch (std::exception& ex) {
			E_LOG("[apc::recvAndDec->{}] get exception: {}", chnlId, ex.what());
			status << TaskStatusType::exce;
			return;
		}
		// 接收数据封装数据包
		RawData payloadBuf = {};
		// 接收RTP队列
		std::deque<Received<Rtp>> recvQueue = {};
		// MPU监控定时器
		InvokeTimerPtr printTimer = nullptr;
		uint16_t lastSeq = 0;
		uint32_t lastTs = 0;
		int size = 0;
		AVPacket* pkt = av_packet_alloc();
		AVFrame* frame = av_frame_alloc();
		int64_t timePoint = 0;
		try {
			I_LOG("[apc::recvAndDec->{}] thread is open, listen {}:{}", chnlId, listenPoint.ip, listenPoint.port);
			while (status) {
				// 1.判断麦克风状态，闭麦状态下不收流
				//if (micType.load() == 0) {
				//	std::this_thread::sleep_for(std::chrono::milliseconds(1));
				//	continue;
				//}
				timePoint = seeker::time::currentTime();
				// 2.接收音频流
				switcher->receiveRtp(recvQueue);

				// 3.若未能收到音频流，等待25ms后重新收流
				while (recvQueue.empty() && status) {
					notifier->waitNotify(25);
					switcher->receiveRtp(recvQueue);
				}
				if (!status) break;
				while (!recvQueue.empty()) {
					auto& receivedRtp = recvQueue.front();
					auto& rtpData = receivedRtp.element;
					auto& from = receivedRtp.from;
					if (rtpData.length() == 0) {
						throw std::runtime_error("error: rtpData.length() == 0");
					}

					// 4.判断音频RTP包seq是否连续，若不连续说明丢包，需要补0
					uint16_t seq = (int)rtpData.seq();
					if (lastSeq == 0) lastSeq = seq;
					else {
						while (seq > lastSeq + 1) {
							if (size) {
								lockGuard lck(srcBufLocker);
								srcBuffer.insert(srcBuffer.end(), size / 2, 0);
							}
							lastSeq++;
						}
					}
					int mark = (int)rtpData.marker();
					rtpData.getPayload(payloadBuf);
					uint32_t ts = rtpData.timestamp();
					
					// 5.将音频RTP包中的数据存入AVPacket
					pkt->size = payloadBuf.size();
					pkt->data = (uint8_t*)av_malloc(pkt->size);
					pkt->pts = ts;
					pkt->dts = pkt->pts;
					memcpy(pkt->data, payloadBuf.data(), payloadBuf.size());
					int ret = av_packet_from_data(pkt, pkt->data, pkt->size);
					if (ret < 0) {
						E_LOG("[apc::recvAndDec->{}] use av_packet_from_data failed", chnlId);
						av_free(pkt->data);
						continue;
					}
				
					// 6.解码音频帧
					decoder->getFrame(pkt, frame);
					int size = frame->nb_samples * av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format))
						* frame->channels;
					int32_t inc = ts - lastTs;
					D_LOG("seq:{}, ts:{}, increment:{}, audio frame size is {}", seq, ts, inc, size);
					lastTs = ts;

					// 7.将解码数据存入源缓存区中
					{
						lockGuard lck(srcBufLocker);
						srcBuffer.insert(srcBuffer.end(), (int16_t*)frame->data[0], (int16_t*)frame->data[0] + size / 2);
					}
					av_frame_unref(frame);
					av_packet_unref(pkt);
					recvQueue.pop_front();
				}
			}
		}
		catch (std::exception& ex) {

		}
	}

	int AudioPorcessChnl::setDecoder(int sampleRate) {
		try {
			if (decoder) {
				decoder->close();
				W_LOG("[apc::setDecoder->{}] Decoder already exists, resetting...", chnlId);
			}
			else {
				decoder = std::make_unique<AudioEngine23::Decoder>();
			}
			decoder->open(sampleRate, AV_SAMPLE_FMT_S16, 1);
			I_LOG("[apc::setDecoder->{}] Decoder opened success", chnlId);
		}
		catch (std::exception& ex) {
			E_LOG("[apc::setDecoder->{}] get exception: {}", chnlId, ex.what());
			return -1;
		}
		return 0;
	}
}
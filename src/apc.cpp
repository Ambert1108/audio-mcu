#include "AudioPorcessChnl.h"

namespace aom {
	AudioPorcessChnl::AudioPorcessChnl(const std::string& id, Point listen, Point dst, double interval)
		: chnlId(id), listenPoint(listen), dstPoint(dst), timeInterval(interval), demuxer(nullptr), decoder(nullptr),
		encoder(nullptr), muxer(nullptr), notifier(nullptr), switcher(nullptr) {
		srcBuffer.reserve(30000);
		dstBuffer.reserve(30000);
	}

	AudioPorcessChnl::~AudioPorcessChnl() {
		close();
	}

	bool AudioPorcessChnl::open(int codecType, int inputRate, int outputRate, int bitrate, int payloadType) {
		status << TaskStatusType::run;
		if (setDecoder(inputRate) != 0) return false;
		if (setEncoder(outputRate) != 0) return false;
		this->payloadType = payloadType;
		work1Th = std::thread{ &AudioPorcessChnl::recvAndDec, this };
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

	const std::vector<int16_t>& AudioPorcessChnl::getBuffer() {
		lockGuard lck(srcBufLocker);
		if (srcBuffer.empty()) {
			D_LOG("[apc::getBuffer->{}] get src buffer is empty, size {}", chnlId, srcBuffer.size());
			return buf;
		}
		I_LOG("get buffer, size is {}", srcBuffer.size());
		buf.clear();
		buf.resize(srcBuffer.size() / 2);
		for (int i = 0; i < srcBuffer.size() - 1; i += 2) {
			int16_t p = srcBuffer[i] + (srcBuffer[i + 1] << 8);
			buf[i / 2] = p;
		}
		srcBuffer.clear();
		//buf.swap(srcBuffer);
		return buf;
	}

	void AudioPorcessChnl::setBuffer(const std::vector<int16_t>& buf) {
		if (buf.empty()) {
			E_LOG("[apc::setBuffer->{}] input buffer is empty", chnlId);
			return;
		}
		lockGuard lck(dstBufLocker);
		for (const int16_t sample : buf) {
			//dstBuffer.push_back((uint8_t)sample);
			dstBuffer.push_back(static_cast<uint8_t>(sample & 0xFF));         // 低字节
			dstBuffer.push_back(static_cast<uint8_t>((sample >> 8) & 0xFF));  // 高字节
		}
		I_LOG("set buffer size is {}", dstBuffer.size());
	}

	void AudioPorcessChnl::setMicType(int val) { micType = val; }

	TaskStatusType AudioPorcessChnl::getStatus() const { return status.getStatus(); }

	void AudioPorcessChnl::recvAndDec() {
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
		// 发送RTP队列
		std::deque<Rtp> sendQueue = {};
		std::vector<uint8_t> outData;
		seeker::rtp::Rtp rtpPacket;
		// MPU监控定时器
		InvokeTimerPtr printTimer = nullptr;
		uint16_t seqNum = 0;
		uint16_t lastSeq = 0;
		uint32_t lastTs = 0;
		int size = 0;
		//随机设置ssrc
		uint32_t ssrc = rand() % 9000000 + 1000000 + (int32_t)seeker::time::currentTime();
		AVPacket* pkt = av_packet_alloc();
		AVFrame* frame = av_frame_alloc();
		AVPacket* dstPkt = av_packet_alloc();
		AVFrame* dstFrame = av_frame_alloc();
		int frameSize = 1;
		int64_t timePoint = 0;
		FILE* outFile = fopen("1.pcm", "wb");
		try {
			I_LOG("[apc::recvAndDec->{}] thread is open, listen {}:{}", chnlId, listenPoint.ip, listenPoint.port);
			while (status) {
				//std::this_thread::sleep_for(std::chrono::milliseconds(1));
				timePoint = seeker::time::currentTime();
				switcher->receiveRtp(recvQueue);
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
				
					//解码
					decoder->getFrame(pkt, frame);
					if (!frame->nb_samples) {
				
					}
					int size = frame->nb_samples * av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format))
						* frame->channels;
					int32_t inc = ts - lastTs;
					D_LOG("seq:{}, ts:{}, increment:{}, audio frame size is {}", seq, ts, inc, frameSize);
					lastTs = ts;
					{
						lockGuard lck(srcBufLocker);
						srcBuffer.insert(srcBuffer.end(), frame->data[0], frame->data[0] + size);
						//srcBuffer.insert(srcBuffer.end(), (int16_t*)frame->data[0], (int16_t*)frame->data[0] + size / 2);
					}
					av_packet_unref(pkt);
					recvQueue.pop_front();
				
					std::vector<uint8_t> dstData{};
					if (!dstBuffer.empty() && dstBuffer.size() >= size) {
						lockGuard lck(dstBufLocker);
						dstData = std::vector<uint8_t>(dstBuffer.begin(), dstBuffer.begin() + size);
						dstBuffer.erase(dstBuffer.begin(), dstBuffer.begin() + size);
						I_LOG("use buffer size is {}", dstData.size());
					}
					else {
						D_LOG("dst buffer is empty");
						dstData = std::vector<uint8_t>(size, 0);
					}

					dstFrame->data[0] = dstData.data();
					//dstFrame->data[0] = frame->data[0];
					dstFrame->nb_samples = frame->nb_samples;
					dstFrame->channels = frame->channels;
					dstFrame->format = frame->format;
					dstFrame->pts = frame->pts;
					av_frame_unref(frame);
					fwrite(dstFrame->data[0], 1, dstFrame->nb_samples * av_get_bytes_per_sample(static_cast<AVSampleFormat>(dstFrame->format)) * dstFrame->channels, outFile);
					encoder->getPacket(dstFrame, dstPkt);

					outData = std::vector<uint8_t>(dstPkt->data, dstPkt->data + dstPkt->size);
					rtpPacket = seeker::rtp::Rtp(payloadType, 1, seqNum++, dstPkt->pts, ssrc, outData);
					outData.clear();
					sendQueue.emplace_back(std::move(rtpPacket));
					if(!sendQueue.empty()) switcher->sendRtp(sendQueue);
					av_frame_unref(dstFrame);
					av_packet_unref(dstPkt);
					//int64_t use = (seeker::time::currentTime() - timePoint) * 1000; //us
					//if(use < 2130) std::this_thread::sleep_for(std::chrono::microseconds(2130 - use));
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

	int AudioPorcessChnl::setEncoder(int sampleRate) {
		try {
			if (encoder) {
				encoder->close();
				W_LOG("[apc::setEncoder->{}] Encoder already exists, resetting...", chnlId);
			}
			else encoder = std::make_unique<AudioEngine23::Encoder>();
			
			encoder->open(sampleRate, AV_SAMPLE_FMT_S16, 1);
			I_LOG("[apc::setEncoder->{}] Encoder opened success.", chnlId);
		}
		catch (std::exception& ex) {
			E_LOG("[apc::setEncoder->{}] get exception: {}", chnlId, ex.what());
			return -1;
		}
		return 0;
	}
}
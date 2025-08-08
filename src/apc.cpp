#include "AudioPorcessChnl.h"

namespace aom {
	double calculateRMS(const std::vector<int16_t>& samples) {
		double sum = 0.0;
		for (const auto& sample : samples) {
			sum += sample * sample;  // 计算每个样本的平方
		}
		double mean = sum / samples.size();  // 计算均值
		return std::sqrt(mean);  // 返回均方根值
	}

	double calculateVolume(const std::vector<int16_t>& samples) {
		double rms = calculateRMS(samples);
		// 防止对 0 取对数
		if (rms <= 0) {
			return -std::numeric_limits<double>::infinity(); // 负无穷大表示无声
		}
		return 20.0 * std::log10(rms);
	}

	int getDB(const std::vector<int16_t>& samples) {
		int db = 0;
		if (samples.empty()) return db;
		double sum = 0;
		for (const auto& c : samples) {
			sum += std::abs(c); 
		}
		sum = sum / samples.size();
		if (sum > 0) {
			db = static_cast<int>(20.0 * log10(sum));
		}
		return db;
	}

	float calculateEnergy(const int16_t* frame, int size) {
		float energy = 0.0f;
		for (int i = 0; i < size; ++i) {
			energy += frame[i] * frame[i];
		}
		return energy / size;
	}

	float calculateZeroCrossingRate(const int16_t* frame, int size) {
		int zeroCrossings = 0;
		for (int i = 1; i < size; ++i) {
			if ((frame[i - 1] >= 0 && frame[i] < 0) || (frame[i - 1] < 0 && frame[i] >= 0)) {
				zeroCrossings++;
			}
		}
		return static_cast<float>(zeroCrossings) / size;
	}

	const int FRAME_SIZE = 1024; // 每帧的样本数
	const float ENERGY_THRESHOLD = 0.1f; // 能量阈值

	float calculateDynamicThreshold(const std::vector<int16_t>& samples) {
		float totalEnergy = 0.0f;
		for (const auto& sample : samples) {
			totalEnergy += sample * sample;
		}
		return (totalEnergy / samples.size()) * 1.5f; // 根据需要调整倍数
	}

	void voiceActivityDetection(const std::vector<int16_t>& samples) {
		float dynamicThreshold = calculateDynamicThreshold(samples);
		std::deque<float> energyHistory;
		const int historySize = 5;

		int numFrames = samples.size() / FRAME_SIZE;
		for (int i = 0; i < numFrames; ++i) {
			const int16_t* frame = &samples[i * FRAME_SIZE];
			float energy = calculateEnergy(frame, FRAME_SIZE);
			float zeroCrossingRate = calculateZeroCrossingRate(frame, FRAME_SIZE);

			energyHistory.push_back(energy);
			if (energyHistory.size() > historySize) {
				energyHistory.pop_front();
			}
			float smoothedEnergy = std::accumulate(energyHistory.begin(), energyHistory.end(), 0.0f) / energyHistory.size();

			if (smoothedEnergy > dynamicThreshold && zeroCrossingRate > ENERGY_THRESHOLD) {
				std::cout << "Frame " << i << ": Voice detected (Energy: " << smoothedEnergy << ", ZCR: " << zeroCrossingRate << ")" << std::endl;
			}
		}
	}


	AudioPorcessChnl::AudioPorcessChnl(const std::string& jobid, const std::string& chnlid, Point listen, Point dst,
		FreePortCallback callback) : jobId(jobid), chnlId(chnlid), listenPoint(listen), dstPoint(dst),
		freePortCallback(callback), decoder(nullptr), encoder(nullptr), notifier(nullptr), switcher(nullptr) {
		srcBuffer.reserve(30000);
		encFrame = av_frame_alloc();
		encPkt = av_packet_alloc();
		std::string name1 = chnlid + "_dec.pcm";
		std::string name2 = chnlid + "_enc.pcm";
		if (saveInput == 1) decFile = fopen(name1.c_str(), "wb");
		if (saveOutput == 1) encFile = fopen(name2.c_str(), "wb");
	}

	AudioPorcessChnl::~AudioPorcessChnl() {
		close();
	}

	bool AudioPorcessChnl::open(int codecType, int inputRate, int outputRate, int payloadType) {
		status << TaskStatusType::run;
		this->codecType = codecType;
		sampleRate = outputRate;
		if (setDecoder(codecType, inputRate) != 0) return false;
		if (setEncoder(codecType, outputRate) != 0) return false;
		this->payloadType = payloadType;
		//随机设置ssrc
		ssrc = rand() % 9000000 + 1000000 + (int32_t)seeker::time::currentTime();
		work1Th = std::thread{ &AudioPorcessChnl::workingLoop, this };
		I_LOG("[apc::open->{}:{}] channel open success. codecType:{}, inputRate:{}, payloadType:{}", 
			jobId, chnlId, codecType, inputRate, payloadType);
		return true;
	}

	void AudioPorcessChnl::close() {
		if (status.getStatus() == TaskStatusType::end) return;
		status << TaskStatusType::down;
		if (work1Th.joinable()) work1Th.join();
		if(encFrame) av_frame_free(&encFrame);
		if(encPkt) av_packet_free(&encPkt);
		status << TaskStatusType::end;
		freePortCallback(listenPoint.port);
		I_LOG("[apc::close->{}:{}] channel close success", jobId, chnlId);
	}

	float AudioPorcessChnl::getVolume() const {
		return db.load();
	}

	size_t AudioPorcessChnl::getLength() const {
		lockGuard lck(srcBufLocker);
		return srcBuffer.size();
	}

	void AudioPorcessChnl::getBuffer(std::vector<int16_t>& dst, size_t length) {
		lockGuard lck(srcBufLocker);
		if (srcBuffer.size() < length) {
			//srcBuffer.clear();
			D_LOG("[apc::getBuffer->{}:{}] get src buffer is empty, size {}", jobId, chnlId, srcBuffer.size());
			return;
		}
		dst.assign(srcBuffer.begin(), srcBuffer.begin() + length);
		srcBuffer.erase(srcBuffer.begin(), srcBuffer.begin() + length);
	}

	void AudioPorcessChnl::setMicType(int val) { 
		micType.store(val); 
		micOpenNeedClear = true;
	}

	void AudioPorcessChnl::updateDestition(const std::string& ip, port_t port) {
		uniqueLock lck(switchLocker);
		switcher->setDestination(ip, port);
	}

	TaskStatusType AudioPorcessChnl::getStatus() const { return status.getStatus(); }

	int16_t AudioPorcessChnl::getPort() const { return listenPoint.port; }

	void AudioPorcessChnl::sendRtp(uint8_t* pcmData, int nb_samples, uint32_t ts) {
		if (codecType == 1) {
			if (saveOutput == 1) fwrite(pcmData, 1, nb_samples * 2, encFile);
		}
		else if (codecType == 2) {
			if (saveOutput == 1) fwrite(pcmData, 1, nb_samples * 4, encFile);
		}
		encFrame->data[0] = pcmData;
		encFrame->nb_samples = nb_samples;
		encFrame->channels = 1;
		encFrame->pts = ts;
		if (codecType == 1) {
			encFrame->format = 1;
		}
		else if (codecType == 2) {
			encFrame->format = 8;
		}
		encoder->getPacket(encFrame, encPkt);
		this->ts += (sampleRate / 50);
		std::vector<uint8_t> payload = std::vector<uint8_t>(encPkt->data, encPkt->data + encPkt->size);
		seeker::rtp::Rtp rtpPacket = seeker::rtp::Rtp(payloadType, 1, seqNum++, this->ts, ssrc, payload);
		std::deque<Rtp> sendQueue{ std::move(rtpPacket) };
		uniqueLock lck(switchLocker);
		switcher->sendRtp(sendQueue);
		av_frame_unref(encFrame);
		av_packet_unref(encPkt);
	}

	bool AudioPorcessChnl::ready() const { return chnlReady.load(); }

	bool AudioPorcessChnl::micOpen() const { return micType != 0 ? true : false; }

	void AudioPorcessChnl::workingLoop() {
		try {
			//设置视频RTP接收器，让接收器绑定收流地址并设置发流地址
			I_LOG("listen addr is {}:{}, dst addr is {}:{}", listenPoint.ip, listenPoint.port, dstPoint.ip, dstPoint.port);
				switcher = std::make_unique<RtpTransceiver>(chnlId, 32);
			if (switcher->open(listenPoint.ip, listenPoint.port) != 0) {
				E_LOG("[apc::workingLoop->{}:{}] rtpTrs bind video recv ip={}, port={} failed.",
					jobId, chnlId, listenPoint.ip, listenPoint.port);
				status << TaskStatusType::exce;
				return;
			}
			switcher->setDestination(dstPoint.ip, dstPoint.port);

			//设置视频RTP接收唤醒器
			notifier = std::make_shared<Notifier>();
			switcher->setRtpNotifier(notifier);
		}
		catch (std::exception& ex) {
			E_LOG("[apc::workingLoop->{}:{}] get exception: {}", jobId, chnlId, ex.what());
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
		int64_t timeTotal = 0;
		int64_t rtpTotal = 0;
		int64_t pcmTotal = 0;
		double dbTotal = 0.0;
		int32_t timeCount = 0;
		try {
			swrContext = swr_alloc_set_opts(NULL,
				AV_CH_LAYOUT_MONO, // 输出声道布局
				AV_SAMPLE_FMT_S16, // 输出采样格式
				48000,     // 输出采样率
				AV_CH_LAYOUT_MONO,  // 输入声道布局
				AV_SAMPLE_FMT_FLT,      // 输入采样格式
				48000,     // 输入采样率
				0, NULL);
			if (swr_init(swrContext) < 0) {
				E_LOG("init swrContext fail");
				throw std::runtime_error("init swrContext fail");
			}
			printTimer = InvokeTimer::CreateTimer(std::chrono::seconds(mpucheckInterval), true, [&] {
				pcmTotal /= 2;
				float timeAvg = (float)timeTotal / timeCount;
				float rtpAvg = rtpTotal == 0 ? 0.0f : (float)rtpTotal / timeCount;
				float pcmAvg = pcmTotal == 0 ? 0.0f : (float)pcmTotal / timeCount;
				float dbAvg = dbTotal == 0 ? 0.0f : (float)dbTotal / timeCount;
				db.store(dbAvg);
				I_LOG("[APC::check->{}:{}] LoopUse[{}ms] process[{}pkt {}pcm data] [avg {:.4f}db]", 
					jobId, chnlId, timeAvg, rtpTotal, pcmTotal, dbAvg);
				timeTotal = 0;
				rtpTotal = 0;
				pcmTotal = 0;
				dbTotal = 0.0;
				timeCount = 0;
			});
			printTimer->Start();
			chnlReady.store(true);
			I_LOG("[apc::workingLoop->{}:{}] thread is open, listen {}:{}", jobId, chnlId, listenPoint.ip, listenPoint.port);
			while (status) {
				// 1.判断麦克风状态，闭麦状态下不收流
				if (micType.load() == 0) {
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					lockGuard lck(srcBufLocker);
					if (!srcBuffer.empty()) srcBuffer.clear();
					continue;
				}
				timePoint = seeker::time::currentTime();
				int64_t usePoint = seeker::time::currentTime();
				// 2.接收音频流
				switcher->receiveRtp(recvQueue);

				// 3.若未能收到音频流，等待1ms后重新收流
				int noRtpCount = 0;
				while (recvQueue.empty() && status && micType.load() != 0) {
					notifier->waitNotify(1);
					switcher->receiveRtp(recvQueue);
					if (noRtpCount > 100) {
						W_LOG("[apc::workingLoop->{}:{}] no rtp data", jobId, chnlId);
						noRtpCount = 0;
					}
					noRtpCount++;
				}
				if (micType.load() == 0) continue;
				int t = seeker::time::currentTime() - usePoint;
				if (t > 65) W_LOG("[apc::workingLoop->{}:{}] recv use {}ms", jobId, chnlId, t);
				if (!status) break;
				while (!recvQueue.empty()) {
					rtpTotal++;
					usePoint = seeker::time::currentTime();
					auto& receivedRtp = recvQueue.front();
					auto& rtpData = receivedRtp.element;
					auto& from = receivedRtp.from;
					if (rtpData.length() == 0) {
						throw std::runtime_error("error: rtpData.length() == 0");
					}

					//payloadType = rtpData.payloadType();
					// 4.判断音频RTP包seq是否连续，若不连续说明丢包，需要补0
					uint16_t seq = (int)rtpData.seq();
					//if (lastSeq == 0) lastSeq = seq;
					//else {
					//	while (seq > lastSeq + 1) {
					//		if (size) {
					//			lockGuard lck(srcBufLocker);
					//			srcBuffer.insert(srcBuffer.end(), size / 2, 0);
					//		}
					//		lastSeq++;
					//	}
					//}
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
					//if (ret < 0) {
					//	E_LOG("[apc::workingLoop->{}:{}] use av_packet_from_data failed", jobId, chnlId);
					//	av_free(pkt->data);
					//	continue;
					//}
				
					// 6.解码音频帧
					if (decoder->getFrame(pkt, frame) != 0) {
						av_frame_unref(frame);
						av_packet_unref(pkt);
						recvQueue.pop_front();
						continue;
					}
					int size = frame->nb_samples * av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format))
						* frame->channels;
					int32_t inc = ts - lastTs;
					D_LOG("seq:{}, ts:{}, increment:{}, audio frame size is {}", seq, ts, inc, size);
					lastTs = ts;
					int t = seeker::time::currentTime() - usePoint;
					if (t > 5) W_LOG("[apc::workingLoop->{}:{}] dec use {}ms", jobId, chnlId, t);
					usePoint = seeker::time::currentTime();
					// 7.将解码数据存入源缓存区中
					if (codecType == 1) {
						if(saveInput == 1) fwrite(frame->data[0], 1, size, decFile);
						{
							lockGuard lck(srcBufLocker);
							pcmTotal += srcBuffer.size();
							if (srcBuffer.size() > (int64_t)sampleRate / 50 * 6) {
								//W_LOG("[apc::workingLoop->{}:{}] buffer size is {}", jobId, chnlId, srcBuffer.size());
								if (micOpenNeedClear) {
									srcBuffer.clear();
									micOpenNeedClear = false;
								}
								else {
									srcBuffer.erase(srcBuffer.begin(), srcBuffer.begin() + (srcBuffer.size() / 2));
								}
							}
							srcBuffer.insert(srcBuffer.end(), (int16_t*)frame->data[0], (int16_t*)frame->data[0] + size / 2);
							D_LOG("recv pcm size = {}", size / 2);
							//ns(srcBuffer);
							//dbTotal += calculateVolume(srcBuffer);
							dbTotal += getDB(srcBuffer);
							//voiceActivityDetection(srcBuffer);
						}
						t = seeker::time::currentTime() - usePoint;
						if (t > 5) W_LOG("[apc::workingLoop->{}:{}] insert use {}ms", jobId, chnlId, t);
					}
					else if (codecType == 2) {
						int outputFrameSize = av_samples_get_buffer_size(NULL, frame->channels, frame->nb_samples, AV_SAMPLE_FMT_S16, 1);
						uint8_t* outputBuffer = (uint8_t*)av_malloc(outputFrameSize);
						int outputSamples = swr_convert(swrContext,
							&outputBuffer,
							frame->nb_samples,
							(const uint8_t**)frame->data,
							frame->nb_samples);
						{
							lockGuard lck(srcBufLocker);
							pcmTotal += srcBuffer.size();
							if (srcBuffer.size() > (int64_t)sampleRate / 50 * 20) {
								W_LOG("[apc::workingLoop->{}:{}] buffer size is {}", jobId, chnlId, srcBuffer.size());
								if (micOpenNeedClear) {
									srcBuffer.clear();
									micOpenNeedClear = false;
								}
								else {
									srcBuffer.erase(srcBuffer.begin(), srcBuffer.begin() + (srcBuffer.size() / 2));
								}
							}
							if (saveInput == 1) fwrite(outputBuffer, 1, outputFrameSize, decFile);
							srcBuffer.insert(srcBuffer.end(), (int16_t*)outputBuffer, (int16_t*)outputBuffer + outputFrameSize / 2);
							D_LOG("recv pcm size = {}", size / 2);
							//ns(srcBuffer);
							//dbTotal += calculateVolume(srcBuffer);
							dbTotal += getDB(srcBuffer);
							//voiceActivityDetection(srcBuffer);
						}
						av_free(outputBuffer);
						t = seeker::time::currentTime() - usePoint;
						if (t > 5) W_LOG("[apc::workingLoop->{}:{}] insert use {}ms", jobId, chnlId, t);
					}
					av_frame_unref(frame);
					av_packet_unref(pkt);
					recvQueue.pop_front();
				}
				int64_t loopTime = seeker::time::currentTime() - timePoint;
				timeTotal += loopTime;
				timeCount++;
			}
			swr_close(swrContext);  // 关闭上下文
			swr_free(&swrContext);  // 释放上下文
		}
		catch (std::exception& ex) {
			E_LOG("[apc::workingLoop->{}:{}] get exception: {}", jobId, chnlId, ex.what());
			status << TaskStatusType::exce;
		}
		if (printTimer) printTimer->Cancel();
		I_LOG("[apc::workingLoop->{}:{}] thread is close, listen {}:{}", jobId, chnlId, listenPoint.ip, listenPoint.port);
	}

	int AudioPorcessChnl::setDecoder(int codecType, int sampleRate) {
		try {
			if (decoder) {
				decoder->close();
				W_LOG("[apc::setDecoder->{}:{}] Decoder already exists, resetting...", jobId, chnlId);
			}
			else {
				decoder = std::make_unique<AudioEngine23::Decoder>();
			}
			if (codecType == 1) {
				decoder->open(AudioEngine23::AudioType::PCMA, sampleRate, AV_SAMPLE_FMT_S16, 1);
			}
			else if (codecType == 2) {
				I_LOG("opus decoder open");
				if (decoder->open(AudioEngine23::AudioType::OPUS, sampleRate, AV_SAMPLE_FMT_FLT, 1) != 0) {
					E_LOG("[apc::setDecoder->{}:{}] open Decoder failed", jobId, chnlId);
					return -1;
				}
			}
			I_LOG("[apc::setDecoder->{}:{}] Decoder opened success", jobId, chnlId);
		}
		catch (std::exception& ex) {
			E_LOG("[apc::setDecoder->{}:{}] get exception: {}", jobId, chnlId, ex.what());
			return -1;
		}
		return 0;
	}


	int AudioPorcessChnl::setEncoder(int codecType, int sampleRate) {
		try {
			if (encoder) {
				encoder->close();
				W_LOG("[apc::setEncoder->{}] Encoder already exists, resetting...", jobId);
			}
			else encoder = std::make_unique<AudioEngine23::Encoder>();

			if (codecType == 1) {
				encoder->open(AudioEngine23::AudioType::PCMA, sampleRate, AV_SAMPLE_FMT_S16, 1);
			}
			else if (codecType == 2) {
				I_LOG("opus encoder open");
				encoder->open(AudioEngine23::AudioType::OPUS, sampleRate, AV_SAMPLE_FMT_FLT, 1);
			}
			I_LOG("[apc::setEncoder->{}] Encoder opened success.", jobId);
		}
		catch (std::exception& ex) {
			E_LOG("[apc::setEncoder->{}] get exception: {}", jobId, ex.what());
			return -1;
		}
		return 0;
	}
}
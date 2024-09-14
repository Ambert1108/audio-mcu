#include "MediaProcessUnit.h"

namespace aom {
	inline std::string parseTime(int64_t timestamp) {
		timestamp *= 0.001;
		int64_t hour = timestamp / 3600;
		int64_t min = timestamp / 60 - hour * (int64_t)60;
		int64_t sec = timestamp - hour * (int64_t)3600 - min * (int64_t)60;
		std::string s = std::to_string(hour) + "h:" + std::to_string(min) + "min:" + std::to_string(sec) + "s";
		return s;
	}

	MediaProcessUnit::MediaProcessUnit(MpuCtxPtr&& ptr, RemoveCallback callback)
		: ctx(std::move(ptr)), autoCloseCallback(callback), APCs(10), mixer(nullptr), encoder(nullptr) {
		startTime = seeker::time::currentTime();
		mixer = std::make_unique<AudioMixer>();
		status << TaskStatusType::run;
		workTh = std::thread{ &MediaProcessUnit::workingLoop, this };
		eventTh = std::thread{ &MediaProcessUnit::eventHandle, this };
		data.jobId = ctx->jobId;
		I_LOG("[mpu::create->{}] codecType={}, outSampleRate={}, bitrate={}, timeInterval={}", 
			ctx->jobId, ctx->codecType, ctx->outSampleRate, ctx->bitrate, ctx->interval);
		data.creatingDuration = seeker::time::currentTime() - startTime;
	}

	MediaProcessUnit::~MediaProcessUnit() {
		//判断关闭线程是否可执行并执行完毕
		if (stopTh.joinable()) stopTh.join();
		
		//MPU转为exce态或长时间无通道时事件处理线程将结束，导致无法接收外部关闭事件，需要自己主动关闭
		stop();

		I_LOG("[MPU::destory] jobId={} success", ctx->jobId);
	}

	void MediaProcessUnit::reportMediaInfo(std::unique_ptr<Event> info) {
		if (!status) return;
		if (!info) {
			E_LOG("[mpu::reportMediaInfo->{}] evnetQue get a nullptr event!", ctx->jobId);
		}
		{
			writeLock lck(eventQueLocker);
			eventQue.push_back(std::move(info));
		}
		eventCondition.notify_one();
	}

	TaskStatusType MediaProcessUnit::getStatus() const { return status.getStatus(); }

	const MediaProcessData& MediaProcessUnit::getData() const { return data; }

	int MediaProcessUnit::getChnlNum() const { return APCs.size(); }

	void MediaProcessUnit::addChannel(const std::string& id, const Point& src, const Point& dst, int sampleRate) {
		if (ctx->inSampleRate == 0) ctx->inSampleRate = sampleRate;
		else {
			if (ctx->inSampleRate != sampleRate) {
				E_LOG("[mpu::addChannel->{}] channel[{}] input sampleRate {} is inconsistent with the set sampleRate {}", 
					ctx->jobId, id, sampleRate, ctx->inSampleRate);
				return;
			}
		}
		{
			uniqueLock lck(apcLocker);
			auto newChnl = APCs.try_emplace(id, std::make_unique<AudioPorcessChnl>(id, src, dst, ctx->interval));

			if (!newChnl.second) {
				E_LOG("[mpu::addChannel->{}] add channel[{}] failed, id is exist", ctx->jobId, id);
				return;
			}
			if (!newChnl.first->second->open(ctx->codecType, sampleRate, ctx->outSampleRate, 
				ctx->bitrate, ctx->payloadType)) {
				E_LOG("[mpu::addChannel->{}] open channel[{}] failed", ctx->jobId, id);
				APCs.erase(id);
				return;
			}
		}
		uniqueLock lck(mixerLocker);
		mixer->addStreamId(id);
	}

	void MediaProcessUnit::removeChannel(const std::string& id) {
		{
			uniqueLock lck(apcLocker);
			auto it = APCs.find(id);
			if (it == APCs.end()) {
				E_LOG("[mpu::removeChannel->{}] remove channel[{}] failed, id not found", ctx->jobId, id);
				return;
			}
			APCs.erase(it);
		}
		uniqueLock lck(mixerLocker);
		mixer->removeId(id);
	}

	void MediaProcessUnit::openChnlMic(const std::string& id) {
		uniqueLock lck(apcLocker);
		auto it = APCs.find(id);
		if (it == APCs.end()) {
			E_LOG("[mpu::openChnlMic->{}] open channel[{}] mic failed, id not found", ctx->jobId, id);
			return;
		}
		it->second->setMicType(1);
	}

	void MediaProcessUnit::closeChnlMic(const std::string& id) {
		uniqueLock lck(apcLocker);
		auto it = APCs.find(id);
		if (it == APCs.end()) {
			E_LOG("[mpu::closeChnlMic->{}] close channel[{}] mic failed, id not found", ctx->jobId, id);
			return;
		}
		it->second->setMicType(0);
	}

	void MediaProcessUnit::stop() {
		//若MPU已经关闭，则不再重复操作
		if (status.getStatus() == TaskStatusType::end) return;

		int64_t stopTimePoint = seeker::time::currentTime();

		//将MPU状态置为释放态
		status << TaskStatusType::down;

		//阻塞的关闭所有线程
		eventCondition.notify_all();
		if (workTh.joinable()) workTh.join();
		if (eventTh.joinable()) eventTh.join();
		APCs.clear();

		data.destroyingDuration = seeker::time::currentTime() - stopTimePoint;
		data.runningDuration = seeker::time::currentTime() - startTime;
		output();

		//将MPU状态置为结束态，此时MPU可销毁
		status << TaskStatusType::end;
	}

	void MediaProcessUnit::output() {
		W_LOG("\n--------- mpu output ---------\njobId={}, closeMethod={}\nMixoutNum={}\n"
			"Duration=[{}ms/{}ms/{}](create/destory/run)\ncurrentChnl={}, maxChnl={}\nopenMic={}\n"
			"--------- mpu output ---------",
			data.jobId, data.closeMethod, data.timeOutNum,
			data.creatingDuration, data.destroyingDuration, parseTime(data.runningDuration),
			data.currentChnl, data.maxChnl, data.openMic);
	}

	void MediaProcessUnit::workingLoop() {
		/* MPU监控定时器 */
		InvokeTimerPtr printTimer = nullptr;
		//每mpucheckInterval秒计算MPU相关参数
		printTimer = InvokeTimer::CreateTimer(std::chrono::seconds(mpucheckInterval), true, [&] {
				
			});
		printTimer->Start();
		FILE* pcmaFile = fopen("test.pcm", "wb");
		setEncoder(ctx->outSampleRate);
		AVFrame* frame = av_frame_alloc();
		AVPacket* pkt = av_packet_alloc();
		int64_t startTime = seeker::time::currentTime();
		while (status) {
			int64_t timePoint = seeker::time::currentTime();
			while (APCs.empty() && status) {
				if (waitChnlTime / 1000 >= noChnlTime) {
					status << TaskStatusType::exce;
					data.closeMethod = "noChnlAutoClose";
					autoCloseCallback(ctx->jobId);
				}
				waitChnlTime = seeker::time::currentTime() - timePoint;
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			std::queue<std::string> mixList{};
			std::unordered_map<std::string, std::vector<int16_t>> srcForm{};
			std::unordered_map<std::string, std::vector<int16_t>> dstForm{};
			bool noNeedMix = false;
			// 向混音工具提供各个通道的音频数据
			{
				uniqueLock lck(apcLocker);
				// 判断最小混音长度，避免混音工具补0
				size_t minLength = INT32_MAX;
				for (const auto& [key, val] : APCs) {
					if (val->getStatus() == TaskStatusType::exce) {
						reportMediaInfo(std::make_unique<RemoveChnlEvent>(RemoveChnlContext(ctx->jobId, key)));
						continue;
					}
					size_t length = val->getLength();
					D_LOG("1 chnlId:{}, length:{}", key, length);
					if (length < 100) {
						//if (APCs.size() == 1) {
						//	noNeedMix = true;
						//	break;
						//}
						//else continue;
						noNeedMix = true;
						break;
					}
					if (length < minLength) {
						minLength = length;
					}
					mixList.push(key);
				}
				if (mixList.empty()) noNeedMix = true;

				if (!noNeedMix) {
					// 获取各通道解码结果
					while (!mixList.empty()) {
						auto& id = mixList.front();
						auto it = APCs.find(id);
						if (it == APCs.end()) continue;
						std::vector<int16_t> data;
						D_LOG("2 chnlId:{} length:{}", id, minLength);
						it->second->getBuffer(data, minLength);
						if (data.empty()) continue;
						srcForm.insert(std::pair<std::string, std::vector<int16_t>>(id, data));
						mixList.pop();
					}
					//for (const auto& [key, val] : APCs) {
					//	std::vector<int16_t> data;
					//	val->getBuffer(data, minLength);
					//	if (data.empty()) continue;
					//	srcForm.insert(std::pair<std::string, std::vector<int16_t>>(key, data));
					//}
				}
			}

			if(!noNeedMix) {
				uniqueLock mlck(mixerLocker);
				// 向混音工具输入数据进行混音
				for (auto& [key, val] : srcForm) {
					mixer->pushData(key, val);
				}

				//获取混音结果
				for (const auto& [key1, val1] : srcForm) {
					std::queue<std::string> idForm{};
					for (const auto& [key2, val2] : srcForm) {
						if (key2 == key1) continue;
						idForm.push(key2);
					}
					dstForm.insert(std::pair<std::string, std::vector<int16_t>>(key1, mixer->getData(idForm)));
				}
				mixer->clearData();
			}

			uint32_t ts = (seeker::time::currentTime() - startTime) * 90;
			//将混音结果编码并下发给各通道发送
			{
				uniqueLock lck(apcLocker);
				for (const auto& [key, val] : dstForm) {
					auto it = APCs.find(key);
					if (it == APCs.end()) continue;
					frame->data[0] = (uint8_t*)val.data();
					frame->nb_samples = val.size();
					frame->format = AV_SAMPLE_FMT_S16;
					frame->channels = 1;
					frame->pts = ts;
					encoder->getPacket(frame, pkt);
					fwrite(pkt->data, 1, pkt->size, pcmaFile);
					it->second->sendRtp(std::vector<uint8_t>(pkt->data, pkt->data + pkt->size), ts);
					av_packet_unref(pkt);
					av_frame_unref(frame);
				}
			}
			int32_t use = seeker::time::currentTime() - timePoint;
			if(use < 25) std::this_thread::sleep_for(std::chrono::milliseconds(use));
		}
		W_LOG("[mpu::workingLoop->{}] Thread is open", ctx->jobId);
			
		if (printTimer) printTimer->Cancel();
		I_LOG("[mpu::workingLoop->{}] thread is closed", ctx->jobId);
	}

	void MediaProcessUnit::eventHandle() {
		try {
			std::deque<std::unique_ptr<Event>> eventHandleQue{};
			while (status) {
				uniqueLock lck(eventLocker);
				eventCondition.wait_for(lck, std::chrono::milliseconds(1), [this] {return !eventQue.empty(); });
				if (!status) break;
				if (eventQue.empty()) continue;

				//读取所有事件
				{
					writeLock lck(eventQueLocker);
					eventHandleQue.swap(eventQue);
				}
				//处理所有事件
				for (const auto& each : eventHandleQue) {
					if (!status) break;
					switch (each->type) {
					case JobHandleType::stop:
						I_LOG("[mpu::eventHandle->{}] handle stop job event", ctx->jobId);
						stopTh = std::thread{ &MediaProcessUnit::stop, this };
						break;

					case JobHandleType::add:
						each->handle(this);
						data.currentChnl++;
						if (data.currentChnl > data.maxChnl) data.maxChnl = data.currentChnl;
						break;

					case JobHandleType::remove:
						each->handle(this);
						data.currentChnl--;
						break;

					case JobHandleType::open:
						each->handle(this);
						data.openMic++;
						break;

					case JobHandleType::close:
						each->handle(this);
						data.openMic--;
						break;

					default:
						E_LOG("[mpu::eventHandle->{}] handle unknown event, type={}", ctx->jobId, (int)each->type);
						break;
					}
				}
				std::deque<std::unique_ptr<Event>>().swap(eventHandleQue);
			}
		}
		catch (std::exception& ex) {
			E_LOG("[mpu::eventHandle->{}] get exception: {}", ctx->jobId, ex.what());
			status << TaskStatusType::exce;
			data.closeMethod = "MpuException";
			autoCloseCallback(ctx->jobId);
		}
		I_LOG("[mpu::eventHandle->{}] eventHandle thread is closed", ctx->jobId);
	}

	int MediaProcessUnit::setEncoder(int sampleRate) {
		try {
			if (encoder) {
				encoder->close();
				W_LOG("[mpu::setEncoder->{}] Encoder already exists, resetting...", ctx->jobId);
			}
			else encoder = std::make_unique<AudioEngine23::Encoder>();

			encoder->open(sampleRate, AV_SAMPLE_FMT_S16, 1);
			I_LOG("[mpu::setEncoder->{}] Encoder opened success.", ctx->jobId);
		}
		catch (std::exception& ex) {
			E_LOG("[mpu::setEncoder->{}] get exception: {}", ctx->jobId, ex.what());
			return -1;
		}
		return 0;
	}
}
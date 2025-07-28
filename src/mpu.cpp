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

	//inline std::shared_ptr<httplib::Client> initClient(const std::string callbackUrl, std::string& ASUrl) {
	//	const std::string str = callbackUrl;
	//	std::string ip;
	//	int port;
	//	std::string Url;
	//	std::string address;
	//	const static std::string ipRegString = R"regex(//(\d+).(\d+).(\d+).(\d+))regex";
	//	const static std::regex ipReg(ipRegString);
	//	const static std::string portRegString = R"regex((\d+)/)regex";
	//	const static std::regex portReg(portRegString);
	//	std::smatch sm;
	//	if (std::regex_search(str, sm, ipReg)) {
	//		ip = std::string(sm[1].first, sm[4].second);
	//		if (std::regex_search(str, sm, portReg)) {
	//			std::string port1(sm[1].first, sm[1].second);
	//			port = std::atoi(port1.c_str());
	//			ASUrl = std::string(sm[1].second, str.end());
	//			address = std::string(str.begin(), sm[1].second);
	//			I_LOG("port {}", port);
	//			I_LOG("url {}", ASUrl);
	//			I_LOG("http address {}", address);
	//		}
	//	}
	//	else {
	//		E_LOG("initClient error");
	//		return nullptr;
	//	}
	//	return std::make_shared<httplib::Client>(ip, port);
	//}

	MediaProcessUnit::MediaProcessUnit(MpuCtxPtr&& ptr, RemoveCallback callback1, FreePortCallback callback2)
		: ctx(std::move(ptr)), autoCloseCallback(callback1), freePortCallback(callback2), APCs(10), mixer(nullptr) {
		startTime = seeker::time::currentTime();
		mixer = std::make_unique<AudioMixer>();
		status << TaskStatusType::run;
		//workTh = std::thread{ &MediaProcessUnit::workingLoop, this };
		eventTh = std::thread{ &MediaProcessUnit::eventHandle, this };
		//client = initClient(ctx->callbackUrl, url);
		//if (client) {
		//	callback = InvokeTimer::CreateTimer(std::chrono::seconds(callbackTime), true, [&] {
		//		if (seeker::time::currentTime() - callbackTimePoint >= noVoiceTime * 1000) {
		//			chnlId = "";
		//		}
		//		if (chnlId != chnlIdRecord) {
		//			CallbackRequest callbackReq{ chnlId };
		//			std::string req_body;
		//			I_LOG("[mpu::callback->{}] req signling body:\n{}", 
		//				ctx->jobId, seeker::json::toJsonString(callbackReq));
		//			auto res = client->Post(url, seeker::json::toJsonString(callbackReq), "application/json");//向指定的地址发送请求body1，并接收回复res1
		//			if (res == nullptr) {
		//				E_LOG("[mpu::callback->{}] no rsp from signling", ctx->jobId);
		//			}
		//			else {
		//				if (res->status == 200) {
		//					I_LOG("[mpu::callback->{}] signling rsp body:\n{}", ctx->jobId, res->body);	
		//				}
		//				else {
		//					E_LOG("[mpu::callback->{}] signling rsp status={}, body:\n{}", ctx->jobId, res->status, res->body);
		//				}
		//			}
		//			chnlIdRecord = chnlId;
		//		}
		//		});
		//	callback->Start();
		//}
		data.jobId = ctx->jobId;
		I_LOG("[mpu::create->{}] url={}", 
			ctx->jobId, ctx->callbackUrl);
		data.creatingDuration = seeker::time::currentTime() - startTime;
	}

	MediaProcessUnit::~MediaProcessUnit() {
		//判断关闭线程是否可执行并执行完毕
		if (stopTh.joinable()) stopTh.join();
		if (callback) callback->Cancel();
		//MPU转为exce态或未收到RTP包时事件处理线程将结束，导致无法接收外部关闭事件，需要自己主动关闭
		stop();

		I_LOG("[mpu::destory->{}] destory success", ctx->jobId);
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

	void MediaProcessUnit::addChannel(const std::string& id, const Point& src, const Point& dst, int pt,
		int codecType, int inRate, int outRate) {
		{
			uniqueLock lck(apcLocker);
			auto newChnl = APCs.try_emplace(id, std::make_unique<AudioPorcessChnl>(ctx->jobId, id, src, dst, freePortCallback));

			if (!newChnl.second) {
				E_LOG("[mpu::addChannel->{}] add channel[{}] failed, id is exist", ctx->jobId, id);
				return;
			}
			I_LOG("addchnl: codecTpye={}, inrate={}, outrate={}", ctx->codecType, inRate, outRate);
			if (ctx->outSampleRate == -1) ctx->outSampleRate = outRate;
			if (ctx->codecType == -1) ctx->codecType = codecType;
			if (!newChnl.first->second->open(ctx->codecType, inRate, outRate, pt)) {
				E_LOG("[mpu::addChannel->{}] open channel[{}] failed", ctx->jobId, id);
				APCs.erase(id);
				return;
			}
		}
		if (!workTh.joinable()) {
			workTh = std::thread{ &MediaProcessUnit::workingLoop, this };
		}
		uniqueLock lck(mixerLocker);
		mixer->addStreamId(id);
		data.portList.push_back(src.port);
	}

	void MediaProcessUnit::removeChannel(const std::string& id) {
		port_t port = 0;
		{
			uniqueLock lck(apcLocker);
			auto it = APCs.find(id);
			if (it == APCs.end()) {
				E_LOG("[mpu::removeChannel->{}] remove channel[{}] failed, id not found", ctx->jobId, id);
				return;
			}
			port = it->second->getPort();
			APCs.erase(it);
		}
		uniqueLock lck(mixerLocker);
		mixer->removeId(id);
		data.portList.erase(std::remove(data.portList.begin(), data.portList.end(), port), data.portList.end());
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

	void MediaProcessUnit::updateDest(const std::string& id, const std::string& ip, port_t port) {
		uniqueLock lck(apcLocker);
		auto it = APCs.find(id);
		if (it == APCs.end()) {
			E_LOG("[mpu::updateDest->{}] update destition address failed, cnlId:{} not found", ctx->jobId, id);
			return;
		}
		it->second->updateDestition(ip, port);
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
		int64_t startTime = seeker::time::currentTime();
		int64_t timePoint = 0;
		int64_t timeTotal = 0;
		int32_t timeCount = 0;
		int noNeedCount = 0;
		uint32_t ts = 0;
		size_t lengthStandard = ctx->outSampleRate / 50; //参考标准长度
		std::string mixId{};
		try {
			swrContext = swr_alloc_set_opts(NULL,
				AV_CH_LAYOUT_MONO, // 输出声道布局
				AV_SAMPLE_FMT_FLT, // 输出采样格式
				48000,     // 输出采样率
				AV_CH_LAYOUT_MONO,  // 输入声道布局
				AV_SAMPLE_FMT_S16,      // 输入采样格式
				48000,     // 输入采样率
				0, NULL);
			if (swr_init(swrContext) < 0) {
				E_LOG("init swrContext fail");
				throw std::runtime_error("init swrContext fail");
			}
			//每mpucheckInterval秒计算MPU相关参数
			printTimer = InvokeTimer::CreateTimer(std::chrono::seconds(mpucheckInterval), true, [&] {
				float timeAvg = (float)timeTotal / timeCount;
				I_LOG("[MPU::check->{}] LoopUse[{}ms] MixList[{}]", ctx->jobId, timeAvg, mixId);
				timeTotal = 0;
				timeCount = 0;
				mixId.clear();
			});
			printTimer->Start();
			W_LOG("[mpu::workingLoop->{}] Thread is open", ctx->jobId);
			while (status) {
				timePoint = seeker::time::currentTime();
				while (APCs.empty() && status) {
					//if (waitChnlTime / 1000 >= noChnlTime) {
					//	status << TaskStatusType::exce;
					//	data.closeMethod = "noChnlAutoClose";
					//	autoCloseCallback(ctx->jobId);
					//}
					//waitChnlTime = seeker::time::currentTime() - timePoint;
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				std::vector<std::pair<std::string, float>> chnlList; //通道分贝排序列表
				std::unordered_map<std::string, std::vector<int16_t>> srcForm{}; //需要混音的列表
				std::unordered_map<std::string, std::vector<int16_t>> dstForm{}; //需要编码发送的列表
				bool needMix = true;
				{
					uniqueLock lck(apcLocker);
					// 判断各通道数据大小是否符合标准，不符则跳过该通道混音
					for (const auto& [key, val] : APCs) {
						// 如果通道尚未初始化完成，跳过该通道
						if (!val->ready()) {
							std::this_thread::sleep_for(std::chrono::milliseconds(1));
							continue;
						}
						// 如果通道异常，自动移除
						if (val->getStatus() == TaskStatusType::exce) {
							reportMediaInfo(std::make_unique<RemoveChnlEvent>(RemoveChnlContext(ctx->jobId, key)));
							continue;
						}
						// 插入需要获取混音结果的通道
						dstForm.insert(std::pair<std::string, std::vector<int16_t>>(key, {}));

						// 如果通道麦克风为闭麦状态，跳过
						if (!val->micOpen()) continue;

						size_t length = val->getLength();
						// 所有小于标准长度的通道，本次不参与混音
						if (length < lengthStandard) {
							D_LOG("chnlId:{} length is {}", key, length);
							continue;
						}
						// 具备混音条件的通道，获取分贝后插入排序列表
						chnlList.push_back(std::pair<std::string, float>(key, val->getVolume()));
					}

					if (chnlList.empty()) {
						// 没有待混音通道，增加一次不混音计数
						++noNeedCount;
						// 当不混音计数达到5次（大概耗时105ms），则给各通道发送静音帧
						// 否则，跳过本次循环
						if (noNeedCount >= 5) {
							needMix = false;
							noNeedCount = 0;
						}
						else {
							int32_t use = seeker::time::currentTime() - timePoint;
							if (use < 20) std::this_thread::sleep_for(std::chrono::milliseconds(20 - use));
							timeTotal += seeker::time::currentTime() - timePoint;
							timeCount++;
							continue;
						}
					}
					else noNeedCount = 0;

					if (needMix) {
						// 将各通道分贝按照从大到小进行排序
						std::sort(chnlList.begin(), chnlList.end(), [](const auto& a, const auto& b) {
							return a.second > b.second;
						});

						// 获取最大分贝用户id，用于高亮显示
						if (chnlList.begin()->second >= dbThreshold) {
							chnlId = chnlList.begin()->first;
							callbackTimePoint = seeker::time::currentTime();
						}

						// 获取各通道音频裸数据
						mixId.clear();
						for (size_t i = 0; i < chnlList.size(); ++i) {
							auto& id = chnlList.at(i).first;
							auto it = APCs.find(id);
							if (it == APCs.end()) continue;
							std::vector<int16_t> data;
							// 所有通道的数据都需要消耗，避免堆积
							it->second->getBuffer(data, lengthStandard);
							if (data.empty()) continue;
							if (i < 5) {
								// 选取前三个通道进行混音
								srcForm.insert(std::pair<std::string, std::vector<int16_t>>(id, data));
								mixId = mixId + "|" + id;
							}
						}
					}
				}
				if (needMix) {
					// 向混音工具输入数据进行混音
					for (auto& [key, val] : srcForm) {
						uniqueLock mlck(mixerLocker);
						mixer->pushData(key, val);
					}

					//获取混音结果
					{
						uniqueLock mlck(mixerLocker);
						for (const auto& [key1, val1] : dstForm) {
							std::queue<std::string> idForm{};
							for (const auto& [key2, val2] : dstForm) {
								if (key2 == key1) continue;
								idForm.push(key2);
							}
							auto vec = mixer->getData(idForm);
							if (vec.empty()) vec = std::vector<int16_t>(lengthStandard, 0);
							dstForm.at(key1) = vec;
						}
						mixer->clearData();
					}
				}
				else {
					// 如果不需要混音，则所有通道都发送静音帧
					for (const auto& [key, val] : dstForm) {
						dstForm.at(key) = std::vector<int16_t>(lengthStandard, 0);
					}
					W_LOG("[mpu::workingLoop->{}] no need mix, send zero data", ctx->jobId);
				}
				ts += lengthStandard;
				//将可能的结果编码并下发给各通道发送
				{
					uniqueLock lck(apcLocker);
					for (const auto& [key, val] : dstForm) {
						auto it = APCs.find(key);
						if (it == APCs.end()) continue;
						if (val.empty()) {
							W_LOG("[mpu::workingLoop->{}:{}] data is empty", ctx->jobId, key);
							continue;
						}
						//frame->data[0] = (uint8_t*)val.data();
						//frame->nb_samples = val.size();
						//frame->format = av_sample_fmt_s16;
						//frame->channels = 1;
						//frame->pts = ts;
						//encoder->getpacket(frame, pkt);
						if (ctx->codecType == 1) {
							it->second->sendRtp((uint8_t*)val.data(), val.size(), ts);
						}
						else if (ctx->codecType == 2) {
							int outputFrameSize = av_samples_get_buffer_size(NULL, 1, val.size(), AV_SAMPLE_FMT_FLT, 1);
							uint8_t* outputBuffer = (uint8_t*)av_malloc(outputFrameSize);
							uint8_t* outputBufferArray[1]{};
							outputBufferArray[0] = (uint8_t*)val.data();
							int outputSamples = swr_convert(swrContext, &outputBuffer,
								val.size(), (const uint8_t**)&outputBufferArray[0], val.size());
							it->second->sendRtp(outputBuffer, val.size(), ts);
							av_free(outputBuffer);
						}
					}
				}
				int32_t use = seeker::time::currentTime() - timePoint;
				if (use < 20) std::this_thread::sleep_for(std::chrono::milliseconds(20 - use));
				timeTotal += seeker::time::currentTime() - timePoint;
				timeCount++;
			}
			swr_close(swrContext);  // 关闭上下文
			swr_free(&swrContext);  // 释放上下文
		}
		catch (std::exception& ex) {
			E_LOG("[mpu::workingLoop->{}] get exception: {}", ctx->jobId, ex.what());
			status << TaskStatusType::exce;
		}
			
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
						I_LOG("[mpu::eventHandle->{}] handle add chnl event", ctx->jobId);
						each->handle(this);
						data.currentChnl++;
						if (data.currentChnl > data.maxChnl) data.maxChnl = data.currentChnl;
						break;

					case JobHandleType::remove:
						I_LOG("[mpu::eventHandle->{}] handle remove chnl event", ctx->jobId);
						each->handle(this);
						data.currentChnl--;
						break;

					case JobHandleType::open:
						I_LOG("[mpu::eventHandle->{}] handle open mic event", ctx->jobId);
						each->handle(this);
						data.openMic++;
						break;

					case JobHandleType::close:
						I_LOG("[mpu::eventHandle->{}] handle close mic event", ctx->jobId);
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
}
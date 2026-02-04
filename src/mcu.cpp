#include "MediaControlUnit.h"

namespace aom {
	MediaControlUnit* MediaControlUnit::mcu = nullptr;
	std::atomic<uint16_t> MediaControlUnit::refCount_ = 0;

	MediaControlUnit::MediaControlUnit() {
		seeker::rtp::RtpTransceiver::init(8);
		eventList.reserve(30);
	}

	MediaControlUnit::~MediaControlUnit() {
		keepWork.store(false);
		memCheck.stop();
		if (mcuCheck) mcuCheck->Cancel();
		closeCondition.notify_all();
		if (AutoCloseThr.joinable()) AutoCloseThr.join();
		if (isZimu){
			auto& manager = aesir::TranscriberManager::getInstance();
			manager.shutdown();
		}
		RtpTransceiver::shutdown();
		W_LOG("[mcu::close] Media Control Unit close Success");
	}

	int MediaControlUnit::init() {
		AutoCloseThr = std::thread{ &MediaControlUnit::autoClose, this };
		endJobFunc = std::bind(&MediaControlUnit::endMpu, this, std::placeholders::_1, std::placeholders::_2);
		freePortFunc = std::bind(&MediaControlUnit::freePort, this, std::placeholders::_1);
		memCheck.start(memcheckInterval, memprintInterval);
		mcuCheck = InvokeTimer::CreateTimer(std::chrono::seconds(mcucheckInterval), true, [&]() {
			I_LOG("mcucheck: runningJob:{} | runningChnl:{} | create err:{} total:{} | "
				"join err:{} total:{} | leave err:{} total:{} | destory err:{} total:{}",
				status.runningJob.load(), status.runningChnl.load(), status.createErrNum.load(), status.createTotalNum.load(),
				status.joinErrNum.load(), status.joinTotalNum.load(), status.leaveErrNum.load(), status.leaveTotalNum.load(),
				status.destoryErrNum.load(), status.destoryTotalNum.load());
			});
		mcuCheck->Start();
		audioPortTool = std::make_unique<PortTool>(portPoint, portRange, "audio");
		if (isZimu) {
			auto& manager = aesir::TranscriberManager::getInstance();
			manager.startup(1, "/home/pangu/workspace/aesir/whispercpp/whisper.cpp-master/models/ggml-large-v3-turbo.bin",
				"/home/pangu/workspace/aesir/whispercpp/whisper.cpp-master/models/ggml-silero-v5.1.2.bin");
		}
		W_LOG("[mcu::init] Media Control Unit Init Success, mcu check={}s", mcucheckInterval);
		return 0;
	}

	void MediaControlUnit::autoClose() {
		int64_t timePoint = 0;
		mpuCloseForm tmpCloseForm{};

		I_LOG("[mcu::autoClose] autoclose is running, checkTime:{}ms", autocheckInterval);
		while (keepWork.load()) {
			uniqueLock lck(closeLocker);
			closeCondition.wait_for(lck, std::chrono::milliseconds(autocheckInterval),
				[this] {return !keepWork.load(); });
			if (!keepWork.load()) break;
			timePoint = seeker::time::currentTime();
			{
				writeLock lck(closeMpuFormLocker);
				tmpCloseForm.swap(closeMpus);
			}
			if (tmpCloseForm.empty()) continue;
			for (auto each = tmpCloseForm.begin(); each != tmpCloseForm.end();) {
				if ((*each)->getStatus() != TaskStatusType::end && (*each)->getStatus() != TaskStatusType::exce) {
					each++;
					continue;
				}
				const MediaProcessData& data = (*each)->getData();
				if (!data.portList.empty()) {
					for (const auto& val : data.portList) {
						audioPortTool->freePort(val);
					}
				}
				if(data.closeMethod != "HttpRequest") autoCloseNum.fetch_add(1);
				each = tmpCloseForm.erase(each);
				status.runningJob.fetch_sub(1);
				status.destoryTotalNum.fetch_add(1);
			}
		}

		I_LOG("[mcu::autoClose] autoclose is closed, handle count:{}", autoCloseNum.load());
	}

	void MediaControlUnit::freePort(port_t val) {
		if(audioPortTool) audioPortTool->freePort(val);
	}

	bool MediaControlUnit::checkJob(const std::string& id) {
		mpuForm::iterator it;
		{
			readLock lck(mpuFormLocker);
			it = mpus.find(id);
		}

		//mpu不存在，业务处理失败
		if (it == mpus.end()) return false;

		return true;
	}

	bool MediaControlUnit::createMpu(const CreateJobContext& context) {
		mpuForm::iterator it;
		{
			readLock lck(mpuFormLocker);
			it = mpus.find(context.jobId);

		}
		if (it != mpus.end()) {
			uniqueLock lck(eventLocker);
			eventList.emplace_back(EventInfo{
				"create",
				context.jobId,
				context.userId,
				seeker::time::toString(seeker::time::currentTime()),
				"fail",
				"未找到会议id" }
			);
			return false;
		}
		//mpu不存在，创建任务


		//尝试构造并加入MPU表单，若加入失败代表对应jobId已存在
		{
			writeLock lck(mpuFormLocker);
			auto newMpu = mpus.try_emplace(context.jobId, 
				std::make_unique<MediaProcessUnit>(std::make_unique<MpuContext>(context.jobId, 
					context.url), endJobFunc, freePortFunc));
			if (!newMpu.second) {
				uniqueLock lck(eventLocker);
				eventList.emplace_back(EventInfo{
					"create",
					context.jobId,
					context.userId,
					seeker::time::toString(seeker::time::currentTime()),
					"fail",
					"服务器内部错误" }
				);
				return false;
			}
		}
		status.runningJob.fetch_add(1);
		status.createTotalNum.fetch_add(1);
		uniqueLock lck(eventLocker);
		eventList.emplace_back(EventInfo{
			"create",
			context.jobId,
			context.userId,
			seeker::time::toString(seeker::time::currentTime()),
			"success",
			"" }
		);
		return true;
	}

	bool MediaControlUnit::endMpu(const std::string& jobId, const std::string& userId) {
		UniqueMPU mpu = nullptr;

		//在MPU表单中查找对应jobId，若不存在返回错误
		{
			writeLock lck(mpuFormLocker);
			auto it = mpus.find(jobId);
			if (it == mpus.end()) {
				W_LOG("[mcu::removeMpu][{}] is not found.", jobId);
				status.destoryErrNum.fetch_add(1);
				uniqueLock lck(eventLocker);
				eventList.emplace_back(EventInfo{
					"end",
					jobId,
					userId,
					seeker::time::toString(seeker::time::currentTime()),
					"fail",
					"未找到会议号" }
				);
				return false;
			}

			mpu = std::move(it->second);

			//移除MPU表单中相关信息
			mpus.erase(jobId);
		}

		//向对应的MPU发送关闭事件
		mpu->reportMediaInfo(std::make_unique<EndEvent>());

		//将需要销毁的MPU移交给待关闭MPU表单
		{
			writeLock lck(closeMpuFormLocker);
			closeMpus.emplace(std::move(mpu));
		}
		uniqueLock lck(eventLocker);
		eventList.emplace_back(EventInfo{
			"end",
			jobId,
			userId,
			seeker::time::toString(seeker::time::currentTime()),
			"success",
			"" }
		);
		return true;
	}

	bool MediaControlUnit::addChnl(const AddChnlContext& context, ListenAddr& addr) {
		//判断jobId是否存在
		mpuForm::iterator it;
		{
			readLock lck(mpuFormLocker);
			it = mpus.find(context.jobId);

		}

		//mpu不存在，业务处理失败
		if (it == mpus.end()) {
			status.joinErrNum.fetch_add(1);
			uniqueLock lck(eventLocker);
			eventList.emplace_back(EventInfo{
				"join",
				context.jobId,
				context.chnlId,
				seeker::time::toString(seeker::time::currentTime()),
				"fail",
				"未找到会议号" }
			);
			return false;
		}

		//申请音频端口
		port_t audioPort = audioPortTool->applyPort();
		if (audioPort == -1) {
			audioPortTool->freePort(audioPort);
			status.joinErrNum.fetch_add(1);
			uniqueLock lck(eventLocker);
			eventList.emplace_back(EventInfo{
				"join",
				context.jobId,
				context.chnlId,
				seeker::time::toString(seeker::time::currentTime()),
				"fail",
				"服务器内部错误：无法申请端口" }
			);
			return false;
		}

		if (context.inSampleRate == -1) {
			E_LOG("[mcu::createMpu][{}] request param: inSampleRate is -1", context.jobId);
			status.joinErrNum.fetch_add(1);
			uniqueLock lck(eventLocker);
			eventList.emplace_back(EventInfo{
				"join",
				context.jobId,
				context.chnlId,
				seeker::time::toString(seeker::time::currentTime()),
				"fail",
				"输入采样率无效" }
			);
			return false;
		}
		if (context.outSampleRate == -1) {
			E_LOG("[mcu::createMpu][{}] request param: outSampleRate is -1", context.jobId);
			status.joinErrNum.fetch_add(1);
			uniqueLock lck(eventLocker);
			eventList.emplace_back(EventInfo{
				"join",
				context.jobId,
				context.chnlId,
				seeker::time::toString(seeker::time::currentTime()),
				"fail",
				"输出采样率无效" }
			);
			return false;
		}

		if (context.codecType != 1 && context.codecType != 2) {
			E_LOG("[mcu::createMpu][{}] request param: codecType is invalid val {}", context.jobId, context.codecType);
			status.joinErrNum.fetch_add(1);
			uniqueLock lck(eventLocker);
			eventList.emplace_back(EventInfo{
				"join",
				context.jobId,
				context.chnlId,
				seeker::time::toString(seeker::time::currentTime()),
				"fail",
				"音频编码格式无效" }
			);
			return false;
		}
		if (it->second->getCodecType() != -1 && it->second->getCodecType() != context.codecType) {
			E_LOG("[mcu::createMpu][{}] current codec type {} != user codec type {}", 
				context.jobId, it->second->getCodecType(), context.codecType);
			status.joinErrNum.fetch_add(1);
			uniqueLock lck(eventLocker);
			eventList.emplace_back(EventInfo{
				"join",
				context.jobId,
				context.chnlId,
				seeker::time::toString(seeker::time::currentTime()),
				"fail",
				"音频编码格式不匹配" }
			);
			return false;
		}
		context.listenIp = mediaIp;
		context.listenPort = audioPort;
		addr.ip = mediaIp;
		addr.port = audioPort;

		//更新mpu
		it->second->reportMediaInfo(std::make_unique<AddChnlEvent>(context));

		status.joinTotalNum.fetch_add(1);
		status.runningChnl.fetch_add(1);
		uniqueLock lck(eventLocker);
		eventList.emplace_back(EventInfo{ 
			"join",
			context.jobId,
			context.chnlId,
			seeker::time::toString(seeker::time::currentTime()),
			"success",
			"" }
		);
		return true;
	}

	bool MediaControlUnit::removeChnl(const RemoveChnlContext& context) {

		//判断jobId是否存在
		mpuForm::iterator it;
		{
			readLock lck(mpuFormLocker);
			it = mpus.find(context.jobId);
			//mpu不存在，业务处理失败
			if (it == mpus.end()) {
				E_LOG("[mcu::removeChnl] find jobId {} failed", context.jobId);
				status.leaveErrNum.fetch_add(1);
				uniqueLock lck(eventLocker);
				eventList.emplace_back(EventInfo{
					"leave",
					context.jobId,
					context.chnlId,
					seeker::time::toString(seeker::time::currentTime()),
					"未找到会议号",
					"" }
				);
				return false;
			}
		}

		//更新mpu
		it->second->reportMediaInfo(std::make_unique<RemoveChnlEvent>(context));
		status.leaveTotalNum.fetch_add(1);
		status.runningChnl.fetch_sub(1);
		uniqueLock lck(eventLocker);
		eventList.emplace_back(EventInfo{
			"leave",
			context.jobId,
			context.chnlId,
			seeker::time::toString(seeker::time::currentTime()),
			"success",
			"" }
		);
		return true;
	}

	bool MediaControlUnit::openMic(const MicCtrlContext& context) {
		//判断jobId是否存在
		mpuForm::iterator it;
		{
			readLock lck(mpuFormLocker);
			it = mpus.find(context.jobId);

		}

		//mpu不存在，业务处理失败
		if (it == mpus.end()) return false;

		//更新mpu
		it->second->reportMediaInfo(std::make_unique<OpenMicEvent>(context));
		return true;
	}

	bool MediaControlUnit::closeMic(const MicCtrlContext& context) {
		//判断jobId是否存在
		mpuForm::iterator it;
		{
			readLock lck(mpuFormLocker);
			it = mpus.find(context.jobId);

		}

		//mpu不存在，业务处理失败
		if (it == mpus.end()) return false;

		//更新mpu
		it->second->reportMediaInfo(std::make_unique<CloseMicEvent>(context));
		return true;
	}

	bool MediaControlUnit::updateDestition(const UpdateContext& context) {
		//判断jobId是否存在
		mpuForm::iterator it;
		{
			readLock lck(mpuFormLocker);
			it = mpus.find(context.jobId);

		}

		//mpu不存在，业务处理失败
		if (it == mpus.end()) return false;

		if (context.codecType != 1 && context.codecType != 2) {
			E_LOG("[mcu::updateDestition][{}] request param: codecType is invalid val {}", context.jobId, context.codecType);
			return false;
		}
		if (it->second->getCodecType() != -1 && it->second->getCodecType() != context.codecType) {
			E_LOG("[mcu::updateDestition][{}] current codec type {} != user codec type {}",
				context.jobId, it->second->getCodecType(), context.codecType);
			return false;
		}

		//更新mpu
		it->second->reportMediaInfo(std::make_unique<UpdateDestEvent>(context));

		return true;
	}

	bool MediaControlUnit::getMpuIdList(mpuIdList& list) {
		//if (mpus.empty()) return NoJobRun;
		if (mpus.empty()) return true;
		for (const auto& each : mpus) {
			list.push_back(each.first);
		}
		return true;
	}

	void MediaControlUnit::getMpuBase(int& jobNum, int& chnlNum) {
		jobNum = status.runningJob.load();
		chnlNum = status.runningChnl.load();
	}

	void MediaControlUnit::getMpuInfo(std::vector<MpuInfo>& info) {
		mpuForm::iterator it;
		{
			readLock lck(mpuFormLocker);
			for (const auto& [key, val] : mpus) {
				info.push_back(val->getInfo());
			}
		}
	}

	void MediaControlUnit::getEventList(std::vector<EventInfo>& infolist) {
		uniqueLock lck(eventLocker);
		infolist.swap(eventList);
	}

	void MediaControlUnit::setCreateErr() {
		status.createErrNum.fetch_add(1);
		status.createTotalNum.fetch_add(1);
	}

	void MediaControlUnit::setJoinErr() {
		status.joinErrNum.fetch_add(1);
		status.joinTotalNum.fetch_add(1);
	}

	void MediaControlUnit::setLeaveErr() {
		status.leaveErrNum.fetch_add(1);
		status.leaveTotalNum.fetch_add(1);
	}

	void MediaControlUnit::setDestoryErr() {
		status.destoryErrNum.fetch_add(1);
		status.destoryTotalNum.fetch_add(1);
	}

	void MediaControlUnit::setEventInfo(const EventInfo& info) {
		uniqueLock lck(eventLocker);
		eventList.emplace_back(std::move(info));
	}
}
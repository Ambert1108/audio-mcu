#include "MediaControlUnit.h"

namespace aom {
	MediaControlUnit* MediaControlUnit::mcu = nullptr;
	std::atomic<uint16_t> MediaControlUnit::refCount_ = 0;

	MediaControlUnit::MediaControlUnit() {
		seeker::rtp::RtpTransceiver::init(8);
	}

	MediaControlUnit::~MediaControlUnit() {
		keepWork.store(false);
		if(mcuCheck) mcuCheck->Cancel();
		closeCondition.notify_all();
		if (AutoCloseThr.joinable()) AutoCloseThr.join();
		RtpTransceiver::shutdown();
		W_LOG("[MCU::close] Media Control Unit close Success");
	}

	int MediaControlUnit::init() {
		std::string deviceId = seeker::IniConfig::Get("main", "device_id", "0");
		AutoCloseThr = std::thread{ &MediaControlUnit::autoClose, this };
		func = std::bind(&MediaControlUnit::endMpu, this, std::placeholders::_1);

		status.hostMemKeepTimePoint = seeker::time::currentTime();
		mcuCheck = InvokeTimer::CreateTimer(std::chrono::seconds(mcucheckInterval), true, [&]() {
			size_t hostMem = seeker::file::getVmRSS();
			if (hostMem > status.maxHostMem) {
				status.hostMemKeepTimePoint = seeker::time::currentTime();
				status.maxHostMem = hostMem;
			}
			double keepTime1 = static_cast<double>(seeker::time::currentTime() - status.hostMemKeepTimePoint)
				/ (1000.0 * 60 * 60);
			I_LOG("[MCU::check] running:{} host mem/max:{}/{}KB, maxKeep={:.3f}h",
				status.runningJob, hostMem, status.maxHostMem, keepTime1);
		});
		mcuCheck->Start();

		audioPortTool = std::make_unique<PortTool>(portPoint, portRange, "audio");
		W_LOG("[MCU::init] Media Control Unit Init Success, deviceId={}, mcu check={}s", deviceId, mcucheckInterval);
		return 0;
	}

	void MediaControlUnit::autoClose() {

		int64_t timePoint = 0;
		mpuCloseForm tmpCloseForm{};

		I_LOG("[MCU::autoClose] autoclose is running, checkTime:{}ms", autocheckInterval);
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
				status.runningChnl.fetch_sub(1);
			}
		}

		I_LOG("[MCU::autoClose] autoclose is closed, handle count:{}", autoCloseNum.load());
	}

	HandleError MediaControlUnit::createMpu(const CreateJobContext& context) {
		mpuForm::iterator it;
		{
			readLock lck(mpuFormLocker);
			it = mpus.find(context.jobId);

		}
		if (it != mpus.end()) return JobidExist;

		//mpu不存在，创建任务
		if (context.codecType == -1) context.codecType = codecType;
		if (context.bitrate == -1) context.bitrate = bitrate;
		if (context.sampleRate == -1) context.sampleRate = samplerate;

		//尝试构造并加入MPU表单，若加入失败代表对应jobId已存在
		{
			writeLock lck(mpuFormLocker);
			auto newMpu = mpus.try_emplace(context.jobId, 
				std::make_unique<MediaProcessUnit>(std::make_unique<MpuContext>(context.jobId, context.codecType,
				context.sampleRate, context.bitrate, context.timeInterval, pt), func));
			if (!newMpu.second) return JoinJobError;
		}
		status.runningJob.fetch_add(1);
		return Success;
	}

	HandleError MediaControlUnit::endMpu(const std::string& jobId) {
		UniqueMPU mpu = nullptr;

		//在MPU表单中查找对应jobId，若不存在返回错误
		{
			writeLock lck(mpuFormLocker);
			auto it = mpus.find(jobId);
			if (it == mpus.end()) {
				W_LOG("[MediaControlUnit::removeMpu][{}] is not found.", jobId);
				return JobidNotFound;
			}

			mpu = std::move(it->second);

			//移除MPU表单中相关信息
			mpus.erase(jobId);
		}

		I_LOG("Debug: send end {} event to mpu", jobId);
		//向对应的MPU发送关闭事件
		mpu->reportMediaInfo(std::make_unique<EndEvent>());

		//将需要销毁的MPU移交给待关闭MPU表单
		{
			writeLock lck(closeMpuFormLocker);
			closeMpus.emplace(std::move(mpu));
		}

		return Success;
	}

	HandleError MediaControlUnit::addChnl(const AddChnlContext& context, ListenAddr& addr) {
		//判断jobId是否存在
		mpuForm::iterator it;
		{
			readLock lck(mpuFormLocker);
			it = mpus.find(context.jobId);

		}

		//mpu不存在，业务处理失败
		if (it == mpus.end()) return JobidNotFound;

		//申请音频端口
		port_t audioPort = audioPortTool->applyPort();
		if (audioPort == APPLY_UDP_PORT_ERROR) {
			audioPortTool->freePort(audioPort);
			return ApplyPortError;
		}
		context.listenIp = mediaIp;
		context.listenPort = audioPort;
		addr.ip = mediaIp;
		addr.port = audioPort;

		//更新mpu
		it->second->reportMediaInfo(std::make_unique<AddChnlEvent>(context));

		return Success;
	}

	HandleError MediaControlUnit::removeChnl(const RemoveChnlContext& context) {

		//判断jobId是否存在
		mpuForm::iterator it;
		{
			readLock lck(mpuFormLocker);
			it = mpus.find(context.jobId);

		}

		//mpu不存在，业务处理失败
		if (it == mpus.end()) return JobidNotFound;

		//更新mpu
		it->second->reportMediaInfo(std::make_unique<RemoveChnlEvent>(context));
		return Success;
	}

	HandleError MediaControlUnit::openMic(const MicCtrlContext& context) {
		//判断jobId是否存在
		mpuForm::iterator it;
		{
			readLock lck(mpuFormLocker);
			it = mpus.find(context.jobId);

		}

		//mpu不存在，业务处理失败
		if (it == mpus.end()) return JobidNotFound;

		//更新mpu
		it->second->reportMediaInfo(std::make_unique<OpenMicEvent>(context));
		return Success;
	}

	HandleError MediaControlUnit::closeMic(const MicCtrlContext& context) {
		//判断jobId是否存在
		mpuForm::iterator it;
		{
			readLock lck(mpuFormLocker);
			it = mpus.find(context.jobId);

		}

		//mpu不存在，业务处理失败
		if (it == mpus.end()) return JobidNotFound;

		//更新mpu
		it->second->reportMediaInfo(std::make_unique<CloseMicEvent>(context));
		return Success;
	}

	HandleError MediaControlUnit::getMpuIdList(mpuIdList& list) {
		//if (mpus.empty()) return NoJobRun;
		if (mpus.empty()) return Success;
		for (const auto& each : mpus) {
			list.push_back(each.first);
		}
		return Success;
	}
}
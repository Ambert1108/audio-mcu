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
		W_LOG("[mcu::close] Media Control Unit close Success");
	}

	int MediaControlUnit::init() {
		AutoCloseThr = std::thread{ &MediaControlUnit::autoClose, this };
		endJobFunc = std::bind(&MediaControlUnit::endMpu, this, std::placeholders::_1);
		freePortFunc = std::bind(&MediaControlUnit::freePort, this, std::placeholders::_1);

		status.hostMemKeepTimePoint = seeker::time::currentTime();
		mcuCheck = InvokeTimer::CreateTimer(std::chrono::seconds(mcucheckInterval), true, [&]() {
			size_t hostMem = seeker::file::getVmRSS();
			if (hostMem > status.maxHostMem) {
				status.hostMemKeepTimePoint = seeker::time::currentTime();
				status.maxHostMem = hostMem;
			}
			double keepTime1 = static_cast<double>(seeker::time::currentTime() - status.hostMemKeepTimePoint)
				/ (1000.0 * 60 * 60);
			I_LOG("[MCU::check] running[{}] host mem/max[{}/{}KB] maxKeep[{:.3f}h]",
				status.runningJob, hostMem, status.maxHostMem, keepTime1);
		});
		mcuCheck->Start();

		audioPortTool = std::make_unique<PortTool>(portPoint, portRange, "audio");
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
				status.runningChnl.fetch_sub(1);
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
		if (it != mpus.end()) return false;
		//mpu不存在，创建任务


		//尝试构造并加入MPU表单，若加入失败代表对应jobId已存在
		{
			writeLock lck(mpuFormLocker);
			auto newMpu = mpus.try_emplace(context.jobId, 
				std::make_unique<MediaProcessUnit>(std::make_unique<MpuContext>(context.jobId, 
					context.url), endJobFunc, freePortFunc));
			if (!newMpu.second) return false;
		}
		status.runningJob.fetch_add(1);
		return true;
	}

	bool MediaControlUnit::endMpu(const std::string& jobId) {
		UniqueMPU mpu = nullptr;

		//在MPU表单中查找对应jobId，若不存在返回错误
		{
			writeLock lck(mpuFormLocker);
			auto it = mpus.find(jobId);
			if (it == mpus.end()) {
				W_LOG("[mcu::removeMpu][{}] is not found.", jobId);
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
		if (it == mpus.end()) return false;

		//申请音频端口
		port_t audioPort = audioPortTool->applyPort();
		if (audioPort == -1) {
			audioPortTool->freePort(audioPort);
			return false;
		}

		if (context.inSampleRate == -1) {
			E_LOG("[mcu::createMpu][{}] request param: inSampleRate is -1", context.jobId);
			return false;
		}
		if (context.outSampleRate == -1) {
			E_LOG("[mcu::createMpu][{}] request param: outSampleRate is -1", context.jobId);
			return false;
		}

		if (context.codecType != 1 && context.codecType != 2) {
			E_LOG("[mcu::createMpu][{}] request param: codecType is invalid val {}", context.jobId, context.codecType);
			return false;
		}
		if (it->second->getCodecType() != -1 && it->second->getCodecType() != context.codecType) {
			E_LOG("[mcu::createMpu][{}] current codec type {} != user codec type {}", ctx->jobId, ctx->codecType, codecType);
			return false;
		}
		context.listenIp = mediaIp;
		context.listenPort = audioPort;
		addr.ip = mediaIp;
		addr.port = audioPort;

		//更新mpu
		it->second->reportMediaInfo(std::make_unique<AddChnlEvent>(context));

		return true;
	}

	bool MediaControlUnit::removeChnl(const RemoveChnlContext& context) {

		//判断jobId是否存在
		mpuForm::iterator it;
		{
			readLock lck(mpuFormLocker);
			it = mpus.find(context.jobId);
			//mpu不存在，业务处理失败
			if (it == mpus.end()) return false;
		}

		//更新mpu
		it->second->reportMediaInfo(std::make_unique<RemoveChnlEvent>(context));
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
}
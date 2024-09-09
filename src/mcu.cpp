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
		func = std::bind(&MediaControlUnit::removeMpu, this, std::placeholders::_1);

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
		
		//申请音频端口端口
		port_t audioPort = audioPortTool->applyPort();
		if (audioPort == APPLY_UDP_PORT_ERROR) {
			audioPortTool->freePort(audioPort);
			return ApplyPortError;
		}

		//尝试构造并加入MPU表单，若加入失败代表对应jobId已存在
		Iterator newMpu;
		{
			writeLock lck(mpuFormLocker);
			newMpu = mpus.try_emplace(context.jobId, std::make_unique<MediaProcessUnit>(context.jobId, func));
		}
		if (!newMpu.second) return JoinJobError;

		//更新mcu的runningJob
		status.runningJob.fetch_add(1);

		return Success;
	}

	HandleError MediaControlUnit::updateMpu(const UpdateJobContext& context) {
		//判断jobId是否存在
		mpuForm::iterator it;
		{
			readLock lck(mpuFormLocker);
			it = mpus.find(context.jobId);

		}

		//mpu不存在，创建失败
		if (it == mpus.end()) return JobidNotFound;

		//更新mpu
		it->second->reportMediaInfo(std::make_unique<UpdateEvent>(context));

		return Success;
	}

	HandleError MediaControlUnit::removeMpu(const std::string& jobId) {
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

		//向对应的MPU发送关闭事件
		mpu->reportMediaInfo(std::make_unique<CloseEvent>());

		//将需要销毁的MPU移交给待关闭MPU表单
		{
			writeLock lck(closeMpuFormLocker);
			closeMpus.emplace(std::move(mpu));
		}

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
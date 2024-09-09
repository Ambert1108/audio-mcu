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
		: ctx(std::move(ptr)), autoCloseCallback(callback), APCs(10) {
		startTime = seeker::time::currentTime();
		status << TaskStatusType::run;
		workTh = std::thread{ &MediaProcessUnit::workingLoop, this };
		eventTh = std::thread{ &MediaProcessUnit::eventHandle, this };
		data.jobId = ctx->jobId;
		I_LOG("[MPU::create] jobId={}, codecType={}, outSampleRate={}, bitrate={}, timeInterval={}", 
			ctx->jobId, ctx->codecType, ctx->outSampleRate, ctx->bitrate, ctx->interval);
		data.creatingDuration = seeker::time::currentTime() - startTime;
	}

	MediaProcessUnit::~MediaProcessUnit() {
		//判断关闭线程是否可执行并执行完毕
		if (stopTh.joinable()) stopTh.join();
		
		//MPU转为exce态或未收到RTP包时事件处理线程将结束，导致无法接收外部关闭事件，需要自己主动关闭
		stop();

		I_LOG("[MPU::destory] jobId={} success", ctx->jobId);
	}

	void MediaProcessUnit::reportMediaInfo(std::unique_ptr<Event> info) {
		if (!status) return;
		if (!info) {
			E_LOG("[MPU::Error][{}] evnetQue get a nullptr event!", ctx->jobId);
		}
		{
			writeLock lck(eventQueLocker);
			eventQue.push_back(std::move(info));
		}
		eventCondition.notify_one();
	}

	TaskStatusType MediaProcessUnit::getStatus() const { return status.getStatus(); }

	const MediaProcessData& MediaProcessUnit::getData() const { return data; }

	void MediaProcessUnit::stop() {
		//若MPU已经关闭，则不再重复操作
		if (status.getStatus() == TaskStatusType::end) return;

		int64_t stopTimePoint = seeker::time::currentTime();

		//将MPU状态置为释放态
		status << TaskStatusType::down;

		//阻塞的关闭所有线程
		if (workTh.joinable()) workTh.join();
		if (eventTh.joinable()) eventTh.join();

		data.destroyingDuration = seeker::time::currentTime() - stopTimePoint;
		data.runningDuration = seeker::time::currentTime() - startTime;
		output();

		//将MPU状态置为结束态，此时MPU可销毁
		status << TaskStatusType::end;
	}

	void MediaProcessUnit::output() {
		W_LOG("\n--------- mpu output ---------\njobId={}, closeMethod={}\nMixoutNum={}\n"
			"Duration=[{}ms/{}ms/{}](create/destory/run)\nupdateCount={}\n"
			"--------- mpu output ---------",
			data.jobId, data.closeMethod, data.timeOutNum,
			data.creatingDuration, data.destroyingDuration, parseTime(data.runningDuration), data.updateCount);
	}

	void MediaProcessUnit::workingLoop() {
		/* MPU监控定时器 */
		InvokeTimerPtr printTimer = nullptr;
		//每mpucheckInterval秒计算MPU相关参数
		printTimer = InvokeTimer::CreateTimer(std::chrono::seconds(mpucheckInterval), true, [&] {
				
			});
		printTimer->Start();

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
						I_LOG("[mpu::eventHandle->{}] handle stop event", ctx->jobId);
						stopTh = std::thread{ &MediaProcessUnit::stop, this };
						break;

					case JobHandleType::update:
						I_LOG("[mpu::eventHandle->{}] handle update event", ctx->jobId);
						each->handle(this);
						data.updateCount++;						
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
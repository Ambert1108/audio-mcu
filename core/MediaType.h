// @brief: 媒体处理消息封装
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.5.8]
// @version: V0.0.1
// @revision: [Ambert@2024.5.9]

#pragma once

#include "SignalMessage.hpp"

namespace aom {

	/* Udp 地址封装 <Ambert 8-May-2024> */
	struct UdpAddress {
		UdpAddress(const std::string& Ip, port_t Port) : ip(Ip), port(Port) {};
		UdpAddress(const UdpAddress& addr) noexcept : ip(addr.ip), port(addr.port) {};
		std::string ip;
		port_t port;
	};

	enum class JobHandleType : uint16_t {
		add,
		remove,
		open,
		close,
		stop
	};

	class MediaProcessUnit;
	class Event {
	public:
		const uint32_t id;
		const JobHandleType type;
		const int64_t time;

		virtual ~Event() {};

		virtual void handle(void* ptr = nullptr)
		{ W_LOG("Base Event do nothing"); };

	protected:
		Event(JobHandleType t) : id(idPoint.fetch_add(1)), type(t), time(seeker::time::currentTime()) {};

	private:
		inline static std::atomic<uint32_t> idPoint = 10000;
	};

	struct HandleError {
		int errCode;
		std::string errMsg;
		HandleError(int code, const std::string& msg) : errCode(code), errMsg(msg) {};
		HandleError() : errCode(0), errMsg("Success") {};

		inline bool operator==(const HandleError& error) const {
			if (this->errCode != error.errCode) return false;
			return true;
		}

		inline bool operator!=(const HandleError& error) const {
			if (*this == error) return false;
			return true;
		}
	};

	inline static HandleError Success{ };
	inline static HandleError ApplyPortError{ APPLY_UDP_PORT_ERROR, APPLY_PORT_MSG };
	inline static HandleError JobidExist{ JOBID_EXIST_ERROR, JOBID_EXIST_MSG };
	inline static HandleError JoinJobError{ JOIN_JOB_ERROR, JOIN_JOB_MSG };
	inline static HandleError JobidNotFound{ JOBID_NOTFOUND_ERROR, JOBID_NOTFOUND_MSG };
	inline static HandleError ParamError{ KEY_PARAM_ERROR, KEY_PARAM_MSG };
	inline static HandleError NoJobRun{ CHECK_JOB_ERROR, NO_JOB_MSG };
	inline static HandleError UnknownError{ UNKNOWN_ERROR, GET_UNKNOWN_MSG };
}
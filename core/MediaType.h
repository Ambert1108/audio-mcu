// @brief: 媒体处理消息封装
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.5.8]
// @version: V0.0.1
// @revision: [Ambert@2024.5.9]

#pragma once

#include "SipProcesser.hpp"

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
}
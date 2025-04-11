#pragma once
#include "SaveQueue.h"

#include <any>
#include <unordered_map>

namespace aom {
	struct Message {
		int id;
		std::any data;
	};

  class hi {
  public:
    static void PostMsg(const Message& message) {
      messageQueue.Push(message);
    }

    static bool GetMsg(Message& message) {
      return messageQueue.TryPop(message);
    }

    static bool GetMsgBlock(Message& message) {
      return messageQueue.WaitPop(message);
    }

  private:
    // 静态线程安全的队列实例
    static base::ThreadSafeQueue<Message> messageQueue;
  };

	enum class MessageType : int {
		CREATE_MEETING = 0,
		ADD_CHANNEL,
	};

	static constexpr int msgTo(MessageType msg) { return static_cast<int>(msg); }

	static std::string enumToString(MessageType e) {
		static const std::unordered_map<MessageType, std::string> enumMap = {
				{MessageType::CREATE_MEETING, "CREATE_MEETING"},
				{MessageType::ADD_CHANNEL, "ADD_CHANNEL"}
		};
		auto it = enumMap.find(e);
		if (it != enumMap.end()) {
			return it->second;
		}
		return "Unknown";
	}
}
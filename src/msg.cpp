#include "Message.h"
namespace aom {
  // 在类外初始化静态成员变量
  base::ThreadSafeQueue<Message> hi::messageQueue;
}
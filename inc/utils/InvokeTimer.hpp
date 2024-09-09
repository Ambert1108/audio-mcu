/**
@author Ambert sk13x
@since 2023/7/12
@version 0.0.1-SNAPSHOT 2023/7/12

@note Please indicate the reference source for the content of the cited document
@refer https://cloud.tencent.com/developer/article/1979227
*/

#pragma once
#include "asio.hpp"

#include <functional>
#include <memory>
#include <thread>

namespace aom {
  class InvokeTimer;

  typedef std::shared_ptr<InvokeTimer> InvokeTimerPtr;
  typedef std::weak_ptr<InvokeTimer> InvokeTimerWPtr;

  class InvokeTimer : std::enable_shared_from_this<InvokeTimer> {
  public:
    virtual ~InvokeTimer() {}

    typedef std::function<void()> Function;

    static InvokeTimerPtr CreateTimer(const asio::steady_timer::duration& duration, bool period,
      Function& f) {
      InvokeTimerPtr it(new InvokeTimer(duration, f, period));
      it->self_ = it;
      return it;
    }

    static InvokeTimerPtr CreateTimer(const asio::steady_timer::duration& duration, bool period,
      Function&& f) {
      InvokeTimerPtr it(new InvokeTimer(duration, std::move(f), period));
      it->self_ = it;
      return it;
    }

    static InvokeTimerPtr CreateTimer(const asio::steady_timer::duration& duration,
      std::size_t count, Function& f) {
      InvokeTimerPtr it(new InvokeTimer(duration, f, count));
      it->self_ = it;
      return it;
    }

    static InvokeTimerPtr CreateTimer(const asio::steady_timer::duration& duration,
      std::size_t count, Function&& f) {
      InvokeTimerPtr it(new InvokeTimer(duration, std::move(f), count));
      it->self_ = it;
      return it;
    }

    void Start() {
      start_ = std::chrono::system_clock::now();
      timer_.async_wait(std::bind(&InvokeTimer::OnTrigger, this, std::placeholders::_1));
      std::thread thr(
        [=] {
          try {
            ioCtx_.run();
            W_LOG("Warning: Invoke Timer ioContext run() is exited...");
          }
          catch (std::exception& ex) {
            E_LOG("Error: Invoke Timer ioContext run() Get exception:{} in {}", ex.what(), __LINE__);
            throw;
          }
        }
      );
      timeThread = std::move(thr);
    }

    void Cancel() {
      periodic_ = false;
      timer_.cancel();
      if (timeThread.joinable()) timeThread.join();
    }

    inline std::size_t getTickCount() { return trigger_count_; }

    inline int64_t getElapsedMillionsecond() const {
      auto end = std::chrono::system_clock::now();
      return std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();
    }

    inline int64_t getElapsedSecond() const {
      auto end = std::chrono::system_clock::now();
      return std::chrono::duration_cast<std::chrono::seconds>(end - start_).count();
    }

    inline bool isTriggered() const { return isTriggered_; }

  private:
    explicit InvokeTimer(asio::io_context& io, const asio::steady_timer::duration& duration, Function& f, bool period)
      : 
      ioCtx_(),
      timer_(ioCtx_, duration),
      duration_(duration),
      periodic_(period),
      callback_(f),
      count_(0),
      trigger_count_(0),
      isTriggered_(false) {}

    explicit InvokeTimer(const asio::steady_timer::duration& duration, Function&& f, bool period)
      : 
      ioCtx_(),
      timer_(ioCtx_, duration),
      duration_(duration),
      periodic_(period),
      callback_(std::move(f)),
      count_(0),
      trigger_count_(0),
      isTriggered_(false) {}

    explicit InvokeTimer(const asio::steady_timer::duration& duration, Function& f,
      std::size_t count)
      : 
      ioCtx_(),
      timer_(ioCtx_, duration),
      duration_(duration),
      periodic_(true),
      callback_(f),
      count_(count),
      trigger_count_(0),
      isTriggered_(false) {}

    explicit InvokeTimer(const asio::steady_timer::duration& duration, Function&& f,
      std::size_t count)
      : 
      ioCtx_(),
      timer_(ioCtx_, duration),
      duration_(duration),
      periodic_(true),
      callback_(std::move(f)),
      count_(count),
      trigger_count_(0),
      isTriggered_(false) {}

    void OnWait() {
      
    }

    void OnTrigger(const asio::error_code& e) {
      if (e.value() != 0) {
        self_.reset();
        return;
      }

      isTriggered_ = true;
      trigger_count_++;

      try {
        if (callback_)
          callback_();
      }
      catch (const std::error_code& errorCode) {
        E_LOG("[FATAL] errorCode: {}", errorCode.message());
      }
      T_LOG("periodic_:{}", periodic_);
      if (periodic_) {
        if (count_ == 0 || trigger_count_ < count_) {
          timer_.expires_from_now(duration_);
          timer_.async_wait(std::bind(&InvokeTimer::OnTrigger, this, std::placeholders::_1));
        }
        else if (trigger_count_ < count_) {
          timer_.expires_from_now(duration_);
          timer_.async_wait(std::bind(&InvokeTimer::OnTrigger, this, std::placeholders::_1));
        }
        else {
          return;
        }
      }
      else {
        return;
      }
    }

  private:
    asio::io_context ioCtx_;
    asio::steady_timer timer_;
    asio::steady_timer::duration duration_;
    bool periodic_;
    InvokeTimerWPtr self_;
    Function callback_;
    std::size_t count_;
    std::size_t trigger_count_;
    std::chrono::system_clock::time_point start_;
    bool isTriggered_;
    std::thread timeThread;
  };

}  // namespace
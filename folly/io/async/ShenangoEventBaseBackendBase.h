#include "sh_event.h"

#include <folly/io/async/EventBaseBackendBase.h>

namespace folly {

class EventBase;

class ShenangoEventBaseEvent {
 public:
  ShenangoEventBaseEvent() = default;
  ~ShenangoEventBaseEvent() {
    if (userData_ && freeFn_) {
      freeFn_(userData_);
    }
  }

  ShenangoEventBaseEvent(const ShenangoEventBaseEvent&) = delete;
  ShenangoEventBaseEvent& operator=(const ShenangoEventBaseEvent&) =  delete;

  typedef void (*FreeFunction)(void* userData);

  rt::Event* getEvent() const { return event_; }
  bool isEventRegistered() const { return event_->IsEventRegistered(); }

  void* getUserData() { return userData_; }
  void setUserData(void* userData) { userData_ = userData; }
  void setUserData(void* userData, FreeFunction freeFn) {
    userData_ = userData;
    freeFn_ = freeFn;
  }

  void eb_event_set(
      rt::UdpConn* sock,
      short events,
      void (*callback)(void*),
      void* arg) {
    event_ = rt::Event::CreateEvent(sock, events, callback, arg);
  }

  rt::UdpConn* eb_ev_fd() const { return event_->GetSocket(); }
  void eb_ev_base(EventBase* evb);
  EventBase* eb_ev_base() const { return evb_; }
  void eb_event_base_set(EventBase* evb);
  void eb_event_add(const struct timeval* timeout);
  void eb_event_del();

 protected:
  rt::Event* event_{nullptr};
  EventBase* evb_{nullptr};
  void* userData_{nullptr};
  FreeFunction freeFn_{nullptr};
};

class ShenangoEventBaseBackendBase {
 public:
  using Event = ShenangoEventBaseEvent;
  using FactoryFunc =
      std::function<std::unique_ptr<folly::EventBaseBackendBase>()>;

  ShenangoEventBaseBackendBase() = default;
  virtual ~ShenangoEventBaseBackendBase() = default;

  ShenangoEventBaseBackendBase(const ShenangoEventBaseBackendBase&) = delete;
  ShenangoEventBaseBackendBase& operator=(const ShenangoEventBaseBackendBase&) = delete;

  virtual rt::EventLoop* getEventBase() = 0;
  virtual void eb_event_base_loop(int flags) = 0;
  // virtual int eb_event_base_loopbreak() = 0;

  virtual void eb_event_add(Event& event, const struct timeval* timeout) = 0;
  virtual void eb_event_del(Event& event) = 0;

  // virtual bool eb_event_active(Event& event, int res) = 0;
};

} // namespace folly
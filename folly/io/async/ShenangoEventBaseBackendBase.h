#pragma once

#include "sh_event.h"

#include <folly/io/async/EventBaseBackendBase.h>

namespace folly {

class EventBase;

class ShenangoEventReadCallback {
 public:
  struct IoVec {
    virtual ~IoVec() = default;

    using FreeFunc = void (*)(IoVec*);
    using CallbackFunc = void (*)(IoVec*, int);
    void* arg_{nullptr};
    struct iovec data_{};
    FreeFunc freeFunc_{nullptr};
    CallbackFunc cbFunc_{nullptr};
  };

  ShenangoEventReadCallback() = default;

  virtual ~ShenangoEventReadCallback() = default;

  virtual IoVec* allocateData() = 0;
};

class ShenangoEventRecvmsgCallback {
 public:
  struct MsgHdr {
    virtual ~MsgHdr() = default;

    using FreeFunc = void (*)(MsgHdr*);
    using CallbackFunc = void (*)(MsgHdr*, int);
    void* arg_{nullptr};
    struct msghdr data_{};
    FreeFunc freeFunc_{nullptr};
    CallbackFunc cbFunc_{nullptr};
  };

  ShenangoEventRecvmsgCallback() = default;

  virtual ~ShenangoEventRecvmsgCallback() = default;

  virtual MsgHdr* allocateData() = 0;
};

struct ShenangoEventCallback {
  enum class Type {
    TYPE_NONE = 0, TYPE_READ = 1, TYPE_RECVMSG = 2
  };
  Type type_{Type::TYPE_NONE};
  union {
    ShenangoEventReadCallback* readCb_;
    ShenangoEventRecvmsgCallback* recvmsgCb_;
  };

  void set(ShenangoEventReadCallback* cb) {
    type_ = Type::TYPE_READ;
    readCb_ = cb;
  }

  void set(ShenangoEventRecvmsgCallback* cb) {
    type_ = Type::TYPE_RECVMSG;
    recvmsgCb_ = cb;
  }

  void reset() { type_ = Type::TYPE_NONE; }
};

class ShenangoEventBaseEvent {
 public:
  ShenangoEventBaseEvent() = default;

  ~ShenangoEventBaseEvent() {
    if (userData_ && freeFn_) {
      freeFn_(userData_);
    }
  }

  ShenangoEventBaseEvent(const ShenangoEventBaseEvent&) = delete;

  ShenangoEventBaseEvent& operator=(const ShenangoEventBaseEvent&) = delete;

  typedef void (* FreeFunction)(void* userData);

  [[nodiscard]] rt::Event* getEvent() const { return event_; }

  [[nodiscard]] bool
  isEventRegistered() const { return event_->IsEventRegistered(); }

  void* getUserData() { return userData_; }

  void setUserData(void* userData) { userData_ = userData; }

  void setUserData(void* userData, FreeFunction freeFn) {
    userData_ = userData;
    freeFn_ = freeFn;
  }

  void eb_event_set(
      rt::UdpConn* sock,
      short events,
      void (* callback)(void*),
      void* arg) {
    event_ = rt::Event::CreateEvent(sock, events, callback, arg);
  }

  [[nodiscard]] rt::UdpConn* eb_ev_fd() const { return event_->GetSocket(); }

  void eb_ev_base(EventBase* evb);

  [[nodiscard]] EventBase* eb_ev_base() const { return evb_; }

  void eb_event_base_set(EventBase* evb);

  void eb_event_add(const struct timeval* timeout);

  void eb_event_del();

  void setCallback(ShenangoEventReadCallback* cb) { cb_.set(cb); }

  void setCallback(ShenangoEventRecvmsgCallback* cb) { cb_.set(cb); }

  void resetCallback() { cb_.reset(); }

  [[nodiscard]] const ShenangoEventCallback& getCallback() const { return cb_; }

 protected:
  rt::Event* event_{nullptr};
  EventBase* evb_{nullptr};
  void* userData_{nullptr};
  FreeFunction freeFn_{nullptr};
  ShenangoEventCallback cb_;
};

class ShenangoEventBaseBackendBase {
 public:
  using Event = ShenangoEventBaseEvent;
  using FactoryFunc =
  std::function<std::unique_ptr<folly::EventBaseBackendBase>()>;

  bool hasSocket = false;

  ShenangoEventBaseBackendBase() = default;

  virtual ~ShenangoEventBaseBackendBase() = default;

  ShenangoEventBaseBackendBase(const ShenangoEventBaseBackendBase&) = delete;

  ShenangoEventBaseBackendBase&
  operator=(const ShenangoEventBaseBackendBase&) = delete;

  virtual rt::EventLoop* getEventBase() = 0;

  virtual void eb_event_base_loop(int flags) = 0;

  virtual int eb_event_base_loop_w_return(int flags) = 0;

  virtual void eb_event_add(Event& event, const struct timeval* timeout) = 0;

  virtual void eb_event_del(Event& event) = 0;
};

} // namespace folly
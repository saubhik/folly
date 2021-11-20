#pragma once

extern "C" {
#include "runtime/poll.h"
}

#include "net.h"

#include <cassert>

#include <folly/io/async/ShenangoEventBaseBackendBase.h>
#include <folly/io/async/EventBase.h>
#include <folly/net/ShNetworkSocket.h>

namespace folly {

class EventBase;

class ShenangoEventHandler {
 public:
  enum ShenangoEventFlags {
    NONE = 0,
    TIMEOUT = SEV_TIMEOUT,
    READ = SEV_READ,
    WRITE = SEV_WRITE,
    SIGNAL = SEV_SIGNAL,
    PERSIST = SEV_PERSIST,
    ET = SEV_ET,
  };

  explicit ShenangoEventHandler(EventBase* eventBase = nullptr,
                                ShNetworkSocket fd = ShNetworkSocket());

  ShenangoEventHandler(const ShenangoEventHandler&) = delete;
  ShenangoEventHandler& operator=(const ShenangoEventHandler&) = delete;

  virtual ~ShenangoEventHandler();

  virtual void handlerReady() noexcept = 0;

  bool registerHandler(uint16_t events) { return registerImpl(events); }

  void unregisterHandler();

  bool isHandlerRegistered() const { return event_.isEventRegistered(); };

  virtual void attachEventBase(EventBase* eventBase);

  virtual void detachEventBase();

  void changeHandlerFD(ShNetworkSocket fd);

  void initHandler(EventBase* eventBase, ShNetworkSocket fd);

  void setEventCallback(ShenangoEventReadCallback* cb) {
    event_.setCallback(cb);
  }

  void setEventCallback(ShenangoEventRecvmsgCallback* cb) {
    event_.setCallback(cb);
  }

  void resetEventCallback() { event_.resetCallback(); }

 private:
  bool registerImpl(uint16_t events);

  void ensureNotRegistered(const char* fn) const;

  void setEventBase(EventBase* eventBase);

  static void shenangoEventCallback(void* arg);

  ShenangoEventBaseBackendBase::Event event_;
  EventBase* eventBase_;
};
} // namespace folly

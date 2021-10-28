extern "C" {
#include "runtime/poll.h"
}

#include "net.h"

#include <cassert>

#include <folly/io/async/ShenangoEventBaseBackendBase.h>

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

  explicit ShenangoEventHandler(EventBase* eventBase = nullptr, rt::UdpConn* sock = nullptr);

  ShenangoEventHandler(const ShenangoEventHandler&) = delete;
  ShenangoEventHandler& operator=(const ShenangoEventHandler&) = delete;

  virtual ~ShenangoEventHandler();

  virtual void handlerReady(uint16_t events) noexcept = 0;
  bool registerHandler(uint16_t events) { return registerImpl(events); }
  void unregisterHandler();
  bool isHandlerRegistered() const { return event_.isEventRegistered(); };
  void attachEventBase(EventBase* eventBase);
  void detachEventBase();
  void initHandler(EventBase* eventBase, rt::UdpConn* sock);
  uint16_t getRegisteredEvents() const;
  bool isPending() const;

 private:
  bool registerImpl(uint16_t events);
  void ensureNotRegistered(const char* fn);
  void setEventBase(EventBase* eventBase);
  static sh_event_callback_fn callbackFn;

  ShenangoEventBaseBackendBase::Event event_;
  EventBase* eventBase_;
};

ShenangoEventHandler::ShenangoEventHandler(EventBase* eventBase, rt::UdpConn* sock) {
  // Currently, event flags are not used inside shenango.
  event_.eb_event_set(sock, 0, &callbackFn, this);
  if (eventBase != nullptr) {
    setEventBase(eventBase);
  } else {
    event_.eb_ev_base(nullptr);
    eventBase_ = nullptr;
  }
}

ShenangoEventHandler::~ShenangoEventHandler() { unregisterHandler(); }

bool ShenangoEventHandler::registerImpl(uint16_t events) {
  assert(event_.eb_ev_base() != nullptr);

  // No need to update flags using events, as shenango does not use flags for now.
}

void ShenangoEventHandler::unregisterHandler() {
  if (isHandlerRegistered()) {
    event_.eb_event_del();
  }
}

void ShenangoEventHandler::setEventBase(EventBase* eventBase) {
  event_.eb_event_base_set(eventBase);
  eventBase_ = eventBase;
}

} // namespace folly
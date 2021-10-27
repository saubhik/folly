extern "C" {
#include "runtime/poll.h"
}

#include "net.h"

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
  bool registerHandler(uint16_t events) { return registerImpl(events, false); }
  void unregisterHandler();
  bool isHandlerRegistered() const { return event_.isEventRegistered(); };
  void attachEventBase(EventBase* eventBase);
  void detachEventBase();
  void initHandler(EventBase* eventBase, rt::UdpConn* sock);
  uint16_t getRegisteredEvents() const;
  bool registerInternalHandler(uint16_t events) { return registerImpl(events, true); }
  bool isPending() const;

 private:
  bool registerImpl(uint16_t events, bool internal);
  void ensureNotRegistered(const char* fn);
  void setEventBase(EventBase* eventBase);
  static sh_event_callback_fn callbackFn;

  ShenangoEventBaseBackendBase::Event event_;
  EventBase* eventBase_;
};

ShenangoEventHandler::ShenangoEventHandler(EventBase* eventBase, rt::UdpConn* sock) {
  event_.eb_event_set(sock, 0, &callbackFn, this);
  if (eventBase != nullptr) {
    setEventBase(eventBase);
  } else {
    event_.eb_ev_base(nullptr);
    eventBase_ = nullptr;
  }
}

ShenangoEventHandler::~ShenangoEventHandler() { unregisterHandler(); }

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
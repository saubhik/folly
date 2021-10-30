extern "C" {
#include "runtime/poll.h"
}

#include "net.h"

#include <cassert>

#include <folly/io/async/ShenangoEventBaseBackendBase.h>
#include <folly/io/async/EventBase.h>

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

  explicit ShenangoEventHandler(EventBase *eventBase = nullptr,
                                rt::UdpConn *sock = nullptr);

  ShenangoEventHandler(const ShenangoEventHandler &) = delete;

  ShenangoEventHandler &operator=(const ShenangoEventHandler &) = delete;

  virtual ~ShenangoEventHandler();

  virtual void handlerReady() noexcept = 0;

  bool registerHandler(uint16_t events) { return registerImpl(events); }

  void unregisterHandler();

  bool isHandlerRegistered() const { return event_.isEventRegistered(); };

  void attachEventBase(EventBase *eventBase);

  void detachEventBase();

  void initHandler(EventBase *eventBase, rt::UdpConn *sock);

 private:
  bool registerImpl(uint16_t events);

  void ensureNotRegistered(const char *fn);

  void setEventBase(EventBase *eventBase);

  static void shenangoEventCallback(void *arg);

  ShenangoEventBaseBackendBase::Event event_;
  EventBase *eventBase_;
};

ShenangoEventHandler::ShenangoEventHandler(EventBase *eventBase,
                                           rt::UdpConn *sock) {
  // Currently, event flags are not used inside shenango.
  event_.eb_event_set(sock, 0, &shenangoEventCallback, this);
  if (eventBase != nullptr) {
    setEventBase(eventBase);
  } else {
    event_.eb_ev_base(nullptr);
    eventBase_ = nullptr;
  }
}

ShenangoEventHandler::~ShenangoEventHandler() { unregisterHandler(); }

bool ShenangoEventHandler::registerImpl(uint16_t events) {
  // No need to update flags using events,
  // as shenango does not use flags for now.
  assert(event_.eb_ev_base() != nullptr);

  // Add the event.
  //
  // Although libevent allows events to wait on both I/O and a timeout,
  // we intentionally don't allow an EventHandler to also use a timeout.
  // Callers must maintain a separate AsyncTimeout object if they want a
  // timeout.
  //
  // Otherwise, it is difficult to handle persistent events properly.  (The I/O
  // event and timeout may both fire together the same time around the event
  // loop.  Normally we would want to inform the caller of the I/O event first,
  // then the timeout.  However, it is difficult to do this properly since the
  // I/O callback could delete the EventHandler.)  Additionally, if a caller
  // uses the same struct event for both I/O and timeout, and they just want to
  // reschedule the timeout, libevent currently makes an epoll_ctl() call even
  // if the I/O event flags haven't changed.  Using a separate event struct is
  // therefore slightly more efficient in this case (although it does take up
  // more space).
  event_.eb_event_add(nullptr);

  return true;
}

void ShenangoEventHandler::unregisterHandler() {
  if (isHandlerRegistered()) {
    event_.eb_event_del();
  }
}

void ShenangoEventHandler::attachEventBase(EventBase *eventBase) {
  // attachEventBase() may only be called on detached handlers
  assert(event_.eb_ev_base() == nullptr);
  assert(!isHandlerRegistered());
  // This must be invoked from the EventBase's thread
  eventBase->dcheckIsInEventBaseThread();

  setEventBase(eventBase);
}

void ShenangoEventHandler::detachEventBase() {
  ensureNotRegistered(__func__);
  event_.eb_ev_base(nullptr);
}

void
ShenangoEventHandler::initHandler(EventBase *eventBase, rt::UdpConn *sock) {
  ensureNotRegistered(__func__);
  event_.eb_event_set(sock, 0, &shenangoEventCallback, this);
  setEventBase(eventBase);
}

void ShenangoEventHandler::ensureNotRegistered(const char *fn) {
  // Neither the EventBase nor file descriptor may be changed while the
  // handler is registered.  Treat it as a programmer bug and abort the program
  // if this requirement is violated.
  if (isHandlerRegistered()) {
    LOG(ERROR) << fn << " called on registered handler; aborting";
    abort();
  }
}

void ShenangoEventHandler::setEventBase(EventBase *eventBase) {
  event_.eb_event_base_set(eventBase);
  eventBase_ = eventBase;
}

void
ShenangoEventHandler::shenangoEventCallback(void *arg) {
  VLOG(11) << "ShenangoEventHandler::shenangoEventCallback() called!";
  auto handler = reinterpret_cast<ShenangoEventHandler *>(arg);

  auto observer = handler->eventBase_->getExecutionObserver();
  if (observer) {
    observer->starting(reinterpret_cast<uintptr_t>(handler));
  }

  // this can't possibly fire if handler->eventBase_ is nullptr
  handler->eventBase_->bumpHandlingTime();

  handler->handlerReady();

  if (observer) {
    observer->stopped(reinterpret_cast<uintptr_t>(handler));
  }
}

} // namespace folly
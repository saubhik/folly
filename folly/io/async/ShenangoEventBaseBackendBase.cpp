#include <folly/io/async/ShenangoEventBaseBackendBase.h>
#include <folly/io/async/EventBase.h>

namespace folly {
void ShenangoEventBaseEvent::eb_ev_base(EventBase* evb) {
  evb_ = evb;
  if (evb) {
    event_->SetEventLoop(evb->getShenangoEventBase());
  } else {
    event_->SetEventLoop(nullptr);
  }
}

void ShenangoEventBaseEvent::eb_event_base_set(EventBase* evb) {
  evb_ = evb;
  auto* base = evb_ ? (evb_->getShenangoEventBase()) : nullptr;
  if (base) {
    event_->SetEventLoop(base);
  }
}

void ShenangoEventBaseEvent::eb_event_add(const struct timeval* timeout) {
  auto* backend = evb_ ? (evb_->getShenangoBackend()) : nullptr;
  if (backend) {
    backend->eb_event_add(*this, timeout);
  }
}

void ShenangoEventBaseEvent::eb_event_del() {
  auto* backend = evb_ ? (evb_->getShenangoBackend()) : nullptr;
  if (backend) {
    rt::Event::DelEvent(this->getEvent());
    // backend->eb_event_del(*this);
  }
}
} // namespace folly

#include "net.h"

#include <folly/io/async/ShenangoAsyncUDPSocket.h>
#include <folly/io/async/EventBase.h>

namespace folly {

ShenangoAsyncUDPSocket::ShenangoAsyncUDPSocket(EventBase* evb)
    : ShenangoEventHandler(CHECK_NOTNULL(evb)),
      readCallback_(nullptr),
      eventBase_(evb),
      fd_() {
  evb->dcheckIsInEventBaseThread();
}

ShenangoAsyncUDPSocket::~ShenangoAsyncUDPSocket() {
  if (fd_->LocalAddr() != nullptr) {
    close();
  }
}

void ShenangoAsyncUDPSocket::bind(const folly::SocketAddress& address) {
  ownership_ = FDOwnership::OWNS;
  localAddress_ = address;
}

void ShenangoAsyncUDPSocket::connect(const folly::SocketAddress& address) {
  ownership_ = FDOwnership::OWNS;
  connectedAddress_ = address;

  // TODO: Get netaddr from SocketAddress object
  fd_ = rt::UdpConn::Dial(localAddress_, connectedAddress_);

  // put the socket in non-blocking mode
  fd_->SetNonblocking(true);

  // attach to EventHandler
  ShenangoAsyncUDPSocket::changeHandlerFD(fd_);

  connected = true;

  if (!localAddress_.isInitialized()) {
    // TODO: Get SocketAddress from netaddr object
    localAddress_ = fd_->LocalAddr();
  }
}

void ShenangoAsyncUDPSocket::detachEventBase() {
  DCHECK(eventBase_ && eventBase_->isInEventBaseThread());
  eventBase_ = nullptr;
  ShenangoEventHandler::detachEventBase();
}

void ShenangoAsyncUDPSocket::attachEventBase(folly::EventBase* evb) {
  DCHECK(!eventBase_);
  DCHECK(evb && evb->isInEventBaseThread());
  eventBase_ = evb;
  ShenangoEventHandler::attachEventBase(evb);
}

} // namespace folly

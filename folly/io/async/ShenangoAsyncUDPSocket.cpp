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
  if (fd_ != ShNetworkSocket()) {
    close();
  }
}

void ShenangoAsyncUDPSocket::init() {
  ShNetworkSocket socket = shnetops::socket();
  if (socket == ShNetworkSocket()) {
    throw AsyncSocketException(
        AsyncSocketException::NOT_OPEN,
        "error creating async udp socket",
        errno);
  }

  auto g = folly::makeGuard([&] { shnetops::close(socket); });

  // put the socket in non-blocking mode
  int ret = shnetops::set_socket_non_blocking(socket);
  if (ret != 0) {
    throw AsyncSocketException(
        AsyncSocketException::NOT_OPEN,
        "failed to put socket in non-blocking mode",
        errno);
  }

  // success
  g.dismiss();
  fd_ = socket;
  ownership_ = FDOwnership::OWNS;

  // attach to EventHandler
  ShenangoEventHandler::changeHandlerFD(fd_);
}

void ShenangoAsyncUDPSocket::bind(const folly::SocketAddress& address) {
  // Do not support UNIX sockets.
  if (address.getFamily() == AF_UNIX) {
    errno = ENOTSUP;
    return;
  }

  init();
  netaddr localAddr{};

  // bind to the address
  localAddr = rt::StringToNetaddr(address.describe());
  if (shnetops::bind(fd_, &localAddr) != 0) {
    throw AsyncSocketException(
        AsyncSocketException::NOT_OPEN,
        "failed to bind the async udp socket for:" + address.describe(),
        errno);
  }

  localAddress_ = folly::SocketAddress(rt::NetaddrToIPString(localAddr),
                                       localAddr.port);
}

void ShenangoAsyncUDPSocket::connect(const folly::SocketAddress& address) {
  // not bound yet
  if (fd_ == ShNetworkSocket()) { init(); }

  netaddr connAddr = rt::StringToNetaddr(connectedAddress_.describe());

  if (shnetops::connect(fd_, &connAddr)) {
    throw AsyncSocketException(
        AsyncSocketException::NOT_OPEN,
        "Failed to connect the udp socket to:" + address.describe(),
        errno);
  }

  connected_ = true;
  connectedAddress_ = address;

  // When connect() is called before bind()
  if (!localAddress_.isInitialized()) {
    netaddr localAddr = fd_.data->LocalAddr();
    localAddress_ = folly::SocketAddress(
        rt::NetaddrToIPString(localAddr),
        localAddr.port);
  }
}

ssize_t ShenangoAsyncUDPSocket::write(
    const folly::SocketAddress& address,
    const std::unique_ptr<folly::IOBuf>& buf) {
  // UDP's typical MTU size is 1500, so high number of buffers
  // really do not make sense. Optimize for buffer chains with
  // buffers less than 16, which is the highest I can think of
  // for a real use case.
  iovec vec[16];
  size_t iovec_len = buf->fillIov(vec, sizeof(vec) / sizeof(vec[0])).numIovecs;
  if (UNLIKELY(iovec_len == 0)) {
    buf->coalesce();
    vec[0].iov_base = const_cast<uint8_t*>(buf->data());
    vec[0].iov_len = buf->length();
    iovec_len = 1;
  }

  CHECK_NE(ShNetworkSocket(), fd_) << "Socket not yet bound";
  netaddr raddr = rt::StringToNetaddr(address.describe());

  struct msghdr msg{};
  if (!connected_) {
    msg.msg_name = reinterpret_cast<void*>(&raddr);
    msg.msg_namelen = address.getActualSize();
  } else {
    if (connectedAddress_ != address) {
      errno = ENOTSUP;
      return -1;
    }
    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
  }
  msg.msg_iov = const_cast<struct iovec*>(vec);
  msg.msg_iovlen = iovec_len;
  msg.msg_control = nullptr;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;

  return sendmsg(fd_, &msg, 0);
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

void ShenangoAsyncUDPSocket::close() {
  eventBase_->dcheckIsInEventBaseThread();

  if (readCallback_) {
    auto cob = readCallback_;
    readCallback_ = nullptr;

    cob->onReadClosed();
  }

  // Unregister any events we are registered for
  unregisterHandler();

  if (fd_ != ShNetworkSocket() && ownership_ == FDOwnership::OWNS) {
    shnetops::close(fd_);
  }

  fd_ = ShNetworkSocket();
}

} // namespace folly

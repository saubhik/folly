#include "net.h"

#include <boost/preprocessor/control/if.hpp>

#include <folly/io/async/EventBase.h>
#include <folly/io/async/AsyncUDPSocket.h>
#include <folly/io/async/ShenangoEventHandler.h>
#include <folly/small_vector.h>

#if FOLLY_HAVE_VLA
#define FOLLY_HAVE_VLA_01 1
#else
#define FOLLY_HAVE_VLA_01 0
#endif

#if PROFILING_ENABLED
namespace {
std::unordered_map<std::string, uint64_t> totElapsed;
}
#endif
namespace folly {

void AsyncUDPSocket::fromMsg(
    FOLLY_MAYBE_UNUSED ReadCallback::OnDataAvailableParams& params,
    FOLLY_MAYBE_UNUSED struct msghdr& msg) {
  // No support yet.
}


AsyncUDPSocket::AsyncUDPSocket(EventBase* evb)
    : ShenangoEventHandler(CHECK_NOTNULL(evb)), readCallback_(nullptr),
      eventBase_(evb), fd_() {
  evb->dcheckIsInEventBaseThread();
}

AsyncUDPSocket::~AsyncUDPSocket() {
  if (fd_ != ShNetworkSocket()) {
    close();
  }
}

void AsyncUDPSocket::init() {
  ShNetworkSocket socket = shnetops::socket();
  if (socket == ShNetworkSocket()) {
    throw AsyncSocketException(AsyncSocketException::NOT_OPEN,
                               "error creating async udp socket", errno);
  }

  VLOG(11) << "async UDP socket created!";

  // auto g = folly::makeGuard([&] { shnetops::close(socket); });

  // put the socket in non-blocking mode
  int ret = shnetops::set_socket_non_blocking(socket);
  if (ret != 0) {
    throw AsyncSocketException(AsyncSocketException::NOT_OPEN,
                               "failed to put socket in non-blocking mode",
                               errno);
  }

  VLOG(11) << "async UDP socket is set to non blocking!";

  // success
  // g.dismiss();
  fd_ = socket;
  ownership_ = FDOwnership::OWNS;

  // attach to EventHandler
  ShenangoEventHandler::changeHandlerFD(fd_);

  VLOG(11) << "init() successful for async UDP socket!";
}

void AsyncUDPSocket::bind(const folly::SocketAddress& address,
                          BindOptions bindOptions) {
  // Do not support UNIX sockets.
  if (address.getFamily() == AF_UNIX) {
    errno = ENOTSUP;
    return;
  }

  VLOG(11) << "Calling init() in async UDP socket bind()!";

  init();
  netaddr localAddr{};

  // bind to the address
  localAddr = rt::StringToNetaddr(address.describe());
  if (shnetops::bind(fd_, &localAddr) != 0) {
    throw AsyncSocketException(
        AsyncSocketException::NOT_OPEN,
        "failed to bind the async udp socket for:" + address.describe(), errno);
  }

  localAddress_ =
      folly::SocketAddress(rt::NetaddrToIPString(localAddr), localAddr.port);
}

void AsyncUDPSocket::connect(const folly::SocketAddress& address) {
  // not bound yet
  if (fd_ == ShNetworkSocket()) {
    init();
  }

  netaddr connAddr = rt::StringToNetaddr(address.describe());

  if (shnetops::connect(fd_, &connAddr)) {
    throw AsyncSocketException(
        AsyncSocketException::NOT_OPEN,
        "Failed to connect the udp socket to:" + address.describe(), errno);
  }

  connected_ = true;
  connectedAddress_ = address;

  // When connect() is called before bind()
  if (!localAddress_.isInitialized()) {
    netaddr localAddr = fd_.data->LocalAddr();
    localAddress_ =
        folly::SocketAddress(rt::NetaddrToIPString(localAddr), localAddr.port);
  }
}

void AsyncUDPSocket::dontFragment(bool df) {
  // This does nothing as of now.
  int optname4 = 0;
  int optval4 = df ? 0 : 0;
  int optname6 = 0;
  int optval6 = df ? 0 : 0;
#if defined(IP_MTU_DISCOVER) && defined(IP_PMTUDISC_DO) && \
    defined(IP_PMTUDISC_WANT)
  optname4 = IP_MTU_DISCOVER;
  optval4 = df ? IP_PMTUDISC_DO : IP_PMTUDISC_WANT;
#endif
#if defined(IPV6_MTU_DISCOVER) && defined(IPV6_PMTUDISC_DO) && \
    defined(IPV6_PMTUDISC_WANT)
  optname6 = IPV6_MTU_DISCOVER;
  optval6 = df ? IPV6_PMTUDISC_DO : IPV6_PMTUDISC_WANT;
#endif
//  if (optname4 && optval4 && address().getFamily() == AF_INET) {
//    if (netops::setsockopt(
//        fd_, IPPROTO_IP, optname4, &optval4, sizeof(optval4))) {
//      throw AsyncSocketException(
//          AsyncSocketException::NOT_OPEN,
//          "Failed to set DF with IP_MTU_DISCOVER",
//          errno);
//    }
//  }
//  if (optname6 && optval6 && address().getFamily() == AF_INET6) {
//    if (netops::setsockopt(
//        fd_, IPPROTO_IPV6, optname6, &optval6, sizeof(optval6))) {
//      throw AsyncSocketException(
//          AsyncSocketException::NOT_OPEN,
//          "Failed to set DF with IPV6_MTU_DISCOVER",
//          errno);
//    }
//  }
}

void AsyncUDPSocket::setDFAndTurnOffPMTU() {
  // This does nothing as of now.
  int optname4 = 0;
  int optval4 = 0;
  int optname6 = 0;
  int optval6 = 0;
#if defined(IP_MTU_DISCOVER) && defined(IP_PMTUDISC_PROBE)
  optname4 = IP_MTU_DISCOVER;
  optval4 = IP_PMTUDISC_PROBE;
#endif
#if defined(IPV6_MTU_DISCOVER) && defined(IPV6_PMTUDISC_PROBE)
  optname6 = IPV6_MTU_DISCOVER;
  optval6 = IPV6_PMTUDISC_PROBE;
#endif
//  if (optname4 && optval4 && address().getFamily() == AF_INET) {
//    if (folly::netops::setsockopt(
//        fd_, IPPROTO_IP, optname4, &optval4, sizeof(optval4))) {
//      throw AsyncSocketException(
//          AsyncSocketException::NOT_OPEN,
//          "Failed to set PMTUDISC_PROBE with IP_MTU_DISCOVER",
//          errno);
//    }
//  }
//  if (optname6 && optval6 && address().getFamily() == AF_INET6) {
//    if (folly::netops::setsockopt(
//        fd_, IPPROTO_IPV6, optname6, &optval6, sizeof(optval6))) {
//      throw AsyncSocketException(
//          AsyncSocketException::NOT_OPEN,
//          "Failed to set PMTUDISC_PROBE with IPV6_MTU_DISCOVER",
//          errno);
//    }
//  }
}

void AsyncUDPSocket::setErrMessageCallback(
    ErrMessageCallback* errMessageCallback) {
  // This does nothing as of now.
  int optname4 = 0;
  int optname6 = 0;
#if defined(IP_RECVERR)
  optname4 = IP_RECVERR;
#endif
#if defined(IPV6_RECVERR)
  optname6 = IPV6_RECVERR;
#endif
  errMessageCallback_ = errMessageCallback;
  int err = (errMessageCallback_ != nullptr);
//  if (optname4 && address().getFamily() == AF_INET &&
//      netops::setsockopt(fd_, IPPROTO_IP, optname4, &err, sizeof(err))) {
//    throw AsyncSocketException(
//        AsyncSocketException::NOT_OPEN, "Failed to set IP_RECVERR", errno);
//  }
//  if (optname6 && address().getFamily() == AF_INET6 &&
//      netops::setsockopt(fd_, IPPROTO_IPV6, optname6, &err, sizeof(err))) {
//    throw AsyncSocketException(
//        AsyncSocketException::NOT_OPEN, "Failed to set IPV6_RECVERR", errno);
//  }
}

ssize_t AsyncUDPSocket::writeGSO(
    const folly::SocketAddress& address,
    const std::unique_ptr<folly::IOBuf>& buf,
    int gso) {
  // UDP's typical MTU size is 1500, so high number of buffers
  // really do not make sense. Optimize for buffer chains with
  // buffers less than 16, which is the highest I can think of
  // for a real use case.
#if PROFILING_ENABLED
  uint64_t st = microtime();
#endif
  iovec vec[16];
  size_t iovec_len = buf->fillIov(vec, sizeof(vec) / sizeof(vec[0])).numIovecs;
  if (UNLIKELY(iovec_len == 0)) {
    buf->coalesce();
    vec[0].iov_base = const_cast<uint8_t*>(buf->data());
    vec[0].iov_len = buf->length();
    iovec_len = 1;
  }
#if PROFILING_ENABLED
  totElapsed["writeGSO"] += microtime() - st;
  VLOG_EVERY_N(1, 10000) << "folly::AsyncUDPSocket::writeGSO()"
                          << " tot = " << totElapsed["writeGSO"] << " micros"
                          << (totElapsed["writeGSO"] = 0);
#endif
  return writev(address, vec, iovec_len);
}

void AsyncUDPSocket::setFD(ShNetworkSocket fd, FDOwnership ownership) {
  CHECK_EQ(ShNetworkSocket(), fd_) << "Already bound to another FD";

  fd_ = fd;
  ownership_ = ownership;

  ShenangoEventHandler::changeHandlerFD(fd_);

  netaddr localAddr{};
  localAddress_ =
      folly::SocketAddress(rt::NetaddrToIPString(localAddr), localAddr.port);
}

ssize_t
AsyncUDPSocket::writeChain(const folly::SocketAddress& address,
                           std::unique_ptr<folly::IOBuf>&& buf) {
  auto ret = write(address, buf);

  if (ioBufFreeFunc_ && buf) {
    ioBufFreeFunc_(std::move(buf));
  }

  return ret;
}

ssize_t AsyncUDPSocket::writev(const folly::SocketAddress& address,
                               const struct iovec* vec,
                               size_t iovec_len) {
#if PROFILING_ENABLED
  uint64_t st = microtime();
#endif
  CHECK_NE(ShNetworkSocket(), fd_) << "Socket not yet bound";
  netaddr raddr{};
  raddr.ip = address.storage_.addr.asV4().toLongHBO();
  raddr.port = address.port_;

  struct msghdr msg{};
  if (!connected_) {
    msg.msg_name = reinterpret_cast<void*>(&raddr);
    msg.msg_namelen = sizeof(raddr);
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
#if PROFILING_ENABLED
  totElapsed["writev"] += microtime() - st;
  VLOG_EVERY_N(1, 10000) << "folly::AsyncUDPSocket::writev()"
                          << " tot = " << totElapsed["writev"] << " micros"
                          << (totElapsed["writev"] = 0);
#endif
  return sendmsg(fd_, &msg, 0);
}

int AsyncUDPSocket::writemGSO(
    Range<SocketAddress const*> addrs,
    const std::unique_ptr<folly::IOBuf>* bufs,
    size_t count,
    const int* gso) {
  return writem(addrs, bufs, count);
}

int AsyncUDPSocket::writem(Range<SocketAddress const*> addrs,
                           const std::unique_ptr<folly::IOBuf>* bufs,
                           size_t count) {
  int ret;
  constexpr size_t kSmallSizeMax = 8;

  if (count <= kSmallSizeMax) {
    // suppress "warning: variable length array 'vec' is used [-Wvla]"
    FOLLY_PUSH_WARNING
    FOLLY_GNU_DISABLE_WARNING("-Wvla")
    mmsghdr vec[BOOST_PP_IF(FOLLY_HAVE_VLA_01, count, kSmallSizeMax)];
    FOLLY_POP_WARNING
    ret = writeImpl(addrs, bufs, count, vec);
  } else {
    std::unique_ptr<mmsghdr[]> vec(new mmsghdr[count]);
    ret = writeImpl(addrs, bufs, count, vec.get());
  }

  return ret;
}

int AsyncUDPSocket::writeImpl(Range<SocketAddress const*> addrs,
                              const std::unique_ptr<folly::IOBuf>* bufs,
                              size_t count, struct mmsghdr* msgvec) {
  // most times we have a single destination addr
  auto addr_count = addrs.size();
  constexpr size_t kAddrCountMax = 1;
  small_vector<full_netaddr, kAddrCountMax> addrStorage(addr_count);

  for (size_t i = 0; i < addr_count; i++) {
    addrStorage[i].addr = rt::StringToNetaddr(addrs[i].describe());
    addrStorage[i].len = folly::to_narrow(addrs[i].getActualSize());
  }

  size_t iov_count = 0;
  for (size_t i = 0; i < count; i++) {
    iov_count += bufs[i]->countChainElements();
  }

  int ret;
  constexpr size_t kSmallSizeMax = 16;
  if (iov_count <= kSmallSizeMax) {
    // suppress "warning: variable length array 'vec' is used [-Wvla]"
    FOLLY_PUSH_WARNING
    FOLLY_GNU_DISABLE_WARNING("-Wvla")
    iovec iov[BOOST_PP_IF(FOLLY_HAVE_VLA_01, iov_count, kSmallSizeMax)];
    FOLLY_POP_WARNING
    fillMsgVec(range(addrStorage), bufs, count, msgvec, iov, iov_count);
    ret = sendmmsg(fd_, msgvec, count, 0);
  } else {
    std::unique_ptr<iovec[]> iov(new iovec[iov_count]);
    fillMsgVec(range(addrStorage), bufs, count, msgvec, iov.get(), iov_count);
    ret = sendmmsg(fd_, msgvec, count, 0);
  }

  return ret;
}

ssize_t AsyncUDPSocket::sendmsg(ShNetworkSocket socket,
    const struct msghdr* message, int flags) {
#if PROFILING_ENABLED
  uint64_t st = microtime();
#endif
  ssize_t ret = shnetops::sendmsg(socket, message, flags);
#if PROFILING_ENABLED
  totElapsed["sendmsg"] += microtime() - st;
  VLOG_EVERY_N(1, 10000) << "folly::AsyncUDPSocket::sendmsg()"
                         << " tot = " << totElapsed["sendmsg"] << " micros"
                         << (totElapsed["sendmsg"] = 0);
#endif
  return ret;
}

void AsyncUDPSocket::fillMsgVec(
    Range<full_netaddr*> addrs, const std::unique_ptr<folly::IOBuf>* bufs,
    size_t count, struct mmsghdr* msgvec, struct iovec* iov, size_t iov_count) {
  auto addr_count = addrs.size();
  DCHECK(addr_count);
  size_t remaining = iov_count;

  size_t iov_pos = 0;
  for (size_t i = 0; i < count; i++) {
    // we can use remaining here to avoid calling countChainElements() again
    auto ret = bufs[i]->fillIov(&iov[iov_pos], remaining);
    size_t iovec_len = ret.numIovecs;
    remaining -= iovec_len;
    auto& msg = msgvec[i].msg_hdr;
    // if we have less addrs compared to count
    // we use the last addr
    if (i < addr_count) {
      msg.msg_name = reinterpret_cast<void*>(&addrs[i].addr);
      msg.msg_namelen = addrs[i].len;
    } else {
      msg.msg_name = reinterpret_cast<void*>(&addrs[addr_count - 1].addr);
      msg.msg_namelen = addrs[addr_count - 1].len;
    }
    msg.msg_iov = &iov[iov_pos];
    msg.msg_iovlen = iovec_len;
    msg.msg_control = nullptr;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    msgvec[i].msg_len = 0;

    iov_pos += iovec_len;
  }
}

ssize_t
AsyncUDPSocket::write(const folly::SocketAddress& address,
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

  return writev(address, vec, iovec_len);
}

ssize_t AsyncUDPSocket::recvmsg(struct msghdr* msg, int flags) {
  return shnetops::recvmsg(fd_, msg, flags);
}

int AsyncUDPSocket::recvmmsg(struct mmsghdr* msgvec, unsigned int vlen,
                             unsigned int flags,
                             struct timespec* timeout) {
  return shnetops::recvmmsg(fd_, msgvec, vlen, flags, timeout);
}

void AsyncUDPSocket::resumeRead(ReadCallback* cob) {
  CHECK(!readCallback_) << "Another read callback already installed";
  CHECK_NE(ShNetworkSocket(), fd_)
    << "UDP server socket not yet bind to an address";

  readCallback_ = CHECK_NOTNULL(cob);
  if (!updateRegistration()) {
    AsyncSocketException ex(AsyncSocketException::NOT_OPEN,
                            "failed to register for accept events");

    readCallback_ = nullptr;
    cob->onReadError(ex);
    return;
  }

  VLOG(11) << "resumeRead completed";
}

void AsyncUDPSocket::pauseRead() {
  // It is ok to pause an already paused socket
  readCallback_ = nullptr;
  updateRegistration();
}

void AsyncUDPSocket::handlerReady() noexcept {
  DCHECK(readCallback_);
  handleRead();
}

void AsyncUDPSocket::handleRead() noexcept {
  VLOG(4) << "handleRead() triggered!";
  void* buf{nullptr};
  size_t len{0};

  if (fd_ == ShNetworkSocket()) {
    // TODO: Maybe this is not required.
    // The socket may have been closed by the error callbacks.
    return;
  }

  if (readCallback_->shouldOnlyNotify()) {
    return readCallback_->onNotifyDataAvailable(*this);
  }

  size_t numReads = maxReadsPerEvent_ ? maxReadsPerEvent_ : size_t(-1);
  EventBase* originalEventBase = eventBase_;
  while (numReads-- && readCallback_ && eventBase_ == originalEventBase) {
    readCallback_->getReadBuffer(&buf, &len);
    if (buf == nullptr || len == 0) {
      AsyncSocketException ex(
          AsyncSocketException::BAD_ARGS,
          "AsyncUDPSocket::getReadBuffer() returned empty buffer");

      auto cob = readCallback_;
      readCallback_ = nullptr;

      cob->onReadError(ex);
      updateRegistration();
      return;
    }

    netaddr addr{};
    ssize_t bytesRead;
    ReadCallback::OnDataAvailableParams params;

    bytesRead = shnetops::recvfrom(fd_, buf, len, MSG_TRUNC, &addr);

    if (bytesRead >= 0) {
      clientAddress_ =
          folly::SocketAddress(rt::NetaddrToIPString(addr), addr.port);

      if (bytesRead > 0) {
        bool truncated = false;
        if ((size_t) bytesRead > len) {
          truncated = true;
          bytesRead = ssize_t(len);
        }

        readCallback_->onDataAvailable(clientAddress_, size_t(bytesRead),
                                       truncated, params);
      }
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // No data could be read without blocking the socket
        return;
      }

      AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
                              "::recvfrom() failed", errno);

      // In case of UDP we can continue reading from the socket
      // even if the current request fails. We notify the user
      // so that he can do some logging/stats collection if he wants.
      auto cob = readCallback_;
      readCallback_ = nullptr;

      cob->onReadError(ex);
      updateRegistration();

      return;
    }
  }
}

bool AsyncUDPSocket::updateRegistration() noexcept {
  uint16_t flags = NONE;

  if (readCallback_) {
    flags |= READ;
  }

  VLOG(11) << "Calling registerHandler()!";

  return registerHandler(uint16_t(flags));
}

bool AsyncUDPSocket::setGSO(int val) {
  // Shenango always supports GSO.
  return true;
}

int AsyncUDPSocket::getGSO() {
  // Shenango always supports GSO.
  return 1;
}

bool AsyncUDPSocket::setGRO(bool bVal) {
  // No support yet.
  return false;
}

// packet timestamping
int AsyncUDPSocket::getTimestamping() {
  // No support yet.
  ts_ = -1;
  return ts_.value();
}

bool AsyncUDPSocket::setTimestamping(int val) {
  // No support yet.
  return false;
}

int AsyncUDPSocket::getGRO() {
  // No support yet.
  return -1;
}

void AsyncUDPSocket::applyOptions(
    const SocketOptionMap& options, SocketOptionKey::ApplyPos pos) {
  VLOG(11) << "Using not implemented AsyncUDPSocket::applyOptions()!";
  // auto result = applySocketOptions(fd_, options, pos);
  auto result = 0;
  if (result != 0) {
    throw AsyncSocketException(
        AsyncSocketException::INTERNAL_ERROR,
        "failed to set socket option",
        result);
  }
}

void AsyncUDPSocket::detachEventBase() {
  DCHECK(eventBase_ && eventBase_->isInEventBaseThread());
  registerHandler(uint16_t(NONE));
  eventBase_ = nullptr;
  ShenangoEventHandler::detachEventBase();
}

void AsyncUDPSocket::attachEventBase(folly::EventBase* evb) {
  DCHECK(!eventBase_);
  DCHECK(evb && evb->isInEventBaseThread());
  eventBase_ = evb;
  ShenangoEventHandler::attachEventBase(evb);
  updateRegistration();
}

void AsyncUDPSocket::close() {
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

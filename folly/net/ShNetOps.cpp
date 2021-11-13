#include <folly/net/ShNetOps.h>

#include <fcntl.h>
#include <cerrno>

#include <cstddef>
#include <stdexcept>

#include <folly/ScopeGuard.h>
#include <folly/net/detail/SocketFileDescriptorMap.h>

#include "net.h"

#if !FOLLY_HAVE_RECVMMSG
#if FOLLY_HAVE_WEAK_SYMBOLS
extern "C" FOLLY_ATTR_WEAK int recvmmsg(
    int sockfd,
    struct mmsghdr* msgvec,
    unsigned int vlen,
    unsigned int flags,
    struct timespec* timeout);
#else
static int (*recvmmsg)(
    int sockfd,
    struct mmsghdr* msgvec,
    unsigned int vlen,
    unsigned int flags,
    struct timespec* timeout) = nullptr;
#endif // FOLLY_HAVE_WEAK_SYMBOLS
#endif // FOLLY_HAVE_RECVMMSG

namespace folly::shnetops {

int bind(ShNetworkSocket s, const netaddr* name) {
  s.data->Bind(name);
  return 0;
}

int close(ShNetworkSocket s) {
  s.data->Shutdown();
  return 0;
}

int connect(ShNetworkSocket s, const netaddr* name) {
  int r = s.data->Connect(name);
  return r;
}

ssize_t recv(ShNetworkSocket s, void* buf, size_t len, int flags) {
  throw std::logic_error("Not implemented!");
}

ssize_t recvfrom(
    ShNetworkSocket socket,
    void* buf,
    size_t len,
    int flags,
    netaddr* from) {
  rt::UdpConn* sock = socket.data;
  return sock->ReadFrom(buf, len, from);
}

ssize_t recvmsg(ShNetworkSocket socket, msghdr* message, int flags) {
  (void) flags;
  rt::UdpConn* sock = socket.data;
  ssize_t bytesReceived = 0;
  for (size_t i = 0; i < message->msg_iovlen; i++) {
    ssize_t r;
    if (message->msg_name != nullptr) {
      r = sock->ReadFrom(
          (void*) message->msg_iov[i].iov_base,
          (size_t) message->msg_iov[i].iov_len,
          (netaddr*) message->msg_name
      );
    } else {
      r = sock->Read(
          (void*) message->msg_iov[i].iov_base,
          (size_t) message->msg_iov[i].iov_len
      );
    }
    if (r == -1 || size_t(r) != message->msg_iov[i].iov_len) {
      // Some error happened
      // TODO: Handle Error?
      return -1;
    }
    bytesReceived += r;
  }
  return bytesReceived;
}

int recvmmsg(
    ShNetworkSocket s,
    mmsghdr* msgvec,
    unsigned int vlen,
    unsigned int flags,
    timespec* timeout) {
  // Implement via recvmsg
  for (unsigned int i = 0; i < vlen; i++) {
    ssize_t ret = recvmsg(s, &msgvec[i].msg_hdr, flags);
    // in case of an error
    // we return the number of msgs received if > 0
    // or an error if no msg was sent
    if (ret < 0) {
      if (i) {
        return static_cast<int>(i);
      }
      return static_cast<int>(ret);
    } else {
      msgvec[i].msg_len = ret;
    }
  }
  return static_cast<int>(vlen);
}

ssize_t send(ShNetworkSocket s, const void* buf, size_t len, int flags) {
  throw std::logic_error("Not implemented!");
}

ssize_t sendmsg(ShNetworkSocket socket, const msghdr* message, int flags) {
  rt::UdpConn* sock = socket.data;
  ssize_t bytesSent = 0;
  for (size_t i = 0; i < message->msg_iovlen; i++) {
    ssize_t r;
    if (message->msg_name != nullptr) {
      r = sock->WriteTo(
          (void*) message->msg_iov[i].iov_base,
          (size_t) message->msg_iov[i].iov_len,
          (netaddr*) message->msg_name
      );
    } else {
      r = sock->Write(
          (void*) message->msg_iov[i].iov_base,
          (size_t) message->msg_iov[i].iov_len
      );
    }
    if (r == -1 || size_t(r) != message->msg_iov[i].iov_len) {
      // Some error happened.
      // TODO: Handle Error?
      return -1;
    }
    bytesSent += r;
  }
  return bytesSent;
}

int sendmmsg(
    ShNetworkSocket socket, mmsghdr* msgvec, unsigned int vlen, int flags) {
  throw std::logic_error("Not implemented!");
}

ssize_t sendto(
    ShNetworkSocket s,
    const void* buf,
    size_t len,
    int flags,
    const sockaddr* to,
    socklen_t tolen) {
  throw std::logic_error("Not implemented!");
}

int shutdown(ShNetworkSocket s, int how) {
  throw std::logic_error("Not implemented!");
}

ShNetworkSocket socket() {
  return ShNetworkSocket(new rt::UdpConn());
}

int set_socket_non_blocking(ShNetworkSocket s) {
  s.data->SetNonblocking(true);
  return 0;
}
} // namespace folly

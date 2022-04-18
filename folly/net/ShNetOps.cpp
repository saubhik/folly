#include <folly/net/ShNetOps.h>

#include <glog/logging.h>

namespace folly::shnetops {

int bind(ShNetworkSocket& s, const netaddr* name) {
  if (s != ShNetworkSocket()) {
    close(s);
  }

  s.localAddr = *name;
  rt::UdpConn* sock = rt::UdpConn::Listen(s.localAddr);
  // sock->SetNonblocking(s.nonBlocking);
  sock->SetNonblocking(true);
  s.data = sock;

  return 0;
}

int close(ShNetworkSocket& s) {
  delete s.data;
  return 0;
}

int connect(ShNetworkSocket& s, const netaddr* name) {
  if (s != ShNetworkSocket()) {
    close(s);
  }

  s.remoteAddr = *name;
  s.data = rt::UdpConn::Dial(s.localAddr, s.remoteAddr);
  // s.data->SetNonblocking(s.nonBlocking);
  s.data->SetNonblocking(true);

  return 0;
}

ssize_t recv(ShNetworkSocket& s, void* buf, size_t len, int flags) {
  throw std::logic_error("Not implemented!");
}

ssize_t recvfrom(ShNetworkSocket& socket, void* buf, size_t len, int flags,
                 netaddr* from) {
  rt::UdpConn* sock = socket.data;
  // sock->SetNonblocking(socket.nonBlocking);
  sock->SetNonblocking(true);
  return sock->ReadFrom(buf, len, from);
}

ssize_t recvmsg(ShNetworkSocket& socket, msghdr* message, int flags) {
  (void) flags;
  rt::UdpConn* sock = socket.data;
  // sock->SetNonblocking(socket.nonBlocking);
  sock->SetNonblocking(true);
  ssize_t bytesReceived = 0;
  for (size_t i = 0; i < message->msg_iovlen; i++) {
    ssize_t r;
    if (message->msg_name != nullptr) {
      r = sock->ReadFrom((void*) message->msg_iov[i].iov_base,
                         (size_t) message->msg_iov[i].iov_len,
                         (netaddr*) message->msg_name);
    } else {
      r = sock->Read((void*) message->msg_iov[i].iov_base,
                     (size_t) message->msg_iov[i].iov_len);
    }
    if (r == -1) {
      // Some error happened
      return -1;
    }
    bytesReceived += r;
  }
  // TODO: Create a sockaddr and set it to message->msg_name.
  return bytesReceived;
}

int recvmmsg(ShNetworkSocket& s, mmsghdr* msgvec, unsigned int vlen,
             unsigned int flags, timespec* timeout) {
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

ssize_t send(ShNetworkSocket& s, const void* buf, size_t len, int flags) {
  throw std::logic_error("Not implemented!");
}

ssize_t sendmsg(ShNetworkSocket &socket, const msghdr *message, int flags,
                rt::CipherMeta **cipherMetas, ssize_t numCipherMetas) {
  VLOG(4) << "shnetops::sendmsg Sending message!";
  rt::UdpConn *sock = socket.data;
  // sock->SetNonblocking(socket.nonBlocking);
  sock->SetNonblocking(true);
  ssize_t bytesSent = 0;
  for (size_t i = 0; i < message->msg_iovlen; i++) {
    ssize_t r;
    if (message->msg_name != nullptr) {
      r = sock->WriteTo((void *)message->msg_iov[i].iov_base,
                        (size_t)message->msg_iov[i].iov_len,
                        (netaddr *)message->msg_name, cipherMetas,
                        numCipherMetas);
    } else {
      r = sock->Write((void *)message->msg_iov[i].iov_base,
                      (size_t)message->msg_iov[i].iov_len, cipherMetas,
                      numCipherMetas);
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

int sendmmsg(ShNetworkSocket& socket, mmsghdr* msgvec, unsigned int vlen,
             int flags) {
  throw std::logic_error("Not implemented!");
}

ssize_t sendto(ShNetworkSocket& s, const void* buf, size_t len, int flags,
               const sockaddr* to, socklen_t tolen) {
  throw std::logic_error("Not implemented!");
}

int shutdown(ShNetworkSocket& s, int how) {
  throw std::logic_error("Not implemented!");
}

ShNetworkSocket socket() {
  netaddr localAddr{0, 0};
  rt::UdpConn* sock = rt::UdpConn::Listen(localAddr);
  return ShNetworkSocket(sock);
}

int set_socket_non_blocking(ShNetworkSocket& s) {
  s.nonBlocking = true;
  rt::UdpConn* sock = s.data;
  if (sock) {
    sock->SetNonblocking(s.nonBlocking);
  }
  return 0;
}
} // namespace folly::shnetops

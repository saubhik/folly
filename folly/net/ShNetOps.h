#pragma once

#include <folly/net/ShNetworkSocket.h>
#include <folly/portability/IOVec.h>

#include <caladan/net.h>

namespace folly::shnetops {
int bind(ShNetworkSocket &s, const netaddr *name);
int close(ShNetworkSocket &s);
int connect(ShNetworkSocket &s, const netaddr *name);
ssize_t recv(ShNetworkSocket &s, void *buf, size_t len, int flags);
ssize_t recvfrom(ShNetworkSocket &s, void *buf, size_t len, bool *isDecrypted,
                 netaddr *from);
ssize_t recvmsg(ShNetworkSocket &s, msghdr *message, bool *isDecrypted);
int recvmmsg(ShNetworkSocket &s, mmsghdr *msgvec, unsigned int vlen,
             unsigned int flags, timespec *timeout);
ssize_t send(ShNetworkSocket &s, const void *buf, size_t len, int flags);
ssize_t sendto(ShNetworkSocket &s, const void *buf, size_t len, int flags,
               const sockaddr *to, socklen_t tolen);
ssize_t sendmsg(ShNetworkSocket &socket, const msghdr *message, int flags,
                rt::CipherMeta **cipherMetas, ssize_t numCipherMetas);
int sendmmsg(ShNetworkSocket &socket, mmsghdr *msgvec, unsigned int vlen,
             int flags);
ShNetworkSocket socket();
int set_socket_non_blocking(ShNetworkSocket &s);
} // namespace folly::shnetops

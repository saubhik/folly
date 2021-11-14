#pragma once

#include <ostream>

#include "net.h"

namespace folly {
/**
 * This is just a very thin wrapper around either a file descriptor or
 * a SOCKET depending on platform, along with a couple of helper methods
 * for explicitly converting to/from file descriptors, even on Windows.
 */
struct ShNetworkSocket {
  // using native_handle_type = int;
  // static constexpr native_handle_type invalid_handle_value = -1;
  using native_handle_type = rt::UdpConn*;
  static constexpr native_handle_type invalid_handle_value = nullptr;

  native_handle_type data;

  netaddr localAddr{0, 0};
  netaddr remoteAddr{0, 0};
  bool nonBlocking{false};
  bool shutDownCalled{false};

  constexpr ShNetworkSocket() : data(invalid_handle_value) {}
  constexpr explicit ShNetworkSocket(native_handle_type d) : data(d) {}

  template<typename T>
  static ShNetworkSocket fromFd(T) = delete;
  static ShNetworkSocket fromFd(rt::UdpConn* fd) { return ShNetworkSocket(fd); }

  native_handle_type toFd() const { return data; }

  friend constexpr bool operator==(
      const ShNetworkSocket& a, const ShNetworkSocket& b) noexcept {
    return a.data == b.data;
  }

  friend constexpr bool operator!=(
      const ShNetworkSocket& a, const ShNetworkSocket& b) noexcept {
    return !(a == b);
  }
};

template<class CharT, class Traits>
inline std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os, const ShNetworkSocket& addr) {
  os << "folly::ShNetworkSocket(" << addr.data << ")";
  return os;
}
} // namespace folly

namespace std {
template<>
struct hash<folly::ShNetworkSocket> {
  size_t operator()(const folly::ShNetworkSocket& s) const noexcept {
    return std::hash<folly::ShNetworkSocket::native_handle_type>()(s.data);
  }
};
} // namespace std

#pragma once

#include "net.h"

#include <folly/Function.h>
#include <folly/SocketAddress.h>
#include <folly/io/IOBuf.h>
#include <folly/io/async/AsyncSocketException.h>
#include <folly/io/async/ShenangoEventHandler.h>
#include <folly/io/async/EventBase.h>

namespace folly {

class ShenangoAsyncUDPSocket : public ShenangoEventHandler {
 public:
  enum class FDOwnership {
    OWNS, SHARED
  };

  class ReadCallback {
   public:
    struct OnDataAvailableParams {
      // int gro = -1;
      // RX timestamp if available
      using Timestamp = std::array<struct timespec, 3>;
      folly::Optional<Timestamp> ts;
    };

    /**
     * Invoked when the socket becomes readable and we want buffer
     * to write to.
     *
     * NOTE: From socket we will end up reading at most `len` bytes
     *       and if there were more bytes in datagram, we will end up
     *       dropping them.
     */
    virtual void getReadBuffer(void** buf, size_t* len) noexcept = 0;

    /**
     * Invoked when a new datagram is available on the socket. `len`
     * is the number of bytes read and `truncated` is true if we had
     * to drop few bytes because of running out of buffer space.
     * OnDataAvailableParams::gro is the GRO segment size
     */
    virtual void onDataAvailable(
        const folly::SocketAddress& client,
        size_t len,
        bool truncated,
        OnDataAvailableParams params) noexcept = 0;

    /**
     * Notifies when data is available. This is only invoked when
     * shouldNotifyOnly() returns true.
     */
    virtual void onNotifyDataAvailable(ShenangoAsyncUDPSocket&) noexcept {}

    /**
     * Returns whether or not the read callback should only notify
     * but not call getReadBuffer.
     * If shouldNotifyOnly() returns true, AsyncUDPSocket will invoke
     * onNotifyDataAvailable() instead of getReadBuffer().
     * If shouldNotifyOnly() returns false, AsyncUDPSocket will invoke
     * getReadBuffer() and onDataAvailable().
     */
    virtual bool shouldOnlyNotify() { return false; }

    /**
     * Invoked when there is an error reading from the socket.
     *
     * NOTE: Since UDP is connectionless, you can still read from the socket.
     *       But you have to re-register readCallback yourself after
     *       onReadError.
     */
    virtual void onReadError(const AsyncSocketException& ex) noexcept = 0;

    /**
     * Invoked when socket is closed and a read callback is registered.
     */
    virtual void onReadClosed() noexcept = 0;

    virtual ~ReadCallback() = default;
  };

  using IOBufFreeFunc = folly::Function<void(std::unique_ptr<folly::IOBuf>&&)>;

  struct WriteOptions {
    WriteOptions() = default;

    // WriteOptions(int gsoVal, bool zerocopyVal)
    //     : gso(gsoVal), zerocopy(zerocopyVal) {}
    // int gso{0};
    // bool zerocopy{false};
    std::chrono::microseconds txTime{0};
  };

  /**
   * Create a new UDP socket that will run in the
   * given event base
   */
  explicit ShenangoAsyncUDPSocket(EventBase* evb);
  ~ShenangoAsyncUDPSocket() override;

  virtual const folly::SocketAddress& address() const {
    CHECK_NE(nullptr, fd_->LocalAddr()) << "Server not yet bound to an address";
    return localAddress_;
  }

  virtual void bind(const folly::SocketAddress& address);

  virtual void connect(const folly::SocketAddress& address);

  /**
   * Use an already bound file descriptor. You can either transfer ownership
   * of this FD by using ownership = FDOwnership::OWNS or share it using
   * FDOwnership::SHARED. In case FD is shared, it will not be `close`d in
   * destructor.
   */
  virtual void setFD(rt::UdpConn* fd, FDOwnership ownership);

  /**
   * Send the data in buffer to destination.
   */
  virtual ssize_t write(
      const folly::SocketAddress& address,
      const std::unique_ptr<folly::IOBuf>& buf);

  /**
   * Send the data in buffers to destination.
   * bufs is an array of std::unique_ptr<folly::IOBuf>
   * of size num
   */
  virtual int writem(
      Range<SocketAddress const*> addrs,
      const std::unique_ptr<folly::IOBuf>* bufs,
      size_t count);

  virtual ssize_t writeChain(
      const folly::SocketAddress& address,
      std::unique_ptr<folly::IOBuf>&& buf,
      WriteOptions options);

  virtual ssize_t writev(
      const folly::SocketAddress& address,
      const struct iovec* vec,
      size_t iovec_len);

  virtual ssize_t recvmsg(struct msghdr* msg, int flags);

  virtual int recvmmsg(
      struct mmsghdr* msgvec,
      unsigned int vlen,
      unsigned int flags,
      struct timespec* timeout);

  /**
   * Start reading datagrams
   */
  virtual void resumeRead(ReadCallback* cob);

  /**
   * Pause reading datagrams
   */
  virtual void pauseRead();

  /**
   * Stop listening on the socket.
   */
  virtual void close();

  /**
   * Get internal FD used by this socket
   */
  virtual rt::UdpConn* getNetworkSocket() const {
    CHECK_NE(nullptr, fd_->LocalAddr()) << "Need to bind before getting FD out";
    return fd_;
  }

  EventBase* getEventBase() const { return eventBase_; }

  virtual bool isBound() const { return fd_->LocalAddr() != nullptr; }

  virtual bool isReading() const { return readCallback_ != nullptr; }

  virtual void detachEventBase();

  virtual void attachEventBase(folly::EventBase* evb);

  void setIOBufFreeFunc(IOBufFreeFunc&& ioBufFreeFunc) {
    ioBufFreeFunc_ = std::move(ioBufFreeFunc);
  }

 protected:
  virtual ssize_t
  sendmsg(rt::UdpConn* socket, const struct msghdr* message, int flags) {
    // TODO
  }

  virtual int
  sendmmsg(rt::UdpConn* socket, struct mmsghdr* msgvec, unsigned int vlen,
           int flags) {
    // TODO
  }

  virtual int writeImpl(
      Range<SocketAddress const*> addrs,
      const std::unique_ptr<folly::IOBuf>* bufs,
      size_t count,
      struct mmsghdr* msgvec);

  static auto constexpr kDefaultReadsPerEvent = 1;

  // Non-null only when we are reading
  ReadCallback* readCallback_;

 private:
  ShenangoAsyncUDPSocket(const ShenangoAsyncUDPSocket&) = delete;
  ShenangoAsyncUDPSocket& operator=(const ShenangoAsyncUDPSocket&) = delete;

  // ShenangoEventHandler
  void handlerReady(uint16_t events) noexcept override;

  EventBase* eventBase_;
  folly::SocketAddress localAddress_;

  rt::UdpConn* fd_;
  FDOwnership ownership_;

  // Temp space to receive client address
  folly::SocketAddress clientAddress_;

  // If the socket is connected
  folly::SocketAddress connectedAddress_;
  bool connected_{false};

  // TODO: Use udp_set_buffers()?
  // int rcvBuf_{0};
  // int sndBuf_{0};

  // packet timestamping
  folly::Optional<int> ts_;

  ErrMessageCallback* errMessageCallback_{nullptr};

  IOBufFreeFunc ioBufFreeFunc_;
};

} // namespace folly

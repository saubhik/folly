#pragma once

#include "net.h"

#include <folly/Function.h>
#include <folly/SocketAddress.h>
#include <folly/io/IOBuf.h>
#include <folly/io/async/AsyncSocketException.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/ShenangoEventHandler.h>
#include <folly/net/ShNetOps.h>
#include <folly/net/ShNetworkSocket.h>
#include <folly/io/SocketOptionMap.h>

namespace folly {

class AsyncUDPSocket : public ShenangoEventHandler {
 public:
  enum class FDOwnership {
    OWNS, SHARED
  };

  class ReadCallback {
   public:
    struct OnDataAvailableParams {
      bool isDecrypted = false;
      int gro = -1;
      // RX timestamp if available
      using Timestamp = std::array<struct timespec, 3>;
      folly::Optional<Timestamp> ts;
      static constexpr size_t kCmsgSpace =
          CMSG_SPACE(sizeof(uint16_t)) + CMSG_SPACE(sizeof(Timestamp));
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
    virtual void onDataAvailable(const folly::SocketAddress& client, size_t len,
                                 bool truncated,
                                 OnDataAvailableParams params) noexcept = 0;

    /**
     * Notifies when data is available. This is only invoked when
     * shouldNotifyOnly() returns true.
     */
    virtual void onNotifyDataAvailable(AsyncUDPSocket&) noexcept {}

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

  class ErrMessageCallback {
   public:
    virtual ~ErrMessageCallback() = default;

    /**
     * errMessage() will be invoked when kernel puts a message to
     * the error queue associated with the socket.
     *
     * @param cmsg      Reference to cmsghdr structure describing
     *                  a message read from error queue associated
     *                  with the socket.
     */
    virtual void errMessage(const cmsghdr& cmsg) noexcept = 0;

    /**
     * errMessageError() will be invoked if an error occurs reading a message
     * from the socket error stream.
     *
     * @param ex        An exception describing the error that occurred.
     */
    virtual void errMessageError(const AsyncSocketException& ex) noexcept = 0;
  };

  static void fromMsg(
      FOLLY_MAYBE_UNUSED ReadCallback::OnDataAvailableParams& params,
      FOLLY_MAYBE_UNUSED struct msghdr& msg);

  using IOBufFreeFunc = folly::Function<void(std::unique_ptr<folly::IOBuf>&&)>;

  /**
   * Create a new UDP socket that will run in the
   * given event base
   */
  explicit AsyncUDPSocket(EventBase* evb);

  ~AsyncUDPSocket() override;

  virtual const folly::SocketAddress& address() const {
    CHECK_NE(ShNetworkSocket(), fd_) << "Server not yet bound to an address";
    return localAddress_;
  }

  /**
   * Contains options to pass to bind.
   */
  struct BindOptions {
    constexpr BindOptions() noexcept {}

    // Whether IPV6_ONLY should be set on the socket.
    bool bindV6Only{true};
  };

  virtual void bind(const folly::SocketAddress& address,
                    BindOptions options = BindOptions());

  virtual void connect(const folly::SocketAddress& address);

  /**
   * Use an already bound file descriptor. You can either transfer ownership
   * of this FD by using ownership = FDOwnership::OWNS or share it using
   * FDOwnership::SHARED. In case FD is shared, it will not be `close`d in
   * destructor.
   */
  virtual void setFD(ShNetworkSocket fd, FDOwnership ownership);

  /**
   * Send the data in buffer to destination.
   */
  virtual ssize_t write(const folly::SocketAddress& address,
                        const std::unique_ptr<folly::IOBuf>& buf);

  /**
   * Send the data in buffers to destination.
   * bufs is an array of std::unique_ptr<folly::IOBuf>
   * of size num
   */
  virtual int writem(Range<SocketAddress const*> addrs,
                     const std::unique_ptr<folly::IOBuf>* bufs, size_t count);

  /**
   * Send the data in buffer to destination. Returns the return code from
   * ::sendmsg.
   *  gso is the generic segmentation offload value
   *  writeGSO will return -1 if
   *  buf->computeChainDataLength() <= gso
   *  Before calling writeGSO with a positive value
   *  verify GSO is supported on this platform by calling getGSO
   */
  virtual ssize_t writeGSO(
      const folly::SocketAddress& address,
      const std::unique_ptr<folly::IOBuf>& buf,
      int gso,
      rt::CipherMeta** cipherMetas,
      ssize_t numCipherMetas);

  virtual ssize_t writeChain(const folly::SocketAddress& address,
                             std::unique_ptr<folly::IOBuf>&& buf);

  /**
   * Send the data in buffers to destination. Returns the return code from
   * ::sendmmsg.
   * bufs is an array of std::unique_ptr<folly::IOBuf>
   * of size num
   * gso is an array with the generic segmentation offload values or nullptr
   *  Before calling writeGSO with a positive value
   *  verify GSO is supported on this platform by calling getGSO
   */
  virtual int writemGSO(
      Range<SocketAddress const*> addrs,
      const std::unique_ptr<folly::IOBuf>* bufs,
      size_t count,
      const int* gso);

  virtual ssize_t writev(
      const folly::SocketAddress& address,
      const struct iovec* vec,
      size_t iovec_len,
      rt::CipherMeta** cipherMetas,
      ssize_t numCipherMetas);

  virtual ssize_t recvmsg(struct msghdr* msg, bool* isDecrypted);

  virtual int recvmmsg(struct mmsghdr* msgvec, unsigned int vlen,
                       unsigned int flags, struct timespec* timeout);

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
  void close();

  /**
   * Get internal FD used by this socket
   */
  virtual ShNetworkSocket getNetworkSocket() const {
    CHECK_NE(ShNetworkSocket(), fd_) << "Need to bind before getting FD out";
    return fd_;
  }

  /**
   * Set reuse port mode to call bind() on the same address multiple times
   */
  virtual void setReusePort(bool reusePort) { reusePort_ = reusePort; }

  /**
   * Set SO_REUSEADDR flag on the socket. Default is OFF.
   */
  virtual void setReuseAddr(bool reuseAddr) { reuseAddr_ = reuseAddr; }

  EventBase* getEventBase() const { return eventBase_; }

  /**
   * Enable or disable fragmentation on the socket.
   *
   * On Linux, this sets IP(V6)_MTU_DISCOVER to IP(V6)_PMTUDISC_DO when enabled,
   * and to IP(V6)_PMTUDISC_WANT when disabled. IP(V6)_PMTUDISC_WANT will use
   * per-route setting to set DF bit. It may be more desirable to use
   * IP(V6)_PMTUDISC_PROBE as opposed to IP(V6)_PMTUDISC_DO for apps that has
   * its own PMTU Discovery mechanism.
   * Note this doesn't work on Apple.
   */
  virtual void dontFragment(bool df);

  /**
   * Set Dont-Fragment (DF) but ignore Path MTU.
   *
   * On Linux, this sets  IP(V6)_MTU_DISCOVER to IP(V6)_PMTUDISC_PROBE.
   * This essentially sets DF but ignores Path MTU for this socket.
   * This may be desirable for apps that has its own PMTU Discovery mechanism.
   * See http://man7.org/linux/man-pages/man7/ip.7.html for more info.
   */
  virtual void setDFAndTurnOffPMTU();

  /**
   * Callback for receiving errors on the UDP sockets
   */
  virtual void setErrMessageCallback(ErrMessageCallback* errMessageCallback);

  virtual bool isBound() const { return fd_ != ShNetworkSocket(); }

  virtual bool isReading() const { return readCallback_ != nullptr; }

  void detachEventBase() override;

  void attachEventBase(folly::EventBase* evb) override;

  // generic segmentation offload get/set
  // negative return value means GSO is not available
  virtual int getGSO();

  bool setGSO(int val);

  // generic receive offload get/set
  // negative return value means GRO is not available
  int getGRO();

  bool setGRO(bool bVal);

  // packet timestamping
  int getTimestamping();

  bool setTimestamping(int val);

  void setIOBufFreeFunc(IOBufFreeFunc&& ioBufFreeFunc) {
    ioBufFreeFunc_ = std::move(ioBufFreeFunc);
  }

  void applyOptions(
      const SocketOptionMap& options, SocketOptionKey::ApplyPos pos);

 protected:
  struct full_netaddr {
    netaddr addr;
    socklen_t len;
  };

  virtual ssize_t sendmsg(
      ShNetworkSocket socket,
      const struct msghdr* message,
      int flags,
      rt::CipherMeta** cipherMetas,
      ssize_t numCipherMetas);

  virtual int sendmmsg(ShNetworkSocket socket, struct mmsghdr* msgvec,
                       unsigned int vlen, int flags) {
    return shnetops::sendmmsg(socket, msgvec, vlen, flags);
  }

  static void fillMsgVec(Range<full_netaddr*> addrs,
                         const std::unique_ptr<folly::IOBuf>* bufs,
                         size_t count, struct mmsghdr* msgvec,
                         struct iovec* iov, size_t iov_count);

  virtual int writeImpl(Range<SocketAddress const*> addrs,
                        const std::unique_ptr<folly::IOBuf>* bufs, size_t count,
                        struct mmsghdr* msgvec);

  static auto constexpr kDefaultReadsPerEvent = 1;
  uint16_t maxReadsPerEvent_{kDefaultReadsPerEvent};

  // Non-null only when we are reading
  ReadCallback* readCallback_;

 private:
  AsyncUDPSocket(const AsyncUDPSocket&) = delete;

  AsyncUDPSocket& operator=(const AsyncUDPSocket&) = delete;

  void init();

  // ShenangoEventHandler
  void handlerReady() noexcept override;

  void handleRead() noexcept;

  bool updateRegistration() noexcept;

  EventBase* eventBase_;
  folly::SocketAddress localAddress_;

  ShNetworkSocket fd_;
  FDOwnership ownership_;

  // Temp space to receive client address
  folly::SocketAddress clientAddress_;

  // If the socket is connected
  folly::SocketAddress connectedAddress_;
  bool connected_{false};

  // These are dummies. Shenango doesn't support this yet.
  bool reuseAddr_{false};
  bool reusePort_{false};

  // TODO: Use udp_set_buffers()?
  // int rcvBuf_{0};
  // int sndBuf_{0};

  // packet timestamping
  folly::Optional<int> ts_;

  ErrMessageCallback* errMessageCallback_{nullptr};

  IOBufFreeFunc ioBufFreeFunc_;
};

} // namespace folly

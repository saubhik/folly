#pragma once

#include <folly/Memory.h>
#include <folly/io/IOBufQueue.h>
#include <folly/io/async/ShenangoAsyncUDPSocket.h>
#include <folly/io/async/EventBase.h>

namespace folly {

/**
 * UDP server socket
 *
 * It wraps a UDP socket waiting for packets and distributes them among
 * a set of event loops in round robin fashion.
 *
 * NOTE: At the moment it is designed to work with single packet protocols
 *       in mind. We distribute incoming packets among all the listeners in
 *       round-robin fashion. So, any protocol that expects to send/recv
 *       more than 1 packet will not work because they will end up with
 *       different event base to process.
 */
class ShenangoAsyncUDPServerSocket
    : private ShenangoAsyncUDPSocket::ReadCallback,
      public AsyncSocketBase {
 public:
  class Callback {
   public:
    using OnDataAvailableParams =
    ShenangoAsyncUDPSocket::ReadCallback::OnDataAvailableParams;

    /**
     * Invoked when we start reading data from socket. It is invoked in
     * each acceptors/listeners event base thread.
     */
    virtual void onListenStarted() noexcept = 0;

    /**
     * Invoked when the server socket is closed. It is invoked in each
     * acceptors/listeners event base thread.
     */
    virtual void onListenStopped() noexcept = 0;

    /**
     * Invoked when the server socket is paused. It is invoked in each
     * acceptors/listeners event base thread.
     */
    virtual void onListenPaused() noexcept {}

    /**
     * Invoked when the server socket is resumed. It is invoked in each
     * acceptors/listeners event base thread.
     */
    virtual void onListenResumed() noexcept {}

    /**
     * Invoked when the server socket can still read but need to inform the
     * callback object that it should not process read from new client address.
     * It is invoked in each acceptors/listeners event base thread.
     */
    virtual void onAcceptNewPeerPaused() noexcept {}

    /**
     * Invoked when need to inform the callback object that it can resume
     * process read from new client address. It is invoked in each
     * acceptors/listeners event base thread.
     */
    virtual void onAcceptNewPeerResumed() noexcept {}

    /**
     * Invoked when a new packet is received
     */
    virtual void onDataAvailable(
        std::shared_ptr<ShenangoAsyncUDPSocket> socket,
        const folly::SocketAddress& addr,
        std::unique_ptr<folly::IOBuf> buf,
        bool truncated,
        OnDataAvailableParams) noexcept = 0;

    virtual ~Callback() = default;
  };

  enum class DispatchMechanism {
    RoundRobin, ClientAddressHash
  };

  /**
   * Create a new UDP server socket
   *
   * Note about packet size - We allocate buffer of packetSize_ size to read.
   * If packet are larger than this value, as per UDP protocol, remaining data
   * is dropped and you get `truncated = true` in onDataAvailable callback
   */
  explicit ShenangoAsyncUDPServerSocket(
      EventBase* evb,
      size_t sz = 1500,
      DispatchMechanism dm = DispatchMechanism::RoundRobin)
      : evb_(evb), packetSize_(sz), dispatchMechanism_(dm), nextListener_(0) {}

  ~ShenangoAsyncUDPServerSocket() override {
    if (socket_) {
      close();
    }
  }

  void bind(
      const folly::SocketAddress& addy,
      const SocketOptionMap& options = emptySocketOptionMap) {
    CHECK(!socket_);

    socket_ = std::make_shared<ShenangoAsyncUDPSocket>(evb_);
    socket_->bind(addy);

    applyEventCallback();
  }

  folly::SocketAddress address() const {
    CHECK(socket_);
    return socket_->address();
  }

  void getAddress(SocketAddress* a) const override { *a = address(); }

  /**
   * Add a listener to the round robin list
   */
  void addListener(EventBase* evb, Callback* callback) {
    listeners_.emplace_back(evb, callback);
  }

  void listen() {
    CHECK(socket_) << "Need to bind before listening";

    for (auto& listener: listeners_) {
      auto callback = listener.second;

      listener.first->runInEventBaseThread(
          [callback]() mutable { callback->onListenStarted(); });
    }

    socket_->resumeRead(this);
  }

  ShNetworkSocket getNetworkSocket() const {
    CHECK(socket_) << "Need to bind before getting ShNetworkSocket";
    return socket_->getNetworkSocket();
  }

  const std::shared_ptr<ShenangoAsyncUDPSocket>&
  getSocket() const { return socket_; }

  void close() {
    CHECK(socket_) << "Need to bind before closing";
    socket_->close();
    socket_.reset();
  }

  EventBase* getEventBase() const override { return evb_; }

  /**
   * Indicates if the current socket is accepting.
   */
  bool isAccepting() const { return socket_->isReading(); }

  /**
   * Pauses accepting datagrams on the underlying socket.
   */
  void pauseAccepting() {
    socket_->pauseRead();
    for (auto& listener: listeners_) {
      auto callback = listener.second;

      listener.first->runInEventBaseThread(
          [callback]() mutable { callback->onListenPaused(); });
    }
  }

  /**
   * Inform the callback object that it should not process read from new client
   * address.
   */
  void pauseAcceptingNewPeer() {
    for (auto& listener: listeners_) {
      auto callback = listener.second;

      listener.first->runInEventBaseThread(
          [callback]() mutable { callback->onAcceptNewPeerPaused(); });
    }
  }

  /**
   * Starts accepting datagrams once again.
   */
  void resumeAccepting() {
    socket_->resumeRead(this);
    for (auto& listener: listeners_) {
      auto callback = listener.second;

      listener.first->runInEventBaseThread(
          [callback]() mutable { callback->onListenResumed(); });
    }
  }

  /**
   * Inform the callback object that it can process read from new client address
   * now.
   */
  void resumeAcceptingNewPeer() {
    for (auto& listener: listeners_) {
      auto callback = listener.second;

      listener.first->runInEventBaseThread(
          [callback]() mutable { callback->onAcceptNewPeerResumed(); });
    }
  }

  void setEventCallback(EventRecvmsgCallback* cb) {
    eventCb_ = cb;
    applyEventCallback();
  }

 private:
  // ShenangoAsyncUDPSocket::ReadCallback
  void getReadBuffer(void** buf, size_t* len) noexcept override {
    std::tie(*buf, *len) = buf_.preallocate(packetSize_, packetSize_);
  }

  void onDataAvailable(
      const folly::SocketAddress& clientAddress,
      size_t len,
      bool truncated,
      OnDataAvailableParams params) noexcept override {
    buf_.postallocate(len);
    auto data = buf_.split(len);

    if (listeners_.empty()) {
      LOG(WARNING) << "UDP server socket dropping packet, "
                   << "no listener registered";
      return;
    }

    uint32_t listenerId = 0;
    uint64_t client_hash_lo = 0;
    switch (dispatchMechanism_) {
      case DispatchMechanism::ClientAddressHash:
        // Hash base on clientAddress.
        // 1. This logic is samilar to: clientAddress.hash() % listeners_.size()
        //    But runs faster as it use multiply and shift instead of division.
        // 2. Only use the lower 32 bit from the address hash result for faster
        //    computation.
        client_hash_lo = static_cast<uint32_t>(clientAddress.hash());
        listenerId = (client_hash_lo * listeners_.size()) >> 32;
        break;
      case DispatchMechanism::RoundRobin: // round robin is default.
      default:
        if (nextListener_ >= listeners_.size()) {
          nextListener_ = 0;
        }
        listenerId = nextListener_;
        ++nextListener_;
        break;
    }

    auto callback = listeners_[listenerId].second;

    // Schedule it in the listener's eventbase
    // XXX: Speed this up
    auto f = [socket = socket_,
        client = clientAddress,
        callback,
        data = std::move(data),
        truncated,
        params]() mutable {
      callback->onDataAvailable(
          socket, client, std::move(data), truncated, params);
    };

    listeners_[listenerId].first->runInEventBaseThread(std::move(f));
  }

  void onReadError(const AsyncSocketException& ex) noexcept override {
    LOG(ERROR) << ex.what();

    // Lets register to continue listening for packets
    socket_->resumeRead(this);
  }

  void onReadClosed() noexcept override {
    for (auto& listener: listeners_) {
      auto callback = listener.second;

      listener.first->runInEventBaseThread(
          [callback]() mutable { callback->onListenStopped(); });
    }
  }

  void applyEventCallback() {
    if (socket_) {
      if (eventCb_) {
        socket_->setEventCallback(eventCb_);
      } else {
        socket_->resetEventCallback();
      }
    }
  }

  EventBase* const evb_;
  const size_t packetSize_;

  std::shared_ptr<ShenangoAsyncUDPSocket> socket_;

  // List of listener to distribute packets among
  typedef std::pair<EventBase*, Callback*> Listener;
  std::vector<Listener> listeners_;

  DispatchMechanism dispatchMechanism_;

  // Next listener to send packet to
  uint32_t nextListener_;

  // Temporary buffer for data
  folly::IOBufQueue buf_;

  EventRecvmsgCallback* eventCb_{nullptr};
};

} // namespace folly

extern "C" {
#include <base/log.h>
}

#include <folly/Conv.h>
#include <folly/SocketAddress.h>
#include <folly/String.h>
#include <folly/experimental/TestUtil.h>
#include <folly/io/IOBuf.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/ShenangoAsyncUDPServerSocket.h>
#include <folly/io/async/ShenangoAsyncUDPSocket.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/portability/Sockets.h>

#include <utility>

#include "runtime.h"
#include "thread.h"

using folly::AsyncTimeout;
using folly::errnoStr;
using folly::EventBase;
using folly::ShenangoAsyncUDPServerSocket;
using folly::ShenangoAsyncUDPSocket;
using folly::SocketAddress;
using namespace testing;

using OnDataAvailableParams =
    folly::ShenangoAsyncUDPSocket::ReadCallback::OnDataAvailableParams;

netaddr raddr;
constexpr int port = 8000;

class UDPAcceptor : public ShenangoAsyncUDPServerSocket::Callback {
public:
  UDPAcceptor(EventBase *evb, int n, folly::SocketAddress serverAddress)
      : evb_(evb), n_(n), serverAddress_(std::move(serverAddress)) {}

  void onListenStarted() noexcept override {}

  void onListenStopped() noexcept override {}

  void
  onDataAvailable(std::shared_ptr<folly::ShenangoAsyncUDPSocket> /* socket */,
                  const folly::SocketAddress &client,
                  std::unique_ptr<folly::IOBuf> data, bool truncated,
                  OnDataAvailableParams) noexcept override {
    lastClient_ = client;
    lastMsg_ = data->clone()->moveToFbString().toStdString();

    auto len = data->computeChainDataLength();
    VLOG(4) << "Worker " << n_ << " read " << len << " bytes "
            << "(trun:" << truncated << ") from " << client.describe() << " - "
            << lastMsg_;

    sendPong();
  }

  void sendPong() noexcept {
    try {
      auto writeSocket = std::make_shared<folly::ShenangoAsyncUDPSocket>(evb_);
      writeSocket->bind(serverAddress_);
      writeSocket->write(lastClient_, folly::IOBuf::copyBuffer(lastMsg_));
    } catch (const std::exception &ex) {
      VLOG(4) << "Failed to send PONG " << ex.what();
    }
  }

private:
  EventBase *const evb_{nullptr};
  const int n_{-1};

  folly::SocketAddress serverAddress_;
  folly::SocketAddress lastClient_;
  std::string lastMsg_;
};

class UDPServer {
public:
  UDPServer(EventBase *evb, folly::SocketAddress addr, int n)
      : evb_(evb), addr_(std::move(addr)), evbs_(n) {}

  void start() {
    CHECK(evb_->isInEventBaseThread());

    socket_ = std::make_unique<folly::ShenangoAsyncUDPServerSocket>(evb_, 1500);

    log_info("Created UDP server socket!");

    try {
      socket_->bind(addr_);
      log_info("UDP server socket is bound!");
      VLOG(4) << "Server listening on " << socket_->address().describe();
    } catch (const std::exception &ex) {
      LOG(FATAL) << ex.what();
    }

    acceptors_.reserve(evbs_.size());
    threads_.reserve(evbs_.size());

    // Add numWorkers thread
    int i = 0;
    for (auto &evb : evbs_) {
      acceptors_.emplace_back(&evb, i, socket_->address());

      rt::Thread t([&]() {
        log_info("I am in worker event loop thread!");
        evb.loopForever();
      });

      evb.waitUntilRunning();

      socket_->addListener(&evb, &acceptors_[i]);
      threads_.emplace_back(std::move(t));
      ++i;
    }

    VLOG(4) << "UDP server workers added!";

    socket_->listen();
    VLOG(4) << "UDP server started!";
  }

  [[nodiscard]] folly::SocketAddress address() const {
    return socket_->address();
  }

  void shutdown() {
    CHECK(evb_->isInEventBaseThread());
    socket_->close();
    socket_.reset();

    for (auto &evb : evbs_) {
      evb.terminateLoopSoon();
    }

    for (auto &t : threads_) {
      t.Join();
    }
  }

  void pauseAccepting() { socket_->pauseAccepting(); }

  void resumeAccepting() { socket_->resumeAccepting(); }

  bool isAccepting() { return socket_->isAccepting(); }

private:
  EventBase *const evb_{nullptr};
  const folly::SocketAddress addr_;

  std::unique_ptr<folly::ShenangoAsyncUDPServerSocket> socket_;
  std::vector<rt::Thread> threads_;
  std::vector<folly::EventBase> evbs_;
  std::vector<UDPAcceptor> acceptors_;
};

enum class BindSocket { YES, NO };

class UDPClient : private ShenangoAsyncUDPSocket::ReadCallback,
                  private AsyncTimeout {
public:
  using ShenangoAsyncUDPSocket::ReadCallback::OnDataAvailableParams;

  ~UDPClient() override = default;

  explicit UDPClient(EventBase *evb) : AsyncTimeout(evb), evb_(evb) {}

  void start(const folly::SocketAddress &server, int n,
             bool sendClustered = false) {
    CHECK(evb_->isInEventBaseThread());
    server_ = server;
    socket_ = std::make_unique<ShenangoAsyncUDPSocket>(evb_);

    // TODO: Fix this.
    connectAddr_ = server_;

    try {
      if (bindSocket_ == BindSocket::YES) {
        socket_->bind(folly::SocketAddress("10.10.1.2", 0));
      }
      if (connectAddr_) {
        socket_->connect(*connectAddr_);
        VLOG(2) << "Client connected to address " << *connectAddr_;
      }
      VLOG(2) << "Client bound to " << socket_->address().describe();
    } catch (const std::exception &ex) {
      LOG(FATAL) << ex.what();
    }

    socket_->resumeRead(this);

    n_ = n;

    log_info("Start playing ping pong!");

    // Start playing ping pong
    if (sendClustered) {
      sendPingsClustered();
    } else {
      sendPing();
    }
  }

  void shutdown() {
    CHECK(evb_->isInEventBaseThread());
    if (socket_) {
      socket_->pauseRead();
      socket_->close();
      socket_.reset();
    }
    evb_->terminateLoopSoon();
  }

  void sendPing() {
    if (n_ == 0) {
      shutdown();
      return;
    }

    --n_;
    scheduleTimeout(5);
    writePing(folly::IOBuf::copyBuffer(folly::to<std::string>("PING ", n_)));
  }

  void sendPingsClustered() {
    scheduleTimeout(5);
    while (n_ > 0) {
      --n_;
      writePing(folly::IOBuf::copyBuffer(folly::to<std::string>("PING ", n_)));
    }
  }

  virtual void writePing(std::unique_ptr<folly::IOBuf> buf) {
    auto ret = socket_->write(server_, buf);
    if (ret == -1) {
      error_ = true;
    }
  }

  void getReadBuffer(void **buf, size_t *len) noexcept override {
    *buf = buf_;
    *len = 1024;
  }

  void onDataAvailable(const folly::SocketAddress &client, size_t len,
                       bool truncated,
                       OnDataAvailableParams) noexcept override {
    VLOG(4) << "Read " << len << " bytes (trun:" << truncated << ") from "
            << client.describe() << " - " << std::string(buf_, len);
    VLOG(4) << n_ << " left";

    ++pongRecvd_;

    sendPing();
  }

  void onReadError(const folly::AsyncSocketException &ex) noexcept override {
    VLOG(4) << ex.what();

    // Start listening for next PONG
    socket_->resumeRead(this);
  }

  void onReadClosed() noexcept override {
    CHECK(false) << "We unregister reads before closing";
  }

  void timeoutExpired() noexcept override {
    VLOG(4) << "Timeout expired";
    sendPing();
  }

  [[nodiscard]] int pongRecvd() const { return pongRecvd_; }

  ShenangoAsyncUDPSocket &getSocket() { return *socket_; }

  void setShouldConnect(const folly::SocketAddress &connectAddr,
                        BindSocket bindSocket) {
    connectAddr_ = connectAddr;
    bindSocket_ = bindSocket;
  }

  [[nodiscard]] bool error() const { return error_; }

  void incrementPongCount(int n) { pongRecvd_ += n; }

protected:
  folly::Optional<folly::SocketAddress> connectAddr_;
  BindSocket bindSocket_{BindSocket::YES};
  EventBase *const evb_{nullptr};

  folly::SocketAddress server_;
  std::unique_ptr<ShenangoAsyncUDPSocket> socket_;
  bool error_{false};

private:
  int pongRecvd_{0};

  int n_{0};
  char buf_[1024]{};
};

class UDPNotifyClient : public UDPClient {
public:
  ~UDPNotifyClient() override = default;

  explicit UDPNotifyClient(EventBase *evb, bool useRecvmmsg = false,
                           unsigned int numMsgs = 1)
      : UDPClient(evb), useRecvmmsg_(useRecvmmsg), numMsgs_(numMsgs) {}

  bool shouldOnlyNotify() override { return true; }

  void onRecvMsg(ShenangoAsyncUDPSocket &sock) {
    struct msghdr msg {};
    memset(&msg, 0, sizeof(msg));

    void *buf{};
    size_t len{};
    getReadBuffer(&buf, &len);

    iovec vec{};
    vec.iov_base = buf;
    vec.iov_len = len;

    netaddr address{};

    msg.msg_name = reinterpret_cast<void *>(&address);
    msg.msg_namelen = sizeof(address);
    msg.msg_iov = &vec;
    msg.msg_iovlen = 1;

    ssize_t ret = sock.recvmsg(&msg, 0);
    if (ret < 0) {
      if (errno != EAGAIN || errno != EWOULDBLOCK) {
        onReadError(folly::AsyncSocketException(
            folly::AsyncSocketException::NETWORK_ERROR, "error"));
        return;
      }
    }

    SocketAddress addr =
        folly::SocketAddress(rt::NetaddrToIPString(address), address.port);
    onDataAvailable(addr, size_t(read), false, OnDataAvailableParams());
  }

  void onRecvMmsg(ShenangoAsyncUDPSocket &sock) {
    std::vector<struct mmsghdr> msgs;
    msgs.reserve(numMsgs_);
    memset(msgs.data(), 0, sizeof(struct mmsghdr) * numMsgs_);

    const socklen_t addrLen = sizeof(struct netaddr);

    const size_t dataSize = 1024;
    std::vector<char> buf;
    buf.reserve(numMsgs_ * dataSize);
    memset(buf.data(), 0, numMsgs_ * dataSize);

    std::vector<struct netaddr> addrs;
    addrs.reserve(numMsgs_);
    memset(addrs.data(), 0, sizeof(struct netaddr) * numMsgs_);

    std::vector<struct iovec> iovecs;
    iovecs.reserve(numMsgs_);
    memset(iovecs.data(), 0, sizeof(struct iovec) * numMsgs_);

    for (unsigned int i = 0; i < numMsgs_; ++i) {
      struct msghdr *msg = &msgs[i].msg_hdr;

      auto rawAddr = reinterpret_cast<void *>(&addrs[i]);

      iovecs[i].iov_base = &buf[i * dataSize];
      iovecs[i].iov_len = dataSize;

      msg->msg_name = rawAddr;
      msg->msg_namelen = addrLen;
      msg->msg_iov = &iovecs[i];
      msg->msg_iovlen = 1;
    }

    int ret = sock.recvmmsg(msgs.data(), numMsgs_, 0x10000 /* MSG_WAITFORONE */,
                            nullptr);
    if (ret < 0) {
      if (errno != EAGAIN || errno != EWOULDBLOCK) {
        onReadError(folly::AsyncSocketException(
            folly::AsyncSocketException::NETWORK_ERROR, "error"));
      }
      return;
    }

    incrementPongCount(ret);

    if (pongRecvd() == (int)numMsgs_) {
      shutdown();
    } else {
      onRecvMmsg(sock);
    }
  }

  void onNotifyDataAvailable(ShenangoAsyncUDPSocket &sock) noexcept override {
    notifyInvoked = true;
    if (useRecvmmsg_) {
      onRecvMmsg(sock);
    } else {
      onRecvMsg(sock);
    }
  }

  bool notifyInvoked{false};
  bool useRecvmmsg_{false};
  unsigned int numMsgs_{1};
};

class AsyncSocketIntegrationTest : public Test {
public:
  void SetUp() override {
    // TODO: Change n to 4.
    server = std::make_unique<UDPServer>(
        &sevb, folly::SocketAddress("127.0.0.1", 0), 1);

    // Start event loop in a separate thread
    serverThread =
        std::make_unique<rt::Thread>([this]() { sevb.loopForever(); });

    // Wait for event loop to start
    sevb.waitUntilRunning();
  }

  void startServer() {
    // Start the server
    sevb.runInEventBaseThreadAndWait([&]() { server->start(); });
    LOG(INFO) << "Server listening=" << server->address();
  }

  void TearDown() override {
    // Shutdown server
    sevb.runInEventBaseThread([&]() {
      server->shutdown();
      sevb.terminateLoopSoon();
    });

    // Wait for server thread to join
    serverThread->Join();
  }

  std::unique_ptr<UDPClient>
  performPingPongTest(folly::SocketAddress writeAddress,
                      folly::Optional<folly::SocketAddress> connectedAddress,
                      BindSocket bindSocket = BindSocket::YES);

  std::unique_ptr<UDPNotifyClient> performPingPongNotifyTest(
      folly::SocketAddress writeAddress,
      folly::Optional<folly::SocketAddress> connectedAddress,
      BindSocket bindSocket = BindSocket::YES);

  std::unique_ptr<UDPNotifyClient> performPingPongNotifyMmsgTest(
      folly::SocketAddress writeAddress, unsigned int numMsgs,
      folly::Optional<folly::SocketAddress> connectedAddress,
      BindSocket bindSocket = BindSocket::YES);

  std::unique_ptr<rt::Thread> serverThread;
  std::unique_ptr<UDPServer> server;
  folly::EventBase sevb;
  folly::EventBase cevb;
};

std::unique_ptr<UDPClient> AsyncSocketIntegrationTest::performPingPongTest(
    folly::SocketAddress writeAddress,
    folly::Optional<folly::SocketAddress> connectedAddress,
    BindSocket bindSocket) {
  auto client = std::make_unique<UDPClient>(&cevb);
  if (connectedAddress) {
    client->setShouldConnect(*connectedAddress, bindSocket);
  }
  // Start event loop in a separate thread
  auto clientThread = rt::Thread([this]() { cevb.loopForever(); });

  // Wait for event loop to start
  cevb.waitUntilRunning();

  // Send ping
  cevb.runInEventBaseThread([&]() { client->start(writeAddress, 100); });

  // Wait for client to finish
  clientThread.Join();
  return client;
}

std::unique_ptr<UDPNotifyClient>
AsyncSocketIntegrationTest::performPingPongNotifyTest(
    folly::SocketAddress writeAddress,
    folly::Optional<folly::SocketAddress> connectedAddress,
    BindSocket bindSocket) {
  auto client = std::make_unique<UDPNotifyClient>(&cevb);
  if (connectedAddress) {
    client->setShouldConnect(*connectedAddress, bindSocket);
  }
  // Start event loop in a separate thread
  auto clientThread = rt::Thread([this]() { cevb.loopForever(); });

  // Wait for event loop to start
  cevb.waitUntilRunning();

  // Send ping
  cevb.runInEventBaseThread([&]() { client->start(writeAddress, 100); });

  // Wait for client to finish
  clientThread.Join();
  return client;
}

std::unique_ptr<UDPNotifyClient>
AsyncSocketIntegrationTest::performPingPongNotifyMmsgTest(
    folly::SocketAddress writeAddress, unsigned int numMsgs,
    folly::Optional<folly::SocketAddress> connectedAddress,
    BindSocket bindSocket) {
  auto client = std::make_unique<UDPNotifyClient>(&cevb, true, numMsgs);
  if (connectedAddress) {
    client->setShouldConnect(*connectedAddress, bindSocket);
  }
  // Start event loop in a separate thread
  auto clientThread = rt::Thread([this]() { cevb.loopForever(); });

  // Wait for event loop to start
  cevb.waitUntilRunning();

  // Send ping
  cevb.runInEventBaseThread(
      [&]() { client->start(writeAddress, numMsgs, true); });

  // Wait for client to finish
  clientThread.Join();
  return client;
}

// TEST_F(AsyncSocketIntegrationTest, PingPong) {
//   startServer();
//   auto pingClient = performPingPongTest(server->address(), folly::none);
//   // This should succeed.
//   ASSERT_GT(pingClient->pongRecvd(), 0);
// }

void ServerHandler(void *arg) {
  folly::EventBase sevb;

  // TODO: Change n to 4.
  std::unique_ptr<UDPServer> server = std::make_unique<UDPServer>(
      &sevb, folly::SocketAddress("10.10.1.4", port), 1);

  // Start event loop in a separate thread
  std::unique_ptr<rt::Thread> serverThread =
      std::make_unique<rt::Thread>([&]() {
        log_info("I am event loop thread!");
        sevb.loopForever();
      });

  // Wait for event loop to start
  sevb.waitUntilRunning();

  log_info("event loop has started!");

  // Start the server
  sevb.runInEventBaseThreadAndWait([&]() { server->start(); });

  // Shutdown server
  //  sevb.runInEventBaseThread([&]() {
  //    server->shutdown();
  //    sevb.terminateLoopSoon();
  //  });

  // Wait for server thread to join
  log_info("waiting for server thread to join!");
  serverThread->Join();
}

void ClientHandler(void *arg) {
  folly::EventBase cevb;

  folly::SocketAddress writeAddress =
      folly::SocketAddress(rt::NetaddrToIPString(raddr), raddr.port);

  auto client = std::make_unique<UDPClient>(&cevb);

  // Start event loop in a separate thread
  auto clientThread = rt::Thread([&]() { cevb.loopForever(); });

  // Wait for event loop to start
  cevb.waitUntilRunning();

  // Send ping
  cevb.runInEventBaseThread([&]() { client->start(writeAddress, 100); });

  // Wait for client to finish
  clientThread.Join();

  log_info("Pong Received: %d (Should be > 0)", client->pongRecvd());
}

int StringToAddr(const char *str, uint32_t *addr) {
  uint8_t a, b, c, d;
  if (sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4)
    return -EINVAL;

  *addr = MAKE_IP_ADDR(a, b, c, d);
  return 0;
}

int main(int argc, char *argv[]) {
  int ret;

  if (argc < 3) {
    std::cerr << "usage: [cfg_file] [cmd] ..." << std::endl;
    return -EINVAL;
  }

  std::string cmd = argv[2];
  if (cmd == "server") {
    ret = runtime_init(argv[1], ServerHandler, nullptr);
    if (ret) {
      printf("failed to start runtime\n");
      return ret;
    }
  } else if (cmd != "client") {
    std::cerr << "invalid command: " << cmd << std::endl;
    return -EINVAL;
  }

  if (argc < 4) {
    std::cerr << "usage: [cfg_file] client [remote_ip]" << std::endl;
    return -EINVAL;
  }

  ret = StringToAddr(argv[3], &raddr.ip);
  if (ret)
    return -EINVAL;
  raddr.port = port;

  ret = runtime_init(argv[1], ClientHandler, nullptr);
  if (ret) {
    printf("failed to start runtime\n");
    return ret;
  }

  return 0;
}

// TEST_F(AsyncSocketIntegrationTest, PingPongNotify) {
//   startServer();
//   auto pingClient = performPingPongNotifyTest(server->address(),
//   folly::none);
//   // This should succeed.
//   ASSERT_GT(pingClient->pongRecvd(), 0);
//   ASSERT_TRUE(pingClient->notifyInvoked);
// }
//
// TEST_F(AsyncSocketIntegrationTest, PingPongNotifyMmsg) {
//   startServer();
//   auto pingClient =
//       performPingPongNotifyMmsgTest(server->address(), 10, folly::none);
//   // This should succeed.
//   ASSERT_EQ(pingClient->pongRecvd(), 10);
//   ASSERT_TRUE(pingClient->notifyInvoked);
// }
//
// class ConnectedAsyncSocketIntegrationTest
//     : public AsyncSocketIntegrationTest,
//       public WithParamInterface<BindSocket> {
// };
//
// TEST_P(ConnectedAsyncSocketIntegrationTest, ConnectedPingPong) {
//   startServer();
//   auto pingClient =
//       performPingPongTest(server->address(), server->address(), GetParam());
//   // This should succeed
//   ASSERT_GT(pingClient->pongRecvd(), 0);
// }
//
// TEST_P(
//     ConnectedAsyncSocketIntegrationTest, ConnectedPingPongServerWrongAddress)
//     {
//   startServer();
//   auto pingClient =
//       performPingPongTest(server->address(), server->address(), GetParam());
//   // This should fail.
//   ASSERT_EQ(pingClient->pongRecvd(), 0);
// }
//
// TEST_P(
//     ConnectedAsyncSocketIntegrationTest, ConnectedPingPongClientWrongAddress)
//     {
//   startServer();
//   folly::SocketAddress connectAddr(
//       server->address().getIPAddress(), server->address().getPort() + 1);
//   auto pingClient =
//       performPingPongTest(server->address(), connectAddr, GetParam());
//   // This should fail.
//   ASSERT_EQ(pingClient->pongRecvd(), 0);
//   EXPECT_TRUE(pingClient->error());
// }
//
// TEST_P(
//     ConnectedAsyncSocketIntegrationTest,
//     ConnectedPingPongDifferentWriteAddress) {
//   startServer();
//   folly::SocketAddress connectAddr(
//       server->address().getIPAddress(), server->address().getPort() + 1);
//   auto pingClient =
//       performPingPongTest(connectAddr, server->address(), GetParam());
//   // This should fail.
//   ASSERT_EQ(pingClient->pongRecvd(), 0);
//   EXPECT_TRUE(pingClient->error());
// }
//
// INSTANTIATE_TEST_CASE_P(
//     ConnectedAsyncSocketIntegrationTests,
//     ConnectedAsyncSocketIntegrationTest,
//     Values(BindSocket::YES, BindSocket::NO));
//
// TEST_F(AsyncSocketIntegrationTest, PingPongPauseResumeListening) {
//   startServer();
//
//   // Exchange should not happen when paused.
//   server->pauseAccepting();
//   EXPECT_FALSE(server->isAccepting());
//   auto pausedClient = performPingPongTest(server->address(), folly::none);
//   ASSERT_EQ(pausedClient->pongRecvd(), 0);
//
//   // Exchange does occur after resuming.
//   server->resumeAccepting();
//   EXPECT_TRUE(server->isAccepting());
//   auto pingClient = performPingPongTest(server->address(), folly::none);
//   ASSERT_GT(pingClient->pongRecvd(), 0);
// }
//
// class MockUDPReadCallback : public ShenangoAsyncUDPSocket::ReadCallback {
//  public:
//   ~MockUDPReadCallback() override = default;
//
//   MOCK_METHOD2(getReadBuffer_, void(void * *, size_t * ));
//
//   void getReadBuffer(void** buf, size_t* len) noexcept override {
//     getReadBuffer_(buf, len);
//   }
//
//   MOCK_METHOD0(shouldOnlyNotify, bool());
//
//   MOCK_METHOD1(onNotifyDataAvailable_, void(folly::ShenangoAsyncUDPSocket&));
//
//   void
//   onNotifyDataAvailable(folly::ShenangoAsyncUDPSocket& sock) noexcept
//   override {
//     onNotifyDataAvailable_(sock);
//   }
//
//   MOCK_METHOD4(
//       onDataAvailable_,
//       void(const folly::SocketAddress&, size_t, bool,
//       OnDataAvailableParams));
//
//   void onDataAvailable(
//       const folly::SocketAddress& client,
//       size_t len,
//       bool truncated,
//       OnDataAvailableParams params) noexcept override {
//     onDataAvailable_(client, len, truncated, params);
//   }
//
//   MOCK_METHOD1(onReadError_, void(const folly::AsyncSocketException&));
//
//   void onReadError(const folly::AsyncSocketException& ex) noexcept override {
//     onReadError_(ex);
//   }
//
//   MOCK_METHOD0(onReadClosed_, void());
//
//   void onReadClosed() noexcept override { onReadClosed_(); }
// };
//
// class ShenangoAsyncUDPSocketTest : public Test {
//  public:
//   void SetUp() override {
//     socket_ = std::make_shared<ShenangoAsyncUDPSocket>(&evb_);
//     addr_ = folly::SocketAddress("127.0.0.1", 0);
//     socket_->bind(addr_);
//   }
//
//   EventBase evb_;
//   MockUDPReadCallback readCb;
//   std::shared_ptr<ShenangoAsyncUDPSocket> socket_;
//   folly::SocketAddress addr_;
// };
//
// TEST_F(ShenangoAsyncUDPSocketTest, TestConnectAfterBind) {
//   socket_->connect(addr_);
// }
//
// TEST_F(ShenangoAsyncUDPSocketTest, TestConnect) {
//   ShenangoAsyncUDPSocket socket(&evb_);
//   EXPECT_FALSE(socket.isBound());
//   folly::SocketAddress address("127.0.0.1", 443);
//   socket.connect(address);
//   EXPECT_TRUE(socket.isBound());
//
//   const auto& localAddr = socket.address();
//   EXPECT_TRUE(localAddr.isInitialized());
//   EXPECT_GT(localAddr.getPort(), 0);
// }
//
// TEST_F(ShenangoAsyncUDPSocketTest, TestBound) {
//   ShenangoAsyncUDPSocket socket(&evb_);
//   EXPECT_FALSE(socket.isBound());
//   folly::SocketAddress address("0.0.0.0", 0);
//   socket.bind(address);
//   EXPECT_TRUE(socket.isBound());
// }
//
// TEST_F(ShenangoAsyncUDPSocketTest, TestBoundUnixSocket) {
//   folly::test::TemporaryDirectory tmpDirectory;
//   const auto kTmpUnixSocketPath{tmpDirectory.path() / "unix_socket_path"};
//   ShenangoAsyncUDPSocket socket(&evb_);
//   EXPECT_FALSE(socket.isBound());
//   socket.bind(folly::SocketAddress::makeFromPath(kTmpUnixSocketPath.string()));
//   EXPECT_TRUE(socket.isBound());
//   socket.close();
// }
//
// TEST_F(ShenangoAsyncUDPSocketTest, TestAttachAfterDetachEvbWithReadCallback)
// {
//   socket_->resumeRead(&readCb);
//   EXPECT_TRUE(socket_->isHandlerRegistered());
//   socket_->detachEventBase();
//   EXPECT_FALSE(socket_->isHandlerRegistered());
//   socket_->attachEventBase(&evb_);
//   EXPECT_TRUE(socket_->isHandlerRegistered());
// }
//
// TEST_F(ShenangoAsyncUDPSocketTest, TestAttachAfterDetachEvbNoReadCallback) {
//   EXPECT_FALSE(socket_->isHandlerRegistered());
//   socket_->detachEventBase();
//   EXPECT_FALSE(socket_->isHandlerRegistered());
//   socket_->attachEventBase(&evb_);
//   EXPECT_FALSE(socket_->isHandlerRegistered());
// }
//
// TEST_F(ShenangoAsyncUDPSocketTest, TestDetachAttach) {
//   folly::EventBase evb2;
//   auto writeSocket = std::make_shared<folly::ShenangoAsyncUDPSocket>(&evb_);
//   folly::SocketAddress address("127.0.0.1", 0);
//   writeSocket->bind(address);
//   std::array<uint8_t, 1024> data{};
//   std::atomic<int> packetsRecvd{0};
//   EXPECT_CALL(readCb, getReadBuffer_(_, _))
//       .WillRepeatedly(Invoke([&](void** buf, size_t* len) {
//         *buf = data.data();
//         *len = 1024;
//       }));
//   EXPECT_CALL(readCb, onDataAvailable_(_, _, _, _))
//       .WillRepeatedly(Invoke([&](const folly::SocketAddress&,
//                                  size_t,
//                                  bool,
//                                  const OnDataAvailableParams&) {
//                                  packetsRecvd++; }));
//   socket_->resumeRead(&readCb);
//   writeSocket->write(socket_->address(), folly::IOBuf::copyBuffer("hello"));
//   while (packetsRecvd != 1) {
//     evb_.loopOnce();
//   }
//   EXPECT_EQ(packetsRecvd, 1);
//
//   socket_->detachEventBase();
//   std::thread t([&] { evb2.loopForever(); });
//   evb2.runInEventBaseThreadAndWait([&] { socket_->attachEventBase(&evb2); });
//   writeSocket->write(socket_->address(), folly::IOBuf::copyBuffer("hello"));
//   auto now = std::chrono::steady_clock::now();
//   while (packetsRecvd != 2 ||
//          std::chrono::steady_clock::now() <
//          now + std::chrono::milliseconds(10)) {
//     std::this_thread::sleep_for(std::chrono::milliseconds(1));
//   }
//   evb2.runInEventBaseThread([&] {
//     socket_ = nullptr;
//     evb2.terminateLoopSoon();
//   });
//   t.join();
//   EXPECT_EQ(packetsRecvd, 2);
// }

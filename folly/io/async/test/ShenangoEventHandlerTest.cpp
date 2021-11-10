extern "C" {
#include <base/log.h>
}

#include "net.h"
#include "runtime.h"
#include "timer.h"

#include <iostream>

#include <folly/io/async/EventBase.h>
#include <folly/io/async/ShenangoEventHandler.h>
#include <folly/io/async/ShenangoAsyncUDPSocket.h>

namespace {

netaddr raddr;
constexpr int port = 8000;

class ShenangoEventHandlerMock : public folly::ShenangoEventHandler {
 public:
  ShenangoEventHandlerMock(folly::EventBase* eb, folly::NetworkSocket fd)
      : ShenangoEventHandler(eb, sock), fd_(fd) {
    fd_.data->SetNonblocking(true);
  };

 private:
  void handlerReady() noexcept override {
    int rcv;

    // Make sure to drain the queue as a callback might be executed
    // after multiple triggers.
    while (true) {
      ssize_t ret = fd_.data->ReadFrom(&rcv, sizeof(rcv), &raddr);
      if (ret != static_cast<ssize_t>(sizeof(rcv))) {
        return;
      }
      log_info("Received %d in handlerReady() call!", rcv);
    }
  }

  folly::NetworkSocket fd_;
};

void ServerHandler(void* arg) {
  folly::NetworkSocket fd = folly::netops::socket();
  fd.data = rt::UdpConn::Listen({0, port})

  folly::EventBase eb;
  ShenangoEventHandlerMock eh(&eb, fd);

  eh.registerHandler(
      folly::ShenangoEventHandler::READ | folly::ShenangoEventHandler::PERSIST);

  log_info("Press return after client is finished");
  getchar();

  eb.loop();

  sock->Shutdown();
}

void FollyClientHandler(void* arg) {
  folly::SocketAddress server_;
  std::unique_ptr<folly::ShenangoAsyncUDPSocket> socket_;

  for (int i = 0; i < 10; ++i) {
    auto ret = socket_->write(server_, folly::IOBuf::copyBuffer(
        folly::to<std::string>("PING ", i)));
    if (ret == -1) { panic("write failed!"); }
  }

  socket_->close();
}

void ClientHandler(void* arg) {
  rt::UdpConn* sock = rt::UdpConn::Dial({0, 0}, raddr);
  if (unlikely(sock == nullptr)) panic("couldn't connect to raddr!");

  int snd = 100;
  for (int i = 0; i < 10; ++i) {
    ssize_t ret = sock->Write(&snd, sizeof(snd));
    if (ret != static_cast<ssize_t>(sizeof(snd))) {
      panic("write failed, ret = %ld", ret);
    }

    ++snd;
    rt::Sleep(1 * rt::kSeconds);
  }

  sock->Shutdown();
}

int StringToAddr(const char* str, uint32_t* addr) {
  uint8_t a, b, c, d;
  if (sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4)
    return -EINVAL;

  *addr = MAKE_IP_ADDR(a, b, c, d);
  return 0;
}
} // anonymous namespace

int main(int argc, char* argv[]) {
  int ret;

  if (argc < 3) {
    std::cerr << "usage: [cfg_file] [cmd] ..." << std::endl;
    return -EINVAL;
  }

  std::string cmd = argv[2];
  if (cmd == "server") {
    ret = runtime_init(argv[1], ServerHandler, NULL);
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

  ret = runtime_init(argv[1], ClientHandler, NULL);
  if (ret) {
    printf("failed to start runtime\n");
    return ret;
  }

  return 0;
}

extern "C" {
#include <base/log.h>
}

#include "net.h"
#include "runtime.h"
#include "timer.h"

#include <iostream>

#include <folly/io/async/EventBase.h>
#include <folly/io/async/ShenangoAsyncUDPSocket.h>
#include <folly/io/async/ShenangoEventHandler.h>
#include <folly/net/ShNetworkSocket.h>

namespace {

netaddr raddr;
constexpr int port = 8000;

int StringToAddr(const char *str, uint32_t *addr);

class ShenangoEventHandlerMock : public folly::ShenangoEventHandler {
public:
  ShenangoEventHandlerMock(folly::EventBase *eb, folly::ShNetworkSocket fd)
      : ShenangoEventHandler(eb, fd), fd_(fd) {
    netaddr localAddr{0, port};
    StringToAddr("10.10.1.4", &localAddr.ip);
    fd_.data->SetNonblocking(true);
    fd_.data->Bind(&localAddr);
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

  folly::ShNetworkSocket fd_;
};

void ServerHandler(void *arg) {
  folly::ShNetworkSocket fd;
  folly::EventBase eb;

  fd = folly::shnetops::socket();
  ShenangoEventHandlerMock eh(&eb, fd);

  eh.registerHandler(folly::ShenangoEventHandler::READ |
                     folly::ShenangoEventHandler::PERSIST);

  eb.loopForever();
}

void ClientHandler(void *arg) {
  rt::UdpConn *sock = rt::UdpConn::Dial({0, 0}, raddr);
  if (unlikely(sock == nullptr))
    panic("couldn't connect to raddr!");

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

int StringToAddr(const char *str, uint32_t *addr) {
  uint8_t a, b, c, d;
  if (sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4)
    return -EINVAL;

  *addr = MAKE_IP_ADDR(a, b, c, d);
  return 0;
}
} // anonymous namespace

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

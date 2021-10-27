extern "C" {
#include <base/log.h>
}

#include "net.h"
#include "runtime.h"

#include <iostream>

#include <folly/io/async/EventBase.h>

namespace {

netaddr raddr;
constexpr int port = 8000;

//class ShenangoEventHandlerMock : public ShenangoEventHandler {
// public:
//  ShenangoEventHandlerMock(folly::EventBase *eb, rt::UdpConn* sock)
//    : ShenangoEventHandler(eb, sock) {}
//  void handlerReady(uint16_t /* events */) noexcept override {
//    // sock->ReadFrom();
//  }
//};

void ServerHandler(void *arg) {
  rt::UdpConn* sock = rt::UdpConn::Listen({0, port});
  if (!sock) panic("couldn't listen for connections");

  folly::EventBase eb;
  //ShenangoEventHandlerMock eh(&eb, &sock);
  //eh.registerHandler(ShenangoEventHandler::READ);
}

void ClientHandler(void *arg) {}

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

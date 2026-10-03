#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/socket.h>
#endif

#include <memory>

struct AVFrame;

class FrameSocketSender {
private:
#ifdef _WIN32
    using socket_t = SOCKET;
#else
    // POSIX sockets are plain file descriptors; -1 marks "no socket".
    using socket_t = int;
#endif

    socket_t sockfd;
    bool connected;

public:
    FrameSocketSender();

    bool initialize(int port);
    bool isConnected() const { return connected; }

    bool sendFrame(const std::shared_ptr<AVFrame>& frame);

    ~FrameSocketSender();
};

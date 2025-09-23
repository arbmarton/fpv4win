#pragma once

#include <windows.h>

#include <memory>

struct AVFrame;

class FrameSocketSender {
private:
    SOCKET sockfd;
    bool connected;

public:
    FrameSocketSender();

    bool initialize(int port);
    bool isConnected() const { return connected; }

    bool sendFrame(const std::shared_ptr<AVFrame>& frame);

    ~FrameSocketSender();
};
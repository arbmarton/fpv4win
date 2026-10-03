#include "FrameSocketSender.h"

#include "ffmpegDecode.h"

#include <iostream>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

// Winsock spellings mapped onto POSIX so the body below stays platform-neutral.
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
static int closesocket(int fd) { return ::close(fd); }
#endif

namespace {
bool startupSockets() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    return true;
#endif
}

void cleanupSockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}
} // namespace

FrameSocketSender::FrameSocketSender() 
    : sockfd(INVALID_SOCKET), 
    connected(false) 
{
}

FrameSocketSender::~FrameSocketSender() {
    if (sockfd != INVALID_SOCKET) {
        closesocket(sockfd);
        cleanupSockets();
    }
}

bool FrameSocketSender::initialize(int port) {
    if (!startupSockets()) {
        return false;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == INVALID_SOCKET) {
        cleanupSockets();
        return false;
    }

    // Allow socket reuse
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(sockfd, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sockfd);
        sockfd = INVALID_SOCKET;
        cleanupSockets();
        return false;
    }

    if (listen(sockfd, 1) == SOCKET_ERROR) {
        closesocket(sockfd);
        sockfd = INVALID_SOCKET;
        cleanupSockets();
        return false;
    }

    std::cout << "Waiting for Python client to connect on port " << port << "..." << std::endl;

    // Accept connection
    socket_t client = accept(sockfd, NULL, NULL);
    if (client == INVALID_SOCKET) {
        closesocket(sockfd);
        sockfd = INVALID_SOCKET;
        cleanupSockets();
        return false;
    }

    closesocket(sockfd);
    sockfd = client;
    connected = true;

    std::cout << "Python client connected!" << std::endl;
    return true;
}

bool FrameSocketSender::sendFrame(const std::shared_ptr<AVFrame>& frame) {
    if (!connected || sockfd == INVALID_SOCKET || !frame) {
        return false;
    }

    // Convert frame to RGB24 (reuse your existing conversion code)
    SwsContext* swsCtx = sws_getContext(
        frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
        frame->width, frame->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!swsCtx) {
        return false;
    }

    int rgbLinesize = frame->width * 3;
    int dataSize = rgbLinesize * frame->height;
    uint8_t* rgbBuffer = new uint8_t[dataSize];
    uint8_t* rgbData[1] = { rgbBuffer };
    int rgbLineSize[1] = { rgbLinesize };

    sws_scale(swsCtx, frame->data, frame->linesize, 0, frame->height,
        rgbData, rgbLineSize);

    // Prepare frame header
    struct FrameHeader {
        int width;
        int height;
        int dataSize;
    } header = { frame->width, frame->height, dataSize };

    bool success = true;

    // Send header first
    size_t headerSent = 0;
    while (headerSent < sizeof(header)) {
        auto result = ::send(sockfd, (char*)&header + headerSent,
            sizeof(header) - headerSent, 0);
        if (result == SOCKET_ERROR) {
            success = false;
            break;
        }
        headerSent += result;
    }

    // Send frame data
    if (success) {
        int dataSent = 0;
        while (dataSent < dataSize) {
            auto result = ::send(sockfd, (char*)rgbBuffer + dataSent,
                dataSize - dataSent, 0);
            if (result == SOCKET_ERROR) {
                success = false;
                break;
            }
            dataSent += result;
        }
    }

    // Cleanup
    delete[] rgbBuffer;
    sws_freeContext(swsCtx);

    if (!success) {
        connected = false;
        std::cout << "Connection lost to Python client" << std::endl;
    }

    return success;
}

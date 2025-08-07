#include "FrameSocketSender.h"

#include "ffmpegDecode.h"

#include <iostream>

FrameSocketSender::FrameSocketSender() 
    : sockfd(INVALID_SOCKET), 
    connected(false) 
{
}

FrameSocketSender::~FrameSocketSender() {
    if (sockfd != INVALID_SOCKET) {
        closesocket(sockfd);
        WSACleanup();
    }
}

bool FrameSocketSender::initialize(int port) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == INVALID_SOCKET) {
        WSACleanup();
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
        WSACleanup();
        return false;
    }

    if (listen(sockfd, 1) == SOCKET_ERROR) {
        closesocket(sockfd);
        WSACleanup();
        return false;
    }

    std::cout << "Waiting for Python client to connect on port " << port << "..." << std::endl;

    // Accept connection
    SOCKET client = accept(sockfd, NULL, NULL);
    if (client == INVALID_SOCKET) {
        closesocket(sockfd);
        WSACleanup();
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
    int headerSent = 0;
    while (headerSent < sizeof(header)) {
        int result = ::send(sockfd, (char*)&header + headerSent,
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
            int result = ::send(sockfd, (char*)rgbBuffer + dataSent,
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
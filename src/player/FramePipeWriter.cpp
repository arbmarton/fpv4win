#ifdef _WIN32

#include "FramePipeWriter.h"

#include "ffmpegDecode.h"

#include <iostream>


FramePipeWriter::FramePipeWriter(const std::string& name)
    : pipeName("\\\\.\\pipe\\" + name)
    , hPipe(INVALID_HANDLE_VALUE) {
}

FramePipeWriter::~FramePipeWriter() {
    if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
    }
}

bool FramePipeWriter::initialize() {
    hPipe = CreateNamedPipe(
        pipeName.c_str(),
        PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_BYTE | PIPE_WAIT,
        1, // max instances
        1024 * 1024, // output buffer size (1MB)
        0, // input buffer size
        0, // timeout
        NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Wait for client to connect
    return ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED;
}

bool FramePipeWriter::sendFrame(const std::shared_ptr<AVFrame>& frame) {
    if (hPipe == INVALID_HANDLE_VALUE || !frame) return false;

    // Convert frame to RGB24 (reuse your existing conversion code)
    SwsContext* swsCtx = sws_getContext(
        frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
        frame->width, frame->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!swsCtx) return false;

    int rgbLinesize = frame->width * 3;
    uint8_t* rgbBuffer = new uint8_t[rgbLinesize * frame->height];
    uint8_t* rgbData[1] = { rgbBuffer };
    int rgbLineSize[1] = { rgbLinesize };

    sws_scale(swsCtx, frame->data, frame->linesize, 0, frame->height,
        rgbData, rgbLineSize);

    // Send frame metadata first
    struct FrameHeader {
        int width;
        int height;
        int dataSize;
    } header = { frame->width, frame->height, rgbLinesize * frame->height };

    DWORD bytesWritten;
    bool success = WriteFile(hPipe, &header, sizeof(header), &bytesWritten, NULL);

    if (success) {
        success = WriteFile(hPipe, rgbBuffer, header.dataSize, &bytesWritten, NULL);
    }

    delete[] rgbBuffer;
    sws_freeContext(swsCtx);

    return success;
}

#endif // _WIN32

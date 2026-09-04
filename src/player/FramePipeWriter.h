#pragma once

#ifdef _WIN32

#include <windows.h>
#include <string>
#include <memory>

struct AVFrame;

class FramePipeWriter {
private:
    HANDLE hPipe;
    std::string pipeName;

public:
    FramePipeWriter(const std::string& name = "frame_pipe");
    ~FramePipeWriter();

    bool initialize();
    bool sendFrame(const std::shared_ptr<AVFrame>& frame);
};

#endif // _WIN32

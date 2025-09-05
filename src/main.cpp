#include "src/QmlNativeAPI.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <player/QQuickRealTimePlayer.h>

#include "player/FrameSocketSender.h"

#include <chrono>
#include <memory>

#pragma comment(lib, "ws2_32.lib")
#define HEADLESS_MODE TRUE

#ifdef DEBUG_MODE
#include <DbgHelp.h>
#pragma comment(lib, "DbgHelp.lib")
// 创建Dump文件
void CreateDumpFile(LPCWSTR lpstrDumpFilePathName, EXCEPTION_POINTERS *pException) {
    HANDLE hDumpFile = CreateFile(
        reinterpret_cast<LPCSTR>(lpstrDumpFilePathName), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    // Dump信息
    MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
    dumpInfo.ExceptionPointers = pException;
    dumpInfo.ThreadId = GetCurrentThreadId();
    dumpInfo.ClientPointers = TRUE;
    // 写入Dump文件内容
    MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpNormal, &dumpInfo, nullptr, nullptr);
    CloseHandle(hDumpFile);
}
// 处理Unhandled Exception的回调函数
LONG ApplicationCrashHandler(EXCEPTION_POINTERS *pException) {
    CreateDumpFile(L"dump.dmp", pException);
    return EXCEPTION_EXECUTE_HANDLER;
}

#endif

bool playStop = false;
shared_ptr<FFmpegDecoder> decoder;

std::chrono::steady_clock::time_point last_frame_send = std::chrono::steady_clock::now();

void start_decode_thread(const int image_send_frequency_ms) {
    std::thread decodeThread([image_send_frequency_ms]() {
        std::unique_ptr<FrameSocketSender> frameSender = std::make_unique<FrameSocketSender>();
        auto decoder = make_shared<FFmpegDecoder>();
    
        std::string url = "sdp/sdp.sdp";
        bool ok = decoder->OpenInput(url);
        if (!ok) {
            std::cout << "error\n";
            return;
        }
        while (!playStop) {
            try {
                auto frame = decoder->GetNextFrame();
                if (!frame) {
                    continue;
                }
                if (!frameSender->isConnected()) {
                    frameSender->initialize();
                }
    
                std::chrono::steady_clock::time_point current_time = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_frame_send).count() > image_send_frequency_ms) {
                   frameSender->sendFrame(frame);
                   last_frame_send = std::chrono::steady_clock::now();
                }
            }
            catch (const exception& e) {
                std::cout << e.what();
                break;
            }
        }
        playStop = true;
    });
    decodeThread.detach();
}

void initialize_acquisition(const int image_send_frequency_ms) {
    // Copied from QmlNativeAPI::Start
    const QString vidPid = "0bda:8812";
    const int channelWidth = 0;
    const int channel = 161;
    const QString keyPath = "gs.key";
    const QString codec = "AUTO";
    mINI::Instance()[CONFIG_CHANNEL] = channel;
    mINI::Instance()[CONFIG_CHANNEL_WIDTH] = channelWidth;
    mINI::Instance()[CONFIG_CHANNEL_KEY] = keyPath.toStdString();
    mINI::Instance()[CONFIG_CHANNEL_CODEC] = codec.toStdString();
    mINI::Instance().dumpFile(CONFIG_FILE);
    QmlNativeAPI::Instance().playerPort = QmlNativeAPI::Instance().GetFreePort();
    QmlNativeAPI::Instance().playerCodec = codec;
    WFBReceiver::Instance().Start(vidPid.toStdString(), channel, channelWidth, keyPath.toStdString());

    QObject::connect(&QmlNativeAPI::Instance(), &QmlNativeAPI::onRtpStream, [image_send_frequency_ms]() {
        start_decode_thread(image_send_frequency_ms);
    });
    while (!playStop) {
        sleep(0.1);
    }
}

int main(int argc, char *argv[]) {
#ifdef DEBUG_MODE
    SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)ApplicationCrashHandler);
#endif
#if HEADLESS_MODE
    int image_send_frequency_ms = -1;
    if (argc > 1) {
        image_send_frequency_ms = std::stoi(argv[1]);
    }
    std::cout << "Image Send Frequency(ms): " << image_send_frequency_ms << std::endl;
    initialize_acquisition(image_send_frequency_ms);
    return 0;
#else
    QGuiApplication app(argc, argv);
    
    QQmlApplicationEngine engine;
    
    qmlRegisterType<QQuickRealTimePlayer>("realTimePlayer", 1, 0, "QQuickRealTimePlayer");
    
    auto &qmlNativeApi = QmlNativeAPI::Instance();
    engine.rootContext()->setContextProperty("NativeApi", &qmlNativeApi);
    
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    
    return QGuiApplication::exec();
#endif
}

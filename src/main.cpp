#include "src/QmlNativeAPI.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <player/QQuickRealTimePlayer.h>

#include "player/FrameSocketSender.h"

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

QString url = "sdp/sdp.sdp";
std::thread analysisThread;
bool playStop = false;
shared_ptr<FFmpegDecoder> decoder;
std::mutex mtx;
FrameSocketSender* frameSender = new FrameSocketSender();
std::queue<shared_ptr<AVFrame>> videoFrameQueue;
volatile bool isMuted = true;

void play() {
    analysisThread = std::thread([]() {
        auto decoder_ = make_shared<FFmpegDecoder>();
        // 打开并分析输入
        std::string asd = url.toStdString();
        bool ok = decoder_->OpenInput(asd);
        if (!ok) {
            std::cout << "error\n";
            return;
        }
        {
            std::lock_guard<std::mutex> lck(mtx);
            decoder = decoder_;
        }

        std::thread decodeThread([]() {
            while (!playStop) {
                try {
                    auto frame = decoder->GetNextFrame();
                    if (!frame) {
                        continue;
                    }
                    if (!frameSender->isConnected()) {
                        frameSender->initialize();
                    }
                    frameSender->sendFrame(frame);
                    {
                        lock_guard<mutex> lck(mtx);
                        if (videoFrameQueue.size() > 10) {
                            videoFrameQueue.pop();
                        }
                        videoFrameQueue.push(frame);
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
        });
    analysisThread.detach();
}

void test() {
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

    QObject::connect(&QmlNativeAPI::Instance(), &QmlNativeAPI::onRtpStream, []() {
        play();
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
    test();
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

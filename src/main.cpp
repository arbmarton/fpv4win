#include "src/QmlNativeAPI.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <player/QQuickRealTimePlayer.h>

#include "player/FrameSocketSender.h"

#pragma comment(lib, "ws2_32.lib")

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



void play() {
    QString url = "sdp/sdp.sdp";
    std::thread analysisThread;
    bool playStop = false;
    shared_ptr<FFmpegDecoder> decoder;
    std::thread decodeThread;
    std::mutex mtx;
    FrameSocketSender* frameSender = new FrameSocketSender();
    std::queue<shared_ptr<AVFrame>> videoFrameQueue;
    volatile bool isMuted = true;

    // 启动分析线程
    analysisThread = std::thread([&, url, frameSender]() {
        auto decoder_ = make_shared<FFmpegDecoder>();
        // 打开并分析输入
        std::string asd = url.toStdString();
        bool ok = decoder_->OpenInput(asd);
        if (!ok) {
            //emit onError("视频加载出错", -2);
            std::cout << "error\n";
            return;
        }
        {   // critical section
            std::lock_guard<std::mutex> lck(mtx);
            decoder = decoder_;
        }
        // 启动解码线程
        decodeThread = std::thread([&playStop, &decoder, frameSender, &videoFrameQueue, &mtx]() {
            while (!playStop) {
                try {
                    // 循环解码
                    auto frame = decoder->GetNextFrame();
                    if (!frame) {
                        continue;
                    }
                    if (!frameSender->isConnected()) {
                        frameSender->initialize();
                    }
                    //SaveFrameAsBMP(frame, "last_frame.bmp");
                    //frameWriter.sendFrame(frame);
                    frameSender->sendFrame(frame);
                    {
                        // 解码获取到视频帧,放入帧缓冲队列
                        lock_guard<mutex> lck(mtx);
                        if (videoFrameQueue.size() > 10) {
                            videoFrameQueue.pop();
                        }
                        videoFrameQueue.push(frame);
                    }
                }
                catch (const exception& e) {
                    //emit onError(e.what(), -2);
                    // 出错，停止
                    std::cout << e.what();
                    break;
                }
            }
            playStop = true;
            // 解码已经停止，触发信号
            //emit onPlayStopped();
            });
        decodeThread.detach();

        //if (decoder->HasVideo()) {
            //onVideoInfoReady(decoder->GetWidth(), decoder->GetHeight(), decoder->GetVideoFrameFormat());
        //}

        // 码率计算回调
        //decoder->onBitrate = [this](uint64_t bitrate) { emit onBitrate(static_cast<long>(bitrate)); };
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
    while (true) {
        sleep(0.1);
    }
}

int main(int argc, char *argv[]) {
#ifdef DEBUG_MODE
    SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)ApplicationCrashHandler);
#endif

    //QGuiApplication app(argc, argv);
    //
    //QQmlApplicationEngine engine;
    //
    //qmlRegisterType<QQuickRealTimePlayer>("realTimePlayer", 1, 0, "QQuickRealTimePlayer");
    //
    //auto &qmlNativeApi = QmlNativeAPI::Instance();
    //engine.rootContext()->setContextProperty("NativeApi", &qmlNativeApi);
    //
    //engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    //
    //return QGuiApplication::exec();


    test();
    return 0;
}

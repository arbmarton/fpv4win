#include "src/QmlNativeAPI.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <player/QQuickRealTimePlayer.h>

#include "player/FrameSocketSender.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#endif

#if defined(DEBUG_MODE) && defined(_WIN32)
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

std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    return ss.str();
}

void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}

std::string createSdpFileForPortNumber(const std::string& inputFilePath, const int portNumber) {
    std::string replacementNumber = std::to_string(portNumber);

    // Read the file
    std::ifstream inputFile(inputFilePath);
    if (!inputFile.is_open()) {
        std::cerr << "Error: Could not open input file: " << inputFilePath << std::endl;
        return "";
    }

    // Read entire file content
    std::stringstream buffer;
    buffer << inputFile.rdbuf();
    std::string fileContent = buffer.str();
    inputFile.close();

    // Replace the string
    replaceAll(fileContent, "52356", replacementNumber);

    // Generate output filename with timestamp
    std::string timestamp = getCurrentTimestamp();
    std::string outputFileName = "sdp_" + std::to_string(portNumber) + ".sdp";

    // Write to new file
    std::ofstream outputFile(outputFileName);
    if (!outputFile.is_open()) {
        std::cerr << "Error: Could not create output file: " << outputFileName << std::endl;
        return "";
    }

    outputFile << fileContent;
    outputFile.close();

    std::cout << "File processed successfully!" << std::endl;
    std::cout << "Output saved to: " << outputFileName << std::endl;
    std::cout << "Replaced all occurrences of '52356' with '" << replacementNumber << "'" << std::endl;

    return outputFileName;
}

void start_decode_thread(const int udp_port, const int python_port, const int image_send_frequency_ms) {
    std::thread decodeThread([udp_port, python_port, image_send_frequency_ms]() {
        const std::unique_ptr<FrameSocketSender> frameSender = std::make_unique<FrameSocketSender>();
        const std::unique_ptr<FFmpegDecoder> decoder = std::make_unique<FFmpegDecoder>();
    
        std::string baseSdpFile = "sdp/sdp.sdp";
        std::string sdpFileForPort = "sdp_" + std::to_string(udp_port) + ".sdp";
        if (!std::filesystem::exists(sdpFileForPort)) {
            createSdpFileForPortNumber(baseSdpFile, udp_port);
        }
        const bool ok = decoder->OpenInput(sdpFileForPort);
        if (!ok) {
            std::cout << "error opening input\n";
            return;
        }
        std::chrono::steady_clock::time_point last_frame_send_time = std::chrono::steady_clock::now();
        while (!playStop) {
            try {
                const auto frame = decoder->GetNextFrame();
                if (!frame) {
                    continue;
                }
                if (!frameSender->isConnected()) {
                    frameSender->initialize(python_port);
                }
    
                std::chrono::steady_clock::time_point current_time = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_frame_send_time).count() > image_send_frequency_ms) {
                   frameSender->sendFrame(frame);
                   last_frame_send_time = std::chrono::steady_clock::now();
                }
            }
            catch (const exception& e) {
                std::cout << e.what() << '\n';
                //break;
            }
        }
        playStop = true;
    });
    decodeThread.detach();
}

void initialize_acquisition(const UsbDeviceId usbDeviceId, const int udp_port, const int python_port, const int channel, const int image_send_frequency_ms) {
    // Copied from QmlNativeAPI::Start
    const QString vidPid = "0bda:8812";
    const int channelWidth = 0;
    const QString keyPath = "gs.key";
    const QString codec = "AUTO";
    mINI::Instance()[CONFIG_CHANNEL] = channel;
    mINI::Instance()[CONFIG_CHANNEL_WIDTH] = channelWidth;
    mINI::Instance()[CONFIG_CHANNEL_KEY] = keyPath.toStdString();
    mINI::Instance()[CONFIG_CHANNEL_CODEC] = codec.toStdString();
    mINI::Instance().dumpFile(CONFIG_FILE);
    QmlNativeAPI::Instance().playerPort = udp_port;
    QmlNativeAPI::Instance().playerCodec = codec;
    WFBReceiver::Instance().StartWithDeviceId(vidPid.toStdString(), usbDeviceId, channel, channelWidth, keyPath.toStdString());

    QObject::connect(&QmlNativeAPI::Instance(), &QmlNativeAPI::onRtpStream, [udp_port, python_port, image_send_frequency_ms]() {
        start_decode_thread(udp_port, python_port, image_send_frequency_ms);
    });
    while (!playStop) {
        sleep(0.1);
    }
}

UsbDeviceId parseUsbDescriptor(const std::string& str) {
    UsbDeviceId dev;
    std::stringstream ss(str);
    std::string token;

    // bus
    if (!std::getline(ss, token, ',')) throw std::runtime_error("Parse error: missing bus");
    dev.bus = static_cast<uint8_t>(std::stoi(token));

    // address
    if (!std::getline(ss, token, ',')) throw std::runtime_error("Parse error: missing address");
    dev.address = static_cast<uint8_t>(std::stoi(token));

    // port_numbers (split by '-')
    if (!std::getline(ss, token, ',')) throw std::runtime_error("Parse error: missing port path");
    std::stringstream portStream(token);
    std::string portToken;
    while (std::getline(portStream, portToken, '-')) {
        dev.portPath.push_back(static_cast<uint8_t>(std::stoi(portToken)));
    }

    // serial (rest of line)
    if (!std::getline(ss, dev.serial)) throw std::runtime_error("Parse error: missing serial");

    return dev;
}

int main(int argc, char *argv[]) {
#if defined(DEBUG_MODE) && defined(_WIN32)
    SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)ApplicationCrashHandler);
#endif
    bool headless = false;
    int image_send_frequency_ms = -1;
    int channel = -1;
    UsbDeviceId usbDeviceId;
    int udp_port = -1;
    int python_port = -1;

    std::cout << "argc: " << argc << std::endl;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::cout << "arg: " << arg << std::endl;

        if (arg == "--headless") {
            const std::string val = argv[++i];
            headless = val == "true";
        }
        else if (arg == "--image_send_frequency_ms") {
            image_send_frequency_ms = std::stoi(argv[++i]);
        }
        else if (arg == "--channel") {
            channel = std::stoi(argv[++i]);
        }
        else if (arg == "--usb_device_identifier") {
            std::string device_id = argv[++i];
            std::cout << "Device ID: " << device_id << std::endl;
            usbDeviceId = parseUsbDescriptor(device_id);
        }
        else if (arg == "--udp_port") {
            udp_port = std::stoi(argv[++i]);
        }
        else if (arg == "--python_port") {
            python_port = std::stoi(argv[++i]);
        }
    }

    if (headless == true) {
        std::cout << "Headless Mode Enabled" << std::endl;
        std::cout << "Channel: " << channel << std::endl;
        std::cout << "Image Send Frequency(ms): " << image_send_frequency_ms << std::endl;
        std::cout << "Python port: " << python_port << "\n";
        std::cout << "UDP port: " << udp_port << "\n";

        initialize_acquisition(usbDeviceId, udp_port, python_port, channel, image_send_frequency_ms);
        return 0;
    }
    else {
        QGuiApplication app(argc, argv);

        QQmlApplicationEngine engine;

        qmlRegisterType<QQuickRealTimePlayer>("realTimePlayer", 1, 0, "QQuickRealTimePlayer");

        auto& qmlNativeApi = QmlNativeAPI::Instance();
        engine.rootContext()->setContextProperty("NativeApi", &qmlNativeApi);

        engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));

        return QGuiApplication::exec();
    }
}

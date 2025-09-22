#include "src/QmlNativeAPI.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <player/QQuickRealTimePlayer.h>

#include "player/FrameSocketSender.h"

#include <chrono>
#include <memory>

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

bool playStop = false;

void start_decode_thread(const int image_send_frequency_ms) {
    std::thread decodeThread([image_send_frequency_ms]() {
        const std::unique_ptr<FrameSocketSender> frameSender = std::make_unique<FrameSocketSender>();
        const std::unique_ptr<FFmpegDecoder> decoder = std::make_unique<FFmpegDecoder>();
    
        std::string url = "sdp/sdp.sdp";
        const bool ok = decoder->OpenInput(url);
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
                    frameSender->initialize();
                }
    
                std::chrono::steady_clock::time_point current_time = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_frame_send_time).count() > image_send_frequency_ms) {
                   frameSender->sendFrame(frame);
                   last_frame_send_time = std::chrono::steady_clock::now();
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

void initialize_acquisition(const UsbDeviceId usbDeviceId, const int channel, const int image_send_frequency_ms) {
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
    QmlNativeAPI::Instance().playerPort = QmlNativeAPI::Instance().GetFreePort();
    QmlNativeAPI::Instance().playerCodec = codec;
    WFBReceiver::Instance().StartWithDeviceId(vidPid.toStdString(), usbDeviceId, channel, channelWidth, keyPath.toStdString());

    QObject::connect(&QmlNativeAPI::Instance(), &QmlNativeAPI::onRtpStream, [image_send_frequency_ms]() {
        start_decode_thread(image_send_frequency_ms);
    });
    while (!playStop) {
        sleep(0.1);
    }
}

void test_libusb_enumeration() {
#define VID 0x0bda
#define PID 0x8812
    libusb_context* ctx = NULL;
    libusb_device** list = NULL;
    ssize_t cnt;
    int rc;

    rc = libusb_init(&ctx);
    if (rc) {
        fprintf(stderr, "libusb_init failed: %d\n", rc);
        return;
    }

    cnt = libusb_get_device_list(ctx, &list);
    if (cnt < 0) {
        fprintf(stderr, "get_device_list failed: %zd\n", cnt);
        libusb_exit(ctx);
        return;
    }

    printf("Found %zd libusb devices\n", cnt);

    for (ssize_t i = 0; i < cnt; ++i) {
        libusb_device* dev = list[i];
        struct libusb_device_descriptor desc;
        rc = libusb_get_device_descriptor(dev, &desc);
        if (rc != 0) continue;

        if (desc.idVendor == VID && desc.idProduct == PID) {
            libusb_device_handle* handle = NULL;
            rc = libusb_open(dev, &handle);
            if (rc != 0 || handle == NULL) {
                fprintf(stderr, "Could not open device (bus %u addr %u): %s\n",
                    libusb_get_bus_number(dev), libusb_get_device_address(dev),
                    libusb_error_name(rc));
                continue;
            }

            // Optional: show bus/address/ports
            uint8_t ports[8];
            int port_count = libusb_get_port_numbers(dev, ports, sizeof(ports));
            printf("Opened device: bus %u addr %u",
                libusb_get_bus_number(dev), libusb_get_device_address(dev));
            if (port_count > 0) {
                printf(", port path:");
                for (int p = 0; p < port_count; ++p) printf(" %u", ports[p]);
            }
            printf("\n");

            // Try to read serial string (if device provides it)
            if (desc.iSerialNumber) {
                unsigned char serial[256];
                rc = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, serial, sizeof(serial));
                if (rc > 0) {
                    printf("  serial: %s\n", (char*)serial);
                }
                else {
                    printf("  serial: <unavailable> (err %d)\n", rc);
                }
            }

            // If kernel driver is attached on Linux you may need to detach:
#if defined(__linux__)
            if (libusb_kernel_driver_active(handle, 0) == 1) {
                rc = libusb_detach_kernel_driver(handle, 0);
                if (rc == 0) printf("  detached kernel driver from iface 0\n");
                else printf("  failed to detach kernel driver: %s\n", libusb_error_name(rc));
            }
#endif

            // Claim interface (adjust interface number to your device)
            rc = libusb_claim_interface(handle, 0);
            if (rc == 0) {
                printf("  claimed interface 0\n");
            }
            else {
                printf("  claim interface failed: %s\n", libusb_error_name(rc));
                // depending on device you might continue anyway
            }

            // store handles somewhere in your real code; here we immediately release
            libusb_release_interface(handle, 0);
            libusb_close(handle);
        }
    }

    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
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
#ifdef DEBUG_MODE
    SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)ApplicationCrashHandler);
#endif
    bool headless = false;
    int image_send_frequency_ms = -1;
    int channel = -1;
    UsbDeviceId usbDeviceId;

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
    }

    if (headless == true) {
        std::cout << "Headless Mode Enabled" << std::endl;
        std::cout << "Channel: " << channel << std::endl;
        std::cout << "Image Send Frequency(ms): " << image_send_frequency_ms << std::endl;

        initialize_acquisition(usbDeviceId, channel, image_send_frequency_ms);
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

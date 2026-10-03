// End-to-end render test: plays an input through QQuickRealTimePlayer (decoder -> frame queue ->
// QQuickFramebufferObject GL renderer) inside a real Qt Quick window, then grabs the window to a PNG.
#include "player/QQuickRealTimePlayer.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QImage>
#include <cstdio>

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: test_render <input> <out.png> [seconds]\n"); return 2; }
    QString input = argv[1], out = argv[2];
    int secs = argc > 3 ? atoi(argv[3]) : 3;
    QGuiApplication app(argc, argv);
    qmlRegisterType<QQuickRealTimePlayer>("realTimePlayer", 1, 0, "QQuickRealTimePlayer");
    QQmlApplicationEngine engine;
    engine.loadData(R"(
import QtQuick 2.15
import QtQuick.Window 2.15
import realTimePlayer 1.0
Window { id: w; visible: true; width: 960; height: 540; color: "#123456"
  QQuickRealTimePlayer { id: player; objectName: "player"; anchors.fill: parent } }
)");
    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *player = win->findChild<QQuickRealTimePlayer *>("player");
    int errors = 0, stops = 0;
    QObject::connect(player, &QQuickRealTimePlayer::onError, [&](QString m, int c) { errors++; fprintf(stderr, "onError: %s (%d)\n", m.toUtf8().constData(), c); });
    QObject::connect(player, &QQuickRealTimePlayer::onPlayStopped, [&]() { stops++; fprintf(stderr, "onPlayStopped\n"); });
    player->play(input);
    int rc = 1;
    QTimer::singleShot(secs * 1000, [&]() {
        QImage img = win->grabWindow();
        // grabWindow() returns a null image if the window is not exposed yet; give it a moment.
        for (int i = 0; i < 20 && img.isNull(); i++) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 250);
            img = win->grabWindow();
        }
        img.save(out);
        // Sanity: the frame must have replaced the (deliberately odd) window colour with video content.
        int magenta = 0, dark = 0, total = 0;
        for (int y = 0; y < img.height(); y += 4) for (int x = 0; x < img.width(); x += 4) {
            QRgb p = img.pixel(x, y); total++;
            if (qRed(p) == 0x12 && qGreen(p) == 0x34 && qBlue(p) == 0x56) magenta++;
            if (qRed(p) < 16 && qGreen(p) < 16 && qBlue(p) < 16) dark++;
        }
        printf("grab %dx%d: %.1f%% background, %.1f%% black, videoSize=%dx%d fmt=%d errors=%d stops=%d\n",
               img.width(), img.height(), 100.0 * magenta / total, 100.0 * dark / total, player->videoWidth(),
               player->videoHeght(), player->videoFormat(), errors, stops);
        bool ok = magenta == 0 && dark < total * 0.9 && errors == 0 && stops == 0 && player->videoWidth() > 0;
        printf("%s\n", ok ? "PASS" : "FAIL");
        rc = ok ? 0 : 1;
        player->stop();
        app.quit();
    });
    app.exec();
    return rc;
}

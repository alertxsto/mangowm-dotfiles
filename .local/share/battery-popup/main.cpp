#include "batterycontroller.h"

#include <QCursor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScreen>
#include <QSGRendererInterface>
#include <QTextStream>
#include <QThread>
#include <QVariantMap>
#include <unistd.h>

namespace {
QString socketName() {
    return QStringLiteral("battery-popup-") + QString::number(getuid());
}

bool closeRunningPopup() {
    QLocalSocket socket;
    socket.connectToServer(socketName());
    if (!socket.waitForConnected(80))
        return false;
    socket.write("close");
    return socket.waitForBytesWritten(80);
}

QVariantMap loadPalette(const QString &path) {
    // Graphite from the approved reference, before any active theme overrides.
    QVariantMap palette{
        {"primary", "#b8d9b0"}, {"onPrimary", "#141817"},
        {"surface", "#141817"}, {"surfaceContainer", "#1c2220"},
        {"surfaceContainerHigh", "#242c28"}, {"onSurface", "#e8ece8"},
        {"onSurfaceVariant", "#a0aaa3"}, {"outlineVariant", "#343d37"}
    };
    static const QHash<QString, QString> keys{
        {"primary", "primary"}, {"on-primary", "onPrimary"},
        {"surface", "surface"}, {"surface-container", "surfaceContainer"},
        {"surface-container-high", "surfaceContainerHigh"},
        {"on-surface", "onSurface"}, {"on-surface-variant", "onSurfaceVariant"},
        {"outline-variant", "outlineVariant"}
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return palette;
    const QRegularExpression token(QStringLiteral(R"(^\s*([a-z-]+):\s*(#[0-9a-fA-F]{6,8});)"));
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const auto match = token.match(stream.readLine());
        if (match.hasMatch()) {
            const auto key = keys.constFind(match.captured(1));
            if (key != keys.cend())
                palette[*key] = match.captured(2);
        }
    }
    return palette;
}
}

int main(int argc, char **argv) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("battery-popup"));
    QGuiApplication::setDesktopFileName(QStringLiteral("battery-popup"));
    app.setQuitOnLastWindowClosed(true);

    if (closeRunningPopup())
        return 0;

    QLockFile lock(QDir::tempPath() + QLatin1Char('/') + socketName() + QStringLiteral(".lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) {
        for (int attempt = 0; attempt < 10; ++attempt) {
            QThread::msleep(25);
            if (closeRunningPopup())
                return 0;
        }
        return 1;
    }

    QLocalServer::removeServer(socketName());
    QLocalServer server;
    if (!server.listen(socketName()))
        return 1;
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        while (QLocalSocket *socket = server.nextPendingConnection()) {
            const auto handleMessage = [socket] {
                if (socket->readAll().contains("close"))
                    QCoreApplication::quit();
                socket->disconnectFromServer();
            };
            QObject::connect(socket, &QLocalSocket::readyRead, socket, handleMessage);
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            if (socket->bytesAvailable())
                handleMessage();
        }
    });

    BatteryController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("batteryController"), &controller);
    engine.rootContext()->setContextProperty(
        QStringLiteral("themeColors"), loadPalette(QDir::homePath() + QStringLiteral("/.config/rofi/colors.rasi")));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app, [](QObject *object, const QUrl &) {
        auto *window = qobject_cast<QQuickWindow *>(object);
        if (!window)
            return;
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen)
            window->setScreen(screen);
    });
    engine.load(QUrl::fromLocalFile(QDir::homePath() + QStringLiteral("/.local/share/battery-popup/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}

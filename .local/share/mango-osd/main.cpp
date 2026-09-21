#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QVariantMap>
#include <unistd.h>

namespace {
QString socketName() {
    return "mango-osd-" + QString::number(getuid());
}

QVariantMap loadPalette(const QString &path) {
    QVariantMap palette{
        {"primary", "#c1cab5"},
        {"primaryContainer", "#565e4d"},
        {"onPrimaryContainer", "#ffffff"},
        {"surfaceContainerHigh", "#2c2d2a"},
        {"onSurface", "#e4e2df"},
        {"onSurfaceVariant", "#c8c6c3"},
        {"outlineVariant", "#5c5c59"}
    };
    static const QHash<QString, QString> keys{
        {"primary", "primary"}, {"primary-container", "primaryContainer"},
        {"on-primary-container", "onPrimaryContainer"},
        {"surface-container-high", "surfaceContainerHigh"},
        {"on-surface", "onSurface"},
        {"on-surface-variant", "onSurfaceVariant"},
        {"outline-variant", "outlineVariant"}
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return palette;
    const QRegularExpression token(R"(^\s*([a-z-]+):\s*(#[0-9a-fA-F]{6,8});)");
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const auto match = token.match(stream.readLine());
        if (match.hasMatch() && keys.contains(match.captured(1)))
            palette[keys.value(match.captured(1))] = match.captured(2);
    }
    return palette;
}

int sendMessage(const QStringList &arguments) {
    if (arguments.size() < 3)
        return 2;

    const QByteArray payload = arguments.mid(1).join(':').toUtf8();
    QLocalSocket socket;
    socket.connectToServer(socketName());
    if (!socket.waitForConnected(60)) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--server"});
        for (int attempt = 0; attempt < 10 && socket.state() != QLocalSocket::ConnectedState; ++attempt) {
            QThread::msleep(30);
            socket.abort();
            socket.connectToServer(socketName());
            socket.waitForConnected(60);
        }
    }
    if (socket.state() != QLocalSocket::ConnectedState)
        return 1;
    socket.write(payload);
    return socket.waitForBytesWritten(100) ? 0 : 1;
}
}

int main(int argc, char *argv[]) {
    const QStringList rawArguments = [&] {
        QStringList values;
        values.reserve(argc);
        for (int index = 0; index < argc; ++index)
            values.push_back(QString::fromLocal8Bit(argv[index]));
        return values;
    }();

    if (argc > 1 && rawArguments.value(1) != "--server") {
        QCoreApplication app(argc, argv);
        return sendMessage(rawArguments);
    }

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationDisplayName("Mango OSD");
    QGuiApplication::setDesktopFileName("mango-osd");

    const QString home = QDir::homePath();
    const QString palettePath = home + "/.config/rofi/colors.rasi";
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("themeColors", loadPalette(palettePath));
    engine.load(QUrl::fromLocalFile(home + "/.local/share/mango-osd/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    QObject *root = engine.rootObjects().constFirst();

    QLocalServer::removeServer(socketName());
    QLocalServer server;
    if (!server.listen(socketName()))
        return 1;

    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        while (QLocalSocket *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, root, palettePath] {
                const QStringList fields = QString::fromUtf8(socket->readAll()).split(':');
                if (fields.size() >= 2) {
                    root->setProperty("colors", loadPalette(palettePath));
                    root->setProperty("osdKind", fields.value(0));
                    root->setProperty("osdValue", fields.value(1).toInt());
                    root->setProperty("osdMuted", fields.value(2) == "1");
                    QMetaObject::invokeMethod(root, "showOsd");
                }
                socket->disconnectFromServer();
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    return app.exec();
}

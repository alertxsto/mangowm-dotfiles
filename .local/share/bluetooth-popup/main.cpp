#include <algorithm>
#include <functional>
#include <memory>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QVariantList>
#include <unistd.h>

namespace {
QString socketName() {
    return "bluetooth-popup-" + QString::number(getuid());
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
    QVariantMap palette{
        {"primary", "#d5e17d"}, {"onPrimary", "#20220f"},
        {"surface", "#171811"}, {"surfaceContainer", "#23251c"},
        {"surfaceContainerHigh", "#2d3025"}, {"onSurface", "#e7e9da"},
        {"onSurfaceVariant", "#bec2ae"}, {"outlineVariant", "#555849"}
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
    const QRegularExpression token(R"(^\s*([a-z-]+):\s*(#[0-9a-fA-F]{6,8});)");
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const auto match = token.match(stream.readLine());
        if (match.hasMatch() && keys.contains(match.captured(1)))
            palette[keys.value(match.captured(1))] = match.captured(2);
    }
    return palette;
}
}

class BluetoothController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool powered READ powered NOTIFY poweredChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit BluetoothController(QObject *parent = nullptr) : QObject(parent) {}

    QVariantList devices() const { return m_devices; }
    bool powered() const { return m_powered; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }

    Q_INVOKABLE void refresh() {
        if (m_busy)
            return;
        setBusy(true);
        setStatus("Updating…");
        loadState();
    }

    Q_INVOKABLE void scan() {
        if (m_busy)
            return;
        setBusy(true);
        setStatus("Scanning for devices…");
        if (!m_powered) {
            runCommand({"power", "on"}, [this](bool success, const QString &message) {
                if (!success) {
                    finishWithError(message);
                    return;
                }
                startScan();
            });
            return;
        }
        startScan();
    }

    Q_INVOKABLE void togglePower() {
        if (m_busy)
            return;
        setBusy(true);
        setStatus(m_powered ? "Turning Bluetooth off…" : "Turning Bluetooth on…");
        runCommand({"power", m_powered ? "off" : "on"}, [this](bool success, const QString &message) {
            if (!success) {
                finishWithError(message);
                return;
            }
            loadState();
        });
    }

    Q_INVOKABLE void activate(int index) {
        if (m_busy || index < 0 || index >= m_devices.size())
            return;
        const QVariantMap device = m_devices[index].toMap();
        const QString address = device.value("address").toString();
        const QString name = device.value("name").toString();
        setBusy(true);
        if (device.value("connected").toBool()) {
            setStatus("Disconnecting " + name + "…");
            runDeviceAction("disconnect", address);
        } else if (device.value("paired").toBool()) {
            setStatus("Connecting " + name + "…");
            runDeviceAction("connect", address);
        } else {
            setStatus("Pairing " + name + "…");
            runCommand({"--timeout", "30", "pair", address}, [this, address](bool success, const QString &message) {
                if (!success) {
                    finishWithError(message);
                    return;
                }
                runCommand({"trust", address}, [this, address](bool trusted, const QString &trustMessage) {
                    if (!trusted) {
                        finishWithError(trustMessage);
                        return;
                    }
                    runDeviceAction("connect", address);
                });
            });
        }
    }

    Q_INVOKABLE void forget(int index) {
        if (m_busy || index < 0 || index >= m_devices.size())
            return;
        const QVariantMap device = m_devices[index].toMap();
        setBusy(true);
        setStatus("Removing " + device.value("name").toString() + "…");
        runDeviceAction("remove", device.value("address").toString());
    }

signals:
    void devicesChanged();
    void poweredChanged();
    void busyChanged();
    void statusChanged();

private:
    using CommandCallback = std::function<void(bool, const QString &)>;

    void runCommand(const QStringList &arguments, CommandCallback callback) {
        auto *process = new QProcess(this);
        auto completed = std::make_shared<bool>(false);
        auto completion = std::make_shared<CommandCallback>(std::move(callback));
        connect(process, &QProcess::finished, this,
                [process, completion, completed](int code, QProcess::ExitStatus exitStatus) {
            if (*completed)
                return;
            *completed = true;
            const QString output = QString::fromUtf8(
                process->readAllStandardOutput() + process->readAllStandardError()).trimmed();
            const bool success = exitStatus == QProcess::NormalExit && code == 0
                && !output.contains("Failed", Qt::CaseInsensitive);
            process->deleteLater();
            (*completion)(success, output);
        });
        connect(process, &QProcess::errorOccurred, this,
                [process, completion, completed](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart || *completed)
                return;
            *completed = true;
            process->deleteLater();
            (*completion)(false, "bluetoothctl is unavailable");
        });
        process->start("bluetoothctl", arguments);
    }

    void loadState() {
        runCommand({"show"}, [this](bool success, const QString &output) {
            if (!success) {
                setPowered(false);
                setDevices({});
                finishWithError(output);
                return;
            }
            bool powered = false;
            for (const QString &line : output.split('\n')) {
                if (line.trimmed().startsWith("Powered:")) {
                    powered = line.trimmed().endsWith("yes", Qt::CaseInsensitive);
                    break;
                }
            }
            setPowered(powered);
            if (!powered) {
                setDevices({});
                setStatus("Bluetooth is off");
                setBusy(false);
                return;
            }
            loadDevices();
        });
    }

    void loadDevices() {
        runCommand({"devices"}, [this](bool success, const QString &output) {
            if (!success) {
                finishWithError(output);
                return;
            }
            static const QRegularExpression deviceLine(
                R"(^Device\s+([0-9A-Fa-f:]{17})\s+(.+)$)");
            QVariantList devices;
            for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
                const auto match = deviceLine.match(line.trimmed());
                if (!match.hasMatch())
                    continue;
                QVariantMap device{
                    {"address", match.captured(1).toUpper()},
                    {"name", match.captured(2)},
                    {"paired", false},
                    {"connected", false},
                    {"trusted", false}
                };
                QProcess info;
                info.start("bluetoothctl", {"info", match.captured(1)});
                if (info.waitForFinished(800))
                    applyInfo(device, QString::fromUtf8(info.readAllStandardOutput()));
                devices.push_back(device);
            }
            std::sort(devices.begin(), devices.end(), [](const QVariant &left, const QVariant &right) {
                const QVariantMap a = left.toMap();
                const QVariantMap b = right.toMap();
                if (a.value("connected").toBool() != b.value("connected").toBool())
                    return a.value("connected").toBool();
                if (a.value("paired").toBool() != b.value("paired").toBool())
                    return a.value("paired").toBool();
                return a.value("name").toString().localeAwareCompare(b.value("name").toString()) < 0;
            });
            setDevices(devices);
            setStatus(devices.isEmpty() ? "No devices found" : "Select a device to connect");
            setBusy(false);
        });
    }

    static void applyInfo(QVariantMap &device, const QString &output) {
        for (const QString &rawLine : output.split('\n')) {
            const QString line = rawLine.trimmed();
            const int separator = line.indexOf(':');
            if (separator < 0)
                continue;
            const QString key = line.left(separator);
            const QString value = line.mid(separator + 1).trimmed();
            if (key == "Name" || (key == "Alias" && device.value("name").toString().isEmpty()))
                device["name"] = value;
            else if (key == "Paired")
                device["paired"] = value == "yes";
            else if (key == "Connected")
                device["connected"] = value == "yes";
            else if (key == "Trusted")
                device["trusted"] = value == "yes";
        }
    }

    void startScan() {
        runCommand({"--timeout", "5", "scan", "on"}, [this](bool success, const QString &message) {
            if (!success) {
                finishWithError(message);
                return;
            }
            setPowered(true);
            loadDevices();
        });
    }

    void runDeviceAction(const QString &action, const QString &address) {
        runCommand({"--timeout", "20", action, address}, [this](bool success, const QString &message) {
            if (!success) {
                finishWithError(message);
                return;
            }
            loadState();
        });
    }

    void finishWithError(const QString &message) {
        const QStringList lines = message.split('\n', Qt::SkipEmptyParts);
        const QString lastLine = lines.isEmpty() ? QString() : lines.constLast();
        setStatus(lastLine.isEmpty() ? "Bluetooth operation failed" : lastLine);
        setBusy(false);
    }

    void setDevices(const QVariantList &devices) {
        if (m_devices == devices)
            return;
        m_devices = devices;
        emit devicesChanged();
    }

    void setPowered(bool powered) {
        if (m_powered == powered)
            return;
        m_powered = powered;
        emit poweredChanged();
    }

    void setBusy(bool busy) {
        if (m_busy == busy)
            return;
        m_busy = busy;
        emit busyChanged();
    }

    void setStatus(const QString &status) {
        if (m_status == status)
            return;
        m_status = status;
        emit statusChanged();
    }

    QVariantList m_devices;
    bool m_powered = false;
    bool m_busy = false;
    QString m_status = "Updating…";
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setDesktopFileName("bluetooth-popup");

    if (closeRunningPopup())
        return 0;

    QLockFile lock(QDir::tempPath() + "/" + socketName() + ".lock");
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

    BluetoothController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("bluetoothController", &controller);
    engine.rootContext()->setContextProperty(
        "themeColors", loadPalette(QDir::homePath() + "/.config/rofi/colors.rasi"));
    engine.load(QUrl::fromLocalFile(QDir::homePath() + "/.local/share/bluetooth-popup/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    controller.refresh();
    return app.exec();
}

#include "main.moc"

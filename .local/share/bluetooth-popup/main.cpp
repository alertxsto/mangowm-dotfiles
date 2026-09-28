#include <algorithm>
#include <functional>
#include <memory>
#include <QMap>
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
#include <QTimer>
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
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit BluetoothController(QObject *parent = nullptr) : QObject(parent) {
        auto *poll = new QTimer(this);
        poll->setInterval(1800);
        connect(poll, &QTimer::timeout, this, [this] {
            if (!m_busy || m_scanning)
                requestRefresh();
        });
        poll->start();
    }

    QVariantList devices() const { return m_devices; }
    bool powered() const { return m_powered; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }

    Q_INVOKABLE void refresh() {
        if (m_busy)
            return;
        setBusy(true);
        setStatus("Updating…");
        requestRefresh();
    }

    Q_INVOKABLE void scan() {
        if (m_busy)
            return;
        beginAction("Scanning for devices…");
        if (m_powered) {
            startScan();
            return;
        }
        runCommand({"power", "on"}, 6000, [this](bool success, const QString &output) {
            if (!success) {
                failAction(output);
                return;
            }
            runCommand({"show"}, 5000, [this](bool shown, const QString &state) {
                if (!shown || !readPower(state, true)) {
                    failAction(shown ? "Bluetooth did not turn on" : state);
                    return;
                }
                setPowered(true);
                startScan();
            });
        });
    }

    Q_INVOKABLE void togglePower() {
        if (m_busy)
            return;
        const bool wanted = !m_powered;
        beginAction(wanted ? "Turning Bluetooth on…" : "Turning Bluetooth off…");
        m_expectation = wanted ? Expectation::PoweredOn : Expectation::PoweredOff;
        runCommand({"power", wanted ? "on" : "off"}, 6000,
                   [this](bool success, const QString &output) {
            if (!success) {
                failAction(output);
                return;
            }
            verifyAction();
        });
    }

    Q_INVOKABLE void activate(int index) {
        if (m_busy || index < 0 || index >= m_devices.size())
            return;
        const QVariantMap device = m_devices[index].toMap();
        const QString address = device.value("address").toString();
        const QString name = device.value("name").toString();
        if (!m_powered) {
            m_actionError = true;
            setError("Turn Bluetooth on before connecting a device");
            return;
        }
        m_actionAddress = address;
        if (device.value("connected").toBool()) {
            beginAction("Disconnecting " + name + "…");
            runDeviceAction("disconnect", address, Expectation::Disconnected);
        } else if (device.value("paired").toBool()) {
            beginAction("Connecting " + name + "…");
            runDeviceAction("connect", address, Expectation::Connected);
        } else {
            beginAction("Pairing " + name + "…");
            runCommand({"--timeout", "30", "pair", address}, 33000,
                       [this, address](bool success, const QString &output) {
                if (!success) {
                    failAction(output);
                    return;
                }
                runCommand({"trust", address}, 6000,
                           [this, address](bool trusted, const QString &trustOutput) {
                    if (!trusted) {
                        failAction(trustOutput);
                        return;
                    }
                    runDeviceAction("connect", address, Expectation::PairedConnected);
                });
            });
        }
    }

    Q_INVOKABLE void forget(int index) {
        if (m_busy || index < 0 || index >= m_devices.size())
            return;
        const QVariantMap device = m_devices[index].toMap();
        m_actionAddress = device.value("address").toString();
        beginAction("Removing " + device.value("name").toString() + "…");
        runDeviceAction("remove", m_actionAddress, Expectation::Removed);
    }

signals:
    void devicesChanged();
    void poweredChanged();
    void busyChanged();
    void statusChanged();
    void errorChanged();

private:
    using CommandCallback = std::function<void(bool, const QString &)>;
    enum class Expectation { None, PoweredOn, PoweredOff, Connected, Disconnected,
                             PairedConnected, Removed };

    struct Snapshot {
        bool powered = false;
        QMap<QString, QVariantMap> devices;
        QStringList addresses;
        int next = 0;
        int active = 0;
        QString error;
    };

    void runCommand(const QStringList &arguments, int timeoutMs, CommandCallback callback) {
        auto *process = new QProcess(this);
        auto completed = std::make_shared<bool>(false);
        auto timedOut = std::make_shared<bool>(false);
        auto completion = std::make_shared<CommandCallback>(std::move(callback));
        connect(process, &QProcess::finished, this,
                [process, completion, completed, timedOut](int code, QProcess::ExitStatus exitStatus) {
            if (*completed)
                return;
            *completed = true;
            QByteArray bytes = process->readAllStandardOutput();
            const QByteArray errors = process->readAllStandardError();
            if (!bytes.isEmpty() && !errors.isEmpty() && !bytes.endsWith('\n'))
                bytes.append('\n');
            bytes.append(errors);
            const QString output = QString::fromUtf8(bytes).trimmed();
            static const QRegularExpression failurePattern(
                R"((?:^|\n)\s*(?:Failed|Error|No default controller)\b)",
                QRegularExpression::CaseInsensitiveOption);
            const bool success = !*timedOut && exitStatus == QProcess::NormalExit && code == 0
                && !failurePattern.match(output).hasMatch();
            process->deleteLater();
            (*completion)(success, *timedOut ? "bluetoothctl timed out" : output);
        });
        connect(process, &QProcess::errorOccurred, this,
                [process, completion, completed](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart || *completed)
                return;
            *completed = true;
            process->deleteLater();
            (*completion)(false, "bluetoothctl is unavailable");
        });
        QTimer::singleShot(timeoutMs, process, [process, completed, timedOut] {
            if (!*completed) {
                *timedOut = true;
                process->kill();
            }
        });
        process->start("bluetoothctl", arguments);
    }

    static bool parsePower(const QString &output, bool &powered) {
        for (const QString &raw : output.split('\n')) {
            const QString line = raw.trimmed();
            if (!line.startsWith("Powered:"))
                continue;
            const QString state = line.mid(8).trimmed();
            if (state.compare("yes", Qt::CaseInsensitive) == 0) {
                powered = true;
                return true;
            }
            if (state.compare("no", Qt::CaseInsensitive) == 0) {
                powered = false;
                return true;
            }
        }
        return false;
    }

    static bool readPower(const QString &output, bool expected) {
        bool powered;
        return parsePower(output, powered) && powered == expected;
    }

    void requestRefresh() {
        if (m_refreshing) {
            m_refreshAgain = true;
            return;
        }
        m_refreshing = true;
        auto snapshot = std::make_shared<Snapshot>();
        runCommand({"show"}, 5000, [this, snapshot](bool success, const QString &output) {
            if (!success || !parsePower(output, snapshot->powered)) {
                finishSnapshot(success ? "Unable to read Bluetooth power state" : output);
                return;
            }
            setPowered(snapshot->powered);
            if (!snapshot->powered) {
                QVariantList paired;
                for (const QVariant &entry : m_devices) {
                    QVariantMap device = entry.toMap();
                    if (device.value("paired").toBool()) {
                        device["connected"] = false;
                        paired.push_back(device);
                    }
                }
                setDevices(paired);
            }
            // Paired devices remain useful while the adapter is off.
            if (snapshot->powered) {
                runCommand({"devices"}, 5000,
                           [this, snapshot](bool listed, const QString &devicesOutput) {
                    if (!listed) {
                        finishSnapshot(devicesOutput);
                        return;
                    }
                    addDevices(snapshot, devicesOutput);
                    loadPaired(snapshot);
                });
            } else {
                loadPaired(snapshot);
            }
        });
    }

    static void addDevices(const std::shared_ptr<Snapshot> &snapshot, const QString &output,
                           bool pairedList = false) {
        static const QRegularExpression deviceLine(
            R"(^Device\s+([0-9A-Fa-f:]{17})\s+(.+)$)");
        for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
            const auto match = deviceLine.match(line.trimmed());
            if (!match.hasMatch())
                continue;
            const QString address = match.captured(1).toUpper();
            if (!snapshot->devices.contains(address)) {
                snapshot->devices.insert(address, {
                    {"address", address}, {"name", match.captured(2)},
                    {"paired", false}, {"connected", false}, {"trusted", false}
                });
            }
            if (pairedList)
                snapshot->devices[address]["paired"] = true;
        }
    }

    void loadPaired(const std::shared_ptr<Snapshot> &snapshot) {
        runCommand({"devices", "Paired"}, 5000,
                   [this, snapshot](bool success, const QString &output) {
            if (!success) {
                finishSnapshot(output);
                return;
            }
            addDevices(snapshot, output, true);
            snapshot->addresses = snapshot->devices.keys();
            pumpInfo(snapshot);
        });
    }

    void pumpInfo(const std::shared_ptr<Snapshot> &snapshot) {
        while (snapshot->active < 4 && snapshot->next < snapshot->addresses.size()) {
            const QString address = snapshot->addresses.at(snapshot->next++);
            ++snapshot->active;
            runCommand({"info", address}, 4000,
                       [this, snapshot, address](bool success, const QString &output) {
                --snapshot->active;
                static const QRegularExpression pairedLine(R"((?:^|\n)\s*Paired:\s*(?:yes|no)\b)");
                static const QRegularExpression connectedLine(R"((?:^|\n)\s*Connected:\s*(?:yes|no)\b)");
                if (success && output.contains("Device " + address, Qt::CaseInsensitive)
                    && output.contains(pairedLine) && output.contains(connectedLine)) {
                    QVariantMap device = snapshot->devices.value(address);
                    applyInfo(device, output);
                    if (!snapshot->powered)
                        device["connected"] = false;
                    snapshot->devices[address] = device;
                } else if (!snapshot->devices.value(address).value("paired").toBool()
                           && (output.contains("not available", Qt::CaseInsensitive)
                               || output.contains("not found", Qt::CaseInsensitive))) {
                    // A nearby device can disappear between listing and querying it.
                    snapshot->devices.remove(address);
                } else if (snapshot->error.isEmpty()) {
                    snapshot->error = success ? "Unable to read " + address : output;
                }
                pumpInfo(snapshot);
            });
        }
        if (snapshot->active != 0 || snapshot->next != snapshot->addresses.size())
            return;
        if (!snapshot->error.isEmpty()) {
            finishSnapshot(snapshot->error);
            return;
        }
        auto sorted = snapshot->devices.values();
        std::sort(sorted.begin(), sorted.end(), [](const QVariantMap &a, const QVariantMap &b) {
            if (a.value("connected").toBool() != b.value("connected").toBool())
                return a.value("connected").toBool();
            if (a.value("paired").toBool() != b.value("paired").toBool())
                return a.value("paired").toBool();
            return a.value("name").toString().localeAwareCompare(b.value("name").toString()) < 0;
        });
        QVariantList devices;
        devices.reserve(sorted.size());
        for (const QVariantMap &device : sorted)
            devices.push_back(device);
        setPowered(snapshot->powered);
        setDevices(devices);
        finishSnapshot({});
    }

    static void applyInfo(QVariantMap &device, const QString &output) {
        for (const QString &rawLine : output.split('\n')) {
            const QString line = rawLine.trimmed();
            const int separator = line.indexOf(':');
            if (separator < 0)
                continue;
            const QString key = line.left(separator);
            const QString value = line.mid(separator + 1).trimmed();
            if (key == "Alias" || (key == "Name" && device.value("name").toString().isEmpty()))
                device["name"] = value;
            else if (key == "Paired")
                device["paired"] = value == "yes";
            else if (key == "Connected")
                device["connected"] = value == "yes";
            else if (key == "Trusted")
                device["trusted"] = value == "yes";
        }
    }

    void finishSnapshot(const QString &error) {
        m_refreshing = false;
        if (!error.isEmpty()) {
            if (m_verifying) {
                m_refreshAgain = false;
                failAction(error);
            } else if (!m_actionInFlight) {
                m_actionError = false;
                setError(error);
                if (!m_refreshAgain)
                    setBusy(false);
            }
        } else {
            if (!m_actionInFlight && !m_actionError)
                setError({});
            if (m_verifying && !m_refreshAgain) {
                if (expectationMet()) {
                    m_verifying = false;
                    m_actionInFlight = false;
                    m_expectation = Expectation::None;
                    setBusy(false);
                    setIdleStatus();
                } else if (++m_verificationAttempts < 3) {
                    const auto serial = m_operationSerial;
                    QTimer::singleShot(350, this, [this, serial] {
                        if (m_verifying && serial == m_operationSerial)
                            requestRefresh();
                    });
                } else {
                    failAction("Bluetooth did not complete the requested change");
                }
            } else if (!m_actionInFlight) {
                if (!m_refreshAgain)
                    setBusy(false);
                if (m_error.isEmpty())
                    setIdleStatus();
            }
        }
        if (m_refreshAgain) {
            m_refreshAgain = false;
            requestRefresh();
        }
    }

    void setIdleStatus() {
        setStatus(m_powered ? (m_devices.isEmpty() ? "No devices found"
                                                   : "Select a device to connect")
                            : "Bluetooth is off");
    }

    bool expectationMet() const {
        if (m_expectation == Expectation::PoweredOn)
            return m_powered;
        if (m_expectation == Expectation::PoweredOff)
            return !m_powered;
        for (const QVariant &entry : m_devices) {
            const QVariantMap device = entry.toMap();
            if (device.value("address").toString() != m_actionAddress)
                continue;
            if (m_expectation == Expectation::Connected)
                return device.value("connected").toBool();
            if (m_expectation == Expectation::Disconnected)
                return !device.value("connected").toBool();
            if (m_expectation == Expectation::PairedConnected)
                return device.value("paired").toBool() && device.value("trusted").toBool()
                    && device.value("connected").toBool();
            if (m_expectation == Expectation::Removed)
                return !device.value("paired").toBool() && !device.value("connected").toBool();
        }
        return m_expectation == Expectation::Removed;
    }

    void beginAction(const QString &status) {
        ++m_operationSerial;
        m_actionError = false;
        m_actionInFlight = true;
        setError({});
        setBusy(true);
        setStatus(status);
    }

    void verifyAction() {
        m_verifying = true;
        m_verificationAttempts = 0;
        requestRefresh();
    }

    void startScan() {
        m_scanning = true;
        requestRefresh();
        runCommand({"--timeout", "7", "scan", "on"}, 10000,
                   [this](bool success, const QString &output) {
            m_scanning = false;
            if (!success) {
                failAction(output);
                return;
            }
            m_expectation = Expectation::PoweredOn;
            verifyAction();
        });
    }

    void runDeviceAction(const QString &action, const QString &address, Expectation expected) {
        m_expectation = expected;
        runCommand({"--timeout", "20", action, address}, 23000,
                   [this](bool success, const QString &output) {
            if (!success) {
                failAction(output);
                return;
            }
            verifyAction();
        });
    }

    void failAction(const QString &message) {
        m_actionError = true;
        m_actionInFlight = false;
        m_verifying = false;
        m_expectation = Expectation::None;
        setError(message);
        setBusy(true);
        requestRefresh();
    }

    void setError(const QString &message) {
        const QStringList lines = message.split('\n', Qt::SkipEmptyParts);
        const QString lastLine = lines.isEmpty() ? QString() : lines.constLast().trimmed();
        const QString error = message.isEmpty() ? QString()
                            : lastLine.isEmpty() ? "Bluetooth operation failed" : lastLine;
        if (m_error == error)
            return;
        m_error = error;
        emit errorChanged();
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
    bool m_actionError = false;
    quint64 m_operationSerial = 0;
    bool m_actionInFlight = false;
    bool m_scanning = false;
    bool m_refreshing = false;
    bool m_refreshAgain = false;
    bool m_verifying = false;
    int m_verificationAttempts = 0;
    Expectation m_expectation = Expectation::None;
    QString m_actionAddress;
    QString m_status = "Updating…";
    QString m_error;
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

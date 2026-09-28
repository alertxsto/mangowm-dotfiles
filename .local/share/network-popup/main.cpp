#include <algorithm>
#include <functional>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QRegularExpression>
#include <QScreen>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <unistd.h>

namespace {
QString socketName() {
    return "network-popup-" + QString::number(getuid());
}

bool closeRunningPopup() {
    QLocalSocket socket;
    socket.connectToServer(socketName());
    if (!socket.waitForConnected(80))
        return false;
    socket.write("close");
    return socket.waitForBytesWritten(80);
}
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
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return palette;
    const QRegularExpression token(R"(^\s*([a-z-]+):\s*(#[0-9a-fA-F]{6,8});)");
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const auto match = token.match(stream.readLine());
        if (match.hasMatch() && keys.contains(match.captured(1)))
            palette[keys.value(match.captured(1))] = match.captured(2);
    }
    return palette;
}

class NetworkController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList networks READ networks NOTIFY networksChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool radioKnown READ radioKnown NOTIFY stateChanged)
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(QString activeSsid READ activeSsid NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
public:
    explicit NetworkController(QObject *parent = nullptr) : QObject(parent) {
        auto *timer = new QTimer(this);
        timer->setInterval(2200);
        connect(timer, &QTimer::timeout, this, [this] { queueRefresh(false); });
        timer->start();
    }
    QVariantList networks() const { return m_networks; }
    bool scanning() const { return m_scanning; }
    bool busy() const { return m_busy; }
    bool radioKnown() const { return m_radioKnown; }
    bool wifiEnabled() const { return m_wifiEnabled; }
    bool connected() const { return m_connected; }
    QString activeSsid() const { return m_activeSsid; }
    QString message() const { return m_message; }
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE void refresh() {
        setError({});
        queueRefresh(true);
    }

    Q_INVOKABLE void activate(int index) {
        if (m_busy || !m_wifiEnabled || index < 0 || index >= m_networks.size()) return;
        const auto network = m_networks[index].toMap();
        const QString ssid = network.value("ssid").toString();
        if (network.value("active").toBool()) {
            runAction({"--wait", "15", "device", "disconnect", "wlan0"},
                      "Disconnecting from " + ssid, false);
            return;
        }
        runAction({"--wait", "20", "device", "wifi", "connect", ssid, "ifname", "wlan0"},
                  "Connecting to " + ssid, false, ssid, network.value("secured").toBool());
    }

    Q_INVOKABLE void connectWithPassword(const QString &ssid, const QString &password) {
        if (m_busy || !m_wifiEnabled || ssid.isEmpty() || password.isEmpty()) return;
        runAction({"--wait", "25", "device", "wifi", "connect", ssid, "password", password,
                   "ifname", "wlan0"}, "Connecting to " + ssid, false, ssid);
    }
    Q_INVOKABLE void setWifiEnabled(bool enabled) {
        if (m_busy || !m_radioKnown || enabled == m_wifiEnabled) return;
        runAction({"--wait", "15", "radio", "wifi", enabled ? "on" : "off"},
                  enabled ? "Turning Wi-Fi on" : "Turning Wi-Fi off", enabled);
    }
    Q_INVOKABLE void advanced() {
        QProcess::startDetached("kitty", {"--class", "network-config", "-e", "nmtui"});
        QCoreApplication::quit();
    }

signals:
    void networksChanged();
    void scanningChanged();
    void busyChanged();
    void stateChanged();
    void messageChanged();
    void errorMessageChanged();
    void passwordRequested(const QString &ssid);
    void connectionSucceeded();

private:
    using Result = std::function<void(int, const QByteArray &, const QByteArray &)>;

    static QStringList fields(const QString &line) {
        QStringList result;
        QString current;
        bool escaped = false;
        for (const QChar ch : line) {
            if (escaped) { current += ch; escaped = false; }
            else if (ch == '\\') escaped = true;
            else if (ch == ':') { result << current; current.clear(); }
            else current += ch;
        }
        if (escaped) current += '\\';
        result << current;
        return result;
    }

    void runNmcli(const QStringList &args, Result callback) {
        auto *process = new QProcess(this);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("LC_ALL", "C");
        process->setProcessEnvironment(environment);
        auto complete = [process, callback](int code) {
            if (process->property("completed").toBool()) return;
            process->setProperty("completed", true);
            callback(code, process->readAllStandardOutput(), process->readAllStandardError());
            process->deleteLater();
        };
        connect(process, &QProcess::finished, this,
                [complete](int code, QProcess::ExitStatus status) {
                    complete(status == QProcess::NormalExit ? code : -1);
                });
        connect(process, &QProcess::errorOccurred, this,
                [complete](QProcess::ProcessError error) {
                    if (error == QProcess::FailedToStart) complete(-1);
                });
        process->start("nmcli", args);
    }

    void setScanning(bool value) {
        if (m_scanning == value) return;
        m_scanning = value;
        emit scanningChanged();
    }
    void setBusy(bool value) {
        if (m_busy == value) return;
        m_busy = value;
        emit busyChanged();
    }
    void setMessage(const QString &value) {
        if (m_message == value) return;
        m_message = value;
        emit messageChanged();
    }
    void setError(const QString &value) {
        if (m_errorMessage == value) return;
        m_errorMessage = value;
        emit errorMessageChanged();
    }
    static QString failure(const QByteArray &stderrOutput, const QString &fallback) {
        const QString detail = QString::fromUtf8(stderrOutput).trimmed();
        return detail.isEmpty() ? fallback : detail;
    }

    void queueRefresh(bool scan) {
        m_refreshPending = true;
        m_scanPending |= scan;
        if (!m_snapshotRunning && !m_actionRunning) startSnapshot();
    }

    void finishSnapshot() {
        m_snapshotRunning = false;
        setScanning(false);
        if (!m_actionRunning) {
            setBusy(false);
            setMessage({});
            if (m_refreshPending) startSnapshot();
        }
    }

    void startSnapshot() {
        if (m_snapshotRunning || m_actionRunning || !m_refreshPending) return;
        m_snapshotRunning = true;
        m_refreshPending = false;
        const bool scan = m_scanPending;
        m_scanPending = false;
        setScanning(scan);
        const unsigned generation = m_generation;
        runNmcli({"radio", "wifi"}, [this, generation, scan](int code, const QByteArray &output,
                                                              const QByteArray &error) {
            if (generation != m_generation) { finishSnapshot(); return; }
            if (code != 0) {
                setError(failure(error, "Could not check Wi-Fi radio"));
                finishSnapshot();
                return;
            }
            const QString radio = QString::fromUtf8(output).trimmed();
            if (radio != "enabled" && radio != "disabled") {
                setError("Could not read Wi-Fi radio state");
                finishSnapshot();
                return;
            }
            const bool enabled = radio == "enabled";
            if (!m_radioKnown || m_wifiEnabled != enabled) {
                m_radioKnown = true;
                m_wifiEnabled = enabled;
                emit stateChanged();
            }
            if (!enabled) {
                if (m_connected || !m_activeSsid.isEmpty()) {
                    m_connected = false;
                    m_activeSsid.clear();
                    emit stateChanged();
                }
                if (!m_networks.isEmpty()) {
                    m_networks.clear();
                    emit networksChanged();
                }
                finishSnapshot();
                return;
            }
            runNmcli({"-g", "GENERAL.STATE", "device", "show", "wlan0"},
                     [this, generation, scan](int stateCode, const QByteArray &state,
                                              const QByteArray &stateError) {
                if (generation != m_generation) { finishSnapshot(); return; }
                if (stateCode != 0) {
                    setError(failure(stateError, "Could not check Wi-Fi connection"));
                    finishSnapshot();
                    return;
                }
                const bool connected = QString::fromUtf8(state).startsWith("100");
                listNetworks(generation, connected, scan);
            });
        });
    }

    void listNetworks(unsigned generation, bool connected, bool scanAfter) {
        runNmcli({"-t", "--escape", "yes", "-f", "IN-USE,SIGNAL,SECURITY,SSID",
                  "device", "wifi", "list", "ifname", "wlan0", "--rescan", "no"},
                 [this, generation, connected, scanAfter](int code, const QByteArray &output,
                                                          const QByteArray &error) {
            if (generation != m_generation) { finishSnapshot(); return; }
            if (code == 0) parseNetworks(output, connected);
            else if (!scanAfter) {
                setError(failure(error, "Could not list Wi-Fi networks"));
                parseNetworks({}, connected);
            }
            if (!scanAfter) { finishSnapshot(); return; }
            runNmcli({"--wait", "15", "device", "wifi", "rescan", "ifname", "wlan0"},
                     [this, generation, connected](int scanCode, const QByteArray &,
                                                    const QByteArray &scanError) {
                if (generation != m_generation) { finishSnapshot(); return; }
                if (scanCode != 0)
                    setError(failure(scanError, "Could not scan for networks"));
                listNetworks(generation, connected, false);
            });
        });
    }

    void parseNetworks(const QByteArray &output, bool deviceConnected) {
        QHash<QString, QVariantMap> strongest;
        QString activeSsid;
        for (const QString &line : QString::fromUtf8(output).split('\n', Qt::SkipEmptyParts)) {
            const auto item = fields(line);
            if (item.size() != 4 || item[3].isEmpty()) continue;
            const bool active = item[0] == "*";
            const int signal = item[1].toInt();
            const auto old = strongest.value(item[3]);
            if (active) activeSsid = item[3];
            if (old.isEmpty() || (active && !old.value("active").toBool()) ||
                (!old.value("active").toBool() && signal > old.value("signal").toInt()))
                strongest[item[3]] = {{"ssid", item[3]}, {"signal", signal},
                                      {"secured", !item[2].isEmpty() && item[2] != "--"},
                                      {"active", active}};
        }
        auto values = strongest.values();
        std::sort(values.begin(), values.end(), [](const QVariantMap &a, const QVariantMap &b) {
            if (a["active"].toBool() != b["active"].toBool()) return a["active"].toBool();
            if (a["signal"].toInt() != b["signal"].toInt())
                return a["signal"].toInt() > b["signal"].toInt();
            return a["ssid"].toString() < b["ssid"].toString();
        });
        QVariantList networks;
        for (const auto &value : values) networks << value;
        if (m_networks != networks) {
            m_networks = networks;
            emit networksChanged();
        }
        if (m_activeSsid != activeSsid || m_connected != (deviceConnected || !activeSsid.isEmpty())) {
            m_activeSsid = activeSsid;
            m_connected = deviceConnected || !activeSsid.isEmpty();
            emit stateChanged();
        }
    }

    void runAction(const QStringList &args, const QString &description, bool scanAfter,
                   const QString &ssid = {}, bool promptOnMissingSecret = false) {
        if (m_actionRunning) return;
        ++m_generation; // Discard an older snapshot when its nmcli process completes.
        m_actionRunning = true;
        setBusy(true);
        setMessage(description);
        setError({});
        runNmcli(args, [this, description, scanAfter, ssid, promptOnMissingSecret](
                              int code, const QByteArray &, const QByteArray &error) {
            m_actionRunning = false;
            if (code != 0) {
                const QString detail = failure(error, description + " failed");
                setError(detail);
                if (promptOnMissingSecret &&
                    (detail.contains("secret", Qt::CaseInsensitive) ||
                     detail.contains("password", Qt::CaseInsensitive) ||
                     detail.contains("credential", Qt::CaseInsensitive)))
                    emit passwordRequested(ssid);
            } else if (!ssid.isEmpty()) {
                emit connectionSucceeded();
            }
            queueRefresh(code == 0 && scanAfter);
        });
    }

    QVariantList m_networks;
    QString m_activeSsid;
    QString m_message;
    QString m_errorMessage;
    bool m_scanning = false;
    bool m_busy = false;
    bool m_radioKnown = false;
    bool m_wifiEnabled = false;
    bool m_connected = false;
    bool m_refreshPending = false;
    bool m_scanPending = false;
    bool m_snapshotRunning = false;
    bool m_actionRunning = false;
    unsigned m_generation = 0;
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setDesktopFileName("network-popup");

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

    NetworkController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("networkController", &controller);
    engine.rootContext()->setContextProperty(
        "themeColors", loadPalette(QDir::homePath() + "/.config/rofi/colors.rasi"));
    engine.load(QUrl::fromLocalFile(QDir::homePath() + "/.local/share/network-popup/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    controller.refresh();
    return app.exec();
}

#include "main.moc"

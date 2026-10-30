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
#include <QSet>
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
            runAction({"--wait", "15", "device", "disconnect", m_interface},
                      "Disconnecting from " + ssid, false);
            return;
        }
        const QString uuid = network.value("uuid").toString();
        if (!uuid.isEmpty()) {
            runAction({"--wait", "20", "connection", "up", "uuid", uuid, "ifname", m_interface},
                      "Connecting to " + ssid, false, ssid, network.value("secured").toBool(), {}, uuid);
        } else {
            runAction({"--wait", "20", "device", "wifi", "connect", ssid, "ifname", m_interface},
                      "Connecting to " + ssid, false, ssid, network.value("secured").toBool());
        }
    }

    Q_INVOKABLE void connectWithPassword(const QString &ssid, const QString &uuid, const QString &password) {
        if (m_busy || !m_wifiEnabled || ssid.isEmpty() || password.isEmpty()) return;
        if (password.contains('\n') || password.contains('\r')) {
            setError("The Wi-Fi password cannot contain a line break");
            return;
        }
        if (!uuid.isEmpty()) {
            QVariantMap profile;
            for (const auto &candidate : m_profiles)
                if (candidate.value("uuid").toString() == uuid) { profile = candidate; break; }
            if (profile.isEmpty()) {
                setError("The selected saved network is no longer available");
                return;
            }
            const QString security = profile.value("security").toString();
            QString property;
            if (security == "wpa-psk" || security == "sae" || security == "wpa-none")
                property = "802-11-wireless-security.psk";
            else if (security == "none")
                property = "802-11-wireless-security.wep-key" + profile.value("wepIndex").toString();
            else if (security == "wpa-eap" || security == "ieee8021x" || security == "wpa-eap-suite-b-192")
                property = "802-1x.password";
            else {
                setError("This saved network requires advanced credential configuration");
                return;
            }
            runAction({"--wait", "25", "connection", "up", "uuid", uuid, "ifname", m_interface,
                       "passwd-file", "/dev/stdin"}, "Connecting to " + ssid, false, ssid, false,
                      property.toUtf8() + ':' + password.toUtf8() + '\n');
        } else {
            runAction({"--ask", "--wait", "25", "device", "wifi", "connect", ssid,
                       "ifname", m_interface}, "Connecting to " + ssid, false, ssid, false,
                      password.toUtf8() + '\n');
        }
    }
    Q_INVOKABLE void forget(const QString &uuid) {
        if (m_busy || uuid.isEmpty()) return;
        runAction({"--wait", "15", "connection", "delete", "uuid", uuid},
                  "Forgetting network", false);
    }
    Q_INVOKABLE void setAutoconnect(const QString &uuid, bool enabled) {
        if (m_busy || uuid.isEmpty()) return;
        runAction({"--wait", "15", "connection", "modify", "uuid", uuid,
                   "connection.autoconnect", enabled ? "yes" : "no"},
                  "Updating automatic connection", false);
    }
    Q_INVOKABLE void setWifiEnabled(bool enabled) {
        if (m_busy || !m_radioKnown || enabled == m_wifiEnabled) return;
        runAction({"--wait", "15", "radio", "wifi", enabled ? "on" : "off"},
                  enabled ? "Turning Wi-Fi on" : "Turning Wi-Fi off", enabled);
    }
    Q_INVOKABLE void advanced() {
        QProcess::startDetached("foot", {"--app-id", "network-config", "nmtui"});
        emit dismissRequested();
    }

signals:
    void networksChanged();
    void scanningChanged();
    void busyChanged();
    void stateChanged();
    void messageChanged();
    void errorMessageChanged();
    void passwordRequested(const QString &ssid, const QString &uuid);
    void connectionSucceeded();
    void dismissRequested();

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

    void runNmcli(const QStringList &args, Result callback, const QByteArray &input = {}) {
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
        if (!input.isEmpty()) {
            process->write(input);
            process->closeWriteChannel();
        }
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
            m_deviceConnected = false;
            m_deviceUuid.clear();
            m_ipAddress.clear();
            m_scanAfter = enabled && scan;
            runNmcli({"-t", "--escape", "yes", "-f", "DEVICE,TYPE,STATE", "device", "status"},
                     [this, generation, enabled](int deviceCode, const QByteArray &devices,
                                                 const QByteArray &deviceError) {
                if (generation != m_generation) { finishSnapshot(); return; }
                QString selectedInterface;
                bool selectedConnected = false;
                if (deviceCode != 0) setError(failure(deviceError, "Could not list network devices"));
                else {
                    for (const QString &line : QString::fromUtf8(devices).split('\n', Qt::SkipEmptyParts)) {
                        const auto item = fields(line);
                        if (item.size() < 3 || item[1] != "wifi") continue;
                        if (selectedInterface.isEmpty() || item[2] == "connected") {
                            selectedInterface = item[0];
                            selectedConnected = item[2] == "connected";
                        }
                        if (selectedConnected) break;
                    }
                }
                m_interface = selectedInterface;
                m_deviceConnected = selectedConnected;
                if (!enabled) m_deviceConnected = false;
                if (!enabled || m_interface.isEmpty()) {
                    listProfiles(generation);
                    return;
                }
                runNmcli({"-t", "--escape", "yes", "-f", "GENERAL.CON-UUID,IP4.ADDRESS,IP6.ADDRESS",
                          "device", "show", m_interface},
                         [this, generation](int detailCode, const QByteArray &details, const QByteArray &) {
                    if (generation != m_generation) { finishSnapshot(); return; }
                    if (detailCode == 0 && m_deviceConnected) {
                        for (const QString &line : QString::fromUtf8(details).split('\n', Qt::SkipEmptyParts)) {
                            const auto item = fields(line);
                            if (item.size() < 2) continue;
                            if (item[0] == "GENERAL.CON-UUID" && item[1] != "--")
                                m_deviceUuid = item[1];
                            else if ((item[0].startsWith("IP4.ADDRESS") || item[0].startsWith("IP6.ADDRESS"))
                                     && m_ipAddress.isEmpty() && item[1] != "--")
                                m_ipAddress = item.mid(1).join(":");
                        }
                    }
                    listProfiles(generation);
                });
            });
        });
    }

    void listProfiles(unsigned generation) {
        m_profiles.clear();
        m_profileUuids.clear();
        m_profileIndex = 0;
        runNmcli({"-t", "--escape", "yes", "-f", "UUID,TYPE", "connection", "show"},
                 [this, generation](int code, const QByteArray &output, const QByteArray &error) {
            if (generation != m_generation) { finishSnapshot(); return; }
            if (code != 0) setError(failure(error, "Could not list saved networks"));
            else {
                for (const QString &line : QString::fromUtf8(output).split('\n', Qt::SkipEmptyParts)) {
                    const auto item = fields(line);
                    if (item.size() == 2 && (item[1] == "802-11-wireless" || item[1] == "wifi"))
                        m_profileUuids << item[0];
                }
            }
            readProfile(generation);
        });
    }

    void readProfile(unsigned generation) {
        if (generation != m_generation) { finishSnapshot(); return; }
        if (m_profileIndex >= m_profileUuids.size()) {
            if (!m_wifiEnabled || m_interface.isEmpty()) {
                parseNetworks({});
                finishSnapshot();
            } else {
                listNetworks(generation, m_scanAfter);
            }
            return;
        }
        const QString uuid = m_profileUuids.at(m_profileIndex++);
        runNmcli({"-t", "--escape", "yes", "-f",
                  "connection.id,802-11-wireless.ssid,connection.autoconnect,802-11-wireless-security.key-mgmt,802-11-wireless-security.wep-tx-keyidx",
                  "connection", "show", "uuid", uuid},
                 [this, generation, uuid](int code, const QByteArray &output, const QByteArray &error) {
            if (generation != m_generation) { finishSnapshot(); return; }
            if (code == 0) {
                QString ssid, security, name, wepIndex = "0";
                bool autoconnect = false;
                for (const QString &line : QString::fromUtf8(output).split('\n', Qt::SkipEmptyParts)) {
                    const auto item = fields(line);
                    if (item.size() < 2) continue;
                    const QString value = item.mid(1).join(":");
                    if (item[0] == "connection.id") name = value;
                    else if (item[0] == "802-11-wireless.ssid") ssid = value;
                    else if (item[0] == "connection.autoconnect") autoconnect = value == "yes";
                    else if (item[0] == "802-11-wireless-security.key-mgmt" && value != "--") security = value;
                    else if (item[0] == "802-11-wireless-security.wep-tx-keyidx" && value != "--") wepIndex = value;
                }
                if (!ssid.isEmpty() && ssid != "--")
                    m_profiles << QVariantMap{{"ssid", ssid}, {"uuid", uuid}, {"name", name},
                                              {"autoconnect", autoconnect}, {"security", security},
                                              {"wepIndex", wepIndex}};
            } else setError(failure(error, "Could not read saved network"));
            readProfile(generation);
        });
    }

    void listNetworks(unsigned generation, bool scanAfter) {
        runNmcli({"-t", "--escape", "yes", "-f", "IN-USE,SIGNAL,SECURITY,SSID",
                  "device", "wifi", "list", "ifname", m_interface, "--rescan", "no"},
                 [this, generation, scanAfter](int code, const QByteArray &output, const QByteArray &error) {
            if (generation != m_generation) { finishSnapshot(); return; }
            if (code == 0) parseNetworks(output);
            else if (!scanAfter) {
                setError(failure(error, "Could not list Wi-Fi networks"));
                parseNetworks({});
            }
            if (!scanAfter) { finishSnapshot(); return; }
            runNmcli({"--wait", "15", "device", "wifi", "rescan", "ifname", m_interface},
                     [this, generation](int scanCode, const QByteArray &, const QByteArray &scanError) {
                if (generation != m_generation) { finishSnapshot(); return; }
                if (scanCode != 0) setError(failure(scanError, "Could not scan for networks"));
                listNetworks(generation, false);
            });
        });
    }

    static bool compatibleSecurity(const QString &profile, const QString &accessPoint) {
        const QString ap = accessPoint.toUpper();
        const bool enterprise = ap.contains("802.1X");
        if (profile.isEmpty()) return ap.isEmpty();
        if (profile == "none") return ap.contains("WEP");
        if (profile == "owe") return ap.contains("OWE");
        if (profile == "sae") return ap.contains("WPA3") && !enterprise;
        if (profile == "wpa-psk" || profile == "wpa-none")
            return !enterprise && (ap.contains("WPA1") || ap.contains("WPA2"));
        if (profile == "ieee8021x") return enterprise && ap.contains("WEP");
        if (profile == "wpa-eap-suite-b-192") return enterprise && ap.contains("WPA3");
        if (profile == "wpa-eap")
            return enterprise && (ap.contains("WPA1") || ap.contains("WPA2") || ap.contains("WPA3"));
        return false;
    }

    void parseNetworks(const QByteArray &output) {
        QHash<QString, QVariantMap> strongest;
        QString activeSsid;
        for (const QString &line : QString::fromUtf8(output).split('\n', Qt::SkipEmptyParts)) {
            const auto item = fields(line);
            if (item.size() != 4 || item[3].isEmpty()) continue;
            const bool active = item[0] == "*";
            const int signal = item[1].toInt();
            const QString key = item[3] + QChar(0) + item[2];
            const auto old = strongest.value(key);
            if (active) activeSsid = item[3];
            if (old.isEmpty() || (active && !old.value("active").toBool()) ||
                (!old.value("active").toBool() && signal > old.value("signal").toInt()))
                strongest[key] = {{"ssid", item[3]}, {"signal", signal},
                                      {"secured", !item[2].isEmpty() && item[2] != "--"},
                                      {"security", item[2] == "--" ? QString() : item[2]},
                                      {"active", active}, {"inRange", true}, {"saved", false},
                                      {"uuid", QString()}, {"autoconnect", false},
                                      {"interface", QString()}, {"ipAddress", QString()}};
        }
        QList<QVariantMap> values;
        QSet<QString> matchedAccessPoints;
        for (const auto &profile : m_profiles) {
            const QString ssid = profile.value("ssid").toString();
            const QString security = profile.value("security").toString();
            const bool active = m_deviceConnected && profile.value("uuid").toString() == m_deviceUuid;
            QVariantMap network;
            for (auto it = strongest.cbegin(); it != strongest.cend(); ++it) {
                const auto ap = it.value();
                if (ap.value("ssid").toString() != ssid ||
                    !compatibleSecurity(security, ap.value("security").toString())) continue;
                matchedAccessPoints.insert(it.key());
                if (network.isEmpty() || (active && ap.value("active").toBool()) ||
                    (!network.value("active").toBool() && ap.value("signal").toInt() > network.value("signal").toInt()))
                    network = ap;
            }
            if (network.isEmpty())
                network = {{"ssid", ssid}, {"signal", -1}, {"inRange", false}};
            network["security"] = security.isEmpty() ? "Open" : security;
            network["secured"] = !security.isEmpty();
            network["name"] = profile.value("name");
            network["active"] = active;
            network["saved"] = true;
            network["uuid"] = profile.value("uuid");
            network["autoconnect"] = profile.value("autoconnect");
            network["interface"] = active ? m_interface : QString();
            network["ipAddress"] = active ? m_ipAddress : QString();
            if (active) activeSsid = ssid;
            values << network;
        }
        for (const QString &key : matchedAccessPoints) strongest.remove(key);
        for (auto network : strongest) {
            if (network.value("active").toBool()) {
                network["interface"] = m_interface;
                network["ipAddress"] = m_ipAddress;
            }
            values << network;
        }
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
        if (m_activeSsid != activeSsid || m_connected != m_deviceConnected) {
            m_activeSsid = activeSsid;
            m_connected = m_deviceConnected;
            emit stateChanged();
        }
    }

    void runAction(const QStringList &args, const QString &description, bool scanAfter,
                   const QString &ssid = {}, bool promptOnMissingSecret = false,
                   const QByteArray &input = {}, const QString &uuid = {}) {
        if (m_actionRunning) return;
        ++m_generation; // Discard an older snapshot when its nmcli process completes.
        m_actionRunning = true;
        setBusy(true);
        setMessage(description);
        setError({});
        runNmcli(args, [this, description, scanAfter, ssid, uuid, promptOnMissingSecret,
                        hasSecret = !input.isEmpty()](
                              int code, const QByteArray &, const QByteArray &error) {
            m_actionRunning = false;
            if (code != 0) {
                const QString detail = hasSecret ? description + " failed"
                                                 : failure(error, description + " failed");
                setError(detail);
                if (promptOnMissingSecret &&
                    (detail.contains("secret", Qt::CaseInsensitive) ||
                     detail.contains("password", Qt::CaseInsensitive) ||
                     detail.contains("credential", Qt::CaseInsensitive)))
                    emit passwordRequested(ssid, uuid);
            } else if (!ssid.isEmpty()) {
                emit connectionSucceeded();
            }
            queueRefresh(code == 0 && scanAfter);
        }, input);
    }

    QVariantList m_networks;
    QList<QVariantMap> m_profiles;
    QStringList m_profileUuids;
    int m_profileIndex = 0;
    QString m_interface;
    QString m_deviceUuid;
    QString m_ipAddress;
    bool m_deviceConnected = false;
    bool m_scanAfter = false;
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
    // A toggle-close needs only the local socket, not another GUI instance.
    if (closeRunningPopup())
        return 0;

    QGuiApplication app(argc, argv);
    QGuiApplication::setDesktopFileName("network-popup");

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

    NetworkController controller;
    QQmlApplicationEngine engine;

    QLocalServer::removeServer(socketName());
    QLocalServer server;
    if (!server.listen(socketName()))
        return 1;
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        while (QLocalSocket *socket = server.nextPendingConnection()) {
            const auto handleMessage = [socket, &engine] {
                if (socket->readAll().contains("close"))
                    QMetaObject::invokeMethod(engine.rootObjects().constFirst(), "dismiss");
                socket->disconnectFromServer();
            };
            QObject::connect(socket, &QLocalSocket::readyRead, socket, handleMessage);
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            if (socket->bytesAvailable())
                handleMessage();
        }
    });

    engine.rootContext()->setContextProperty("networkController", &controller);
    engine.rootContext()->setContextProperty(
        "themeColors", loadPalette(QDir::homePath() + "/.config/rofi/colors.rasi"));
    engine.load(QUrl::fromLocalFile(QDir::homePath() + "/.local/share/network-popup/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    controller.refresh();
    return app.exec();
}

#include "main.moc"

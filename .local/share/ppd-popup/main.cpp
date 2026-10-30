#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QVariantList>
#include <unistd.h>

namespace {
QString socketName() {
    return "ppd-popup-" + QString::number(getuid());
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

class PpdController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString activeProfile READ activeProfile NOTIFY activeProfileChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit PpdController(QObject *parent = nullptr)
        : QObject(parent),
          m_serviceWatcher(serviceName(), m_bus,
                           QDBusServiceWatcher::WatchForOwnerChange, this) {
        m_signalsConnected = m_bus.connect(
            serviceName(), objectPath(), "org.freedesktop.DBus.Properties",
            "PropertiesChanged", this,
            SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
        connect(&m_serviceWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
                [this](const QString &, const QString &, const QString &owner) {
            ++m_request;
            setProfiles({});
            setActiveProfile({});
            m_pendingProfile.clear();
            if (owner.isEmpty()) {
                finishWithError("power-profiles-daemon is unavailable");
            } else {
                loadState();
            }
        });
    }

    QVariantList profiles() const { return m_profiles; }
    QString activeProfile() const { return m_activeProfile; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }

    Q_INVOKABLE void refresh() {
        if (!m_busy)
            loadState();
    }

    Q_INVOKABLE void activate(int index) {
        if (m_busy || index < 0 || index >= m_profiles.size())
            return;
        const QString name = m_profiles[index].toMap().value("name").toString();
        if (name == m_activeProfile)
            return;
        setBusy(true);
        setStatus("Switching to " + name + "…");
        m_activation = true;
        QDBusMessage message = QDBusMessage::createMethodCall(
            serviceName(), objectPath(), "org.freedesktop.DBus.Properties", "Set");
        message << serviceName() << QStringLiteral("ActiveProfile")
                << QVariant::fromValue(QDBusVariant(name));
        auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, name](QDBusPendingCallWatcher *watcher) {
            const QDBusPendingReply<> reply = *watcher;
            watcher->deleteLater();
            m_activation = false;
            if (reply.isError()) {
                ++m_request;
                finishWithError("Failed to switch power profile: " + reply.error().message());
                return;
            }
            m_pendingProfile = name;
            loadState();
        });
    }

signals:
    void profilesChanged();
    void activeProfileChanged();
    void busyChanged();
    void statusChanged();

private slots:
    void propertiesChanged(const QString &interface, const QVariantMap &changed,
                           const QStringList &invalidated) {
        if (interface != serviceName())
            return;
        if (changed.contains("Profiles") || changed.contains("ActiveProfile")
            || invalidated.contains("Profiles") || invalidated.contains("ActiveProfile")) {
            if (changed.contains("ActiveProfile"))
                setActiveProfile(changed.value("ActiveProfile").toString());
            if (invalidated.contains("ActiveProfile"))
                setActiveProfile({});
            if (invalidated.contains("Profiles"))
                setProfiles({});
            loadState();
        }
    }

private:
    static QString serviceName() { return QStringLiteral("net.hadess.PowerProfiles"); }
    static QString objectPath() { return QStringLiteral("/net/hadess/PowerProfiles"); }

    void loadState() {
        const quint64 request = ++m_request;
        if (!m_bus.isConnected() || !m_signalsConnected) {
            setProfiles({});
            setActiveProfile({});
            finishWithError("Unable to connect to power-profiles-daemon on the system bus");
            return;
        }
        setBusy(true);
        if (!m_activation)
            setStatus("Updating…");
        QDBusMessage message = QDBusMessage::createMethodCall(
            serviceName(), objectPath(), "org.freedesktop.DBus.Properties", "GetAll");
        message << serviceName();
        auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, request](QDBusPendingCallWatcher *watcher) {
            const QDBusPendingReply<QVariantMap> reply = *watcher;
            watcher->deleteLater();
            // Ignore replies from before a newer request or service-owner change.
            if (request != m_request)
                return;
            if (reply.isError()) {
                setProfiles({});
                setActiveProfile({});
                m_pendingProfile.clear();
                finishWithError("Power profiles unavailable: " + reply.error().message());
                return;
            }
            const QVariantMap state = reply.value();
            const QVariant rawProfiles = state.value("Profiles");
            QList<QVariantMap> available;
            if (rawProfiles.metaType() == QMetaType::fromType<QDBusArgument>()) {
                const QDBusArgument argument = rawProfiles.value<QDBusArgument>();
                argument.beginArray();
                while (!argument.atEnd()) {
                    QVariantMap profile;
                    argument >> profile;
                    available.append(profile);
                }
                argument.endArray();
            } else {
                for (const QVariant &profile : rawProfiles.toList())
                    available.append(profile.toMap());
            }
            // Show only profiles actually advertised by PPD, in a stable order.
            static const QList<QPair<QString, QString>> descriptions{
                {"power-saver", "Reduce power use and extend battery life"},
                {"balanced", "Balance performance and power use"},
                {"performance", "Prioritize performance over power saving"}
            };
            QVariantList profiles;
            for (const auto &description : descriptions) {
                for (const QVariantMap &profile : available) {
                    if (profile.value("Profile").toString() == description.first) {
                        profiles.append(QVariantMap{{"name", description.first},
                                                    {"description", description.second}});
                        break;
                    }
                }
            }
            const QString active = state.value("ActiveProfile").toString();
            setProfiles(profiles);
            setActiveProfile(active);
            if (m_activation)
                return;
            const QString expected = m_pendingProfile;
            m_pendingProfile.clear();
            if (!expected.isEmpty() && expected != active) {
                finishWithError("Profile change was not confirmed by power-profiles-daemon");
                return;
            }
            setStatus(active.isEmpty() ? "No active profile" : "Active: " + active);
            setBusy(false);
        });
    }

    void finishWithError(const QString &message) {
        const QStringList lines = message.split('\n', Qt::SkipEmptyParts);
        setStatus(lines.isEmpty() ? "Operation failed" : lines.join(" "));
        setBusy(m_activation);
    }

    void setProfiles(const QVariantList &profiles) {
        if (m_profiles == profiles)
            return;
        m_profiles = profiles;
        emit profilesChanged();
    }

    void setActiveProfile(const QString &activeProfile) {
        if (m_activeProfile == activeProfile)
            return;
        m_activeProfile = activeProfile;
        emit activeProfileChanged();
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

    QVariantList m_profiles;
    QString m_activeProfile;
    bool m_busy = false;
    QString m_status = "Updating…";
    QDBusConnection m_bus = QDBusConnection::systemBus();
    QDBusServiceWatcher m_serviceWatcher;
    bool m_activation = false;
    QString m_pendingProfile;
    quint64 m_request = 0;
    bool m_signalsConnected = false;
};

int main(int argc, char **argv) {
    // A toggle-close needs only the local socket, not another GUI instance.
    if (closeRunningPopup())
        return 0;

    QGuiApplication app(argc, argv);
    QGuiApplication::setDesktopFileName("ppd-popup");

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

    PpdController controller;
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

    engine.rootContext()->setContextProperty("ppdController", &controller);
    engine.rootContext()->setContextProperty(
        "themeColors", loadPalette(QDir::homePath() + "/.config/rofi/colors.rasi"));
    engine.load(QUrl::fromLocalFile(QDir::homePath() + "/.local/share/ppd-popup/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    controller.refresh();
    return app.exec();
}

#include "main.moc"

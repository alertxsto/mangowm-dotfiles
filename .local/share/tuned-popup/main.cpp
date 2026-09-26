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
    return "tuned-popup-" + QString::number(getuid());
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

class TunedController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString activeProfile READ activeProfile NOTIFY activeProfileChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit TunedController(QObject *parent = nullptr) : QObject(parent) {}

    QVariantList profiles() const { return m_profiles; }
    QString activeProfile() const { return m_activeProfile; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }

    Q_INVOKABLE void refresh() {
        if (m_busy)
            return;
        setBusy(true);
        setStatus("Updating…");
        loadState();
    }

    Q_INVOKABLE void activate(int index) {
        if (m_busy || index < 0 || index >= m_profiles.size())
            return;
        const QVariantMap profile = m_profiles[index].toMap();
        const QString name = profile.value("name").toString();
        if (name == m_activeProfile)
            return;
        setBusy(true);
        setStatus("Switching to " + name + "…");
        auto *process = new QProcess(this);
        connect(process, &QProcess::finished, this,
                [this, process, name](int code, QProcess::ExitStatus exitStatus) {
            const QString output = QString::fromUtf8(
                process->readAllStandardOutput() + process->readAllStandardError()).trimmed();
            process->deleteLater();
            if (exitStatus != QProcess::NormalExit || code != 0) {
                finishWithError(output.isEmpty() ? "Failed to switch profile" : output);
                return;
            }
            loadState();
        });
        connect(process, &QProcess::errorOccurred, this,
                [this, process](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
                return;
            process->deleteLater();
            finishWithError("tuned-adm is unavailable");
        });
        process->start("tuned-adm", {"profile", name});
    }

signals:
    void profilesChanged();
    void activeProfileChanged();
    void busyChanged();
    void statusChanged();

private:
    void loadState() {
        QProcess process;
        process.start("tuned-adm", {"list"});
        if (!process.waitForFinished(1500)) {
            finishWithError("tuned-adm timed out");
            return;
        }
        const QString output = QString::fromUtf8(process.readAllStandardOutput());
        static const QRegularExpression profileLine(R"(^-\s+(\S+)\s+-\s+(.+)$)");
        static const QRegularExpression activeLine(R"(^Current active profile:\s*(\S+))");
        // Only profiles useful for a daily laptop; keep ordered by use-case.
        static const QStringList whitelist{
            "laptop-battery-powersave", "powersave",
            "balanced", "balanced-battery",
            "desktop", "desktop-powersave",
            "throughput-performance", "latency-performance"
        };
        QVariantList profiles;
        QString active;
        for (const QString &line : output.split('\n')) {
            const auto pm = profileLine.match(line);
            if (pm.hasMatch()) {
                if (!whitelist.contains(pm.captured(1)))
                    continue;
                profiles.push_back(QVariantMap{
                    {"name", pm.captured(1)},
                    {"description", pm.captured(2)}
                });
                continue;
            }
            const auto am = activeLine.match(line);
            if (am.hasMatch())
                active = am.captured(1);
        }
        // Sort by whitelist order; active profile pinned to top.
        std::sort(profiles.begin(), profiles.end(), [&active](const QVariant &l, const QVariant &r) {
            const QString a = l.toMap().value("name").toString();
            const QString b = r.toMap().value("name").toString();
            if ((a == active) != (b == active))
                return a == active;
            const auto rank = [](const QString &n) {
                static const QStringList order{
                    "laptop-battery-powersave", "powersave",
                    "balanced", "balanced-battery",
                    "desktop", "desktop-powersave",
                    "throughput-performance", "latency-performance"
                };
                const int i = order.indexOf(n);
                return i < 0 ? 99 : i;
            };
            return rank(a) < rank(b);
        });
        setProfiles(profiles);
        setActiveProfile(active);
        setStatus(active.isEmpty() ? "No active profile" : "Active: " + active);
        setBusy(false);
    }

    void finishWithError(const QString &message) {
        const QStringList lines = message.split('\n', Qt::SkipEmptyParts);
        const QString lastLine = lines.isEmpty() ? QString() : lines.constLast();
        setStatus(lastLine.isEmpty() ? "Operation failed" : lastLine);
        setBusy(false);
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
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setDesktopFileName("tuned-popup");

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

    TunedController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("tunedController", &controller);
    engine.rootContext()->setContextProperty(
        "themeColors", loadPalette(QDir::homePath() + "/.config/rofi/colors.rasi"));
    engine.load(QUrl::fromLocalFile(QDir::homePath() + "/.local/share/tuned-popup/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    controller.refresh();
    return app.exec();
}

#include "main.moc"

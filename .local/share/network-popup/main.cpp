#include <QGuiApplication>
#include <QFile>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QScreen>
#include <QRegularExpression>
#include <QTextStream>
#include <QVariantList>
#include <QDir>

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
public:
    explicit NetworkController(QObject *parent = nullptr) : QObject(parent) {}
    QVariantList networks() const { return m_networks; }
    bool scanning() const { return m_scanning; }

    Q_INVOKABLE void refresh() {
        if (m_scanning) return;
        setScanning(true);
        auto *scan = new QProcess(this);
        connect(scan, &QProcess::finished, this, [this, scan](int, QProcess::ExitStatus) {
            scan->deleteLater();
            auto *list = new QProcess(this);
            connect(list, &QProcess::finished, this, [this, list](int, QProcess::ExitStatus) {
                parse(list->readAllStandardOutput());
                list->deleteLater();
                setScanning(false);
            });
            list->start("nmcli", {"-t", "--escape", "yes", "-f", "SIGNAL,SECURITY,SSID", "device", "wifi", "list", "ifname", "wlan0", "--rescan", "no"});
        });
        scan->start("nmcli", {"device", "wifi", "rescan", "ifname", "wlan0"});
    }

    Q_INVOKABLE void activate(int index) {
        if (index < 0 || index >= m_networks.size()) return;
        const auto network = m_networks[index].toMap();
        const QString ssid = network.value("ssid").toString();
        if (network.value("active").toBool()) {
            QProcess::startDetached("nmcli", {"device", "disconnect", "wlan0"});
            QCoreApplication::quit();
            return;
        }
        auto *connect = new QProcess(this);
        QObject::connect(connect, &QProcess::finished, this, [this, connect, ssid](int code, QProcess::ExitStatus) {
            if (code == 0) QCoreApplication::quit();
            else emit passwordRequested(ssid);
            connect->deleteLater();
        });
        connect->start("nmcli", {"--wait", "12", "device", "wifi", "connect", ssid, "ifname", "wlan0"});
    }

    Q_INVOKABLE void connectWithPassword(const QString &ssid, const QString &password) {
        QProcess::startDetached("nmcli", {"--wait", "20", "device", "wifi", "connect", ssid, "password", password, "ifname", "wlan0"});
        QCoreApplication::quit();
    }
    Q_INVOKABLE void wifiOff() { QProcess::startDetached("nmcli", {"radio", "wifi", "off"}); QCoreApplication::quit(); }
    Q_INVOKABLE void advanced() { QProcess::startDetached("kitty", {"--class", "network-config", "-e", "nmtui"}); QCoreApplication::quit(); }

signals:
    void networksChanged();
    void scanningChanged();
    void passwordRequested(const QString &ssid);

private:
    static QStringList fields(const QString &line) {
        QStringList result; QString current; bool escaped = false;
        for (const QChar ch : line) {
            if (escaped) { current += ch; escaped = false; }
            else if (ch == '\\') escaped = true;
            else if (ch == ':') { result << current; current.clear(); }
            else current += ch;
        }
        result << current; return result;
    }
    void parse(const QByteArray &output) {
        QProcess current;
        current.start("nmcli", {"-g", "GENERAL.CONNECTION", "device", "show", "wlan0"});
        current.waitForFinished(500);
        const QString active = QString::fromUtf8(current.readAllStandardOutput()).trimmed();
        QHash<QString, QVariantMap> strongest;
        for (const QString &line : QString::fromUtf8(output).split('\n', Qt::SkipEmptyParts)) {
            const auto item = fields(line); if (item.size() != 3 || item[2].isEmpty()) continue;
            const int signal = item[0].toInt();
            if (!strongest.contains(item[2]) || strongest[item[2]]["signal"].toInt() < signal)
                strongest[item[2]] = {{"ssid", item[2]}, {"signal", signal}, {"secured", !item[1].isEmpty() && item[1] != "--"}, {"active", item[2] == active}};
        }
        auto values = strongest.values();
        std::sort(values.begin(), values.end(), [](const QVariantMap &a, const QVariantMap &b) {
            if (a["active"].toBool() != b["active"].toBool()) return a["active"].toBool();
            return a["signal"].toInt() > b["signal"].toInt();
        });
        m_networks.clear(); for (const auto &value : values) m_networks << value;
        emit networksChanged();
    }
    void setScanning(bool value) { if (m_scanning == value) return; m_scanning = value; emit scanningChanged(); }
    QVariantList m_networks;
    bool m_scanning = false;
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setDesktopFileName("network-popup");
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

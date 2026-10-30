#include "notificationservice.h"
#include "systemcontrols.h"

#include <LayerShellQt/Window>
#include <QAbstractListModel>
#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScreen>
#include <QSGRendererInterface>
#include <QSortFilterProxyModel>
#include <QTextStream>
#include <QTimer>
#include <cstdio>

namespace {
constexpr auto controlName = "org.mango.Notifications";
constexpr auto controlPath = "/org/mango/Notifications";
constexpr auto controlInterface = "org.mango.Notifications";

int roleFor(const QAbstractItemModel *model, const QByteArray &name) {
    const auto roles = model->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value() == name)
            return it.key();
    return -1;
}
}

class Palette final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
public:
    explicit Palette(QObject *parent = nullptr) : QObject(parent) {
        connect(&watcher, &QFileSystemWatcher::fileChanged, this, &Palette::reload);
        connect(&watcher, &QFileSystemWatcher::directoryChanged, this, &Palette::reload);
        reload();
    }
    QVariantMap colors() const { return values; }
public slots:
    void reload() {
        QVariantMap next{
            {"primary", "#d79921"}, {"onPrimary", "#282828"},
            {"surface", "#282828"}, {"surfaceContainer", "#32302f"},
            {"surfaceContainerHigh", "#3c3836"}, {"onSurface", "#ebdbb2"},
            {"onSurfaceVariant", "#a89984"}, {"outlineVariant", "#504945"},
            {"error", "#cc241d"}, {"onError", "#282828"},
            {"blue", "#83a598"}, {"cyan", "#8ec07c"}, {"magenta", "#d3869b"}
        };
        static const QHash<QString, QString> keys{
            {"primary", "primary"}, {"on-primary", "onPrimary"},
            {"surface", "surface"}, {"surface-container", "surfaceContainer"},
            {"surface-container-high", "surfaceContainerHigh"}, {"on-surface", "onSurface"},
            {"on-surface-variant", "onSurfaceVariant"}, {"outline-variant", "outlineVariant"},
            {"error", "error"}, {"on-error", "onError"}, {"blue", "blue"},
            {"cyan", "cyan"}, {"magenta", "magenta"}
        };
        const QString directory = QDir::homePath() + "/.config/rofi";
        const QString path = directory + "/colors.rasi";
        QFile file(path);
        const QRegularExpression token(QStringLiteral(R"(^\s*([a-z-]+):\s*(#[0-9a-fA-F]{6,8});)"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream input(&file);
            while (!input.atEnd()) {
                const auto match = token.match(input.readLine());
                if (match.hasMatch() && keys.contains(match.captured(1)))
                    next[keys.value(match.captured(1))] = match.captured(2);
            }
        }
        if (!watcher.directories().contains(directory) && QDir(directory).exists())
            watcher.addPath(directory);
        if (!watcher.files().contains(path) && QFile::exists(path))
            watcher.addPath(path);
        if (next != values) {
            values = next;
            emit changed();
        }
    }
signals:
    void changed();
private:
    QVariantMap values;
    QFileSystemWatcher watcher;
};

class ToastModel final : public QSortFilterProxyModel {
public:
    explicit ToastModel(NotificationService *service) {
        toastRole = roleFor(service, "toastVisible");
        setSourceModel(service);
        setDynamicSortFilter(true);
        connect(service, &NotificationService::toastCountChanged, this, [this] { invalidate(); });
    }
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override {
        return sourceModel()->data(sourceModel()->index(row, 0, parent), toastRole).toBool();
    }
private:
    int toastRole = -1;
};

class HistoryModel final : public QSortFilterProxyModel {
public:
    explicit HistoryModel(NotificationService *service) {
        appRole = roleFor(service, "appName");
        dateRole = roleFor(service, "timestamp");
        setSourceModel(service);
        setDynamicSortFilter(false);
        const auto refresh = [this] {
            if (queued)
                return;
            queued = true;
            QTimer::singleShot(0, this, [this] {
                queued = false;
                latest.clear();
                for (int row = 0; row < sourceModel()->rowCount(); ++row) {
                    const QModelIndex index = sourceModel()->index(row, 0);
                    const QString app = index.data(appRole).toString();
                    latest[app] = qMax(latest.value(app), index.data(dateRole).toDateTime().toMSecsSinceEpoch());
                }
                invalidate();
                sort(0);
            });
        };
        connect(service, &QAbstractItemModel::rowsInserted, this, refresh);
        connect(service, &QAbstractItemModel::rowsRemoved, this, refresh);
        connect(service, &QAbstractItemModel::modelReset, this, refresh);
        connect(service, &QAbstractItemModel::dataChanged, this, refresh);
        refresh();
    }
protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override {
        const QString a = left.data(appRole).toString();
        const QString b = right.data(appRole).toString();
        if (a != b) {
            if (latest.value(a) != latest.value(b))
                return latest.value(a) > latest.value(b);
            return a < b;
        }
        const auto at = left.data(dateRole).toDateTime();
        const auto bt = right.data(dateRole).toDateTime();
        return at == bt ? left.row() < right.row() : at > bt;
    }
private:
    int appRole = -1;
    int dateRole = -1;
    bool queued = false;
    QHash<QString, qint64> latest;
};

class NotificationImages final : public QQuickImageProvider {
public:
    explicit NotificationImages(NotificationService *source)
        : QQuickImageProvider(QQuickImageProvider::Image), service(source) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override {
        QImage image = service->notificationImage(id.section('?', 0, 0).toUInt());
        if (size)
            *size = image.size();
        if (!image.isNull() && requested.isValid())
            image = image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return image;
    }
private:
    NotificationService *service;
};

class DesktopIcons final : public QQuickImageProvider {
public:
    DesktopIcons() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override {
        const QSize target = requested.isValid() ? requested : QSize(40, 40);
        QImage image = QIcon::fromTheme(id).pixmap(target).toImage();
        if (size)
            *size = image.size();
        return image;
    }
};

class Control final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mango.Notifications")
public:
    Control(NotificationService *source, Palette *palette, QObject *parent = nullptr)
        : QObject(parent), service(source), theme(palette) {
        connect(service, &NotificationService::stateChanged, this, [this] { emit StateChanged(State()); });
    }
    Q_INVOKABLE void copyText(const QString &text) { QGuiApplication::clipboard()->setText(text); }
public slots:
    void Toggle() { service->setPanelVisible(!service->panelVisible()); }
    void Open() { service->setPanelVisible(true); }
    void Close() { service->setPanelVisible(false); }
    void ToggleDnd() { service->setDnd(!service->dnd()); }
    void SetDnd(bool value) { service->setDnd(value); }
    void Reload() { theme->reload(); }
    uint Count() const { return uint(service->count()); }
    QString State() const {
        const int count = service->count();
        const bool dnd = service->dnd();
        const QString alt = (dnd ? "dnd-" : "") + QString(count ? "notification" : "none");
        const QString tooltip = QString::number(count) + (count == 1 ? " notification" : " notifications")
            + (dnd ? " · Do not disturb on" : "");
        return QString::fromUtf8(QJsonDocument(QJsonObject{
            {"text", ""}, {"alt", alt}, {"class", dnd ? "dnd" : count ? "notification" : "none"},
            {"tooltip", tooltip}
        }).toJson(QJsonDocument::Compact));
    }
    void Clear() { service->clear(); }
    void Quit() { QTimer::singleShot(0, QCoreApplication::instance(), [] { QCoreApplication::exit(0); }); }
signals:
    void StateChanged(const QString &state);
private:
    NotificationService *service;
    Palette *theme;
};

class Subscriber final : public QObject {
    Q_OBJECT
public slots:
    void state(const QString &value) {
        const QByteArray line = value.toUtf8();
        std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }
};

int runClient(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString option = app.arguments().value(1);
    if (option == "--version") {
        std::puts("mango-notify 1.0");
        return 0;
    }
    if (option == "--help") {
        std::puts("mango-notify [--daemon|--toggle|--open|--close|--dnd|--dnd-on|--dnd-off|--reload|--count|--subscribe|--clear|--quit]");
        return 0;
    }
    const QHash<QString, QString> methods{
        {"--toggle", "Toggle"}, {"--open", "Open"}, {"--close", "Close"},
        {"--dnd", "ToggleDnd"}, {"--dnd-on", "SetDnd"}, {"--dnd-off", "SetDnd"},
        {"--reload", "Reload"}, {"--count", "Count"}, {"--clear", "Clear"}, {"--quit", "Quit"},
        {"--subscribe", "State"}
    };
    if (!methods.contains(option) || app.arguments().size() != 2) {
        std::fprintf(stderr, "Unknown arguments. Use mango-notify --help.\n");
        return 2;
    }
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        std::fprintf(stderr, "mango-notify: %s\n", qPrintable(bus.lastError().message()));
        return 1;
    }
    if (!bus.interface()->isServiceRegistered(controlName))
        bus.interface()->startService(controlName);
    QDBusInterface remote(controlName, controlPath, controlInterface, bus);
    if (!remote.isValid()) {
        std::fprintf(stderr, "mango-notify: %s\n", qPrintable(remote.lastError().message()));
        return 1;
    }
    Subscriber listener;
    if (option == "--subscribe" && !bus.connect(controlName, controlPath, controlInterface, "StateChanged", &listener, SLOT(state(QString))))
        return 1;
    const QDBusMessage reply = option == "--dnd-on" || option == "--dnd-off"
        ? remote.call("SetDnd", option == "--dnd-on") : remote.call(methods.value(option));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        std::fprintf(stderr, "mango-notify: %s\n", qPrintable(reply.errorMessage()));
        return 1;
    }
    if (option == "--subscribe") {
        listener.state(reply.arguments().value(0).toString());
        return app.exec();
    }
    if (option == "--count")
        std::printf("%u\n", reply.arguments().value(0).toUInt());
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && QByteArray(argv[1]) != "--daemon")
        return runClient(argc, argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("mango-notify");
    QGuiApplication::setDesktopFileName("mango-notify");
    app.setQuitOnLastWindowClosed(false);
    const auto session = QDBusConnection::sessionBus();
    if (session.isConnected() && session.interface()->isServiceRegistered(controlName))
        return 0;
    NotificationService notifications;
    if (!notifications.registered()) {
        std::fprintf(stderr, "mango-notify: %s\n", qPrintable(notifications.registrationError()));
        return 1;
    }
    Palette palette;
    Control control(&notifications, &palette);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(controlPath, &control, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals) || !bus.registerService(controlName)) {
        std::fprintf(stderr, "mango-notify: cannot register control service\n");
        return 1;
    }
    SystemControls controls;
    QObject::connect(&controls, &SystemControls::launchRequested, &app, [&](const QString &target) {
        notifications.setPanelVisible(false);
        if (target == "audio") {
            QProcess::startDetached("pavucontrol", {});
        } else {
            static const QHash<QString, QString> names{{"network", "network-popup"}, {"bluetooth", "bluetooth-popup"}, {"power", "ppd-popup"}};
            const auto name = names.constFind(target);
            if (name != names.cend())
                QProcess::startDetached(QDir::homePath() + "/.local/bin/toggle-popup", {*name});
        }
    });
    ToastModel toasts(&notifications);
    HistoryModel history(&notifications);
    QQmlApplicationEngine engine;
    engine.addImageProvider("notification", new NotificationImages(&notifications));
    engine.addImageProvider("desktop", new DesktopIcons);
    engine.rootContext()->setContextProperty("notifyService", &notifications);
    engine.rootContext()->setContextProperty("systemControls", &controls);
    engine.rootContext()->setContextProperty("themePalette", &palette);
    engine.rootContext()->setContextProperty("notifyControl", &control);
    engine.rootContext()->setContextProperty("toastModel", &toasts);
    engine.rootContext()->setContextProperty("historyModel", &history);
    engine.load(QUrl::fromLocalFile(QDir::homePath() + "/.local/share/mango-notify/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (window) {
        LayerShellQt::Window *layer = nullptr;
        if (QGuiApplication::platformName().startsWith("wayland")) {
            layer = LayerShellQt::Window::get(window);
            layer->setScope(QStringLiteral("mango-notify"));
            layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop)
                              | LayerShellQt::Window::AnchorRight);
            layer->setLayer(LayerShellQt::Window::LayerOverlay);
            layer->setMargins(QMargins(0, 50, 12, 0));
            layer->setExclusiveZone(-1);
            layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
            const auto resizeLayer = [window, layer] {
                layer->setDesiredSize(QSize(440, window->property("preferredHeight").toInt()));
            };
            QObject::connect(window, &QWindow::heightChanged, window, resizeLayer);
            resizeLayer();
        }
        const auto chooseScreen = [window] {
            QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
            if (!screen)
                screen = QGuiApplication::primaryScreen();
            if (screen)
                window->setScreen(screen);
        };
        QObject::connect(&notifications, &NotificationService::panelVisibleChanged, window, [&, chooseScreen, layer] {
            if (layer)
                layer->setKeyboardInteractivity(notifications.panelVisible()
                    ? LayerShellQt::Window::KeyboardInteractivityOnDemand
                    : LayerShellQt::Window::KeyboardInteractivityNone);
            if (notifications.panelVisible()) {
                chooseScreen();
                QTimer::singleShot(30, window, &QWindow::requestActivate);
            }
        });
        chooseScreen();
    }
    return app.exec();
}

#include "main.moc"

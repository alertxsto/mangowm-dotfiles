#include "systemcontrols.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QSocketNotifier>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <climits>
#include <functional>
#include <pulse/pulseaudio.h>
#include <pulse/timeval.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

// Pulse's mainloop objects are intentionally opaque in its public API.
struct pa_io_event {
    pa_mainloop_api *api;
    int fd;
    pa_io_event_cb_t callback;
    void *userdata;
    pa_io_event_destroy_cb_t destroy = nullptr;
    QSocketNotifier *read = nullptr, *write = nullptr, *exception = nullptr;
};
struct pa_time_event {
    pa_mainloop_api *api;
    pa_time_event_cb_t callback;
    void *userdata;
    pa_time_event_destroy_cb_t destroy = nullptr;
    timeval deadline{};
    QTimer *timer = nullptr;
};
struct pa_defer_event {
    pa_mainloop_api *api;
    pa_defer_event_cb_t callback;
    void *userdata;
    pa_defer_event_destroy_cb_t destroy = nullptr;
    QTimer *timer = nullptr;
};

namespace {
const QString nmService = QStringLiteral("org.freedesktop.NetworkManager");
const QString nmPath = QStringLiteral("/org/freedesktop/NetworkManager");
const QString nmInterface = QStringLiteral("org.freedesktop.NetworkManager");
const QString bluezService = QStringLiteral("org.bluez");
const QString adapterInterface = QStringLiteral("org.bluez.Adapter1");
const QString powerService = QStringLiteral("net.hadess.PowerProfiles");
const QString powerPath = QStringLiteral("/net/hadess/PowerProfiles");
const QString powerInterface = QStringLiteral("net.hadess.PowerProfiles");
const QString playerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString playerPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString mprisPrefix = QStringLiteral("org.mpris.MediaPlayer2.");

QVariant unwrapped(const QVariant &value) {
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return value.value<QDBusVariant>().variant();
    return value;
}
QVariantMap variantMap(const QVariant &value) {
    const QVariant v = unwrapped(value);
    if (v.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<QVariantMap>(v.value<QDBusArgument>());
    return v.toMap();
}
QString objectPath(const QVariant &value) {
    const QVariant v = unwrapped(value);
    return v.canConvert<QDBusObjectPath>() ? v.value<QDBusObjectPath>().path() : v.toString();
}
qint64 readNumber(const QString &path, bool *ok = nullptr) {
    QFile file(path);
    bool valid = file.open(QIODevice::ReadOnly);
    const qint64 number = valid ? file.read(64).trimmed().toLongLong(&valid) : 0;
    if (ok) *ok = valid;
    return number;
}

// No GLib dependency and no helper thread: Pulse runs on Qt's own event loop.
class PulseLoop final : public QObject {
public:
    pa_mainloop_api api{};
    QSet<pa_io_event *> io;
    QSet<pa_time_event *> times;
    QSet<pa_defer_event *> deferred;

    explicit PulseLoop(QObject *parent) : QObject(parent) {
        api.userdata = this;
        api.io_new = [](pa_mainloop_api *a, int fd, pa_io_event_flags_t flags,
                        pa_io_event_cb_t callback, void *userdata) {
            auto *loop = static_cast<PulseLoop *>(a->userdata);
            auto *event = new pa_io_event{a, fd, callback, userdata};
            event->read = new QSocketNotifier(fd, QSocketNotifier::Read, loop);
            event->write = new QSocketNotifier(fd, QSocketNotifier::Write, loop);
            event->exception = new QSocketNotifier(fd, QSocketNotifier::Exception, loop);
            QObject::connect(event->read, &QSocketNotifier::activated, loop, [event] {
                event->callback(event->api, event, event->fd, PA_IO_EVENT_INPUT, event->userdata);
            });
            QObject::connect(event->write, &QSocketNotifier::activated, loop, [event] {
                event->callback(event->api, event, event->fd, PA_IO_EVENT_OUTPUT, event->userdata);
            });
            QObject::connect(event->exception, &QSocketNotifier::activated, loop, [event] {
                event->callback(event->api, event, event->fd, PA_IO_EVENT_ERROR, event->userdata);
            });
            loop->io.insert(event);
            a->io_enable(event, flags);
            return event;
        };
        api.io_enable = [](pa_io_event *e, pa_io_event_flags_t flags) {
            e->read->setEnabled(flags & (PA_IO_EVENT_INPUT | PA_IO_EVENT_HANGUP | PA_IO_EVENT_ERROR));
            e->write->setEnabled(flags & PA_IO_EVENT_OUTPUT);
            e->exception->setEnabled(flags & PA_IO_EVENT_ERROR);
        };
        api.io_free = [](pa_io_event *e) {
            static_cast<PulseLoop *>(e->api->userdata)->io.remove(e);
            if (e->destroy) e->destroy(e->api, e, e->userdata);
            for (auto *notifier : {e->read, e->write, e->exception}) {
                notifier->setEnabled(false);
                // An event may free itself from its activation callback.
                QObject::disconnect(notifier, nullptr, nullptr, nullptr);
                notifier->deleteLater();
            }
            delete e;
        };
        api.io_set_destroy = [](pa_io_event *e, pa_io_event_destroy_cb_t cb) { e->destroy = cb; };
        api.time_new = [](pa_mainloop_api *a, const timeval *tv, pa_time_event_cb_t cb, void *data) {
            auto *loop = static_cast<PulseLoop *>(a->userdata);
            auto *e = new pa_time_event{a, cb, data};
            e->timer = new QTimer(loop);
            e->timer->setSingleShot(true);
            e->timer->setTimerType(Qt::PreciseTimer);
            QObject::connect(e->timer, &QTimer::timeout, loop, [e] {
                timeval now{};
                gettimeofday(&now, nullptr);
                e->callback(e->api, e, &now, e->userdata);
            });
            loop->times.insert(e);
            a->time_restart(e, tv);
            return e;
        };
        api.time_restart = [](pa_time_event *e, const timeval *tv) {
            e->timer->stop();
            if (!tv) return;
            e->deadline = *tv;
            timeval now{};
            gettimeofday(&now, nullptr);
            const qint64 us = (qint64(tv->tv_sec) - now.tv_sec) * 1000000 + tv->tv_usec - now.tv_usec;
            e->timer->start(int(std::clamp<qint64>((us + 999) / 1000, 0, INT_MAX)));
        };
        api.time_free = [](pa_time_event *e) {
            static_cast<PulseLoop *>(e->api->userdata)->times.remove(e);
            if (e->destroy) e->destroy(e->api, e, e->userdata);
            e->timer->stop();
            QObject::disconnect(e->timer, nullptr, nullptr, nullptr);
            e->timer->deleteLater();
            delete e;
        };
        api.time_set_destroy = [](pa_time_event *e, pa_time_event_destroy_cb_t cb) { e->destroy = cb; };
        api.defer_new = [](pa_mainloop_api *a, pa_defer_event_cb_t cb, void *data) {
            auto *loop = static_cast<PulseLoop *>(a->userdata);
            auto *e = new pa_defer_event{a, cb, data};
            e->timer = new QTimer(loop);
            QObject::connect(e->timer, &QTimer::timeout, loop, [e] {
                e->callback(e->api, e, e->userdata);
            });
            loop->deferred.insert(e);
            e->timer->start(0);
            return e;
        };
        api.defer_enable = [](pa_defer_event *e, int enabled) {
            if (enabled) e->timer->start(0); else e->timer->stop();
        };
        api.defer_free = [](pa_defer_event *e) {
            static_cast<PulseLoop *>(e->api->userdata)->deferred.remove(e);
            if (e->destroy) e->destroy(e->api, e, e->userdata);
            e->timer->stop();
            QObject::disconnect(e->timer, nullptr, nullptr, nullptr);
            e->timer->deleteLater();
            delete e;
        };
        api.defer_set_destroy = [](pa_defer_event *e, pa_defer_event_destroy_cb_t cb) { e->destroy = cb; };
        api.quit = [](pa_mainloop_api *, int code) { QCoreApplication::exit(code); };
    }
    ~PulseLoop() override {
        while (!io.isEmpty()) api.io_free(*io.begin());
        while (!times.isEmpty()) api.time_free(*times.begin());
        while (!deferred.isEmpty()) api.defer_free(*deferred.begin());
    }
};
}

class SystemControlsPrivate final : public QObject {
public:
    SystemControls *q;
    PulseLoop pulseLoop;
    pa_context *pulse = nullptr;
    QString sinkName, sourceName;
    uint32_t sinkIndex = PA_INVALID_INDEX, sourceIndex = PA_INVALID_INDEX;
    pa_cvolume sinkVolume{};
    int volume = 0;
    bool muted = false, micMuted = false, audioAvailable = false;
    int brightness = 0;
    bool brightnessAvailable = false;
    QString backlightPath;
    QFileSystemWatcher backlightWatcher;
    QSocketNotifier *backlightNotifier = nullptr;
    int backlightFd = -1;
    QTimer brightnessDebounce;
    QProcess *brightnessProcess = nullptr;
    int pendingBrightness = -1;
    QString networkName;
    bool networkConnected = false, wifiEnabled = false;
    QString activeConnection;
    quint64 networkRevision = 0, bluezRevision = 0;
    QMap<QString, bool> adapters;
    QString powerProfile, error;
    quint64 powerRevision = 0;
    struct Player { QString owner; QVariantMap properties; quint64 revision = 0; };
    QMap<QString, Player> players;
    QString selectedPlayer;
    QString mediaTitle, mediaArtist, mediaArt;
    quint64 playerRevision = 0;
    bool changePending = false;
    bool shuttingDown = false;

    explicit SystemControlsPrivate(SystemControls *owner)
        : QObject(owner), q(owner), pulseLoop(this) {
        brightnessDebounce.setSingleShot(true);
        brightnessDebounce.setInterval(40);
        connect(&brightnessDebounce, &QTimer::timeout, this, [this] { writeBrightness(); });
        connect(&backlightWatcher, &QFileSystemWatcher::directoryChanged, this,
                [this] { discoverBacklight(); });
        connect(&backlightWatcher, &QFileSystemWatcher::fileChanged, this,
                [this] { discoverBacklight(); });
    }
    ~SystemControlsPrivate() override {
        shuttingDown = true;
        if (pulse) {
            pa_context_set_state_callback(pulse, nullptr, nullptr);
            pa_context_set_subscribe_callback(pulse, nullptr, nullptr);
            pa_context_disconnect(pulse);
            pa_context_unref(pulse);
        }
        closeBacklight();
    }
    void notify() {
        if (changePending) return;
        changePending = true;
        QTimer::singleShot(0, this, [this] { changePending = false; emit q->changed(); });
    }
    void setError(const QString &message) {
        if (error == message) return;
        error = message;
        notify();
    }
    using Reply = std::function<void(const QDBusMessage &)>;
    void call(const QDBusConnection &bus, const QString &service, const QString &path,
              const QString &interface, const QString &method, const QVariantList &arguments,
              Reply complete = {}, bool reportError = false) {
        QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
        message.setArguments(arguments);
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, 5000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, watcher, complete, reportError] {
            const QDBusMessage reply = watcher->reply();
            watcher->deleteLater();
            if (reply.type() == QDBusMessage::ErrorMessage && reportError)
                setError(reply.errorMessage());
            if (complete) complete(reply);
        });
    }
    void getAll(const QDBusConnection &bus, const QString &service, const QString &path,
                const QString &interface, std::function<void(const QVariantMap &)> complete) {
        call(bus, service, path, propertiesInterface, QStringLiteral("GetAll"), {interface},
             [complete](const QDBusMessage &reply) {
            if (reply.type() != QDBusMessage::ErrorMessage && !reply.arguments().isEmpty())
                complete(variantMap(reply.arguments().first()));
        });
    }
    void setProperty(const QString &service, const QString &path, const QString &interface,
                     const QString &property, const QVariant &value) {
        call(QDBusConnection::systemBus(), service, path, propertiesInterface,
             QStringLiteral("Set"), {interface, property, QVariant::fromValue(QDBusVariant(value))},
             [this, service](const QDBusMessage &reply) {
            if (reply.type() == QDBusMessage::ErrorMessage) return;
            if (service == nmService) refreshNetwork(); else refreshBluetooth();
        }, true);
    }
    static void unref(pa_operation *operation) { if (operation) pa_operation_unref(operation); }
    static void audioWriteComplete(pa_context *context, int success, void *userdata) {
        auto *d = static_cast<SystemControlsPrivate *>(userdata);
        if (!success) d->setError(QString::fromUtf8(pa_strerror(pa_context_errno(context))));
    }
    void audioOperation(pa_operation *operation) {
        if (!operation) setError(QString::fromUtf8(pa_strerror(pa_context_errno(pulse))));
        unref(operation);
    }
    void connectPulse() {
        if (shuttingDown) return;
        if (pulse) { pa_context_unref(pulse); pulse = nullptr; }
        pulse = pa_context_new(&pulseLoop.api, "mango-notify");
        if (!pulse) { setError(QStringLiteral("Could not create audio connection")); return; }
        pa_context_set_state_callback(pulse, [](pa_context *context, void *userdata) {
            auto *d = static_cast<SystemControlsPrivate *>(userdata);
            const pa_context_state_t state = pa_context_get_state(context);
            if (state == PA_CONTEXT_READY) {
                pa_context_set_subscribe_callback(context,
                    [](pa_context *, pa_subscription_event_type_t event, uint32_t index, void *data) {
                        auto *self = static_cast<SystemControlsPrivate *>(data);
                        const int facility = event & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
                        if (facility == PA_SUBSCRIPTION_EVENT_SERVER) self->refreshAudio();
                        else if (facility == PA_SUBSCRIPTION_EVENT_SINK
                                 && (index == self->sinkIndex || self->sinkIndex == PA_INVALID_INDEX)) self->refreshSink();
                        else if (facility == PA_SUBSCRIPTION_EVENT_SOURCE
                                 && (index == self->sourceIndex || self->sourceIndex == PA_INVALID_INDEX)) self->refreshSource();
                    }, d);
                unref(pa_context_subscribe(context, pa_subscription_mask_t(
                    PA_SUBSCRIPTION_MASK_SERVER | PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE),
                    nullptr, nullptr));
                d->refreshAudio();
            } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
                d->audioAvailable = false;
                d->sinkIndex = d->sourceIndex = PA_INVALID_INDEX;
                d->notify();
                if (state == PA_CONTEXT_FAILED) {
                    d->setError(QString::fromUtf8(pa_strerror(pa_context_errno(context))));
                    // A one-shot retry only after a disconnected server, never a status poll.
                    QTimer::singleShot(3000, d, [d] { d->connectPulse(); });
                }
            }
        }, this);
        if (pa_context_connect(pulse, nullptr, PA_CONTEXT_NOFAIL, nullptr) < 0) {
            setError(QString::fromUtf8(pa_strerror(pa_context_errno(pulse))));
        }
    }
    void refreshAudio() {
        unref(pa_context_get_server_info(pulse, [](pa_context *, const pa_server_info *info, void *data) {
            if (!info) return;
            auto *d = static_cast<SystemControlsPrivate *>(data);
            d->sinkName = QString::fromUtf8(info->default_sink_name ? info->default_sink_name : "");
            d->sourceName = QString::fromUtf8(info->default_source_name ? info->default_source_name : "");
            d->sinkIndex = d->sourceIndex = PA_INVALID_INDEX;
            d->audioAvailable = false;
            d->refreshSink();
            d->refreshSource();
            d->notify();
        }, this));
    }
    void refreshSink() {
        if (sinkName.isEmpty() || pa_context_get_state(pulse) != PA_CONTEXT_READY) return;
        unref(pa_context_get_sink_info_by_name(pulse, sinkName.toUtf8().constData(),
            [](pa_context *, const pa_sink_info *info, int eol, void *data) {
                auto *d = static_cast<SystemControlsPrivate *>(data);
                if (eol < 0) { d->audioAvailable = false; d->sinkIndex = PA_INVALID_INDEX; d->notify(); }
                if (!info || QString::fromUtf8(info->name) != d->sinkName) return;
                d->sinkIndex = info->index;
                d->sinkVolume = info->volume;
                d->volume = std::clamp(int((quint64(pa_cvolume_max(&info->volume)) * 100 + PA_VOLUME_NORM / 2)
                                          / PA_VOLUME_NORM), 0, 100);
                d->muted = info->mute;
                d->audioAvailable = true;
                d->notify();
            }, this));
    }
    void refreshSource() {
        if (sourceName.isEmpty() || pa_context_get_state(pulse) != PA_CONTEXT_READY) return;
        unref(pa_context_get_source_info_by_name(pulse, sourceName.toUtf8().constData(),
            [](pa_context *, const pa_source_info *info, int eol, void *data) {
                auto *d = static_cast<SystemControlsPrivate *>(data);
                if (eol < 0) { d->sourceIndex = PA_INVALID_INDEX; d->notify(); }
                if (!info || QString::fromUtf8(info->name) != d->sourceName) return;
                d->sourceIndex = info->index;
                d->micMuted = info->mute;
                d->notify();
            }, this));
    }
    void refreshNetwork() {
        const quint64 revision = ++networkRevision;
        getAll(QDBusConnection::systemBus(), nmService, nmPath, nmInterface,
               [this, revision](const QVariantMap &properties) {
            if (revision != networkRevision) return;
            wifiEnabled = properties.value("WirelessEnabled").toBool();
            networkConnected = properties.value("State").toUInt() >= 50;
            activeConnection = objectPath(properties.value("PrimaryConnection"));
            networkName.clear();
            notify();
            if (activeConnection.isEmpty() || activeConnection == "/") return;
            getAll(QDBusConnection::systemBus(), nmService, activeConnection,
                   QStringLiteral("org.freedesktop.NetworkManager.Connection.Active"),
                   [this, revision](const QVariantMap &connection) {
                if (revision != networkRevision) return;
                networkName = connection.value("Id").toString();
                networkConnected = networkConnected && connection.value("State").toUInt() == 2;
                notify();
            });
        });
    }
    void refreshBluetooth() {
        const quint64 revision = ++bluezRevision;
        call(QDBusConnection::systemBus(), bluezService, QStringLiteral("/"),
             QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("GetManagedObjects"), {},
             [this, revision](const QDBusMessage &reply) {
            if (revision != bluezRevision || reply.type() == QDBusMessage::ErrorMessage
                || reply.arguments().isEmpty()) return;
            QMap<QString, bool> result;
            const QDBusArgument objects = reply.arguments().first().value<QDBusArgument>();
            objects.beginMap();
            while (!objects.atEnd()) {
                QDBusObjectPath path;
                objects.beginMapEntry();
                objects >> path;
                objects.beginMap();
                while (!objects.atEnd()) {
                    QString interface;
                    QVariantMap properties;
                    objects.beginMapEntry();
                    objects >> interface >> properties;
                    objects.endMapEntry();
                    if (interface == adapterInterface)
                        result.insert(path.path(), properties.value("Powered").toBool());
                }
                objects.endMap();
                objects.endMapEntry();
            }
            objects.endMap();
            adapters = result;
            notify();
        });
    }
    void closeBacklight() {
        if (backlightNotifier) { delete backlightNotifier; backlightNotifier = nullptr; }
        if (backlightFd >= 0) { ::close(backlightFd); backlightFd = -1; }
    }
    void watchPath(QFileSystemWatcher &watcher, const QString &path) {
        if (QFileInfo::exists(path) && !watcher.files().contains(path) && !watcher.directories().contains(path))
            watcher.addPath(path);
    }
    void discoverBacklight() {
        const QString root = QStringLiteral("/sys/class/backlight");
        watchPath(backlightWatcher, QStringLiteral("/sys/class"));
        watchPath(backlightWatcher, root);
        QString chosen;
        int bestScore = -1;
        const QString requested = qEnvironmentVariable("BACKLIGHT_DEVICE");
        for (const QString &name : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString path = root + '/' + name;
            if (readNumber(path + "/max_brightness") <= 0) continue;
            QFile type(path + "/type");
            const QByteArray kind = type.open(QIODevice::ReadOnly) ? type.readAll().trimmed() : QByteArray();
            const int score = name == requested ? 100 : kind == "raw" ? 3 : kind == "platform" ? 2 : 1;
            if (score > bestScore) { bestScore = score; chosen = path; }
        }
        if (chosen != backlightPath) {
            closeBacklight();
            if (!backlightPath.isEmpty()) {
                backlightWatcher.removePath(backlightPath);
                backlightWatcher.removePath(backlightPath + "/brightness");
            }
            backlightPath = chosen;
            if (!chosen.isEmpty()) {
                watchPath(backlightWatcher, chosen);
                watchPath(backlightWatcher, chosen + "/brightness");
                const QByteArray actual = QFile::encodeName(chosen + "/actual_brightness");
                backlightFd = ::open(actual.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (backlightFd >= 0) {
                    // Backlight core uses sysfs_notify(actual_brightness), delivered as POLLPRI.
                    backlightNotifier = new QSocketNotifier(backlightFd, QSocketNotifier::Exception, this);
                    connect(backlightNotifier, &QSocketNotifier::activated, this, [this] { readBrightness(); });
                }
            }
        }
        readBrightness();
    }
    void readBrightness() {
        if (backlightFd >= 0) {
            char buffer[64];
            ::lseek(backlightFd, 0, SEEK_SET);
            const auto ignored = ::read(backlightFd, buffer, sizeof buffer);
            Q_UNUSED(ignored);
        }
        bool valid = false;
        const qint64 maximum = readNumber(backlightPath + "/max_brightness");
        const qint64 current = readNumber(backlightPath + "/brightness", &valid);
        brightnessAvailable = !backlightPath.isEmpty() && valid && maximum > 0;
        brightness = brightnessAvailable ? int(std::clamp<qint64>((current * 100 + maximum / 2) / maximum, 0, 100)) : 0;
        notify();
    }
    void writeBrightness() {
        if (brightnessProcess || pendingBrightness < 0) return;
        if (!brightnessAvailable) { pendingBrightness = -1; setError(QStringLiteral("No backlight is available")); return; }
        const int value = pendingBrightness;
        pendingBrightness = -1;
        auto *process = new QProcess(this);
        brightnessProcess = process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("BACKLIGHT_DEVICE", QFileInfo(backlightPath).fileName());
        process->setProcessEnvironment(environment);
        auto complete = [this, process](bool success, const QString &failure) {
            if (brightnessProcess != process) return;
            brightnessProcess = nullptr;
            if (!success) setError(failure);
            process->deleteLater();
            readBrightness();
            if (pendingBrightness >= 0) brightnessDebounce.start();
        };
        connect(process, &QProcess::finished, this, [process, complete](int exitCode, QProcess::ExitStatus status) {
            const QString detail = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
            complete(status == QProcess::NormalExit && exitCode == 0,
                     detail.isEmpty() ? QStringLiteral("Could not set brightness") : detail);
        });
        connect(process, &QProcess::errorOccurred, this, [process, complete](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) complete(false, process->errorString());
        });
        process->start(QDir::homePath() + "/.local/bin/brightness-control", {"set", QString::number(value)});
    }
    void updatePowerProfile(const QString &profile) {
        if (powerProfile == profile) return;
        powerProfile = profile;
        notify();
    }
    void readProfile() {
        const quint64 revision = ++powerRevision;
        call(QDBusConnection::systemBus(), powerService, powerPath, propertiesInterface,
             QStringLiteral("Get"), {powerInterface, QStringLiteral("ActiveProfile")},
             [this, revision](const QDBusMessage &reply) {
            if (revision != powerRevision) return;
            const QString profile = reply.type() != QDBusMessage::ErrorMessage
                    && !reply.arguments().isEmpty()
                ? qvariant_cast<QDBusVariant>(reply.arguments().first()).variant().toString()
                : QString();
            updatePowerProfile(profile);
        });
    }
    void discoverPlayers() {
        call(QDBusConnection::sessionBus(), QStringLiteral("org.freedesktop.DBus"),
             QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
             QStringLiteral("ListNames"), {}, [this](const QDBusMessage &reply) {
            if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) return;
            const QStringList names = reply.arguments().first().toStringList();
            for (const QString &name : names) {
                if (!name.startsWith(mprisPrefix)) continue;
                call(QDBusConnection::sessionBus(), QStringLiteral("org.freedesktop.DBus"),
                     QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
                     QStringLiteral("GetNameOwner"), {name}, [this, name](const QDBusMessage &owner) {
                    if (owner.type() != QDBusMessage::ErrorMessage && !owner.arguments().isEmpty()
                        && !players.contains(name)) addPlayer(name, owner.arguments().first().toString());
                });
            }
        });
    }
    void addPlayer(const QString &name, const QString &owner) {
        Player player;
        player.owner = owner;
        player.revision = ++playerRevision;
        players.insert(name, player);
        refreshPlayer(name);
    }
    void refreshPlayer(const QString &name) {
        if (!players.contains(name)) return;
        const quint64 revision = ++playerRevision;
        players[name].revision = revision;
        getAll(QDBusConnection::sessionBus(), name, playerPath, playerInterface,
               [this, name, revision](const QVariantMap &properties) {
            auto player = players.find(name);
            if (player == players.end() || player->revision != revision) return;
            player->properties = properties;
            choosePlayer(name);
        });
    }
    void choosePlayer(const QString &updated = {}) {
        const auto usable = [this](const QString &name) {
            const auto player = players.constFind(name);
            return player != players.cend() && !player->properties.isEmpty();
        };
        const auto playing = [this](const QString &name) {
            const auto player = players.constFind(name);
            return player != players.cend() && player->properties.value("PlaybackStatus").toString() == "Playing";
        };
        if (playing(updated)) selectedPlayer = updated;
        else if (!playing(selectedPlayer)) {
            QString active;
            for (auto it = players.cbegin(); it != players.cend(); ++it)
                if (playing(it.key())) { active = it.key(); break; }
            if (!active.isEmpty()) selectedPlayer = active;
            else if (!usable(selectedPlayer)) {
                selectedPlayer.clear();
                for (auto it = players.cbegin(); it != players.cend(); ++it)
                    if (usable(it.key())) { selectedPlayer = it.key(); break; }
            }
        }
        const QVariantMap metadata = variantMap(media().value("Metadata"));
        mediaTitle = metadata.value("xesam:title").toString();
        const QVariant artist = unwrapped(metadata.value("xesam:artist"));
        const QStringList artists = artist.metaType() == QMetaType::fromType<QDBusArgument>()
            ? qdbus_cast<QStringList>(artist.value<QDBusArgument>()) : artist.toStringList();
        mediaArtist = artists.join(QStringLiteral(", "));
        const QUrl art(metadata.value("mpris:artUrl").toString());
        mediaArt = art.isValid() && (art.scheme() == "file" || art.scheme() == "https" || art.scheme() == "http")
            ? art.toString() : QString();
        notify();
    }
    const QVariantMap &media() const {
        static const QVariantMap empty;
        const auto player = players.constFind(selectedPlayer);
        return player != players.cend() ? player->properties : empty;
    }
    bool mediaCapability(const char *property) const {
        const QVariantMap &properties = media();
        return properties.value("CanControl", false).toBool() && properties.value(QLatin1String(property), false).toBool();
    }
    void mediaAction(const QString &method) {
        if (selectedPlayer.isEmpty()) { setError(QStringLiteral("No media player is available")); return; }
        const char *capability = method == "Previous" ? "CanGoPrevious" : method == "Next" ? "CanGoNext"
            : media().value("PlaybackStatus").toString() == "Playing" ? "CanPause" : "CanPlay";
        if (!mediaCapability(capability)) { setError(QStringLiteral("The media player does not support this action")); return; }
        setError({});
        call(QDBusConnection::sessionBus(), selectedPlayer, playerPath, playerInterface, method, {}, {}, true);
    }
};

SystemControls::SystemControls(QObject *parent) : QObject(parent), d(std::make_unique<SystemControlsPrivate>(this)) {
    auto system = QDBusConnection::systemBus();
    auto session = QDBusConnection::sessionBus();
    system.connect(nmService, {}, propertiesInterface, "PropertiesChanged", this,
                   SLOT(systemPropertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));
    system.connect(bluezService, {}, propertiesInterface, "PropertiesChanged", this,
                   SLOT(systemPropertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));
    system.connect(powerService, powerPath, propertiesInterface, "PropertiesChanged", this,
                   SLOT(systemPropertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));
    system.connect(bluezService, "/", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", this,
                   SLOT(bluezObjectsChanged(QDBusMessage)));
    system.connect(bluezService, "/", "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved", this,
                   SLOT(bluezObjectsChanged(QDBusMessage)));
    system.connect("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged", this,
                   SLOT(systemOwnerChanged(QString,QString,QString)));
    session.connect({}, playerPath, propertiesInterface, "PropertiesChanged", this,
                    SLOT(mediaPropertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));
    session.connect("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged", this,
                    SLOT(sessionOwnerChanged(QString,QString,QString)));
    d->connectPulse();
    d->discoverBacklight();
    d->readProfile();
    d->refreshNetwork();
    d->refreshBluetooth();
    d->discoverPlayers();
}
SystemControls::~SystemControls() = default;

int SystemControls::volume() const { return d->volume; }
bool SystemControls::muted() const { return d->muted; }
bool SystemControls::micMuted() const { return d->micMuted; }
bool SystemControls::audioAvailable() const { return d->audioAvailable; }
int SystemControls::brightness() const { return d->brightness; }
bool SystemControls::brightnessAvailable() const { return d->brightnessAvailable; }
QString SystemControls::networkName() const { return d->networkName; }
bool SystemControls::networkConnected() const { return d->networkConnected; }
bool SystemControls::wifiEnabled() const { return d->wifiEnabled; }
bool SystemControls::bluetoothAvailable() const { return !d->adapters.isEmpty(); }
bool SystemControls::bluetoothEnabled() const {
    return std::any_of(d->adapters.cbegin(), d->adapters.cend(), [](bool powered) { return powered; });
}
QString SystemControls::powerProfile() const { return d->powerProfile; }
bool SystemControls::mediaAvailable() const { return !d->selectedPlayer.isEmpty(); }
QString SystemControls::mediaTitle() const { return d->mediaTitle; }
QString SystemControls::mediaArtist() const { return d->mediaArtist; }
QString SystemControls::mediaArt() const { return d->mediaArt; }
bool SystemControls::mediaPlaying() const { return d->media().value("PlaybackStatus").toString() == "Playing"; }
bool SystemControls::canPrevious() const { return d->mediaCapability("CanGoPrevious"); }
bool SystemControls::canNext() const { return d->mediaCapability("CanGoNext"); }
QString SystemControls::error() const { return d->error; }

void SystemControls::setVolume(int value) {
    if (!d->audioAvailable || d->sinkIndex == PA_INVALID_INDEX) { d->setError("Audio output is unavailable"); return; }
    d->setError({});
    pa_cvolume volume = d->sinkVolume;
    const pa_volume_t target = pa_volume_t((quint64(std::clamp(value, 0, 100)) * PA_VOLUME_NORM + 50) / 100);
    pa_cvolume_scale(&volume, target);
    d->audioOperation(pa_context_set_sink_volume_by_index(d->pulse, d->sinkIndex, &volume,
                                                        SystemControlsPrivate::audioWriteComplete, d.get()));
}
void SystemControls::setMuted(bool value) {
    if (!d->audioAvailable || d->sinkIndex == PA_INVALID_INDEX) { d->setError("Audio output is unavailable"); return; }
    d->setError({});
    d->audioOperation(pa_context_set_sink_mute_by_index(d->pulse, d->sinkIndex, value,
                                                      SystemControlsPrivate::audioWriteComplete, d.get()));
}
void SystemControls::setMicMuted(bool value) {
    if (!d->pulse || pa_context_get_state(d->pulse) != PA_CONTEXT_READY || d->sourceIndex == PA_INVALID_INDEX) {
        d->setError("Microphone is unavailable"); return;
    }
    d->setError({});
    d->audioOperation(pa_context_set_source_mute_by_index(d->pulse, d->sourceIndex, value,
                                                        SystemControlsPrivate::audioWriteComplete, d.get()));
}
void SystemControls::setBrightness(int value) {
    if (!d->brightnessAvailable) { d->setError("No backlight is available"); return; }
    d->setError({});
    d->pendingBrightness = std::clamp(value, 0, 100);
    d->brightnessDebounce.start();
}
void SystemControls::setWifiEnabled(bool value) {
    d->setError({});
    d->setProperty(nmService, nmPath, nmInterface, QStringLiteral("WirelessEnabled"), value);
}
void SystemControls::setBluetoothEnabled(bool value) {
    if (d->adapters.isEmpty()) { d->setError("No Bluetooth adapter is available"); return; }
    d->setError({});
    const auto adapters = d->adapters;
    for (auto it = adapters.cbegin(); it != adapters.cend(); ++it)
        if (it.value() != value) d->setProperty(bluezService, it.key(), adapterInterface, QStringLiteral("Powered"), value);
}
void SystemControls::mediaPlayPause() { d->mediaAction(QStringLiteral("PlayPause")); }
void SystemControls::mediaPrevious() { d->mediaAction(QStringLiteral("Previous")); }
void SystemControls::mediaNext() { d->mediaAction(QStringLiteral("Next")); }
void SystemControls::launch(const QString &target) {
    if (target != "network" && target != "bluetooth" && target != "power" && target != "audio") {
        d->setError(QStringLiteral("Unknown settings target")); return;
    }
    d->setError({});
    emit launchRequested(target);
}
void SystemControls::systemPropertiesChanged(const QString &interface, const QVariantMap &properties,
                                             const QStringList &invalidated, const QDBusMessage &message) {
    if (interface == nmInterface || (interface == "org.freedesktop.NetworkManager.Connection.Active"
                                    && message.path() == d->activeConnection)) d->refreshNetwork();
    else if (interface == adapterInterface) d->refreshBluetooth();
    else if (interface == powerInterface && message.path() == powerPath) {
        if (properties.contains(QStringLiteral("ActiveProfile"))) {
            ++d->powerRevision;
            d->updatePowerProfile(properties.value(QStringLiteral("ActiveProfile")).toString());
        } else if (invalidated.contains(QStringLiteral("ActiveProfile"))) {
            d->updatePowerProfile({});
            d->readProfile();
        }
    }
}
void SystemControls::mediaPropertiesChanged(const QString &interface, const QVariantMap &properties,
                                            const QStringList &invalidated, const QDBusMessage &message) {
    if (interface != playerInterface) return;
    for (auto it = d->players.begin(); it != d->players.end(); ++it) {
        if (it->owner != message.service()) continue;
        if (it->properties.isEmpty() || !invalidated.isEmpty()) { d->refreshPlayer(it.key()); continue; }
        ++d->playerRevision;
        it->revision = d->playerRevision;
        for (auto p = properties.cbegin(); p != properties.cend(); ++p) it->properties.insert(p.key(), p.value());
        d->choosePlayer(it.key());
    }
}
void SystemControls::systemOwnerChanged(const QString &name, const QString &, const QString &newOwner) {
    if (name == nmService) {
        ++d->networkRevision;
        d->networkName.clear();
        d->networkConnected = d->wifiEnabled = false;
        d->activeConnection.clear();
        d->notify();
        if (!newOwner.isEmpty()) d->refreshNetwork();
    } else if (name == bluezService) {
        ++d->bluezRevision;
        d->adapters.clear();
        d->notify();
        if (!newOwner.isEmpty()) d->refreshBluetooth();
    } else if (name == powerService) {
        ++d->powerRevision;
        d->updatePowerProfile({});
        if (!newOwner.isEmpty()) d->readProfile();
    }
}
void SystemControls::sessionOwnerChanged(const QString &name, const QString &, const QString &newOwner) {
    if (!name.startsWith(mprisPrefix)) return;
    if (newOwner.isEmpty()) { d->players.remove(name); d->choosePlayer(); }
    else d->addPlayer(name, newOwner);
}
void SystemControls::bluezObjectsChanged(const QDBusMessage &) { d->refreshBluetooth(); }

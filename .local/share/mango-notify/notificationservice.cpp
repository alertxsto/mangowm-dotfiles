#include "notificationservice.h"

#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusVariant>
#include <QFileInfo>
#include <QImageReader>
#include <QSettings>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QTextDocument>
#include <QThreadPool>
#include <QUrl>
#include <algorithm>
#include <atomic>

namespace {

constexpr int HistoryLimit = 200;
constexpr int ImageEdge = 512;
constexpr int MaximumImageDimension = 4096;
constexpr qint64 MaximumImageInputBytes = 16 * 1024 * 1024;
constexpr qsizetype ImageBudget = 16 * 1024 * 1024;
constexpr auto ServiceName = "org.freedesktop.Notifications";
constexpr auto ObjectPath = "/org/freedesktop/Notifications";

QVariant hintValue(const QVariantMap &hints, const QString &key)
{
    QVariant value = hints.value(key);
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    return value;
}

QString localImagePath(const QString &value)
{
    if (value.startsWith(QLatin1Char('/')))
        return value;
    const QUrl url(value);
    return url.isLocalFile() ? url.toLocalFile() : QString();
}

QString iconValue(const QString &value)
{
    const QString local = localImagePath(value);
    if (!local.isEmpty())
        return QUrl::fromLocalFile(local).toString();
    // app_icon may be a theme icon name, but is never a remote URL to fetch.
    return QUrl(value).scheme().isEmpty() ? value : QString();
}

QImage boundedImage(QImage image)
{
    if (image.isNull())
        return {};
    if (image.width() > ImageEdge || image.height() > ImageEdge)
        image = image.scaled(ImageEdge, ImageEdge, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    return image.convertToFormat(QImage::Format_RGBA8888);
}

QImage imageFromData(const QVariant &value)
{
    if (value.metaType() != QMetaType::fromType<QDBusArgument>())
        return {};
    const QDBusArgument argument = value.value<QDBusArgument>();
    if (argument.currentSignature() != QStringLiteral("(iiibiiay)"))
        return {};

    int width = 0, height = 0, rowStride = 0, bitsPerSample = 0, channels = 0;
    bool hasAlpha = false;
    QByteArray bytes;
    argument.beginStructure();
    argument >> width >> height >> rowStride >> hasAlpha >> bitsPerSample >> channels >> bytes;
    argument.endStructure();
    if (width <= 0 || height <= 0 || width > MaximumImageDimension
        || height > MaximumImageDimension || rowStride <= 0 || bitsPerSample != 8
        || (channels != 3 && channels != 4) || (hasAlpha && channels != 4)
        || bytes.size() > MaximumImageInputBytes)
        return {};
    const qint64 rowBytes = qint64(width) * channels;
    const qint64 required = qint64(rowStride) * height;
    if (rowStride < rowBytes || required > bytes.size()
        || required > MaximumImageInputBytes)
        return {};

    const QImage::Format format = channels == 3 ? QImage::Format_RGB888
        : hasAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGBX8888;
    // This view is valid only while bytes is alive. Detach even when conversion
    // and scaling leave its format and dimensions unchanged.
    QImage view(reinterpret_cast<const uchar *>(bytes.constData()), width, height,
                rowStride, format);
    QImage result = boundedImage(view);
    return result.constBits() == view.constBits() ? result.copy() : result;
}

QImage imageFromPath(const QString &value)
{
    const QString path = localImagePath(value);
    if (path.isEmpty())
        return {};
    const QFileInfo info(path);
    if (!info.isFile() || info.size() <= 0 || info.size() > MaximumImageInputBytes)
        return {};
    QImageReader reader(path);
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > MaximumImageDimension
        || size.height() > MaximumImageDimension
        || qint64(size.width()) * size.height() * 4 > MaximumImageInputBytes)
        return {};
    if (size.width() > ImageEdge || size.height() > ImageEdge)
        reader.setScaledSize(size.scaled(ImageEdge, ImageEdge, Qt::KeepAspectRatio));
    return boundedImage(reader.read());
}
}

// File-system image decoding never blocks the notification bus or GUI thread.
// The task's signal has a queued connection to the service; destroying the
// service automatically disconnects it without waiting for an I/O worker.
class NotificationImageTask final : public QObject
{
    Q_OBJECT
public:
    NotificationImageTask(uint id, quint64 revision, QString path,
                          std::shared_ptr<std::atomic_bool> cancelled)
        : m_id(id), m_revision(revision), m_path(std::move(path)),
          m_cancelled(std::move(cancelled)) {}

    void run()
    {
        QImage image;
        if (!m_cancelled->load(std::memory_order_relaxed))
            image = imageFromPath(m_path);
        emit loaded(m_id, m_revision, image);
    }

signals:
    void loaded(uint id, quint64 revision, const QImage &image);

private:
    uint m_id;
    quint64 m_revision;
    QString m_path;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};

class NotificationAdaptor final : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    explicit NotificationAdaptor(NotificationService *service)
        : QDBusAbstractAdaptor(service), m_service(service)
    {
        setAutoRelaySignals(false);
    }

public slots:
    uint Notify(const QString &app_name, uint replaces_id, const QString &app_icon,
                const QString &summary, const QString &body, const QStringList &actions,
                const QVariantMap &hints, int expire_timeout)
    {
        return m_service->notify(app_name, replaces_id, app_icon, summary, body,
                                 actions, hints, expire_timeout);
    }

    void CloseNotification(uint id) { m_service->closeNotification(id); }

    QStringList GetCapabilities() const
    {
        return {QStringLiteral("actions"), QStringLiteral("body"),
                QStringLiteral("icon-static"),
                QStringLiteral("persistence"), QStringLiteral("inline-reply")};
    }

    QString GetServerInformation(QString &vendor, QString &version, QString &spec_version) const
    {
        vendor = QStringLiteral("mangowm-dotfiles");
        version = QStringLiteral("1.0");
        spec_version = QStringLiteral("1.2");
        return QStringLiteral("mango-notify");
    }

signals:
    void NotificationClosed(uint id, uint reason);
    void ActionInvoked(uint id, const QString &action_key);
    void NotificationReplied(uint id, const QString &text);

private:
    NotificationService *m_service;
};

struct NotificationService::Notification
{
    uint id = 0;
    QString appName;
    QString summary;
    QString body;
    QString icon;
    QString imageUrl;
    QImage image;
    QDateTime timestamp;
    QVariantList actions;
    QStringList actionKeys;
    QTimer *timer = nullptr;
    quint64 imageRevision = 0;
    std::shared_ptr<std::atomic_bool> imageCancelled =
        std::make_shared<std::atomic_bool>(false);
    int urgency = 1;
    int progress = -1;
    bool toastVisible = false;
    bool isRead = false;
    bool resident = false;
    bool transient = false;
    bool closed = false;
};

NotificationService::NotificationService(QObject *parent)
    : QAbstractListModel(parent)
{
    QSettings settings(QStringLiteral("mangowm"), QStringLiteral("mango-notify"));
    m_dnd = settings.value(QStringLiteral("dnd"), false).toBool();
    m_adaptor = new NotificationAdaptor(this);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        m_registrationError = bus.lastError().message();
        if (m_registrationError.isEmpty())
            m_registrationError = QStringLiteral("The session D-Bus is unavailable.");
        return;
    }
    if (!bus.registerObject(QString::fromLatin1(ObjectPath), this,
                            QDBusConnection::ExportAdaptors)) {
        m_registrationError = bus.lastError().message();
        return;
    }
    if (!bus.registerService(QString::fromLatin1(ServiceName))) {
        m_registrationError = bus.lastError().message();
        if (m_registrationError.isEmpty())
            m_registrationError = QStringLiteral("Another notification daemon owns org.freedesktop.Notifications.");
        bus.unregisterObject(QString::fromLatin1(ObjectPath));
        return;
    }
    m_registered = true;
}

NotificationService::~NotificationService()
{
    for (const auto &notification : m_notifications) {
        notification->imageCancelled->store(true, std::memory_order_relaxed);
        closeOnce(*notification, 4);
    }
    if (m_registered) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.unregisterService(QString::fromLatin1(ServiceName));
        bus.unregisterObject(QString::fromLatin1(ObjectPath));
    }
}

int NotificationService::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant NotificationService::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.column() != 0 || index.row() < 0 || index.row() >= count())
        return {};
    const Notification &notification = *m_notifications[index.row()];
    switch (role) {
    case NotificationIdRole: return notification.id;
    case AppNameRole: return notification.appName;
    case SummaryRole: return notification.summary;
    case BodyRole: return notification.body;
    case IconRole: return notification.icon;
    case ImageRole: return notification.imageUrl;
    case TimestampRole: return notification.timestamp;
    case UrgencyRole: return notification.urgency;
    case ActionsRole: return notification.actions;
    case ProgressRole: return notification.progress;
    case ToastVisibleRole: return notification.toastVisible;
    case IsReadRole: return notification.isRead;
    default: return {};
    }
}

QHash<int, QByteArray> NotificationService::roleNames() const
{
    static const QHash<int, QByteArray> roles{{NotificationIdRole, "notificationId"}, {AppNameRole, "appName"},
            {SummaryRole, "summary"}, {BodyRole, "body"}, {IconRole, "icon"},
            {ImageRole, "image"}, {TimestampRole, "timestamp"}, {UrgencyRole, "urgency"},
            {ActionsRole, "actions"}, {ProgressRole, "progress"},
            {ToastVisibleRole, "toastVisible"}, {IsReadRole, "isRead"}};
    return roles;
}

QImage NotificationService::notificationImage(uint id) const
{
    const int row = rowForId(id);
    return row < 0 ? QImage() : m_notifications[row]->image;
}

int NotificationService::rowForId(uint id) const
{
    for (int row = 0; row < count(); ++row) {
        if (m_notifications[row]->id == id)
            return row;
    }
    return -1;
}

uint NotificationService::nextId()
{
    uint id;
    do {
        id = m_nextId++;
    } while (id == 0 || rowForId(id) >= 0);
    return id;
}

uint NotificationService::notify(const QString &appName, uint replacesId,
                                 const QString &appIcon, const QString &summary,
                                 const QString &body, const QStringList &actions,
                                 const QVariantMap &hints, int expireTimeout)
{
    int row = replacesId == 0 ? -1 : rowForId(replacesId);
    auto entry = std::make_unique<Notification>();
    entry->id = row >= 0 ? replacesId : nextId();
    entry->appName = appName;
    entry->summary = summary;
    entry->body = Qt::mightBeRichText(body)
        ? QTextDocumentFragment::fromHtml(body).toPlainText() : body;
    entry->icon = iconValue(appIcon);
    entry->timestamp = QDateTime::currentDateTime();
    bool validUrgency = false;
    const int urgency = hintValue(hints, QStringLiteral("urgency")).toInt(&validUrgency);
    entry->urgency = validUrgency ? std::clamp(urgency, 0, 2) : 1;
    entry->resident = hintValue(hints, QStringLiteral("resident")).toBool();
    entry->transient = hintValue(hints, QStringLiteral("transient")).toBool();
    bool validProgress = false;
    const int progress = hintValue(hints, QStringLiteral("value")).toInt(&validProgress);
    if (validProgress)
        entry->progress = std::clamp(progress, 0, 100);
    entry->isRead = m_panelVisible;
    entry->toastVisible = !m_panelVisible && (!m_dnd || entry->urgency == 2);
    for (int i = 0; i + 1 < actions.size(); i += 2) {
        if (actions[i].isEmpty() || entry->actionKeys.contains(actions[i]))
            continue;
        entry->actionKeys.append(actions[i]);
        entry->actions.append(QVariantMap{{QStringLiteral("key"), actions[i]},
                                          {QStringLiteral("label"), actions[i + 1]}});
    }

    // The current spelling wins over the deprecated aliases from earlier specs.
    for (const QString &key : {QStringLiteral("image-data"), QStringLiteral("image_data"),
                               QStringLiteral("icon_data")}) {
        if (hints.contains(key)) {
            entry->image = imageFromData(hintValue(hints, key));
            if (!entry->image.isNull())
                break;
        }
    }
    QString imagePath;
    if (entry->image.isNull()) {
        imagePath = hintValue(hints, QStringLiteral("image-path")).toString();
        if (imagePath.isEmpty())
            imagePath = hintValue(hints, QStringLiteral("image_path")).toString();
        imagePath = localImagePath(imagePath);
    }
    entry->imageRevision = ++m_imageRevision;
    if (!entry->image.isNull()) {
        entry->imageUrl = QStringLiteral("image://notification/%1?%2")
                              .arg(entry->id).arg(entry->imageRevision);
    }

    entry->timer = new QTimer(this);
    entry->timer->setSingleShot(true);
    const uint id = entry->id;
    connect(entry->timer, &QTimer::timeout, this, [this, id] { expire(id); });
    const int timeout = entry->urgency == 2 ? 0
        : expireTimeout < 0 ? (entry->urgency == 0 ? 4000 : 6000) : expireTimeout;

    if (row >= 0) {
        Notification &old = *m_notifications[row];
        old.imageCancelled->store(true, std::memory_order_relaxed);
        old.timer->stop();
        old.timer->deleteLater();
        m_toastCount -= old.toastVisible ? 1 : 0;
        m_imageBytes -= old.image.sizeInBytes();
        m_notifications[row] = std::move(entry);
        // A replacement updates its existing history row rather than jumping
        // under the pointer while the user interacts with the list.
        m_toastCount += m_notifications[row]->toastVisible ? 1 : 0;
        m_imageBytes += m_notifications[row]->image.sizeInBytes();
        emit dataChanged(index(row), index(row));
    } else {
        if (count() == HistoryLimit)
            removeAt(count() - 1, 4);
        beginInsertRows(QModelIndex(), 0, 0);
        m_toastCount += entry->toastVisible ? 1 : 0;
        m_imageBytes += entry->image.sizeInBytes();
        m_notifications.insert(m_notifications.begin(), std::move(entry));
        endInsertRows();
        row = 0;
    }
    if (timeout > 0)
        m_notifications[row]->timer->start(timeout);
    if (!imagePath.isEmpty()) {
        const Notification &notification = *m_notifications[row];
        auto *task = new NotificationImageTask(id, notification.imageRevision,
                                               imagePath, notification.imageCancelled);
        connect(task, &NotificationImageTask::loaded, this,
                [this](uint loadedId, quint64 revision, const QImage &image) {
            const int loadedRow = rowForId(loadedId);
            if (loadedRow < 0 || image.isNull()
                || m_notifications[loadedRow]->imageRevision != revision)
                return;
            Notification &loaded = *m_notifications[loadedRow];
            m_imageBytes -= loaded.image.sizeInBytes();
            loaded.image = image;
            m_imageBytes += loaded.image.sizeInBytes();
            loaded.imageUrl = QStringLiteral("image://notification/%1?%2")
                                  .arg(loadedId).arg(revision);
            emit dataChanged(index(loadedRow), index(loadedRow), {ImageRole});
            enforceImageBudget();
        }, Qt::QueuedConnection);
        connect(task, &NotificationImageTask::loaded, task, &QObject::deleteLater,
                Qt::QueuedConnection);
        QThreadPool::globalInstance()->start([task] { task->run(); });
    }
    enforceImageBudget();
    publishState();
    return id;
}

void NotificationService::closeOnce(Notification &notification, uint reason)
{
    if (notification.closed)
        return;
    notification.closed = true;
    notification.timer->stop();
    emit m_adaptor->NotificationClosed(notification.id, reason);
}

void NotificationService::removeAt(int row, uint reason)
{
    if (row < 0 || row >= count())
        return;
    Notification &notification = *m_notifications[row];
    notification.imageCancelled->store(true, std::memory_order_relaxed);
    closeOnce(notification, reason);
    notification.timer->stop();
    notification.timer->deleteLater();
    beginRemoveRows(QModelIndex(), row, row);
    m_toastCount -= notification.toastVisible ? 1 : 0;
    m_imageBytes -= notification.image.sizeInBytes();
    m_notifications.erase(m_notifications.begin() + row);
    endRemoveRows();
}

void NotificationService::closeNotification(uint id)
{
    const int row = rowForId(id);
    if (row < 0)
        return;
    removeAt(row, 3);
    publishState();
}

void NotificationService::dismiss(uint id)
{
    const int row = rowForId(id);
    if (row < 0)
        return;
    removeAt(row, 2);
    publishState();
}

void NotificationService::clear()
{
    if (m_notifications.empty())
        return;
    for (const auto &notification : m_notifications) {
        notification->imageCancelled->store(true, std::memory_order_relaxed);
        closeOnce(*notification, 2);
        notification->timer->deleteLater();
    }
    beginResetModel();
    m_notifications.clear();
    m_toastCount = 0;
    m_imageBytes = 0;
    endResetModel();
    publishState();
}

void NotificationService::expire(uint id)
{
    const int row = rowForId(id);
    if (row < 0 || m_notifications[row]->closed)
        return;
    Notification &notification = *m_notifications[row];
    if (notification.transient) {
        removeAt(row, 1);
    } else {
        closeOnce(notification, 1);
        if (notification.toastVisible) {
            notification.toastVisible = false;
            --m_toastCount;
            emit dataChanged(index(row), index(row), {ToastVisibleRole});
        }
    }
    publishState();
}

void NotificationService::invoke(uint id, const QString &key)
{
    const int row = rowForId(id);
    if (row < 0 || key == QStringLiteral("inline-reply")
        || !m_notifications[row]->actionKeys.contains(key))
        return;
    emit m_adaptor->ActionInvoked(id, key);
    if (!m_notifications[row]->resident)
        dismiss(id);
}

void NotificationService::reply(uint id, const QString &text)
{
    const int row = rowForId(id);
    if (row < 0 || !m_notifications[row]->actionKeys.contains(QStringLiteral("inline-reply")))
        return;
    emit m_adaptor->NotificationReplied(id, text);
    if (!m_notifications[row]->resident)
        dismiss(id);
}

void NotificationService::hideToasts()
{
    if (m_toastCount == 0)
        return;
    for (int row = 0; row < count(); ++row) {
        if (!m_notifications[row]->toastVisible)
            continue;
        m_notifications[row]->toastVisible = false;
        emit dataChanged(index(row), index(row), {ToastVisibleRole});
    }
    m_toastCount = 0;
    publishState();
}

void NotificationService::markRead()
{
    bool changed = false;
    for (int row = 0; row < count(); ++row) {
        if (m_notifications[row]->isRead)
            continue;
        m_notifications[row]->isRead = true;
        emit dataChanged(index(row), index(row), {IsReadRole});
        changed = true;
    }
    if (changed)
        publishState();
}

void NotificationService::setPanelVisible(bool visible)
{
    if (m_panelVisible == visible)
        return;
    m_panelVisible = visible;
    if (visible) {
        for (int row = 0; row < count(); ++row) {
            Notification &notification = *m_notifications[row];
            notification.toastVisible = false;
            notification.isRead = true;
        }
        m_toastCount = 0;
        if (count() > 0)
            emit dataChanged(index(0), index(count() - 1), {ToastVisibleRole, IsReadRole});
    }
    emit panelVisibleChanged();
    publishState();
}

void NotificationService::setDnd(bool enabled)
{
    if (m_dnd == enabled)
        return;
    m_dnd = enabled;
    QSettings settings(QStringLiteral("mangowm"), QStringLiteral("mango-notify"));
    settings.setValue(QStringLiteral("dnd"), enabled);
    if (enabled) {
        for (int row = 0; row < count(); ++row) {
            Notification &notification = *m_notifications[row];
            if (!notification.toastVisible || notification.urgency == 2)
                continue;
            notification.toastVisible = false;
            --m_toastCount;
            emit dataChanged(index(row), index(row), {ToastVisibleRole});
        }
    }
    emit dndChanged();
    publishState();
}

void NotificationService::enforceImageBudget()
{
    for (int row = count() - 1; row >= 0 && m_imageBytes > ImageBudget; --row) {
        Notification &notification = *m_notifications[row];
        if (notification.image.isNull())
            continue;
        m_imageBytes -= notification.image.sizeInBytes();
        notification.image = QImage();
        notification.imageUrl.clear();
        emit dataChanged(index(row), index(row), {ImageRole});
    }
}

void NotificationService::publishState()
{
    if (m_publishedCount != count()) {
        m_publishedCount = count();
        emit countChanged();
    }
    if (m_publishedToastCount != m_toastCount) {
        m_publishedToastCount = m_toastCount;
        emit toastCountChanged();
    }
    emit stateChanged();
}

#include "notificationservice.moc"

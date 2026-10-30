#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QImage>
#include <QVariantList>
#include <memory>
#include <vector>

class NotificationAdaptor;
class QTimer;

class NotificationService final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool dnd READ dnd WRITE setDnd NOTIFY dndChanged)
    Q_PROPERTY(bool panelVisible READ panelVisible WRITE setPanelVisible NOTIFY panelVisibleChanged)
    Q_PROPERTY(int toastCount READ toastCount NOTIFY toastCountChanged)

public:
    enum Role {
        NotificationIdRole = Qt::UserRole + 1,
        AppNameRole,
        SummaryRole,
        BodyRole,
        IconRole,
        ImageRole,
        TimestampRole,
        UrgencyRole,
        ActionsRole,
        ProgressRole,
        ToastVisibleRole,
        IsReadRole
    };
    Q_ENUM(Role)

    explicit NotificationService(QObject *parent = nullptr);
    ~NotificationService() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool registered() const { return m_registered; }
    QString registrationError() const { return m_registrationError; }
    int count() const { return static_cast<int>(m_notifications.size()); }
    bool dnd() const { return m_dnd; }
    bool panelVisible() const { return m_panelVisible; }
    int toastCount() const { return m_toastCount; }
    QImage notificationImage(uint id) const;

    void setDnd(bool enabled);
    void setPanelVisible(bool visible);

    Q_INVOKABLE void dismiss(uint id);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void invoke(uint id, const QString &key);
    Q_INVOKABLE void reply(uint id, const QString &text);
    Q_INVOKABLE void hideToasts();
    Q_INVOKABLE void markRead();
    Q_INVOKABLE void showPanel() { setPanelVisible(true); }
    Q_INVOKABLE void closePanel() { setPanelVisible(false); }
    Q_INVOKABLE void togglePanel() { setPanelVisible(!m_panelVisible); }

signals:
    void countChanged();
    void dndChanged();
    void panelVisibleChanged();
    void toastCountChanged();
    void stateChanged();

private:
    friend class NotificationAdaptor;
    struct Notification;

    uint notify(const QString &appName, uint replacesId, const QString &appIcon,
                const QString &summary, const QString &body, const QStringList &actions,
                const QVariantMap &hints, int expireTimeout);
    void closeNotification(uint id);
    int rowForId(uint id) const;
    uint nextId();
    void closeOnce(Notification &notification, uint reason);
    void removeAt(int row, uint reason);
    void expire(uint id);
    void publishState();
    void enforceImageBudget();

    std::vector<std::unique_ptr<Notification>> m_notifications;
    NotificationAdaptor *m_adaptor = nullptr;
    QString m_registrationError;
    uint m_nextId = 1;
    quint64 m_imageRevision = 0;
    qsizetype m_imageBytes = 0;
    int m_toastCount = 0;
    int m_publishedCount = 0;
    int m_publishedToastCount = 0;
    bool m_registered = false;
    bool m_dnd = false;
    bool m_panelVisible = false;
};

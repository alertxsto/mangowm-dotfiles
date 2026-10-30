#include "notificationservice.h"
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QSignalSpy>
#include <QtTest>

class NotificationServiceTest final : public QObject {
    Q_OBJECT
    NotificationService *service = nullptr;
    QDBusAbstractAdaptor *adaptor = nullptr;

    uint send(uint replaces = 0, int timeout = 0, const QVariantMap &hints = {},
              const QStringList &actions = {}) {
        QDBusInterface remote("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                              "org.freedesktop.Notifications", QDBusConnection::sessionBus());
        const QDBusReply<uint> reply = remote.call("Notify", "Regression", replaces, "", "Message",
                                                 "Body", actions, hints, timeout);
        if (!reply.isValid())
            qFatal("Notify failed: %s", qPrintable(reply.error().message()));
        return reply.value();
    }
private slots:
    void initTestCase() {
        service = new NotificationService(this);
        QVERIFY2(service->registered(), qPrintable(service->registrationError()));
        adaptor = service->findChild<QDBusAbstractAdaptor *>();
        QVERIFY(adaptor);
    }
    void init() { service->clear(); service->setDnd(false); service->setPanelVisible(false); }
    void cleanup() { service->clear(); service->setDnd(false); service->setPanelVisible(false); }
    void replacementKeepsIdentityAndCount() {
        const uint id = send();
        QCOMPARE(send(id), id);
        QCOMPARE(service->count(), 1);
        QCOMPARE(service->data(service->index(0), NotificationService::NotificationIdRole).toUInt(), id);
    }
    void expiryRetainsHistoryWithoutDuplicateClose() {
        QSignalSpy closed(adaptor, SIGNAL(NotificationClosed(uint,uint)));
        const uint id = send(0, 25);
        QTRY_COMPARE(closed.count(), 1);
        QCOMPARE(closed.at(0), QVariantList({id, 1u}));
        QCOMPARE(service->count(), 1);
        QCOMPARE(service->toastCount(), 0);
        service->dismiss(id);
        QCOMPARE(service->count(), 0);
        QCOMPARE(closed.count(), 1);
    }
    void transientExpiryRemovesHistory() {
        QSignalSpy closed(adaptor, SIGNAL(NotificationClosed(uint,uint)));
        const uint id = send(0, 25, {{"transient", true}});
        QTRY_COMPARE(service->count(), 0);
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.at(0), QVariantList({id, 1u}));
    }
    void dndSuppressesOnlyNoncriticalToasts() {
        service->setDnd(true);
        send();
        QCOMPARE(service->count(), 1);
        QCOMPARE(service->toastCount(), 0);
        send(0, 0, {{"urgency", uchar(2)}});
        QCOMPARE(service->toastCount(), 1);
        service->setPanelVisible(true);
        QCOMPARE(service->toastCount(), 0);
        for (int row = 0; row < service->count(); ++row)
            QVERIFY(service->data(service->index(row), NotificationService::IsReadRole).toBool());
    }
    void residentActionsAndRepliesRetainNotification() {
        QSignalSpy invoked(adaptor, SIGNAL(ActionInvoked(uint,QString)));
        QSignalSpy replied(adaptor, SIGNAL(NotificationReplied(uint,QString)));
        const uint id = send(0, 0, {{"resident", true}}, {"default", "Open", "inline-reply", "Reply"});
        service->invoke(id, "unknown");
        QCOMPARE(invoked.count(), 0);
        service->invoke(id, "default");
        QCOMPARE(invoked.count(), 1);
        QCOMPARE(invoked.at(0), QVariantList({id, QString("default")}));
        service->reply(id, "Received");
        QCOMPARE(replied.count(), 1);
        QCOMPARE(replied.at(0), QVariantList({id, QString("Received")}));
        QCOMPARE(service->count(), 1);
    }
    void nonresidentActionDismissesOnce() {
        QSignalSpy closed(adaptor, SIGNAL(NotificationClosed(uint,uint)));
        const uint id = send(0, 0, {}, {"default", "Open"});
        service->invoke(id, "default");
        QCOMPARE(service->count(), 0);
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.at(0), QVariantList({id, 2u}));
        service->dismiss(id);
        QCOMPARE(closed.count(), 1);
    }
    void boundedHistoryEvictsOldest() {
        QSignalSpy closed(adaptor, SIGNAL(NotificationClosed(uint,uint)));
        const uint first = send();
        for (int i = 0; i < 200; ++i)
            send();
        QCOMPARE(service->count(), 200);
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.at(0), QVariantList({first, 4u}));
    }
};

QTEST_GUILESS_MAIN(NotificationServiceTest)
#include "notificationservice-test.moc"

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
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

// Exercise the real controller without launching the popup window.
#define private public
#define main popupApplicationMain
#include "../main.cpp"
#undef main
#undef private

class NetworkControllerTest : public QObject {
    Q_OBJECT
private slots:
    void sameSsidKeepsProfileIdentityAndSecurity() {
        NetworkController controller;
        controller.m_profiles = {
            {{"ssid", "Cafe"}, {"uuid", "open-uuid"}, {"name", "Open"}, {"security", ""}},
            {{"ssid", "Cafe"}, {"uuid", "psk-uuid"}, {"name", "Private"}, {"security", "wpa-psk"}},
            {{"ssid", "Cafe"}, {"uuid", "eap-uuid"}, {"name", "Enterprise"}, {"security", "wpa-eap"}}
        };
        controller.parseNetworks(":85:WPA2:Cafe\n:50:--:Cafe\n");
        QHash<QString, QVariantMap> byUuid;
        for (const auto &entry : controller.networks()) {
            const auto network = entry.toMap();
            byUuid.insert(network.value("uuid").toString(), network);
        }
        QCOMPARE(byUuid.size(), 3);
        QCOMPARE(byUuid["open-uuid"].value("security").toString(), QString("Open"));
        QCOMPARE(byUuid["open-uuid"].value("signal").toInt(), 50);
        QCOMPARE(byUuid["psk-uuid"].value("security").toString(), QString("wpa-psk"));
        QCOMPARE(byUuid["psk-uuid"].value("signal").toInt(), 85);
        QCOMPARE(byUuid["eap-uuid"].value("security").toString(), QString("wpa-eap"));
        QVERIFY(!byUuid["eap-uuid"].value("inRange").toBool());
    }

    void missingSecretPromptKeepsSelectedUuid() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QFile fake(root.filePath("nmcli"));
        QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("#!/bin/sh\n"
                   "echo 'Secrets were required, but not provided' >&2\n"
                   "exit 1\n");
        fake.close();
        QVERIFY(fake.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", root.path().toLocal8Bit() + ':' + oldPath);
        NetworkController controller;
        controller.m_wifiEnabled = true;
        controller.m_interface = "wlan0";
        controller.m_profiles = {{{"ssid", "Cafe"}, {"uuid", "target-uuid"}, {"security", "wpa-psk"}}};
        controller.parseNetworks(":70:WPA2:Cafe\n");
        QSignalSpy prompt(&controller, &NetworkController::passwordRequested);
        controller.activate(0);
        QTRY_COMPARE(prompt.size(), 1);
        QCOMPARE(prompt.at(0).at(0).toString(), QString("Cafe"));
        QCOMPARE(prompt.at(0).at(1).toString(), QString("target-uuid"));
        qputenv("PATH", oldPath);
    }

    void savedRetryUsesSelectedProfileAndSecretOnStdin() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QFile fake(root.filePath("nmcli"));
        QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("#!/bin/sh\n"
                   "case \"$*\" in\n"
                   "  *'connection up uuid target-uuid ifname wlan0 passwd-file /dev/stdin'*)\n"
                   "    IFS= read -r secret\n"
                   "    [ \"$secret\" = '802-11-wireless-security.psk:example secret' ] && exit 0\n"
                   "    exit 1;;\n"
                   "  *) exit 1;;\n"
                   "esac\n");
        fake.close();
        QVERIFY(fake.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", root.path().toLocal8Bit() + ':' + oldPath);
        NetworkController controller;
        controller.m_wifiEnabled = true;
        controller.m_interface = "wlan0";
        controller.m_profiles = {{{"ssid", "Cafe"}, {"uuid", "other-uuid"}, {"security", "wpa-eap"}},
                                 {{"ssid", "Cafe"}, {"uuid", "target-uuid"}, {"security", "wpa-psk"}}};
        QSignalSpy success(&controller, &NetworkController::connectionSucceeded);
        controller.connectWithPassword("Cafe", "target-uuid", "example secret");
        QTRY_COMPARE(success.size(), 1);
        qputenv("PATH", oldPath);
    }
};

QTEST_GUILESS_MAIN(NetworkControllerTest)
#include "networkcontroller-test.moc"

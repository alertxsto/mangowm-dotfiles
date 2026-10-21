#include "batterycontroller.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool writeFile(const QString &path, const QByteArray &contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(contents) == contents.size();
}

bool writeSupply(const QTemporaryDir &root, const QString &name, const QByteArray &contents) {
    const QString directory = root.path() + QLatin1Char('/') + name;
    return QDir().mkpath(directory) && writeFile(directory + QStringLiteral("/uevent"), contents);
}

const QByteArray batteryHeader("POWER_SUPPLY_TYPE=Battery\nPOWER_SUPPLY_SCOPE=System\nPOWER_SUPPLY_PRESENT=1\n");
}

class BatteryControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void matchingUnitsOnly() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeSupply(root, QStringLiteral("BAT0"), batteryHeader
            + "POWER_SUPPLY_STATUS=Charging\nPOWER_SUPPLY_ENERGY_NOW=30000000\n"
              "POWER_SUPPLY_CHARGE_NOW=2000000\nPOWER_SUPPLY_CHARGE_FULL=4000000\n"
              "POWER_SUPPLY_CHARGE_FULL_DESIGN=3000000\nPOWER_SUPPLY_CURRENT_NOW=2000000\n"
              "POWER_SUPPLY_VOLTAGE_NOW=12000000\nPOWER_SUPPLY_POWER_NOW=10000000\n"));
        BatteryController controller(root.path());
        QCOMPARE(controller.percentage(), 50);
        QVERIFY(qAbs(controller.health() - 400.0 / 3.0) < 0.000001);
        QCOMPARE(controller.energy(), 30.0);
        QCOMPARE(controller.fullEnergy(), -1.0);
        QCOMPARE(controller.designEnergy(), -1.0);
        QCOMPARE(controller.power(), 10.0);
        QCOMPARE(controller.estimateSeconds(), qint64(3600));
        QVERIFY(controller.approximateEstimate());

        // ENERGY_NOW cannot be divided by CHARGE_FULL, either for capacity
        // or for the amount remaining to charge. No matching NOW/FULL pair.
        QVERIFY(writeSupply(root, QStringLiteral("BAT0"), batteryHeader
            + "POWER_SUPPLY_STATUS=Charging\nPOWER_SUPPLY_ENERGY_NOW=30000000\n"
              "POWER_SUPPLY_CHARGE_FULL=4000000\nPOWER_SUPPLY_CHARGE_FULL_DESIGN=5000000\n"
              "POWER_SUPPLY_POWER_NOW=10000000\n"));
        controller.refresh();
        QCOMPARE(controller.percentage(), -1);
        QCOMPARE(controller.health(), 80.0);
        QCOMPARE(controller.estimateSeconds(), qint64(-1));
        QVERIFY(!controller.approximateEstimate());

        // A charge-only battery can estimate Ah/A without fabricating Wh.
        QVERIFY(writeSupply(root, QStringLiteral("BAT0"), batteryHeader
            + "POWER_SUPPLY_STATUS=Discharging\nPOWER_SUPPLY_CHARGE_NOW=2000000\n"
              "POWER_SUPPLY_CHARGE_FULL=4000000\nPOWER_SUPPLY_CHARGE_FULL_DESIGN=3000000\n"
              "POWER_SUPPLY_CURRENT_NOW=-2000000\nPOWER_SUPPLY_VOLTAGE_NOW=12000000\n"));
        controller.refresh();
        QCOMPARE(controller.percentage(), 50);
        QVERIFY(qAbs(controller.health() - 400.0 / 3.0) < 0.000001);
        QCOMPARE(controller.energy(), -1.0);
        QCOMPARE(controller.fullEnergy(), -1.0);
        QCOMPARE(controller.power(), 24.0);
        QCOMPARE(controller.estimateSeconds(), qint64(3600));
        QVERIFY(controller.approximateEstimate());

        // Above-design energy health is also preserved, not clamped to 100.
        QVERIFY(writeSupply(root, QStringLiteral("BAT0"), batteryHeader
            + "POWER_SUPPLY_ENERGY_FULL=63000000\nPOWER_SUPPLY_ENERGY_FULL_DESIGN=60000000\n"));
        controller.refresh();
        QCOMPARE(controller.health(), 105.0);
    }

    void missingIsNotZero() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeSupply(root, QStringLiteral("BAT0"), batteryHeader
            + "POWER_SUPPLY_STATUS=Charging\nPOWER_SUPPLY_CAPACITY=0\nPOWER_SUPPLY_CYCLE_COUNT=0\n"
              "POWER_SUPPLY_ENERGY_NOW=0\nPOWER_SUPPLY_ENERGY_FULL=0\n"
              "POWER_SUPPLY_ENERGY_FULL_DESIGN=0\nPOWER_SUPPLY_POWER_NOW=0\n"));
        const QString directory = root.path() + QStringLiteral("/BAT0");
        QVERIFY(writeFile(directory + QStringLiteral("/temp"), "0\n"));
        QVERIFY(writeFile(directory + QStringLiteral("/charge_control_start_threshold"), "0\n"));
        QVERIFY(writeFile(directory + QStringLiteral("/charge_control_end_threshold"), "100\n"));
        BatteryController controller(root.path());
        QCOMPARE(controller.percentage(), 0);
        QCOMPARE(controller.cycleCount(), 0);
        QCOMPARE(controller.energy(), 0.0);
        QCOMPARE(controller.fullEnergy(), 0.0);
        QCOMPARE(controller.designEnergy(), 0.0);
        QCOMPARE(controller.power(), 0.0);
        QCOMPARE(controller.temperature(), 0.0);
        QCOMPARE(controller.chargeStart(), 0);
        QCOMPARE(controller.chargeEnd(), 100);
        QCOMPARE(controller.health(), -1.0);
        QCOMPARE(controller.estimateSeconds(), qint64(-1));

        QSignalSpy cycleChanges(&controller, &BatteryController::cycleCountChanged);
        QSignalSpy availabilityChanges(&controller, &BatteryController::availableChanged);
        controller.refresh();
        QCOMPARE(cycleChanges.count(), 0);
        QCOMPARE(availabilityChanges.count(), 0);

        QVERIFY(writeSupply(root, QStringLiteral("BAT0"), batteryHeader));
        QVERIFY(QFile::remove(directory + QStringLiteral("/temp")));
        QVERIFY(QFile::remove(directory + QStringLiteral("/charge_control_start_threshold")));
        QVERIFY(QFile::remove(directory + QStringLiteral("/charge_control_end_threshold")));
        controller.refresh();
        QVERIFY(controller.available());
        QCOMPARE(controller.percentage(), -1);
        QCOMPARE(controller.cycleCount(), -1);
        QCOMPARE(controller.energy(), -1.0);
        QCOMPARE(controller.fullEnergy(), -1.0);
        QCOMPARE(controller.designEnergy(), -1.0);
        QCOMPARE(controller.power(), -1.0);
        QCOMPARE(controller.temperature(), -1000.0);
        QCOMPARE(controller.chargeStart(), -1);
        QCOMPARE(controller.chargeEnd(), -1);
        QCOMPARE(cycleChanges.count(), 1);
        QCOMPARE(availabilityChanges.count(), 0);
    }

    void noInactiveOrZeroFlowEstimate_data() {
        QTest::addColumn<QString>("status");
        QTest::addColumn<qint64>("power");
        QTest::newRow("full") << QStringLiteral("Full") << qint64(10000000);
        QTest::newRow("not-charging") << QStringLiteral("Not charging") << qint64(10000000);
        QTest::newRow("unknown") << QStringLiteral("Unknown") << qint64(10000000);
        QTest::newRow("charging-zero") << QStringLiteral("Charging") << qint64(0);
        QTest::newRow("discharging-zero") << QStringLiteral("Discharging") << qint64(0);
    }

    void noInactiveOrZeroFlowEstimate() {
        QFETCH(QString, status);
        QFETCH(qint64, power);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeSupply(root, QStringLiteral("BAT0"), batteryHeader
            + "POWER_SUPPLY_STATUS=" + status.toUtf8()
            + "\nPOWER_SUPPLY_POWER_NOW=" + QByteArray::number(power)
            + "\nPOWER_SUPPLY_ENERGY_NOW=30000000\nPOWER_SUPPLY_ENERGY_FULL=60000000\n"));
        BatteryController controller(root.path());
        QCOMPARE(controller.status(), status);
        QCOMPARE(controller.estimateSeconds(), qint64(-1));
        QVERIFY(!controller.approximateEstimate());
    }

    void presenceAndAdapterState() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeSupply(root, QStringLiteral("00-mouse"),
            "POWER_SUPPLY_TYPE=Battery\nPOWER_SUPPLY_SCOPE=Device\nPOWER_SUPPLY_PRESENT=1\n"));
        QVERIFY(writeSupply(root, QStringLiteral("BAT1"), batteryHeader + "POWER_SUPPLY_CAPACITY=60\n"));
        QVERIFY(writeSupply(root, QStringLiteral("BAT0"), batteryHeader + "POWER_SUPPLY_CAPACITY=50\n"));
        QVERIFY(writeSupply(root, QStringLiteral("AC"), "POWER_SUPPLY_TYPE=Mains\nPOWER_SUPPLY_ONLINE=0\n"));
        QVERIFY(writeSupply(root, QStringLiteral("mouse-charger"),
            "POWER_SUPPLY_TYPE=USB\nPOWER_SUPPLY_SCOPE=Device\nPOWER_SUPPLY_ONLINE=1\n"));
        BatteryController controller(root.path());
        QVERIFY(controller.available());
        QCOMPARE(controller.deviceName(), QStringLiteral("BAT0"));
        QCOMPARE(controller.adapterOnline(), 0);

        QVERIFY(QFile::remove(root.path() + QStringLiteral("/BAT0/uevent")));
        controller.refresh();
        QCOMPARE(controller.deviceName(), QStringLiteral("BAT1"));
        QCOMPARE(controller.percentage(), 60);
        QVERIFY(writeSupply(root, QStringLiteral("BAT1"),
            "POWER_SUPPLY_TYPE=Battery\nPOWER_SUPPLY_SCOPE=System\nPOWER_SUPPLY_PRESENT=0\n"));
        controller.refresh();
        QVERIFY(!controller.available());
        QVERIFY(controller.deviceName().isEmpty());
        QCOMPARE(controller.percentage(), -1);

        QVERIFY(writeSupply(root, QStringLiteral("BAT0"), batteryHeader + "POWER_SUPPLY_CAPACITY=50\n"));
        // Newly attached online USB-C supply overrides an existing offline AC.
        QVERIFY(writeSupply(root, QStringLiteral("USB-C"),
            "POWER_SUPPLY_TYPE=USB\nPOWER_SUPPLY_SCOPE=System\nPOWER_SUPPLY_ONLINE=1\n"));
        controller.refresh();
        QVERIFY(controller.available());
        QCOMPARE(controller.deviceName(), QStringLiteral("BAT0"));
        QCOMPARE(controller.adapterOnline(), 1);
        QVERIFY(QFile::remove(root.path() + QStringLiteral("/USB-C/uevent")));
        QVERIFY(QFile::remove(root.path() + QStringLiteral("/AC/uevent")));
        controller.refresh();
        QCOMPARE(controller.adapterOnline(), -1);
    }
};

QTEST_GUILESS_MAIN(BatteryControllerTest)
#include "batterycontroller-test.moc"

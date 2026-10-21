#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class BatteryController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceNameChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int percentage READ percentage NOTIFY percentageChanged)
    Q_PROPERTY(double health READ health NOTIFY healthChanged)
    Q_PROPERTY(int cycleCount READ cycleCount NOTIFY cycleCountChanged)
    Q_PROPERTY(double energy READ energy NOTIFY energyChanged)
    Q_PROPERTY(double fullEnergy READ fullEnergy NOTIFY fullEnergyChanged)
    Q_PROPERTY(double designEnergy READ designEnergy NOTIFY designEnergyChanged)
    Q_PROPERTY(double power READ power NOTIFY powerChanged)
    Q_PROPERTY(double voltage READ voltage NOTIFY voltageChanged)
    Q_PROPERTY(double temperature READ temperature NOTIFY temperatureChanged)
    Q_PROPERTY(QString manufacturer READ manufacturer NOTIFY manufacturerChanged)
    Q_PROPERTY(QString model READ model NOTIFY modelChanged)
    Q_PROPERTY(QString technology READ technology NOTIFY technologyChanged)
    Q_PROPERTY(int chargeStart READ chargeStart NOTIFY chargeStartChanged)
    Q_PROPERTY(int chargeEnd READ chargeEnd NOTIFY chargeEndChanged)
    Q_PROPERTY(qint64 estimateSeconds READ estimateSeconds NOTIFY estimateSecondsChanged)
    Q_PROPERTY(bool approximateEstimate READ approximateEstimate NOTIFY approximateEstimateChanged)
    Q_PROPERTY(int adapterOnline READ adapterOnline NOTIFY adapterOnlineChanged)

public:
    explicit BatteryController(const QString &supplyDirectory = QStringLiteral("/sys/class/power_supply"),
                               QObject *parent = nullptr);

    bool available() const { return m_state.available; }
    QString deviceName() const { return m_state.deviceName; }
    QString status() const { return m_state.status; }
    int percentage() const { return m_state.percentage; }
    double health() const { return m_state.health; }
    int cycleCount() const { return m_state.cycleCount; }
    double energy() const { return m_state.energy; }
    double fullEnergy() const { return m_state.fullEnergy; }
    double designEnergy() const { return m_state.designEnergy; }
    double power() const { return m_state.power; }
    double voltage() const { return m_state.voltage; }
    double temperature() const { return m_state.temperature; }
    QString manufacturer() const { return m_state.manufacturer; }
    QString model() const { return m_state.model; }
    QString technology() const { return m_state.technology; }
    int chargeStart() const { return m_state.chargeStart; }
    int chargeEnd() const { return m_state.chargeEnd; }
    qint64 estimateSeconds() const { return m_state.estimateSeconds; }
    bool approximateEstimate() const { return m_state.approximateEstimate; }
    int adapterOnline() const { return m_state.adapterOnline; }

    void refresh();

signals:
    void availableChanged();
    void deviceNameChanged();
    void statusChanged();
    void percentageChanged();
    void healthChanged();
    void cycleCountChanged();
    void energyChanged();
    void fullEnergyChanged();
    void designEnergyChanged();
    void powerChanged();
    void voltageChanged();
    void temperatureChanged();
    void manufacturerChanged();
    void modelChanged();
    void technologyChanged();
    void chargeStartChanged();
    void chargeEndChanged();
    void estimateSecondsChanged();
    void approximateEstimateChanged();
    void adapterOnlineChanged();

private:
    struct State {
        bool available = false;
        QString deviceName;
        QString status;
        int percentage = -1;
        double health = -1;
        int cycleCount = -1;
        double energy = -1;
        double fullEnergy = -1;
        double designEnergy = -1;
        double power = -1;
        double voltage = -1;
        double temperature = -1000;
        QString manufacturer;
        QString model;
        QString technology;
        int chargeStart = -1;
        int chargeEnd = -1;
        qint64 estimateSeconds = -1;
        bool approximateEstimate = false;
        int adapterOnline = -1;
    };

    void apply(State &&state);

    const QString m_supplyDirectory;
    QString m_deviceDirectory;
    QString m_deviceName;
    State m_state;
    QTimer m_timer;
};

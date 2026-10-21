#include "batterycontroller.h"

#include <QByteArrayView>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace {
using Number = std::optional<qint64>;

struct Supply {
    bool readable = false;
    QString type;
    QString scope;
    QString status;
    QString manufacturer;
    QString model;
    QString technology;
    Number present;
    Number online;
    Number capacity;
    Number cycles;
    Number energyNow;
    Number energyFull;
    Number energyDesign;
    Number chargeNow;
    Number chargeFull;
    Number chargeDesign;
    Number power;
    Number current;
    Number voltage;
    Number temperature;
    Number chargeStart;
    Number chargeEnd;
};

Number number(QByteArrayView text) {
    qint64 result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return std::nullopt;
    return result;
}

Number readNumber(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray value = file.readAll().trimmed();
    return number(value);
}

Supply readSupply(const QString &directory) {
    QFile file(directory + QStringLiteral("/uevent"));
    Supply supply;
    if (!file.open(QIODevice::ReadOnly))
        return supply;
    const QByteArray data = file.readAll();
    supply.readable = !data.isEmpty();
    // Parse views into a single read buffer; no line list or per-poll hash table.
    qsizetype start = 0;
    while (start < data.size()) {
        qsizetype end = data.indexOf('\n', start);
        if (end < 0)
            end = data.size();
        const QByteArrayView line(data.constData() + start, end - start);
        const qsizetype equals = line.indexOf('=');
        if (equals > 0) {
            const QByteArrayView key = line.first(equals);
            const QByteArrayView value = line.sliced(equals + 1);
            if (key == "POWER_SUPPLY_TYPE") supply.type = QString::fromUtf8(value.data(), value.size());
            else if (key == "POWER_SUPPLY_SCOPE") supply.scope = QString::fromUtf8(value.data(), value.size());
            else if (key == "POWER_SUPPLY_STATUS") supply.status = QString::fromUtf8(value.data(), value.size());
            else if (key == "POWER_SUPPLY_MANUFACTURER") supply.manufacturer = QString::fromUtf8(value.data(), value.size());
            else if (key == "POWER_SUPPLY_MODEL_NAME") supply.model = QString::fromUtf8(value.data(), value.size());
            else if (key == "POWER_SUPPLY_TECHNOLOGY") supply.technology = QString::fromUtf8(value.data(), value.size());
            else if (key == "POWER_SUPPLY_PRESENT") supply.present = number(value);
            else if (key == "POWER_SUPPLY_ONLINE") supply.online = number(value);
            else if (key == "POWER_SUPPLY_CAPACITY") supply.capacity = number(value);
            else if (key == "POWER_SUPPLY_CYCLE_COUNT") supply.cycles = number(value);
            else if (key == "POWER_SUPPLY_ENERGY_NOW") supply.energyNow = number(value);
            else if (key == "POWER_SUPPLY_ENERGY_FULL") supply.energyFull = number(value);
            else if (key == "POWER_SUPPLY_ENERGY_FULL_DESIGN") supply.energyDesign = number(value);
            else if (key == "POWER_SUPPLY_CHARGE_NOW") supply.chargeNow = number(value);
            else if (key == "POWER_SUPPLY_CHARGE_FULL") supply.chargeFull = number(value);
            else if (key == "POWER_SUPPLY_CHARGE_FULL_DESIGN") supply.chargeDesign = number(value);
            else if (key == "POWER_SUPPLY_POWER_NOW") supply.power = number(value);
            else if (key == "POWER_SUPPLY_CURRENT_NOW") supply.current = number(value);
            else if (key == "POWER_SUPPLY_VOLTAGE_NOW") supply.voltage = number(value);
            else if (key == "POWER_SUPPLY_TEMP") supply.temperature = number(value);
            else if (key == "POWER_SUPPLY_CHARGE_CONTROL_START_THRESHOLD") supply.chargeStart = number(value);
            else if (key == "POWER_SUPPLY_CHARGE_CONTROL_END_THRESHOLD") supply.chargeEnd = number(value);
        }
        start = end + 1;
    }
    return supply;
}

bool isSystemBattery(const Supply &supply) {
    return supply.readable && supply.type == QLatin1String("Battery")
        && (supply.scope.isEmpty() || supply.scope == QLatin1String("System"))
        && (!supply.present || *supply.present == 1);
}

bool nonnegative(const Number &value) {
    return value && *value >= 0;
}

int integer(const Number &value) {
    return nonnegative(value) && *value <= std::numeric_limits<int>::max()
        ? static_cast<int>(*value) : -1;
}

double microUnits(const Number &value) {
    return nonnegative(value) ? static_cast<double>(*value) / 1000000.0 : -1;
}

double ratio(const Number &numerator, const Number &denominator) {
    return nonnegative(numerator) && denominator && *denominator > 0
        ? 100.0 * static_cast<double>(*numerator) / static_cast<double>(*denominator) : -1;
}

qint64 seconds(double remaining, double flow) {
    if (remaining < 0 || flow <= 0)
        return -1;
    const double estimate = remaining / flow * 3600.0;
    if (!std::isfinite(estimate) || estimate >= static_cast<double>(std::numeric_limits<qint64>::max()))
        return -1;
    return static_cast<qint64>(std::round(estimate));
}
}

BatteryController::BatteryController(const QString &supplyDirectory, QObject *parent)
    : QObject(parent), m_supplyDirectory(supplyDirectory) {
    m_timer.setTimerType(Qt::VeryCoarseTimer);
    m_timer.setInterval(15000);
    connect(&m_timer, &QTimer::timeout, this, &BatteryController::refresh);
    refresh();
    m_timer.start();
}

void BatteryController::refresh() {
    State state;
    Supply battery;
    if (!m_deviceDirectory.isEmpty())
        battery = readSupply(m_deviceDirectory);

    // Keep the selected device until it disappears. Rediscovery also handles
    // systems that had no present battery when the popup was opened.
    const QDir root(m_supplyDirectory);
    const QStringList entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (!isSystemBattery(battery)) {
        m_deviceDirectory.clear();
        m_deviceName.clear();
        for (const QString &entry : entries) {
            const QString path = root.filePath(entry);
            Supply candidate = readSupply(path);
            if (isSystemBattery(candidate)) {
                m_deviceDirectory = path;
                m_deviceName = entry;
                battery = std::move(candidate);
                break;
            }
        }
    }

    // ONLINE is not inferred from battery status: "Not charging" can occur
    // both on and off external power. Aggregate all system-scoped chargers;
    // enumerate names so newly attached USB-C/dock supplies are included.
    bool knownAdapter = false;
    for (const QString &entry : entries) {
        if (entry == m_deviceName)
            continue;
        const Supply adapter = readSupply(root.filePath(entry));
        if (!adapter.readable || adapter.type.isEmpty() || adapter.type == QLatin1String("Battery")
            || (!adapter.scope.isEmpty() && adapter.scope != QLatin1String("System")))
            continue;
        if (adapter.online && *adapter.online == 1) {
            state.adapterOnline = 1;
            break;
        }
        if (adapter.online && *adapter.online == 0)
            knownAdapter = true;
    }
    if (state.adapterOnline != 1 && knownAdapter)
        state.adapterOnline = 0;

    if (!isSystemBattery(battery)) {
        apply(std::move(state));
        return;
    }

    state.available = true;
    state.deviceName = m_deviceName;
    state.status = battery.status;
    state.manufacturer = battery.manufacturer;
    state.model = battery.model;
    state.technology = battery.technology;
    state.percentage = integer(battery.capacity);
    if (state.percentage < 0) {
        double capacity = ratio(battery.energyNow, battery.energyFull);
        if (capacity < 0)
            capacity = ratio(battery.chargeNow, battery.chargeFull);
        if (capacity >= 0 && capacity <= std::numeric_limits<int>::max() - 0.5)
            state.percentage = static_cast<int>(std::round(capacity));
    }
    state.health = ratio(battery.energyFull, battery.energyDesign);
    if (state.health < 0)
        state.health = ratio(battery.chargeFull, battery.chargeDesign);
    state.cycleCount = integer(battery.cycles);
    state.energy = microUnits(battery.energyNow);
    state.fullEnergy = microUnits(battery.energyFull);
    state.designEnergy = microUnits(battery.energyDesign);
    state.voltage = microUnits(battery.voltage);
    if (battery.power)
        state.power = std::abs(static_cast<double>(*battery.power)) / 1000000.0;
    else if (battery.current && state.voltage >= 0)
        state.power = std::abs(static_cast<double>(*battery.current)) / 1000000.0 * state.voltage;

    // These optional attributes are not included in every driver's uevent.
    if (!battery.temperature)
        battery.temperature = readNumber(m_deviceDirectory + QStringLiteral("/temp"));
    if (!battery.chargeStart)
        battery.chargeStart = readNumber(m_deviceDirectory + QStringLiteral("/charge_control_start_threshold"));
    if (!battery.chargeEnd)
        battery.chargeEnd = readNumber(m_deviceDirectory + QStringLiteral("/charge_control_end_threshold"));
    if (battery.temperature)
        state.temperature = static_cast<double>(*battery.temperature) / 10.0;
    state.chargeStart = integer(battery.chargeStart);
    state.chargeEnd = integer(battery.chargeEnd);

    const bool charging = state.status == QLatin1String("Charging");
    const bool discharging = state.status == QLatin1String("Discharging");
    if (charging || discharging) {
        if (state.energy >= 0 && state.power > 0
            && (!charging || state.fullEnergy > 0)) {
            const double remaining = charging ? std::max(0.0, state.fullEnergy - state.energy) : state.energy;
            state.estimateSeconds = seconds(remaining, state.power);
        }
        // Ah/A gives hours directly; do not divide charge by watts or combine
        // a CHARGE_FULL value with ENERGY_NOW from a partially populated driver.
        if (state.estimateSeconds < 0 && nonnegative(battery.chargeNow) && battery.current
            && *battery.current != 0 && (!charging || (battery.chargeFull && *battery.chargeFull > 0))) {
            const double remaining = charging
                ? std::max(0.0, static_cast<double>(*battery.chargeFull) - static_cast<double>(*battery.chargeNow))
                : static_cast<double>(*battery.chargeNow);
            state.estimateSeconds = seconds(remaining, std::abs(static_cast<double>(*battery.current)));
        }
        state.approximateEstimate = state.estimateSeconds >= 0;
    }
    apply(std::move(state));
}

void BatteryController::apply(State &&state) {
    const State previous = std::move(m_state);
    m_state = std::move(state);
    // Commit the complete snapshot before notifying QML; dependent bindings
    // always see a coherent reading, and unchanged fields do not wake bindings.
#define NOTIFY_CHANGED(field) if (previous.field != m_state.field) emit field##Changed();
    NOTIFY_CHANGED(available)
    NOTIFY_CHANGED(deviceName)
    NOTIFY_CHANGED(status)
    NOTIFY_CHANGED(percentage)
    NOTIFY_CHANGED(health)
    NOTIFY_CHANGED(cycleCount)
    NOTIFY_CHANGED(energy)
    NOTIFY_CHANGED(fullEnergy)
    NOTIFY_CHANGED(designEnergy)
    NOTIFY_CHANGED(power)
    NOTIFY_CHANGED(voltage)
    NOTIFY_CHANGED(temperature)
    NOTIFY_CHANGED(manufacturer)
    NOTIFY_CHANGED(model)
    NOTIFY_CHANGED(technology)
    NOTIFY_CHANGED(chargeStart)
    NOTIFY_CHANGED(chargeEnd)
    NOTIFY_CHANGED(estimateSeconds)
    NOTIFY_CHANGED(approximateEstimate)
    NOTIFY_CHANGED(adapterOnline)
#undef NOTIFY_CHANGED
}

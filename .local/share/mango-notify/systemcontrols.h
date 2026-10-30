#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <memory>

class QDBusMessage;
class SystemControlsPrivate;

class SystemControls final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int volume READ volume NOTIFY changed)
    Q_PROPERTY(bool muted READ muted NOTIFY changed)
    Q_PROPERTY(bool micMuted READ micMuted NOTIFY changed)
    Q_PROPERTY(bool audioAvailable READ audioAvailable NOTIFY changed)
    Q_PROPERTY(int brightness READ brightness NOTIFY changed)
    Q_PROPERTY(bool brightnessAvailable READ brightnessAvailable NOTIFY changed)
    Q_PROPERTY(QString networkName READ networkName NOTIFY changed)
    Q_PROPERTY(bool networkConnected READ networkConnected NOTIFY changed)
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled NOTIFY changed)
    Q_PROPERTY(bool bluetoothEnabled READ bluetoothEnabled NOTIFY changed)
    Q_PROPERTY(bool bluetoothAvailable READ bluetoothAvailable NOTIFY changed)
    Q_PROPERTY(QString powerProfile READ powerProfile NOTIFY changed)
    Q_PROPERTY(bool mediaAvailable READ mediaAvailable NOTIFY changed)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY changed)
    Q_PROPERTY(QString mediaArtist READ mediaArtist NOTIFY changed)
    Q_PROPERTY(QString mediaArt READ mediaArt NOTIFY changed)
    Q_PROPERTY(bool mediaPlaying READ mediaPlaying NOTIFY changed)
    Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY changed)
    Q_PROPERTY(bool canNext READ canNext NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit SystemControls(QObject *parent = nullptr);
    ~SystemControls() override;

    int volume() const;
    bool muted() const;
    bool micMuted() const;
    bool audioAvailable() const;
    int brightness() const;
    bool brightnessAvailable() const;
    QString networkName() const;
    bool networkConnected() const;
    bool wifiEnabled() const;
    bool bluetoothEnabled() const;
    bool bluetoothAvailable() const;
    QString powerProfile() const;
    bool mediaAvailable() const;
    QString mediaTitle() const;
    QString mediaArtist() const;
    QString mediaArt() const;
    bool mediaPlaying() const;
    bool canPrevious() const;
    bool canNext() const;
    QString error() const;

    Q_INVOKABLE void setVolume(int value);
    Q_INVOKABLE void setMuted(bool value);
    Q_INVOKABLE void setMicMuted(bool value);
    Q_INVOKABLE void setBrightness(int value);
    Q_INVOKABLE void setWifiEnabled(bool value);
    Q_INVOKABLE void setBluetoothEnabled(bool value);
    Q_INVOKABLE void mediaPlayPause();
    Q_INVOKABLE void mediaPrevious();
    Q_INVOKABLE void mediaNext();
    Q_INVOKABLE void launch(const QString &target);

signals:
    void changed();
    void launchRequested(const QString &target);

private slots:
    void systemPropertiesChanged(const QString &interface, const QVariantMap &properties,
                                 const QStringList &invalidated, const QDBusMessage &message);
    void mediaPropertiesChanged(const QString &interface, const QVariantMap &properties,
                                const QStringList &invalidated, const QDBusMessage &message);
    void systemOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void sessionOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void bluezObjectsChanged(const QDBusMessage &message);

private:
    std::unique_ptr<SystemControlsPrivate> d;
};

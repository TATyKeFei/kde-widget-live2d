#pragma once

#include <QMap>
#include <QObject>
#include <QPointF>
#include <QSet>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>
#include <map>
#include <memory>

// Read-only evdev observation: no grab, input injection, or key logging.
// Shared by running, visible widgets that select desktop keys or relative mouse.
class GlobalInput : public QObject
{
    Q_OBJECT
  public:
    static GlobalInput *instance();
    explicit GlobalInput(QObject *parent = nullptr);
    ~GlobalInput() override;
    void subscribe(QObject *client, const QSet<QString> &keys, bool motion = false);
    void unsubscribe(QObject *client);
    QString errorString() const { return m_error; }
    QString motionErrorString() const { return m_motionError; }
    bool motionAvailable() const { return m_motionAvailable; }

  Q_SIGNALS:
    void keyChanged(const QString &key, bool down);
    void pointerMoved(const QPointF &delta);
    void pointerReset();
    void statusChanged();

  private:
    struct Device {
        int fd = -1;
        std::unique_ptr<QSocketNotifier> notifier;
        QSet<unsigned short> down;
        QPointF motion;
        bool relative = false;
        bool dropped = false;
        ~Device();
    };
    void scan();
    void applyScan(std::map<QString, std::unique_ptr<Device>> devices, const QSet<QString> &available,
                   bool motionAvailable);
    void readDevice(const QString &path);
    void releaseDevice(Device &device);
    void closeDevices();
    void event(Device &device, unsigned short type, unsigned short code, int value);
    QMap<QObject *, QSet<QString>> m_clients;
    QSet<QObject *> m_motionClients;
    QMap<QObject *, QMetaObject::Connection> m_clientConnections;
    std::map<QString, std::unique_ptr<Device>> m_devices;
    QMap<QString, int> m_downCounts;
    QSet<QString> m_keys;
    bool m_motionAvailable = false;
    QTimer m_rescan;
    QThread m_probeThread;
    QObject *m_probeWorker = nullptr;
    bool m_scanBusy = false;
    QString m_error, m_motionError;
    friend class GlobalInputTest;
};

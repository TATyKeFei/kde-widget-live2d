#include "globalinput.h"
#include "inputkeys.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSocketNotifier>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

GlobalInput::Device::~Device()
{
    if (fd >= 0)
        ::close(fd);
}

GlobalInput *GlobalInput::instance()
{
    static auto *input = new GlobalInput(QCoreApplication::instance());
    return input;
}
GlobalInput::GlobalInput(QObject *parent) : QObject(parent)
{
    m_probeWorker = new QObject;
    m_probeWorker->moveToThread(&m_probeThread);
    connect(&m_probeThread, &QThread::finished, m_probeWorker, &QObject::deleteLater);
    m_probeThread.start();
    m_rescan.setInterval(2000);
    connect(&m_rescan, &QTimer::timeout, this, &GlobalInput::scan);
}
GlobalInput::~GlobalInput()
{
    m_probeThread.quit();
    m_probeThread.wait();
    closeDevices();
}

void GlobalInput::subscribe(QObject *client, const QSet<QString> &keys, bool motion)
{
    if (m_clients.value(client) == keys && m_clients.contains(client) &&
        m_motionClients.contains(client) == motion)
        return;
    if (!m_clients.contains(client))
        m_clientConnections.insert(
            client, connect(client, &QObject::destroyed, this, [this, client] { unsubscribe(client); }));
    m_clients.insert(client, keys);
    const bool previousMotion = !m_motionClients.isEmpty();
    if (motion)
        m_motionClients.insert(client);
    else
        m_motionClients.remove(client);
    const auto previous = m_keys;
    m_keys.clear();
    for (const auto &set : m_clients)
        m_keys.unite(set);
    if (previous != m_keys || previousMotion != !m_motionClients.isEmpty())
        closeDevices();
    scan();
    m_rescan.start();
}
void GlobalInput::unsubscribe(QObject *client)
{
    if (!m_clients.remove(client))
        return;
    disconnect(m_clientConnections.take(client));
    const bool previousMotion = !m_motionClients.isEmpty();
    m_motionClients.remove(client);
    const auto previous = m_keys;
    m_keys.clear();
    for (const auto &set : m_clients)
        m_keys.unite(set);
    if (previous != m_keys || previousMotion != !m_motionClients.isEmpty())
        closeDevices();
    if (m_clients.isEmpty()) {
        m_rescan.stop();
        closeDevices();
        m_error.clear();
        m_motionError.clear();
        Q_EMIT statusChanged();
    } else {
        scan();
    }
}
void GlobalInput::closeDevices()
{
    for (auto &entry : m_devices)
        releaseDevice(*entry.second);
    m_devices.clear();
    m_motionAvailable = false;
}
void GlobalInput::releaseDevice(Device &device)
{
    for (const auto code : std::as_const(device.down)) {
        const auto name = InputKeys::linuxNames().value(code);
        if (--m_downCounts[name] <= 0) {
            m_downCounts.remove(name);
            Q_EMIT keyChanged(name, false);
        }
    }
    device.down.clear();
    device.motion = {};
    if (device.relative)
        Q_EMIT pointerReset();
}
void GlobalInput::scan()
{
    if (m_scanBusy || m_clients.isEmpty())
        return;
    m_scanBusy = true;
    const auto keys = m_keys;
    const bool motion = !m_motionClients.isEmpty();
    // Opening evdev devices and capability ioctls can block in drivers (even
    // with O_NONBLOCK). Never execute them on Plasma's GUI thread.
    QMetaObject::invokeMethod(
        m_probeWorker,
        [this, keys, motion] {
            auto devices = std::make_shared<std::map<QString, std::unique_ptr<Device>>>();
            QSet<QString> available;
            bool motionAvailable = false;
            const auto paths = QDir(QStringLiteral("/dev/input"))
                                   .entryList({QStringLiteral("event*")}, QDir::System | QDir::Files);
            for (const auto &name : paths) {
                const auto path = QStringLiteral("/dev/input/") + name;
                auto device = std::make_unique<Device>();
                device->fd = ::open(path.toLocal8Bit(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (device->fd < 0)
                    continue;
                std::array<unsigned char, (KEY_MAX + 8) / 8> bits{};
                bool relevant = false;
                if (ioctl(device->fd, EVIOCGBIT(EV_KEY, bits.size()), bits.data()) >= 0) {
                    for (auto it = InputKeys::linuxNames().begin(); it != InputKeys::linuxNames().end();
                         ++it) {
                        if (keys.contains(it.value()) && (bits[it.key() / 8] & (1 << (it.key() % 8)))) {
                            relevant = true;
                            available.insert(it.value());
                        }
                    }
                }
                std::array<unsigned char, (REL_MAX + 8) / 8> relativeBits{};
                device->relative =
                    motion &&
                    ioctl(device->fd, EVIOCGBIT(EV_REL, relativeBits.size()), relativeBits.data()) >= 0 &&
                    (relativeBits[REL_X / 8] & (1 << (REL_X % 8))) &&
                    (relativeBits[REL_Y / 8] & (1 << (REL_Y % 8)));
                motionAvailable = motionAvailable || device->relative;
                if (relevant || device->relative)
                    devices->emplace(path, std::move(device));
            }
            QMetaObject::invokeMethod(
                this,
                [this, devices, keys, motion, available, motionAvailable] {
                    m_scanBusy = false;
                    if (m_clients.isEmpty() || keys != m_keys || motion != !m_motionClients.isEmpty()) {
                        QMetaObject::invokeMethod(
                            m_probeWorker, [devices] { devices->clear(); }, Qt::QueuedConnection);
                        scan(); // Subscription changed while probing; discard stale descriptors.
                        return;
                    }
                    applyScan(std::move(*devices), available, motionAvailable);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}
void GlobalInput::applyScan(std::map<QString, std::unique_ptr<Device>> devices,
                            const QSet<QString> &available, bool motionAvailable)
{
    for (auto it = m_devices.begin(); it != m_devices.end();) {
        if (!devices.count(it->first)) {
            releaseDevice(*it->second);
            it = m_devices.erase(it);
        } else {
            // Keep the active fd and its held-key state; retire the probe fd below.
            ++it;
        }
    }
    for (auto &entry : devices) {
        const auto path = entry.first;
        if (m_devices.count(path))
            continue;
        auto &device = entry.second;
        device->notifier = std::make_unique<QSocketNotifier>(device->fd, QSocketNotifier::Read, this);
        connect(device->notifier.get(), &QSocketNotifier::activated, this,
                [this, path] { readDevice(path); });
        m_devices.emplace(path, std::move(device));
    }
    // Driver close callbacks may also sleep. Retire unused probe descriptors
    // on the worker rather than reintroducing a periodic GUI-thread pause.
    auto retired = std::make_shared<std::map<QString, std::unique_ptr<Device>>>(std::move(devices));
    QMetaObject::invokeMethod(m_probeWorker, [retired] { retired->clear(); }, Qt::QueuedConnection);
    const auto missing = m_keys - available;
    const auto error =
        missing.isEmpty()
            ? QString()
            : QStringLiteral("全局动作按键不可用（%1）。Wayland 下需要读取输入设备的权限；请运行仓库 "
                             "scripts/enable-global-input.sh，授权后会自动重试，无需注销。")
                  .arg(QStringList(missing.begin(), missing.end()).join(QStringLiteral(", ")));
    const auto motionError =
        !m_motionClients.isEmpty() && !motionAvailable
            ? QStringLiteral("相对鼠标移动不可用。请运行仓库 scripts/enable-global-input.sh "
                             "授权鼠标输入设备，授权后会自动重试，无需注销。")
            : QString();
    if (error != m_error || motionError != m_motionError || motionAvailable != m_motionAvailable) {
        m_error = error;
        m_motionError = motionError;
        m_motionAvailable = motionAvailable;
        Q_EMIT statusChanged();
    }
}
void GlobalInput::readDevice(const QString &path)
{
    const auto it = m_devices.find(path);
    if (it == m_devices.end())
        return;
    auto &device = *it->second;
    std::array<input_event, 64> events;
    // Bound work per notifier activation so a busy device cannot starve Qt.
    const auto count = ::read(device.fd, events.data(), sizeof(events));
    if (count <= 0) {
        if (count < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        releaseDevice(device);
        m_devices.erase(it);
        return;
    }
    for (size_t i = 0; i < size_t(count) / sizeof(input_event); ++i)
        event(device, events[i].type, events[i].code, events[i].value);
}
void GlobalInput::event(Device &device, unsigned short type, unsigned short code, int value)
{
    if (type == EV_SYN && code == SYN_DROPPED) {
        releaseDevice(device);
        device.dropped = true;
    } else if (type == EV_SYN && code == SYN_REPORT) {
        if (!device.dropped && device.relative && !m_motionClients.isEmpty() && !device.motion.isNull())
            Q_EMIT pointerMoved(device.motion);
        device.motion = {};
        device.dropped = false;
    } else if (type == EV_REL && !device.dropped && device.relative && !m_motionClients.isEmpty()) {
        if (code == REL_X)
            device.motion.rx() += value;
        else if (code == REL_Y)
            device.motion.ry() += value;
    } else if (type == EV_KEY && !device.dropped && value != 2) {
        const auto name = InputKeys::linuxNames().value(code);
        if (name.isEmpty() || !m_keys.contains(name))
            return;
        if (value == 1 && !device.down.contains(code)) {
            device.down.insert(code);
            if (++m_downCounts[name] == 1)
                Q_EMIT keyChanged(name, true);
        } else if (value == 0 && device.down.remove(code)) {
            if (--m_downCounts[name] <= 0) {
                m_downCounts.remove(name);
                Q_EMIT keyChanged(name, false);
            }
        }
    }
}

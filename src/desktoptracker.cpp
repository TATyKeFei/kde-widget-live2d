#include "desktoptracker.h"
#include <QCoreApplication>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace
{
constexpr auto objectPath = "/org/kde/Live2D/Pointer";
QDBusMessage scriptingCall(const QString &method, const QVariantList &arguments)
{
    auto call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
                                               QStringLiteral("org.kde.kwin.Scripting"), method);
    call.setArguments(arguments);
    return call;
}
QByteArray jsString(const QString &value)
{
    const auto array = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return array.mid(1, array.size() - 2);
}
} // namespace

DesktopTracker *DesktopTracker::instance()
{
    static auto *tracker = new DesktopTracker(QDBusConnection::sessionBus(), qApp);
    return tracker;
}

DesktopTracker::DesktopTracker(const QDBusConnection &bus, QObject *parent) : QObject(parent), m_bus(bus)
{
    m_registered =
        m_bus.registerObject(QString::fromLatin1(objectPath), this, QDBusConnection::ExportScriptableSlots);
    m_pluginName =
        QStringLiteral("live2d-pointer-") + m_bus.baseService().replace(':', '_').replace('.', '_');
    m_retry.setInterval(5000);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &DesktopTracker::reconcile);
    m_watchdog.setInterval(1000);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        if (!m_lastUpdate.isValid() || m_lastUpdate.elapsed() > 3000) {
            failed(QStringLiteral("未收到 KWin 指针数据，正在重试。"));
        }
    });
    auto *watcher = new QDBusServiceWatcher(QStringLiteral("org.kde.KWin"), m_bus,
                                            QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) { ownerChanged(owner); });
    const auto generation = m_generation;
    auto query = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
    query << QStringLiteral("org.kde.KWin");
    auto *pending = new QDBusPendingCallWatcher(m_bus.asyncCall(query, 3000), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *call) {
                const QDBusPendingReply<QString> reply = *call;
                call->deleteLater();
                if (generation == m_generation)
                    ownerChanged(reply.isError() ? QString() : reply.value());
            });
}

DesktopTracker::~DesktopTracker()
{
    if (m_phase != Phase::Idle)
        m_bus.send(scriptingCall(QStringLiteral("unloadScript"), {m_pluginName}));
    if (m_registered)
        m_bus.unregisterObject(QString::fromLatin1(objectPath));
}

void DesktopTracker::subscribe(QObject *client)
{
    if (m_clients.contains(client))
        return;
    m_clients.insert(client);
    connect(client, &QObject::destroyed, this, [this, client] { unsubscribe(client); });
    reconcile();
}

void DesktopTracker::unsubscribe(QObject *client)
{
    if (!m_clients.remove(client))
        return;
    disconnect(client, &QObject::destroyed, this, nullptr);
    reconcile();
}

void DesktopTracker::ownerChanged(const QString &owner)
{
    ++m_generation;
    m_owner = owner;
    m_phase = Phase::Idle;
    m_scriptId = -1;
    m_script.reset();
    m_retry.stop();
    m_watchdog.stop();
    m_state = {};
    m_lastUpdate.invalidate();
    Q_EMIT changed();
    reconcile();
}

void DesktopTracker::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    Q_EMIT changed();
}

void DesktopTracker::failed(const QString &error)
{
    m_state = {};
    m_watchdog.stop();
    setError(error);
    m_retry.start();
    stopScript();
}

void DesktopTracker::reconcile()
{
    if (m_clients.isEmpty()) {
        m_retry.stop();
        m_watchdog.stop();
        m_state = {};
        if (m_phase == Phase::Running)
            stopScript();
        return;
    }
    if (m_phase != Phase::Idle || m_retry.isActive())
        return;
    if (!m_registered || !m_bus.isConnected()) {
        setError(QStringLiteral("无法连接会话 D-Bus，全局跟随不可用。"));
        return;
    }
    if (m_owner.isEmpty()) {
        setError(QStringLiteral("未找到 KWin，全局跟随不可用。"));
        return;
    }
    QFile resource(QStringLiteral(":/live2d/kwin-pointer.js"));
    if (!resource.open(QIODevice::ReadOnly)) {
        failed(QStringLiteral("无法读取 KWin 指针脚本。"));
        return;
    }
    auto code = resource.readAll();
    code.replace("@RECEIVER@", jsString(m_bus.baseService()));
    code.replace("@PLUGIN@", jsString(m_pluginName));
    m_script =
        std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/live2d-pointer-XXXXXX.js"));
    if (!m_script->open() || m_script->write(code) != code.size() || !m_script->flush()) {
        failed(QStringLiteral("无法创建 KWin 指针脚本。"));
        return;
    }
    m_phase = Phase::Loading;
    const auto generation = m_generation;
    auto *pending = new QDBusPendingCallWatcher(
        m_bus.asyncCall(scriptingCall(QStringLiteral("loadScript"), {m_script->fileName(), m_pluginName}),
                        3000),
        this);
    connect(
        pending, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *call) {
            const QDBusPendingReply<int> reply = *call;
            call->deleteLater();
            if (generation != m_generation)
                return;
            if (reply.isError() || reply.value() < 0) {
                failed(QStringLiteral("KWin 指针脚本加载失败：%1")
                           .arg(reply.isError() ? reply.error().message() : QStringLiteral("脚本不可用")));
                return;
            }
            m_scriptId = reply.value();
            if (m_clients.isEmpty())
                stopScript();
            else
                runScript(generation);
        });
}

void DesktopTracker::runScript(int generation, bool legacyPath)
{
    m_scriptPath = legacyPath ? QStringLiteral("/%1").arg(m_scriptId)
                              : QStringLiteral("/Scripting/Script%1").arg(m_scriptId);
    const auto message =
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), m_scriptPath,
                                       QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run"));
    auto *pending = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 3000), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this,
            [this, generation, legacyPath](QDBusPendingCallWatcher *call) {
                const QDBusPendingReply<> reply = *call;
                call->deleteLater();
                if (generation != m_generation)
                    return;
                if (reply.isError()) {
                    if (!legacyPath && (reply.error().type() == QDBusError::UnknownObject ||
                                        reply.error().type() == QDBusError::UnknownMethod ||
                                        reply.error().type() == QDBusError::UnknownInterface)) {
                        runScript(generation, true);
                    } else
                        failed(QStringLiteral("KWin 指针脚本启动失败：%1").arg(reply.error().message()));
                    return;
                }
                m_phase = Phase::Running;
                m_lastUpdate.start();
                m_watchdog.start();
                reconcile();
            });
}

void DesktopTracker::stopScript()
{
    m_phase = Phase::Stopping;
    m_watchdog.stop();
    m_state = {};
    const auto generation = m_generation;
    auto *pending = new QDBusPendingCallWatcher(
        m_bus.asyncCall(scriptingCall(QStringLiteral("unloadScript"), {m_pluginName}), 3000), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *call) {
                const QDBusPendingReply<bool> reply = *call;
                call->deleteLater();
                if (generation != m_generation)
                    return;
                m_phase = Phase::Idle;
                m_scriptId = -1;
                m_script.reset();
                if (reply.isError())
                    m_retry.start();
                reconcile();
            });
}

bool DesktopTracker::update(const QString &json)
{
    if (m_clients.isEmpty() || (calledFromDBus() && message().service() != m_owner))
        return false;
    if (json.size() > 1024)
        return false;
    const auto object = QJsonDocument::fromJson(json.toUtf8()).object();
    const auto x = object.value("x"), y = object.value("y");
    if (!x.isDouble() || !y.isDouble() || !std::isfinite(x.toDouble()) || !std::isfinite(y.toDouble()) ||
        !object.value("normal").isBool() || !object.value("fullscreen").isBool())
        return false;
    m_state = {{x.toDouble(), y.toDouble()},
               object.value("normal").toBool(),
               object.value("fullscreen").toBool(),
               true};
    m_lastUpdate.start();
    setError({});
    return true;
}

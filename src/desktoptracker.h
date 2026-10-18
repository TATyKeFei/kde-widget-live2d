#pragma once

#include "cursorstate.h"
#include <QDBusConnection>
#include <QDBusContext>
#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QTemporaryFile>
#include <QTimer>
#include <memory>

// One temporary KWin script per host process, shared by all visible, running items.
class DesktopTracker : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.Live2D.Pointer")
  public:
    static DesktopTracker *instance();
    explicit DesktopTracker(const QDBusConnection &bus, QObject *parent = nullptr);
    ~DesktopTracker() override;
    void subscribe(QObject *client);
    void unsubscribe(QObject *client);
    const CursorTracking::State &state() const { return m_state; }
    QString errorString() const { return m_error; }

  public Q_SLOTS:
    Q_SCRIPTABLE bool update(const QString &json);

  Q_SIGNALS:
    void changed();

  private:
    void ownerChanged(const QString &owner);
    void reconcile();
    void runScript(int generation, bool legacyPath = false);
    void stopScript();
    void setError(const QString &error);
    void failed(const QString &error);
    QDBusConnection m_bus;
    QSet<QObject *> m_clients;
    CursorTracking::State m_state;
    QString m_owner, m_pluginName, m_scriptPath, m_error;
    std::unique_ptr<QTemporaryFile> m_script;
    QTimer m_retry, m_watchdog;
    QElapsedTimer m_lastUpdate;
    enum class Phase { Idle, Loading, Running, Stopping };
    Phase m_phase = Phase::Idle;
    int m_generation = 0, m_scriptId = -1;
    bool m_registered = false;
};

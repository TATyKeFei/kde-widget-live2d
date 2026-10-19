#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

// One input state per model instance. Chords fire on the down edge; repeats,
// releases, toggles and cancellation remain identical for Qt and evdev input.
class ModelInputState
{
  public:
    void setBindings(const QJsonArray &bindings) { m_bindings = bindings; }
    QSet<QString> keys() const
    {
        QSet<QString> keys;
        for (const auto &value : m_bindings)
            for (const auto &key : value.toObject()["keys"].toArray())
                keys.insert(key.toString());
        return keys;
    }
    QJsonArray key(const QString &token, bool down)
    {
        if (token.isEmpty() || m_pressed.contains(token) == down)
            return {};
        if (down)
            m_pressed.insert(token);
        else
            m_pressed.remove(token);
        QJsonArray actions;
        const auto previousExpressions = expressions();
        for (int i = 0; i < m_bindings.size(); ++i) {
            const auto binding = m_bindings[i].toObject();
            const auto keys = binding["keys"].toArray();
            bool matched = !keys.isEmpty();
            for (const auto &key : keys)
                matched &= m_pressed.contains(key.toString());
            if (matched == m_active.contains(i))
                continue;
            if (matched)
                m_active.insert(i);
            else
                m_active.remove(i);
            if (!matched)
                continue;
            if (binding["action"].toString() == QLatin1String("motion")) {
                actions.append(binding);
            } else if (!binding["onRelease"].toBool()) {
                const auto name = binding["name"].toString();
                if (!m_toggled.remove(name))
                    m_toggled.insert(name);
            }
        }
        const auto currentExpressions = expressions();
        for (const auto &name : previousExpressions - currentExpressions)
            actions.append(QJsonObject{{"action", "expression"}, {"name", name}, {"active", false}});
        for (const auto &name : currentExpressions - previousExpressions)
            actions.append(QJsonObject{{"action", "expression"}, {"name", name}, {"active", true}});
        return actions;
    }
    QJsonArray clear()
    {
        QJsonArray actions;
        for (const auto &name : expressions())
            actions.append(QJsonObject{{"action", "expression"}, {"name", name}, {"active", false}});
        m_pressed.clear();
        m_active.clear();
        m_toggled.clear();
        return actions;
    }

  private:
    QSet<QString> expressions() const
    {
        auto names = m_toggled;
        for (const auto index : m_active) {
            const auto binding = m_bindings[index].toObject();
            if (binding["action"].toString() == QLatin1String("expression") && binding["onRelease"].toBool())
                names.insert(binding["name"].toString());
        }
        return names;
    }
    QJsonArray m_bindings;
    QSet<QString> m_pressed, m_toggled;
    QSet<int> m_active;
};

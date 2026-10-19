#pragma once

#include "pointermapping.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <vector>

// All model references are resolved within the selected model directory.
// Resolve symlinks as well as '..' before allowing the SDK to read a file.
class ModelAssets
{
  public:
    explicit ModelAssets(const QUrl &source);
    QByteArray read(const QString &relativePath) const;
    QByteArray readJson(const QString &relativePath) const;
    const QByteArray &settings() const { return m_settings; }
    const QJsonArray &hotkeys() const { return m_hotkeys; }
    const std::vector<PointerMapping> &pointerMappings() const { return m_pointerMappings; }

  private:
    QString resolve(const QString &relativePath) const;
    QString resolveVTubeResource(const QString &path, const QString &suffix) const;
    void importVTube(const QString &configName, const QString &modelPath, QJsonObject &root);
    QDir m_directory;
    QByteArray m_settings;
    QJsonArray m_hotkeys;
    std::vector<PointerMapping> m_pointerMappings;
};

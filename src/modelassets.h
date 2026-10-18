#pragma once

#include <QDir>
#include <QJsonObject>
#include <QUrl>

// All model references are resolved within the selected model directory.
// Resolve symlinks as well as '..' before allowing the SDK to read a file.
class ModelAssets
{
  public:
    explicit ModelAssets(const QUrl &source);
    QByteArray read(const QString &relativePath) const;
    QByteArray readJson(const QString &relativePath) const;
    const QByteArray &settings() const { return m_settings; }

  private:
    QString resolve(const QString &relativePath) const;
    QDir m_directory;
    QByteArray m_settings;
};

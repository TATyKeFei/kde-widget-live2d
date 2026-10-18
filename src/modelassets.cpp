#include "modelassets.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <stdexcept>

namespace
{
[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("无法读取文件：%1（%2）").arg(path, file.errorString()));
    }
    constexpr qint64 maxFileSize = 128 * 1024 * 1024;
    if (file.size() <= 0 || file.size() > maxFileSize) {
        fail(QStringLiteral("文件为空或超过 128 MiB：%1").arg(path));
    }
    const auto data = file.readAll();
    if (file.error() != QFile::NoError) {
        fail(QStringLiteral("读取文件失败：%1").arg(path));
    }
    return data;
}

QJsonObject parseObject(const QByteArray &data, const QString &path)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        fail(QStringLiteral("无效的 JSON：%1（%2）").arg(path, error.errorString()));
    }
    return document.object();
}
} // namespace

ModelAssets::ModelAssets(const QUrl &source)
{
    if (!source.isLocalFile() && !source.isRelative()) {
        fail(QStringLiteral("请选择本地 .model3.json 文件。"));
    }
    const QFileInfo info(source.isLocalFile() ? source.toLocalFile() : source.toString());
    if (!info.fileName().endsWith(QLatin1String(".model3.json")) || !info.isFile()) {
        fail(QStringLiteral("模型文件不存在，或不是 .model3.json：%1").arg(info.filePath()));
    }
    m_directory = QDir(info.canonicalPath());
    m_settings = readFile(info.canonicalFilePath());
    const auto root = parseObject(m_settings, info.filePath());
    const auto refs = root.value(QStringLiteral("FileReferences")).toObject();
    if (root.value(QStringLiteral("Version")).toInt() != 3 ||
        refs.value(QStringLiteral("Moc")).toString().isEmpty() ||
        !refs.value(QStringLiteral("Textures")).isArray()) {
        fail(QStringLiteral("模型缺少 Version: 3、Moc 或 Textures。"));
    }
}

QString ModelAssets::resolve(const QString &relativePath) const
{
    if (relativePath.isEmpty() || QDir::isAbsolutePath(relativePath) ||
        relativePath.contains(QLatin1Char('\\'))) {
        fail(QStringLiteral("无效的模型资源路径：%1").arg(relativePath));
    }
    const QFileInfo info(m_directory.filePath(relativePath));
    const auto canonical = info.canonicalFilePath();
    if (!info.isFile() || !canonical.startsWith(m_directory.canonicalPath() + QLatin1Char('/'))) {
        fail(QStringLiteral("模型资源不存在或位于模型目录之外：%1").arg(relativePath));
    }
    return canonical;
}

QByteArray ModelAssets::read(const QString &relativePath) const { return readFile(resolve(relativePath)); }

QByteArray ModelAssets::readJson(const QString &relativePath) const
{
    const auto data = read(relativePath);
    parseObject(data, relativePath);
    return data;
}

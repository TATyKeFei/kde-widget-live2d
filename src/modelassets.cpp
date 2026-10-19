#include "modelassets.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <algorithm>
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
    auto root = parseObject(m_settings, info.filePath());
    auto refs = root.value(QStringLiteral("FileReferences")).toObject();
    if (root.value(QStringLiteral("Version")).toInt() != 3 ||
        refs.value(QStringLiteral("Moc")).toString().isEmpty() ||
        !refs.value(QStringLiteral("Textures")).isArray()) {
        fail(QStringLiteral("模型缺少 Version: 3、Moc 或 Textures。"));
    }

    const auto configName =
        info.fileName().chopped(QStringLiteral(".model3.json").size()) + QStringLiteral(".vtube.json");
    if (!QFileInfo::exists(m_directory.filePath(configName)))
        return;
    try {
        const auto original = root;
        importVTube(configName, info.canonicalFilePath(), root);
        // Cubism's numeric parser requires newlines after terminal numbers.
        if (root != original)
            m_settings = QJsonDocument(root).toJson(QJsonDocument::Indented);
    } catch (const std::exception &error) {
        qWarning().noquote() << "忽略 VTube Studio 配置：" << error.what();
    }
}

QString ModelAssets::resolveVTubeResource(const QString &path, const QString &suffix) const
{
    if (path.isEmpty() || !path.endsWith(suffix))
        fail(QStringLiteral("无效的 VTube Studio 资源：%1").arg(path));
    auto relative = path;
    if (!path.contains(QLatin1Char('/')) && !path.contains(QLatin1Char('\\')) &&
        !QFileInfo::exists(m_directory.filePath(path))) {
        relative.prepend(suffix == QLatin1String(".motion3.json") ? QStringLiteral("animations/")
                                                                  : QStringLiteral("expressions/"));
    }
    readJson(relative); // Includes canonical path and symlink containment checks.
    return relative;
}

void ModelAssets::importVTube(const QString &configName, const QString &modelPath, QJsonObject &root)
{
    const auto config = parseObject(read(configName), configName);
    const auto vtsRefs = config["FileReferences"].toObject();
    if (resolve(vtsRefs["Model"].toString()) != modelPath)
        return;
    for (const auto &value : config["ParameterSettings"].toArray()) {
        if (const auto mapping = PointerMapping::fromJson(value.toObject())) {
            const auto existing =
                std::find_if(m_pointerMappings.begin(), m_pointerMappings.end(),
                             [&](const auto &entry) { return entry.parameter == mapping->parameter; });
            if (existing == m_pointerMappings.end())
                m_pointerMappings.push_back(*mapping);
            else
                *existing = *mapping;
        }
    }
    auto refs = root["FileReferences"].toObject();
    auto motions = refs.value("Motions").toObject();
    auto expressions = refs.value("Expressions").toArray();
    auto addMotion = [&](const QString &file, const QString &preferred, const QJsonObject &options) {
        // Reuse an existing reference instead of replacing native motion groups.
        for (auto it = motions.begin(); it != motions.end(); ++it) {
            const auto entries = it.value().toArray();
            for (int index = 0; index < entries.size(); ++index) {
                if (entries[index].toObject()["File"].toString() == file)
                    return QJsonObject{{"group", it.key()}, {"index", index}};
            }
        }
        auto group = preferred;
        for (int n = 2; motions.contains(group); ++n)
            group = preferred + QStringLiteral(" (%1)").arg(n);
        auto entry = options;
        entry.insert("File", file);
        motions.insert(group, QJsonArray{entry});
        return QJsonObject{{"group", group}, {"index", 0}};
    };
    const auto idle = vtsRefs["IdleAnimation"].toString();
    if (motions.value("Idle").toArray().isEmpty() && !idle.isEmpty()) {
        try {
            const auto file = resolveVTubeResource(idle, QStringLiteral(".motion3.json"));
            motions.insert("Idle", QJsonArray{QJsonObject{{"File", file}}});
        } catch (const std::exception &error) {
            qWarning().noquote() << "忽略 VTube Studio 待机动作：" << error.what();
        }
    }
    for (const auto &value : config["Hotkeys"].toArray()) {
        const auto hotkey = value.toObject();
        if (!hotkey["IsActive"].toBool(true))
            continue;
        const auto action = hotkey["Action"].toString();
        const auto path = hotkey["File"].toString();
        if (path.isEmpty() ||
            (action != QLatin1String("TriggerAnimation") && action != QLatin1String("ToggleExpression")))
            continue;
        try {
            const bool animation = action == QLatin1String("TriggerAnimation");
            const auto suffix = animation ? QStringLiteral(".motion3.json") : QStringLiteral(".exp3.json");
            const auto file = resolveVTubeResource(path, suffix);
            const auto base = QFileInfo(file).fileName().chopped(suffix.size());
            QJsonObject binding{{"action", animation ? "motion" : "expression"},
                                {"onRelease", hotkey["DeactivateAfterKeyUp"].toBool()}};
            if (animation) {
                const double fade = std::clamp(hotkey["FadeSecondsAmount"].toDouble(0.5), 0.0, 10.0);
                const auto target = addMotion(file, base, {{"FadeInTime", fade}, {"FadeOutTime", fade}});
                binding.insert("group", target["group"]);
                binding.insert("index", target["index"]);
            } else {
                QString name;
                QSet<QString> names;
                for (const auto &existing : expressions) {
                    const auto entry = existing.toObject();
                    names.insert(entry["Name"].toString());
                    if (entry["File"].toString() == file)
                        name = entry["Name"].toString();
                }
                if (name.isEmpty()) {
                    name = hotkey["Name"].toString().trimmed();
                    if (name.isEmpty())
                        name = base;
                    const auto preferred = name;
                    for (int n = 2; names.contains(name); ++n)
                        name = preferred + QStringLiteral(" (%1)").arg(n);
                    expressions.append(QJsonObject{{"Name", name}, {"File", file}});
                }
                binding.insert("name", name);
            }
            QJsonArray triggers;
            const auto keys = hotkey["Triggers"].toObject();
            for (const auto *key : {"Trigger1", "Trigger2", "Trigger3"}) {
                const auto token = keys[key].toString();
                if (!token.isEmpty() && !triggers.contains(token))
                    triggers.append(token);
            }
            binding.insert("keys", triggers);
            m_hotkeys.append(binding);
        } catch (const std::exception &error) {
            // A stale hotkey should not disable valid actions or the model.
            qWarning().noquote() << "忽略 VTube Studio 动作：" << error.what();
        }
    }
    if (!motions.isEmpty())
        refs.insert("Motions", motions);
    if (!expressions.isEmpty())
        refs.insert("Expressions", expressions);
    root.insert("FileReferences", refs);
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

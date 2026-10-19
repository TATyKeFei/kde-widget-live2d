// Keep Qt OpenGL headers out of this file; their macros conflict with GLEW.
#include "cubismmodel.h"
#include "modelassets.h"
#include "motionpartopacity.h"
#ifdef LIVE2D_VULKAN
#include "graphicscontext.h"
#include <Rendering/Vulkan/CubismDeviceInfo_Vulkan.hpp>
#include <Rendering/Vulkan/CubismRenderer_Vulkan.hpp>
using ModelRenderer = Csm::Rendering::CubismRenderer_Vulkan;
#else
#include <GL/glew.h>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>
using ModelRenderer = Csm::Rendering::CubismRenderer_OpenGLES2;
#endif

#include <CubismModelSettingJson.hpp>
#include <Id/CubismIdManager.hpp>
#include <Model/CubismUserModel.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismMotionQueueEntry.hpp>
#include <QDebug>
#include <QFile>
#include <QImageReader>
#include <QRandomGenerator>
#include <QRectF>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

namespace
{
class Allocator final : public Csm::ICubismAllocator
{
  public:
    void *Allocate(Csm::csmSizeType size) override { return std::malloc(size); }
    void Deallocate(void *memory) override { std::free(memory); }
    void *AllocateAligned(Csm::csmSizeType size, Csm::csmUint32 alignment) override
    {
        void *memory = nullptr;
        return posix_memalign(&memory, std::max<size_t>(alignment, sizeof(void *)), size) == 0 ? memory
                                                                                               : nullptr;
    }
    void DeallocateAligned(void *memory) override { std::free(memory); }
};

Allocator allocator;
Csm::CubismFramework::Option options;
int sdkUsers = 0;
void *sdkShareGroup = nullptr;

#ifdef LIVE2D_VULKAN
struct Texture {
    Csm::CubismImageVulkan image;
    ~Texture() { destroyVulkanTexture(image); }
};
#else
struct Texture {
    GLuint id = 0;
    Texture() { glGenTextures(1, &id); }
    ~Texture()
    {
        if (id) {
            glDeleteTextures(1, &id);
        }
    }
};

#endif

Csm::csmByte *loadShader(const std::string path, Csm::csmSizeInt *size)
{
    QFile file(QStringLiteral(":/live2d/") + QString::fromStdString(path));
    *size = 0;
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot load Cubism shader:" << file.fileName();
        return nullptr;
    }
    const auto bytes = file.readAll();
    auto *result = new Csm::csmByte[bytes.size()];
    std::memcpy(result, bytes.constData(), bytes.size());
    *size = static_cast<Csm::csmSizeInt>(bytes.size());
    return result;
}

void sdkLog(const char *message) { qDebug().noquote() << "[Live2D]" << message; }

const Csm::csmByte *bytes(const QByteArray &data)
{
    return reinterpret_cast<const Csm::csmByte *>(data.constData());
}

[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }
} // namespace

void acquireCubism(void *shareGroup, bool compatibleContext)
{
#ifndef LIVE2D_VULKAN
    if (!compatibleContext) {
        fail(QStringLiteral("此版本需要桌面 OpenGL 兼容上下文，请使用 OpenGL 后端运行。"));
    }
    if (sdkUsers && shareGroup != sdkShareGroup) {
        fail(QStringLiteral("当前 OpenGL "
                            "上下文与已有角色不共享资源；此版本无法同时在这两个窗口中渲染。"));
    }
    glewExperimental = GL_TRUE;
    const auto result = glewInit();
    // System GLEW may report no GLX display under Wayland after loading GL
    // entry points successfully. The EGL build does not have that limitation.
    if (result != GLEW_OK && result != GLEW_ERROR_NO_GLX_DISPLAY) {
        fail(QStringLiteral("OpenGL 初始化失败：%1")
                 .arg(QString::fromLatin1(reinterpret_cast<const char *>(glewGetErrorString(result)))));
    }
    if (!glCreateShader || !glGenFramebuffers) {
        fail(QStringLiteral("显卡未提供 Live2D 所需的 OpenGL 功能。"));
    }
    while (glGetError() != GL_NO_ERROR) {
    } // GLEW can leave GL_INVALID_ENUM.
#else
    Q_UNUSED(compatibleContext);
#endif
    if (!sdkUsers) {
        options = {};
        options.LogFunction = sdkLog;
        options.LoggingLevel = Csm::CubismFramework::Option::LogLevel_Warning;
        options.LoadFileFunction = loadShader;
        options.ReleaseBytesFunction = [](Csm::csmByte *data) { delete[] data; };
        if (!Csm::CubismFramework::StartUp(&allocator, &options)) {
            fail(QStringLiteral("Cubism Framework 初始化失败。"));
        }
        Csm::CubismFramework::Initialize();
        sdkShareGroup = shareGroup;
    }
    ++sdkUsers;
}

void releaseCubism()
{
    if (--sdkUsers == 0) {
#ifdef LIVE2D_VULKAN
        Csm::Rendering::CubismDeviceInfo_Vulkan::ReleaseAllDeviceInfo();
#endif
        Csm::CubismFramework::Dispose();
        Csm::CubismFramework::CleanUp();
        sdkShareGroup = nullptr;
    }
}

class CubismModel::Impl : public Csm::CubismUserModel
{
  public:
    explicit Impl(const QUrl &source, const QSize &size) : assets(source)
    {
        const auto &json = assets.settings();
        settings = std::make_unique<Csm::CubismModelSettingJson>(bytes(json), json.size());
        const auto moc = assets.read(QString::fromUtf8(settings->GetModelFileName()));
        LoadModel(bytes(moc), moc.size(), true);
        if (!_model) {
            fail(QStringLiteral("无法加载 MOC3；请检查模型是否完整以及 SDK 版本是否兼容。"));
        }
        auto *ids = Csm::CubismFramework::GetIdManager();
        for (const auto &mapping : assets.pointerMappings()) {
            const auto id = ids->GetId(mapping.parameter.toUtf8().constData());
            // Do not create Cubism's synthetic parameters for stale VTS IDs.
            for (int index = 0; index < _model->GetParameterCount(); ++index) {
                if (_model->GetParameterId(index) == id) {
                    pointerMappings.push_back({mapping, id, index, _model->GetParameterValue(index)});
                    break;
                }
            }
        }
        if (*settings->GetPhysicsFileName()) {
            const auto data = assets.readJson(QString::fromUtf8(settings->GetPhysicsFileName()));
            LoadPhysics(bytes(data), data.size());
        }
        if (*settings->GetPoseFileName()) {
            const auto data = assets.readJson(QString::fromUtf8(settings->GetPoseFileName()));
            LoadPose(bytes(data), data.size());
        }
        if (settings->GetEyeBlinkParameterCount()) {
            _eyeBlink = Csm::CubismEyeBlink::Create(settings.get());
        }
        for (int i = 0; i < settings->GetEyeBlinkParameterCount(); ++i) {
            blinkIds.PushBack(settings->GetEyeBlinkParameterId(i));
        }
        for (int i = 0; i < settings->GetLipSyncParameterCount(); ++i) {
            lipIds.PushBack(settings->GetLipSyncParameterId(i));
        }
        for (int i = 0; i < settings->GetExpressionCount(); ++i) {
            expressionNames.append(QString::fromUtf8(settings->GetExpressionName(i)));
        }

        // Fit the actual meshes, which may occupy only part of the model canvas.
        float left = std::numeric_limits<float>::max(), bottom = left;
        float right = -left, top = -left;
        for (int i = 0; i < _model->GetDrawableCount(); ++i) {
            const auto *vertices = _model->GetDrawableVertexPositions(i);
            for (int j = 0; j < _model->GetDrawableVertexCount(i); ++j) {
                left = std::min(left, vertices[j].X);
                right = std::max(right, vertices[j].X);
                bottom = std::min(bottom, vertices[j].Y);
                top = std::max(top, vertices[j].Y);
            }
        }
        bounds = QRectF(QPointF(left, bottom), QPointF(right, top));
        if (!bounds.isValid() || !std::isfinite(bounds.width()) || !std::isfinite(bounds.height())) {
            fail(QStringLiteral("模型没有有效的可绘制网格。"));
        }
        CreateRenderer(size.width(), size.height());
        auto *renderer = GetRenderer<ModelRenderer>();
        renderer->IsPremultipliedAlpha(true);
        if (!settings->GetTextureCount()) {
            fail(QStringLiteral("模型没有纹理。"));
        }
        for (int i = 0; i < settings->GetTextureCount(); ++i) {
            const auto textureData = assets.read(QString::fromUtf8(settings->GetTextureFileName(i)));
            QImage image = QImage::fromData(textureData);
            if (image.isNull() || image.width() > 16384 || image.height() > 16384) {
                fail(QStringLiteral("无法解码模型纹理，或纹理尺寸超过限制。"));
            }
            image = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            auto texture = std::make_unique<Texture>();
#ifdef LIVE2D_VULKAN
            texture->image = uploadVulkanTexture(image);
            renderer->BindTexture(texture->image);
#else
            if (!texture->id) {
                fail(QStringLiteral("无法创建模型纹理。"));
            }
            glBindTexture(GL_TEXTURE_2D, texture->id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width(), image.height(), 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, image.constBits());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glGenerateMipmap(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
            renderer->BindTexture(i, texture->id);
#endif
            textures.push_back(std::move(texture));
        }
    }

    ~Impl() override
    {
        // Cached motions outlive every queue entry that references them. Base
        // managers are destroyed after derived members, so stop them explicitly.
        _motionManager->StopAllMotions();
        _expressionManager->StopAllMotions();
        heldExpressions.clear();
        DeleteRenderer();
    }

    struct MotionDeleter {
        void operator()(Csm::ACubismMotion *motion) const { Csm::ACubismMotion::Delete(motion); }
    };
    using MotionPtr = std::unique_ptr<Csm::ACubismMotion, MotionDeleter>;
    struct MotionCache {
        QByteArray json;
        std::vector<MotionPtr> instances;
    };

    // A motion holds mutable playback state in addition to queue-entry state.
    // Reuse it only after its previous entry has left this manager, preserving
    // overlapping presses and fade-outs without parsing on every replay.
    template <typename Loader>
    Csm::ACubismMotion *cachedMotion(MotionCache &cache, Csm::CubismMotionQueueManager *manager, Loader load)
    {
        auto *entries = manager->GetCubismMotionQueueEntries();
        for (const auto &instance : cache.instances) {
            bool playing = false;
            for (Csm::csmUint32 i = 0; i < entries->GetSize(); ++i) {
                auto *entry = entries->At(i);
                if (entry && entry->GetCubismMotion() == instance.get()) {
                    playing = true;
                    break;
                }
            }
            if (!playing)
                return instance.get();
        }
        MotionPtr instance(load());
        if (!instance)
            return nullptr;
        auto *result = instance.get();
        cache.instances.push_back(std::move(instance));
        return result;
    }

    Csm::ACubismMotion *cachedExpression(const QString &name, int index,
                                         Csm::CubismExpressionMotionManager *manager, bool held)
    {
        // Held expressions use zero fade-in and must not modify the normal
        // expression's cached object or share its mutable fade weight.
        auto &cache = held ? heldExpressionCache[name] : expressionCache[name];
        if (cache.json.isEmpty())
            cache.json = assets.readJson(QString::fromUtf8(settings->GetExpressionFileName(index)));
        return cachedMotion(cache, manager, [&] {
            auto *motion = LoadExpression(bytes(cache.json), cache.json.size(), name.toUtf8().constData());
            if (motion && held)
                motion->SetFadeInTime(0);
            return motion;
        });
    }

    bool startMotion(const QString &group, int priority, int requestedIndex = -1)
    {
        const auto name = group.toUtf8();
        const int count = settings->GetMotionCount(name.constData());
        if (count <= 0 || requestedIndex >= count ||
            (priority < 3 && !_motionManager->ReserveMotion(priority))) {
            return false;
        }
        const int index = requestedIndex < 0 ? QRandomGenerator::global()->bounded(count) : requestedIndex;
        auto &cache = motionCache[{group, index}];
        if (cache.json.isEmpty()) {
            cache.json =
                assets.readJson(QString::fromUtf8(settings->GetMotionFileName(name.constData(), index)));
            if (!_pose)
                motionPartOpacity.addMotion(_model, cache.json);
        }
        auto *motion = cachedMotion(cache, _motionManager, [&] {
            auto *loaded = static_cast<Csm::CubismMotion *>(
                LoadMotion(bytes(cache.json), cache.json.size(), nullptr, nullptr, nullptr, settings.get(),
                           name.constData(), index));
            if (loaded)
                loaded->SetEffectIds(blinkIds, lipIds);
            return loaded;
        });
        if (!motion) {
            _motionManager->SetReservePriority(0);
            return false;
        }
        _motionManager->StartMotionPriority(motion, false, priority);
        return true;
    }

    void expression(const QString &name)
    {
        const int index = expressionNames.indexOf(name);
        if (index < 0) {
            return;
        }
        if (auto *motion = cachedExpression(name, index, _expressionManager, false))
            _expressionManager->StartMotion(motion, false);
    }

    void expressionActive(const QString &name, bool active)
    {
        if (!expressionNames.contains(name))
            return;
        auto &manager = heldExpressions[name];
        if (!manager)
            manager = std::make_unique<Csm::CubismExpressionMotionManager>();
        manager->StopAllMotions();
        if (!active)
            return;
        const int index = expressionNames.indexOf(name);
        if (auto *motion = cachedExpression(name, index, manager.get(), true))
            manager->StartMotion(motion, false);
    }

    void draw(float delta, const QSize &size, float zoom, const QPointF &gaze, const QPointF &pointer,
              bool follow, const QString &idle)
    {
        _model->LoadParameters();
        if (_motionManager->IsFinished() && !idle.isEmpty()) {
            startMotion(idle, 1);
        }
        const bool updated = _motionManager->UpdateMotion(_model, delta);
        if (!_pose)
            motionPartOpacity.apply(_model);
        _model->SaveParameters();
        if (!updated && _eyeBlink) {
            _eyeBlink->UpdateParameters(_model, delta);
        }
        for (auto &entry : pointerMappings) {
            if (follow) {
                const double target = std::clamp(entry.mapping.target(pointer),
                                                 double(_model->GetParameterMinimumValue(entry.index)),
                                                 double(_model->GetParameterMaximumValue(entry.index)));
                entry.value = entry.mapping.smooth(entry.value, target, delta);
                _model->SetParameterValue(entry.index, entry.value);
            } else {
                // Mappings are transient, after SaveParameters. Turning follow
                // off immediately restores the animation's own parameter value.
                entry.value = _model->GetParameterValue(entry.index);
            }
        }
        _expressionManager->UpdateMotion(_model, delta);
        for (const auto &entry : heldExpressions)
            entry.second->UpdateMotion(_model, delta);
        elapsed += delta;
        auto *ids = Csm::CubismFramework::GetIdManager();
        _model->AddParameterValue(ids->GetId("ParamBreath"), 0.5f + 0.5f * std::sin(elapsed * 1.8f));
        SetDragging(follow ? gaze.x() : 0, follow ? gaze.y() : 0);
        _dragManager->Update(delta);
        const auto addGaze = [&](const char *name, float value) {
            const auto id = ids->GetId(name);
            if (std::none_of(pointerMappings.begin(), pointerMappings.end(),
                             [&](const auto &entry) { return entry.id == id; }))
                _model->AddParameterValue(id, value);
        };
        addGaze("ParamAngleX", _dragManager->GetX() * 30);
        addGaze("ParamAngleY", _dragManager->GetY() * 30);
        addGaze("ParamEyeBallX", _dragManager->GetX());
        addGaze("ParamEyeBallY", _dragManager->GetY());
        if (_physics) {
            _physics->Evaluate(_model, delta);
        }
        if (_pose) {
            _pose->UpdateParameters(_model, delta);
        }
        _model->Update();

        const float aspect = float(size.width()) / std::max(1, size.height());
        const float scale =
            0.92f * zoom * std::min(2.0f * aspect / float(bounds.width()), 2.0f / float(bounds.height()));
        // Convert model coordinates directly into clip space. Keep this exact
        // matrix for hit testing, including aspect ratio, fitting and zoom.
        mvp.LoadIdentity();
        mvp.Scale(scale / aspect, scale);
        mvp.Translate(-bounds.center().x() * scale / aspect, -bounds.center().y() * scale);
        auto *renderer = GetRenderer<ModelRenderer>();
        SetRenderTargetSize(size.width(), size.height());
        renderer->SetMvpMatrix(&mvp);
        renderer->SetModelColor(1, 1, 1, _model->GetModelOpacity());
        renderer->DrawModel();
    }

    void tap(const QPointF &point)
    {
        const float x = mvp.InvertTransformX(point.x() * 2 - 1);
        const float y = mvp.InvertTransformY(1 - point.y() * 2);
        // IsHit expects coordinates transformed through _modelMatrix.
        for (int i = 0; i < settings->GetHitAreasCount(); ++i) {
            if (!IsHit(settings->GetHitAreaId(i), _modelMatrix->TransformX(x), _modelMatrix->TransformY(y))) {
                continue;
            }
            const auto area = QString::fromUtf8(settings->GetHitAreaName(i));
            if (area.compare(QLatin1String("Head"), Qt::CaseInsensitive) == 0 && !expressionNames.isEmpty()) {
                expression(expressionNames.at(QRandomGenerator::global()->bounded(expressionNames.size())));
            } else {
                startMotion(QStringLiteral("Tap") + area, 2);
            }
            return;
        }
        if (!settings->GetHitAreasCount()) {
            startMotion(QStringLiteral("TapBody"), 2);
        }
    }

    ModelAssets assets;
    struct PointerState {
        PointerMapping mapping;
        Csm::CubismIdHandle id;
        int index;
        double value;
    };
    std::vector<PointerState> pointerMappings;
    MotionPartOpacity motionPartOpacity;
    std::unique_ptr<Csm::CubismModelSettingJson> settings;
    std::vector<std::unique_ptr<Texture>> textures;
    QStringList expressionNames;
    std::map<std::pair<QString, int>, MotionCache> motionCache;
    std::map<QString, MotionCache> expressionCache, heldExpressionCache;
    std::map<QString, std::unique_ptr<Csm::CubismExpressionMotionManager>> heldExpressions;
    Csm::csmVector<Csm::CubismIdHandle> blinkIds, lipIds;
    Csm::CubismMatrix44 mvp;
    QRectF bounds;
    float elapsed = 0;
};

CubismModel::CubismModel(const QUrl &source, const QSize &size) : d(std::make_unique<Impl>(source, size)) {}
CubismModel::~CubismModel() = default;
void CubismModel::draw(float delta, const QSize &size, float zoom, const QPointF &gaze,
                       const QPointF &pointer, bool follow, const QString &idle)
{
    d->draw(delta, size, zoom, gaze, pointer, follow, idle);
}
void CubismModel::tap(const QPointF &point) { d->tap(point); }
void CubismModel::setExpression(const QString &name) { d->expression(name); }
const QStringList &CubismModel::expressions() const { return d->expressionNames; }
void CubismModel::setExpressionActive(const QString &name, bool active) { d->expressionActive(name, active); }
void CubismModel::playMotion(const QString &group, int index) { d->startMotion(group, 3, index); }
QStringList CubismModel::motionGroups() const
{
    QStringList groups;
    for (int i = 0; i < d->settings->GetMotionGroupCount(); ++i)
        if (d->settings->GetMotionCount(d->settings->GetMotionGroupName(i)) > 0)
            groups.append(QString::fromUtf8(d->settings->GetMotionGroupName(i)));
    return groups;
}
const QJsonArray &CubismModel::hotkeys() const { return d->assets.hotkeys(); }

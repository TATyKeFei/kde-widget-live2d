// Keep Qt OpenGL headers out of this file; their macros conflict with GLEW.
#include "cubismmodel.h"
#include "modelassets.h"
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

    ~Impl() override { DeleteRenderer(); }

    bool startMotion(const QString &group, int priority)
    {
        const auto name = group.toUtf8();
        const int count = settings->GetMotionCount(name.constData());
        if (count <= 0 || !_motionManager->ReserveMotion(priority)) {
            return false;
        }
        const int index = QRandomGenerator::global()->bounded(count);
        const auto data =
            assets.readJson(QString::fromUtf8(settings->GetMotionFileName(name.constData(), index)));
        auto *motion = static_cast<Csm::CubismMotion *>(LoadMotion(
            bytes(data), data.size(), nullptr, nullptr, nullptr, settings.get(), name.constData(), index));
        if (!motion) {
            _motionManager->SetReservePriority(0);
            return false;
        }
        motion->SetEffectIds(blinkIds, lipIds);
        _motionManager->StartMotionPriority(motion, true, priority);
        return true;
    }

    void expression(const QString &name)
    {
        const int index = expressionNames.indexOf(name);
        if (index < 0) {
            return;
        }
        const auto data = assets.readJson(QString::fromUtf8(settings->GetExpressionFileName(index)));
        if (auto *motion = LoadExpression(bytes(data), data.size(), name.toUtf8().constData())) {
            _expressionManager->StartMotion(motion, true);
        }
    }

    void draw(float delta, const QSize &size, float zoom, const QPointF &gaze, bool follow,
              const QString &idle)
    {
        _model->LoadParameters();
        if (_motionManager->IsFinished() && !idle.isEmpty()) {
            startMotion(idle, 1);
        }
        const bool updated = _motionManager->UpdateMotion(_model, delta);
        _model->SaveParameters();
        if (!updated && _eyeBlink) {
            _eyeBlink->UpdateParameters(_model, delta);
        }
        _expressionManager->UpdateMotion(_model, delta);
        elapsed += delta;
        auto *ids = Csm::CubismFramework::GetIdManager();
        _model->AddParameterValue(ids->GetId("ParamBreath"), 0.5f + 0.5f * std::sin(elapsed * 1.8f));
        SetDragging(follow ? gaze.x() : 0, follow ? gaze.y() : 0);
        _dragManager->Update(delta);
        _model->AddParameterValue(ids->GetId("ParamAngleX"), _dragManager->GetX() * 30);
        _model->AddParameterValue(ids->GetId("ParamAngleY"), _dragManager->GetY() * 30);
        _model->AddParameterValue(ids->GetId("ParamEyeBallX"), _dragManager->GetX());
        _model->AddParameterValue(ids->GetId("ParamEyeBallY"), _dragManager->GetY());
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
    std::unique_ptr<Csm::CubismModelSettingJson> settings;
    std::vector<std::unique_ptr<Texture>> textures;
    QStringList expressionNames;
    Csm::csmVector<Csm::CubismIdHandle> blinkIds, lipIds;
    Csm::CubismMatrix44 mvp;
    QRectF bounds;
    float elapsed = 0;
};

CubismModel::CubismModel(const QUrl &source, const QSize &size) : d(std::make_unique<Impl>(source, size)) {}
CubismModel::~CubismModel() = default;
void CubismModel::draw(float delta, const QSize &size, float zoom, const QPointF &gaze, bool follow,
                       const QString &idle)
{
    d->draw(delta, size, zoom, gaze, follow, idle);
}
void CubismModel::tap(const QPointF &point) { d->tap(point); }
void CubismModel::setExpression(const QString &name) { d->expression(name); }
const QStringList &CubismModel::expressions() const { return d->expressionNames; }

#include "graphicscontext.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glew.h>
#include <stdexcept>

namespace
{
class OpenGLContext final : public GraphicsContext
{
  public:
    OpenGLContext()
    {
        // Prefer the EGL provider's default display.
        // A surfaceless display also permits rendering without a compositor.
        m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (!eglInitialize(m_display, nullptr, nullptr)) {
            const auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
                eglGetProcAddress("eglGetPlatformDisplayEXT"));
            m_display = getPlatformDisplay
                            ? getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                            : EGL_NO_DISPLAY;
            if (m_display == EGL_NO_DISPLAY || !eglInitialize(m_display, nullptr, nullptr)) {
                throw std::runtime_error("无法初始化 EGL 离屏显示设备。");
            }
        }
        if (!eglBindAPI(EGL_OPENGL_API)) {
            fail("EGL 未提供桌面 OpenGL。");
        }
        const EGLint attributes[] = {EGL_SURFACE_TYPE,
                                     EGL_PBUFFER_BIT,
                                     EGL_RENDERABLE_TYPE,
                                     EGL_OPENGL_BIT,
                                     EGL_RED_SIZE,
                                     8,
                                     EGL_GREEN_SIZE,
                                     8,
                                     EGL_BLUE_SIZE,
                                     8,
                                     EGL_ALPHA_SIZE,
                                     8,
                                     EGL_NONE};
        EGLint count = 0;
        if (!eglChooseConfig(m_display, attributes, &m_config, 1, &count) || !count) {
            fail("找不到支持透明颜色的 EGL 配置。");
        }
        const EGLint pbuffer[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        m_surface = eglCreatePbufferSurface(m_display, m_config, pbuffer);
        m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, nullptr);
        if (m_surface == EGL_NO_SURFACE || m_context == EGL_NO_CONTEXT ||
            !eglMakeCurrent(m_display, m_surface, m_surface, m_context)) {
            fail("无法创建 EGL OpenGL 上下文。");
        }
        glewExperimental = GL_TRUE;
        const auto result = glewInit();
        if ((result != GLEW_OK && result != GLEW_ERROR_NO_GLX_DISPLAY) || !glGenFramebuffers) {
            fail("无法加载 OpenGL 帧缓冲功能。");
        }
        while (glGetError() != GL_NO_ERROR) {
        }
    }
    ~OpenGLContext() override { cleanup(); }
    void resize(const QSize &size) override
    {
        if (size == m_size) {
            return;
        }
        destroyFramebuffer();
        m_size = size;
        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glGenTextures(1, &m_color);
        glBindTexture(GL_TEXTURE_2D, m_color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.width(), size.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color, 0);
        glGenRenderbuffers(1, &m_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, size.width(), size.height());
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("无法创建 OpenGL 离屏帧缓冲。");
        }
    }
    void begin() override
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, m_size.width(), m_size.height());
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }
    QImage readback() override
    {
        QImage image(m_size, QImage::Format_RGBA8888_Premultiplied);
        glReadPixels(0, 0, m_size.width(), m_size.height(), GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
        if (glGetError() != GL_NO_ERROR) {
            throw std::runtime_error("OpenGL 渲染失败。");
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        return image.flipped(Qt::Vertical);
#else
        return image.mirrored(false, true);
#endif
    }

  private:
    [[noreturn]] void fail(const char *message)
    {
        cleanup();
        throw std::runtime_error(message);
    }
    void destroyFramebuffer()
    {
        if (m_fbo) {
            glDeleteFramebuffers(1, &m_fbo);
        }
        if (m_color) {
            glDeleteTextures(1, &m_color);
        }
        if (m_depth) {
            glDeleteRenderbuffers(1, &m_depth);
        }
        m_fbo = m_color = m_depth = 0;
    }
    void cleanup()
    {
        if (m_context != EGL_NO_CONTEXT && eglMakeCurrent(m_display, m_surface, m_surface, m_context)) {
            destroyFramebuffer();
        }
        if (m_display != EGL_NO_DISPLAY) {
            eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (m_context != EGL_NO_CONTEXT) {
                eglDestroyContext(m_display, m_context);
            }
            if (m_surface != EGL_NO_SURFACE) {
                eglDestroySurface(m_display, m_surface);
            }
            eglTerminate(m_display);
        }
        m_context = EGL_NO_CONTEXT;
        m_surface = EGL_NO_SURFACE;
        m_display = EGL_NO_DISPLAY;
    }
    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLConfig m_config = nullptr;
    EGLContext m_context = EGL_NO_CONTEXT;
    EGLSurface m_surface = EGL_NO_SURFACE;
    GLuint m_fbo = 0, m_color = 0, m_depth = 0;
    QSize m_size;
};
} // namespace

std::unique_ptr<GraphicsContext> createGraphicsContext() { return std::make_unique<OpenGLContext>(); }

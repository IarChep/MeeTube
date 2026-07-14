/*
 * Copyright (C) 2026 IarChep
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "harmattan/eglvideoitem.h"

#if !defined(BUILD_N9)   // ---- host stub: keeps the QML loading unchanged ----

EglVideoItem::EglVideoItem(QDeclarativeItem *parent)
    : QDeclarativeItem(parent), m_pipeline(0), m_program(0), m_ctxGiven(false),
      m_paintCount(0) {}
EglVideoItem::~EglVideoItem() {}
void EglVideoItem::setPipeline(QObject *pipeline)
{
    if (m_pipeline == pipeline) return;
    m_pipeline = pipeline;
    emit pipelineChanged();
}
void EglVideoItem::onFrameReady() {}
void EglVideoItem::ensureProgram() {}
void EglVideoItem::paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *) {}

#else                    // ---- device: canon QtMultimediaKit EGLImage draw ----

#include "media/gstpipeline.h"
#include "media/meegovideotexture.h"
#include "core/debuglog.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QX11Info>
#include <QtOpenGL/qgl.h>
#include <QtOpenGL/QGLShaderProgram>
#include <EGL/egl.h>

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif
#ifndef EGL_SYNC_FENCE_KHR
#define EGL_SYNC_FENCE_KHR 0x30F9
#endif
// canon (qgstreamergltexturerenderer.cpp "from extdefs.h"): the SDK eglext.h has
// no fence prototypes — resolve eglCreateSyncKHR at runtime.
typedef void *EGLSyncKHR_;
typedef EGLSyncKHR_ (*PfnEglCreateSyncKHR)(EGLDisplay, EGLenum, const EGLint *);
typedef EGLBoolean (*PfnEglDestroySyncKHR)(EGLDisplay, EGLSyncKHR_);
static PfnEglCreateSyncKHR s_eglCreateSyncKHR = 0;
static PfnEglDestroySyncKHR s_eglDestroySyncKHR = 0;   // canon resolves it too (unused there as well)
static bool s_eglSyncResolved = false;

// canon shaders — qeglimagetexturesurface.cpp verbatim.
static const char *kVertexShader =
    "attribute highp vec4 vertexCoordArray;\n"
    "attribute mediump vec2 textureCoordArray;\n"
    "uniform highp mat4 positionMatrix;\n"
    "varying mediump vec2 textureCoord;\n"
    "void main (void)\n"
    "{\n"
    "   gl_Position = positionMatrix * vertexCoordArray;\n"
    "   textureCoord = textureCoordArray;\n"
    "}";

static const char *kFragmentShader =
    "#extension GL_OES_EGL_image_external: enable\n"
    "\n"
    "uniform samplerExternalOES texRgb;\n"
    "varying mediump vec2 textureCoord;\n"
    "\n"
    "void main (void)\n"
    "{\n"
    "    gl_FragColor = texture2D(texRgb, textureCoord);\n"
    "}";

EglVideoItem::EglVideoItem(QDeclarativeItem *parent)
    : QDeclarativeItem(parent), m_pipeline(0), m_program(0), m_ctxGiven(false),
      m_paintCount(0)
{
    setFlag(QGraphicsItem::ItemHasNoContents, false);   // we paint
}

EglVideoItem::~EglVideoItem() {}

void EglVideoItem::setPipeline(QObject *pipeline)
{
    if (m_pipeline == pipeline) return;
    if (m_pipeline) disconnect(m_pipeline, 0, this, 0);
    m_pipeline = pipeline;
    if (m_pipeline)
        connect(m_pipeline, SIGNAL(glFrameReady()), this, SLOT(onFrameReady()));
    emit pipelineChanged();
}

// GUI thread (queued from the pipeline's frame-ready marshal): schedule a repaint.
void EglVideoItem::onFrameReady() { update(); }

void EglVideoItem::ensureProgram()
{
    if (m_program) return;
    // canon: QGLShaderProgram against the current (scene) context.
    m_program = new QGLShaderProgram(QGLContext::currentContext(), this);
    if (!m_program->addShaderFromSourceCode(QGLShader::Vertex, kVertexShader))
        PLOG() << "eglitem: vertex shader:" << qPrintable(m_program->log());
    if (!m_program->addShaderFromSourceCode(QGLShader::Fragment, kFragmentShader))
        PLOG() << "eglitem: fragment shader:" << qPrintable(m_program->log());
    m_program->bindAttributeLocation("textureCoordArray", 1);   // canon
    if (!m_program->link())
        PLOG() << "eglitem: link:" << qPrintable(m_program->log());
}

void EglVideoItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    yt::media::GstAppPipeline *pipe =
        qobject_cast<yt::media::GstAppPipeline *>(m_pipeline);
    if (!pipe) return;

    // canon (qgraphicsvideoitem_maemo6): hand the renderer the GL context only
    // once the item actually painted — the context is current right here.
    if (!m_ctxGiven && QGLContext::currentContext()) {
        pipe->setGlContext(const_cast<QGLContext *>(QGLContext::currentContext()));
        m_ctxGiven = true;
    }

    void *sinkVoid = pipe->glSink();
    int frame = -1;
    if (!sinkVoid || !pipe->currentGlFrame(&frame)) {
        painter->fillRect(boundingRect(), QBrush(Qt::black));   // canon idle fill
        return;
    }
    MeegoGstVideoTexture *sink = MEEGO_GST_VIDEO_TEXTURE(sinkVoid);

    const QRectF target = boundingRect();

    // canon: snapshot stencil/scissor before native painting and re-enable after
    // (beginNativePainting resets GL state Qt was relying on).
    const bool stencilTestEnabled = glIsEnabled(GL_STENCIL_TEST);
    const bool scissorTestEnabled = glIsEnabled(GL_SCISSOR_TEST);

    painter->beginNativePainting();

    if (stencilTestEnabled)
        glEnable(GL_STENCIL_TEST);
    if (scissorTestEnabled)
        glEnable(GL_SCISSOR_TEST);

    ensureProgram();

    // canon positionMatrix — qeglimagetexturesurface.cpp verbatim.
    const int width  = QGLContext::currentContext()->device()->width();
    const int height = QGLContext::currentContext()->device()->height();
    const QTransform transform = painter->deviceTransform();
    const GLfloat wfactor = 2.0 / width;
    const GLfloat hfactor = -2.0 / height;
    const GLfloat positionMatrix[4][4] = {
        { GLfloat(wfactor * transform.m11() - transform.m13()),
          GLfloat(hfactor * transform.m12() + transform.m13()), 0.0,
          GLfloat(transform.m13()) },
        { GLfloat(wfactor * transform.m21() - transform.m23()),
          GLfloat(hfactor * transform.m22() + transform.m23()), 0.0,
          GLfloat(transform.m23()) },
        { 0.0, 0.0, -1.0, 0.0 },
        { GLfloat(wfactor * transform.dx() - transform.m33()),
          GLfloat(hfactor * transform.dy() + transform.m33()), 0.0,
          GLfloat(transform.m33()) }
    };

    const GLfloat vTop = GLfloat(target.top());          // TopToBottom scan
    const GLfloat vBottom = GLfloat(target.bottom() + 1);
    const GLfloat vertexCoordArray[] = {
        GLfloat(target.left())     , vBottom,
        GLfloat(target.right() + 1), vBottom,
        GLfloat(target.left())     , vTop,
        GLfloat(target.right() + 1), vTop
    };
    static const GLfloat textureCoordArray[] = {
        0, 1,
        1, 1,
        0, 0,
        1, 0
    };

    m_program->bind();
    m_program->enableAttributeArray("vertexCoordArray");
    m_program->enableAttributeArray("textureCoordArray");
    m_program->setAttributeArray("vertexCoordArray", vertexCoordArray, 2);
    m_program->setAttributeArray("textureCoordArray", textureCoordArray, 2);
    m_program->setUniformValue("positionMatrix", positionMatrix);
    m_program->setUniformValue("texRgb", 0);

    // canon map(): acquire + bind (the sink binds its EGLImage-backed external
    // texture itself — the client creates NO texture object).
    if (!meego_gst_video_texture_acquire_frame(sink, frame))
        PLOG() << "eglitem: acquire-frame failed" << frame;
    if (!meego_gst_video_texture_bind_frame(sink, GL_TEXTURE_EXTERNAL_OES, frame))
        PLOG() << "eglitem: bind-frame failed" << frame;

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // canon unmap(): unbind, then hand the sink an EGL fence with the release so
    // it recycles the buffer only after the GPU finished sampling it.
    meego_gst_video_texture_bind_frame(sink, GL_TEXTURE_EXTERNAL_OES, -1);
    if (!s_eglSyncResolved) {
        s_eglCreateSyncKHR = (PfnEglCreateSyncKHR) eglGetProcAddress("eglCreateSyncKHR");
        s_eglDestroySyncKHR = (PfnEglDestroySyncKHR) eglGetProcAddress("eglDestroySyncKHR");
        s_eglSyncResolved = true;
        PLOG() << "eglitem: eglCreateSyncKHR" << (s_eglCreateSyncKHR ? "resolved" : "MISSING");
    }
    void *sync = s_eglCreateSyncKHR
        ? s_eglCreateSyncKHR(eglGetDisplay((EGLNativeDisplayType) QX11Info::display()),
                             EGL_SYNC_FENCE_KHR, 0)
        : 0;
    meego_gst_video_texture_release_frame(sink, frame, sync);

    m_program->release();
    painter->endNativePainting();

    const GLenum err = glGetError();
    if (m_paintCount == 0 || (m_paintCount & 63) == 0 || err != GL_NO_ERROR)
        PLOG() << "eglitem: paint #" << m_paintCount << "frame" << frame << "glErr" << err;
    ++m_paintCount;
    // (canon releases the streaming thread when renderGLFrame schedules the draw,
    // not after the actual paint — the pipeline's onGlFrame() handles that.)
}

#endif

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
    : QDeclarativeItem(parent), m_pipeline(0), m_program(0), m_givenCtx(0),
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
    : QDeclarativeItem(parent), m_pipeline(0), m_program(0), m_givenCtx(0),
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
    // once the item actually painted — the context is current right here. Re-hand
    // it whenever it CHANGES: the meego graphicssystem destroys and recreates the
    // scene context on visibility switches (device-observed mid-playback: "Meego
    // graphics system destroyed"), and the next pipeline build must get the live
    // context, not the dead one.
    const QGLContext *cur = QGLContext::currentContext();
    if (cur && m_givenCtx != cur) {
        pipe->setGlContext(const_cast<QGLContext *>(cur));
        m_givenCtx = cur;
        // The shader program lives in the OLD context's object namespace; after the meego
        // graphicssystem recreates the scene context (un-minimize), its stale id is invalid
        // in the new one -> every draw throws GL_INVALID_VALUE (glErr 1281) and the video
        // stops rendering. Drop our reference so ensureProgram() rebuilds it against the live
        // context below. We do NOT delete the old program here (its dtor would touch the
        // just-destroyed context) — it stays parented to `this` and frees on page-leave.
        m_program = 0;
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

    // The scene GL context can lose its paint device MID-TEARDOWN: device-confirmed that
    // minimizing during playback tears down the meego graphicssystem context, and a paint
    // fired during the minimize animation hit QGLContext::currentContext()->device() == null
    // -> SIGSEGV in QPaintDevice::width() (crash pc in libQtGui, lr in this frame, fault
    // addr 0x4 = null deref). Bail out of this frame safely when the device is gone; the
    // next real paint (context recreated) resumes normally.
    const QGLContext *glctx = QGLContext::currentContext();
    QPaintDevice *dev = glctx ? glctx->device() : 0;
    if (!dev) {
        painter->endNativePainting();
        painter->fillRect(boundingRect(), QBrush(Qt::black));
        return;
    }

    // canon positionMatrix — qeglimagetexturesurface.cpp verbatim.
    const int width  = dev->width();
    const int height = dev->height();
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
    // texture itself — the client creates NO texture object). ONLY draw a frame we
    // actually acquired AND bound: on a source switch the rebuilt sink emits a
    // preroll frame-ready before it can serve that frame, so acquire fails — and a
    // naive glDrawArrays then samples the stale/unbound external texture and paints
    // garbage (the "шакально" flicker on switch). On failure, skip the draw and
    // fall through to the black idle-fill below.
    const bool acquired = meego_gst_video_texture_acquire_frame(sink, frame);
    const bool bound = acquired
        && meego_gst_video_texture_bind_frame(sink, GL_TEXTURE_EXTERNAL_OES, frame);
    if (bound)
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    else
        PLOG() << "eglitem: frame" << frame << "not ready (acquired=" << acquired << ") — skip draw";

    // canon unmap(): unbind, then hand the sink an EGL fence with the release so it
    // recycles the buffer only after the GPU finished sampling it — but only for a
    // frame we actually acquired (nothing to release/unbind otherwise).
    if (acquired) {
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
    }

    m_program->release();
    painter->endNativePainting();

    if (!bound)   // no valid frame this paint -> idle black instead of garbage
        painter->fillRect(boundingRect(), QBrush(Qt::black));

    const GLenum err = glGetError();
    if (m_paintCount == 0 || (m_paintCount & 63) == 0 || err != GL_NO_ERROR)
        PLOG() << "eglitem: paint #" << m_paintCount << "frame" << frame << "glErr" << err;
    ++m_paintCount;
    // (canon releases the streaming thread when renderGLFrame schedules the draw,
    // not after the actual paint — the pipeline's onGlFrame() handles that.)
}

#endif

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

#ifndef EGLVIDEOITEM_H
#define EGLVIDEOITEM_H
#include <QDeclarativeItem>

class QGLShaderProgram;

// In-scene video output for the in-house GStreamer pipeline (GstAppPipeline in
// texture mode): draws the gltexturesink's current frame — an EGLImage the sink
// binds to a GL_TEXTURE_EXTERNAL_OES texture — as a quad inside the QML GL
// scene. A faithful port of QtMultimediaKit's renderer pair
// (qgstreamergltexturerenderer.cpp + qeglimagetexturesurface.cpp): the same
// shaders, the same positionMatrix from the painter's device transform, and the
// same acquire/bind -> draw -> unbind -> EGL-fence -> release frame protocol.
// The item's first paint hands the scene's current QGLContext to the pipeline
// (Nokia: "don't set the surface until the item is painted") — the sink is then
// created against that EGL context so its EGLImages share our texture space.
class EglVideoItem : public QDeclarativeItem {
    Q_OBJECT
    Q_PROPERTY(QObject *pipeline READ pipeline WRITE setPipeline NOTIFY pipelineChanged)
public:
    explicit EglVideoItem(QDeclarativeItem *parent = 0);
    ~EglVideoItem();
    QObject *pipeline() const { return m_pipeline; }
    void setPipeline(QObject *pipeline);
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
Q_SIGNALS:
    void pipelineChanged();
private Q_SLOTS:
    void onFrameReady();
private:
    void ensureProgram();
    QObject          *m_pipeline;    // the GstAppPipeline (or 0)
    QGLShaderProgram *m_program;     // canon shader pair (device; 0 on host)
    bool              m_ctxGiven;    // scene QGLContext handed to the pipeline
    int               m_paintCount;
};
#endif

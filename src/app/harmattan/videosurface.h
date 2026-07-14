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

#ifndef VIDEOSURFACE_H
#define VIDEOSURFACE_H
#include <QDeclarativeItem>

class QGraphicsVideoItem;   // QtMultimediaKit (device only)

// In-scene video output for the QtMultimediaKit playback path. Wraps the stock
// QGraphicsVideoItem (Harmattan build renders via gltexturesink -> EGLImage ->
// GL_TEXTURE_EXTERNAL_OES inside the QML GL scene — true alpha with QML on top)
// and binds it to the QMediaPlayer handed in through the mediaObject property
// (the `qtmMedia` context property, main.cpp). Host build: an empty placeholder
// so the QML loads unchanged.
class VideoSurface : public QDeclarativeItem {
    Q_OBJECT
    Q_PROPERTY(QObject *mediaObject READ mediaObject WRITE setMediaObject NOTIFY mediaObjectChanged)
public:
    explicit VideoSurface(QDeclarativeItem *parent = 0);
    QObject *mediaObject() const { return m_media; }
    void setMediaObject(QObject *media);
Q_SIGNALS:
    void mediaObjectChanged();
protected:
    void geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry);
private:
    QObject *m_media;
    QGraphicsVideoItem *m_item;   // child graphics item (device); 0 on host
};
#endif

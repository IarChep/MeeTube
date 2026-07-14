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

#include "harmattan/videosurface.h"

#if defined(BUILD_N9)
#include <qgraphicsvideoitem.h>
#include <qmediaobject.h>

VideoSurface::VideoSurface(QDeclarativeItem *parent)
    : QDeclarativeItem(parent), m_media(0), m_item(new QGraphicsVideoItem(this))
{
    m_item->setAspectRatioMode(Qt::KeepAspectRatio);
}

void VideoSurface::setMediaObject(QObject *media)
{
    if (m_media == media) return;
    if (QMediaObject *old = qobject_cast<QMediaObject *>(m_media))
        old->unbind(m_item);
    m_media = media;
    if (QMediaObject *mo = qobject_cast<QMediaObject *>(m_media))
        mo->bind(m_item);   // QGraphicsVideoItem is QMediaBindable -> engine renderer attaches
    emit mediaObjectChanged();
}

void VideoSurface::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    m_item->setSize(newGeometry.size());
    QDeclarativeItem::geometryChanged(newGeometry, oldGeometry);
}

#else   // ---- host: inert placeholder ----

VideoSurface::VideoSurface(QDeclarativeItem *parent)
    : QDeclarativeItem(parent), m_media(0), m_item(0) {}

void VideoSurface::setMediaObject(QObject *media)
{
    if (m_media == media) return;
    m_media = media;
    emit mediaObjectChanged();
}

void VideoSurface::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QDeclarativeItem::geometryChanged(newGeometry, oldGeometry);
}

#endif

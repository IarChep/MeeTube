/*
 * Copyright (C) 2016 Stuart Howarth <showarth@marxoft.co.uk>
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

#include "videorequest.h"

namespace yt {

void VideoRequest::list(const QString &, const QString &)             { fail("not supported"); }
void VideoRequest::search(const QString &, const QString &)           { fail("not supported"); }
void VideoRequest::get(const QString &)                               { fail("not supported"); }
void VideoRequest::favourite(const QString &, bool)                   { fail("not supported"); }
void VideoRequest::rate(const QString &, int)                         { fail("not supported"); }
void VideoRequest::addToPlaylist(const QString &, const QString &)    { fail("not supported"); }
void VideoRequest::removeFromPlaylist(const QString &, const QString &){ fail("not supported"); }
void VideoRequest::deliver(const QList<CT::Video> &videos, const QString &next) {
    setStatus(Ready);
    emit ready(videos, next);
}

}

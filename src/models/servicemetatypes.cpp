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

#include "servicemetatypes.h"
#include "servicedatatypes.h"
#include "requests/servicerequest.h"
#include "requests/videorequest.h"
#include "requests/streamsrequest.h"
#include "requests/commentrequest.h"
#include "requests/categoryrequest.h"
#include "requests/subtitlesrequest.h"
#include <QMetaType>

void registerMeeTubeMetaTypes() {
    // Result value lists carried by the typed ready() signals.
    qRegisterMetaType<QList<CT::Video> >("QList<CT::Video>");
    qRegisterMetaType<QList<CT::Stream> >("QList<CT::Stream>");
    qRegisterMetaType<QList<CT::Comment> >("QList<CT::Comment>");
    qRegisterMetaType<QList<CT::Category> >("QList<CT::Category>");
    qRegisterMetaType<QList<CT::Subtitle> >("QList<CT::Subtitle>");
    // Status enum used by QSignalSpy / statusChanged.
    qRegisterMetaType<yt::ServiceRequest::Status>("yt::ServiceRequest::Status");
    // Typed request pointers (QML/registration convenience).
    qRegisterMetaType<yt::VideoRequest*>("yt::VideoRequest*");
    qRegisterMetaType<yt::StreamsRequest*>("yt::StreamsRequest*");
    qRegisterMetaType<yt::CommentRequest*>("yt::CommentRequest*");
    qRegisterMetaType<yt::CategoryRequest*>("yt::CategoryRequest*");
    qRegisterMetaType<yt::SubtitlesRequest*>("yt::SubtitlesRequest*");
}

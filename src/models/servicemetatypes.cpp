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
#include <QMetaType>

void registerMeeTubeMetaTypes() {
    // Value types carried by the typed ready()/watchReady() signals — string-based
    // Qt4 connects and QSignalSpy need these registered. (Request-pointer metatypes
    // are gone with the create*Request() Q_INVOKABLEs; the tree hands out QObject*.)
    qRegisterMetaType<CT::Account>("CT::Account");
    qRegisterMetaType<CT::Video>("CT::Video");
    qRegisterMetaType<QList<CT::Video> >("QList<CT::Video>");
    qRegisterMetaType<QList<CT::Stream> >("QList<CT::Stream>");
    qRegisterMetaType<QList<CT::Comment> >("QList<CT::Comment>");
    qRegisterMetaType<QList<CT::Subtitle> >("QList<CT::Subtitle>");
    qRegisterMetaType<QList<CT::Playlist> >("QList<CT::Playlist>");
    qRegisterMetaType<QList<CT::User> >("QList<CT::User>");
    qRegisterMetaType<yt::ServiceRequest::Status>("yt::ServiceRequest::Status");
}

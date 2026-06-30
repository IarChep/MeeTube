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

#ifndef YT_INNERTUBE_H
#define YT_INNERTUBE_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include "innertube/innertubeclient.h"
#include "innertube/session.h"
#include "requests/videorequest.h"
#include "requests/streamsrequest.h"
#include "requests/commentrequest.h"
#include "requests/categoryrequest.h"
#include "requests/subtitlesrequest.h"
#include "requests/playlistrequest.h"
#include "requests/userrequest.h"

namespace yt {

// The engine: a plain GUI-thread singleton owning the InnertubeClient transport.
// Replaces cuteTube2's worker-threaded plugin Service — no QThread, no PluginHost,
// no moveToThread, no blocking-queued factories. Requests are `new`ed directly on
// the GUI thread and parented to the engine.
class Innertube : public QObject {
    Q_OBJECT
public:
    static Innertube* instance();              // lazy singleton

    Session& session() { return m_client.session(); }

    // gl = region, hl = language (Innertube context locale).
    Q_INVOKABLE void applySettings(const QString &region, const QString &language);

    // Factories return a heap request parented to the engine. Created on the GUI
    // thread with the shared transport — no worker hop. (CategoryRequest is
    // synchronous and needs no transport.)
    Q_INVOKABLE VideoRequest*    createVideoRequest()    { return new VideoRequest(&m_client, this); }
    Q_INVOKABLE StreamsRequest*  createStreamsRequest()  { return new StreamsRequest(&m_client, this); }
    Q_INVOKABLE CommentRequest*  createCommentRequest()  { return new CommentRequest(&m_client, this); }
    Q_INVOKABLE CategoryRequest* createCategoryRequest() { return new CategoryRequest(this); }
    Q_INVOKABLE SubtitlesRequest* createSubtitlesRequest() { return new SubtitlesRequest(&m_client, this); }
    Q_INVOKABLE PlaylistRequest* createPlaylistRequest() { return new PlaylistRequest(&m_client, this); }
    Q_INVOKABLE UserRequest*     createUserRequest()     { return new UserRequest(&m_client, this); }

    Q_INVOKABLE QVariantList navEntries() const;       // hardcoded (ported from the YouTube plugin)
    Q_INVOKABLE QVariantList searchTypes() const;      // hardcoded (ported from the YouTube plugin)

private:
    explicit Innertube(QObject *parent = 0);
    static Innertube *self;
    InnertubeClient m_client;
};

}

#endif // YT_INNERTUBE_H

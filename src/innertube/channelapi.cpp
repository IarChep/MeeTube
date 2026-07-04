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

#include "channelapi.h"
#include "models/channelmodel.h"
#include "models/videomodel.h"
#include "innertube/channeldetails.h"
#include "innertube/innertube.h"
#include "core/chains.h"

namespace yt {

ChannelApi::ChannelApi(QObject *parent) : QObject(parent) {}

// Fire a fire-and-forget action chain via the engine's transport (see VideoApi).
static void fireChannelAction(core::ActionKind kind, const QString &channelId) {
    Innertube *e = Innertube::instance();
    if (!e) return;
    const ApiRef api = e->apiRef();
    if (!api.host || !api.http) return;
    const core::JobToken job = core::newJob();
    api.host->invoke([api, kind, channelId, job]() {
        core::submitAction(*api.http, kind, channelId, job, [](bool) {});
    });
}

QObject* ChannelApi::byId(const QString &channelId) {
    ChannelDetails *d = qobject_cast<ChannelDetails *>(m_details.data());
    if (!d) { d = new ChannelDetails(this); m_details = d; }
    d->loadById(channelId);
    return d;
}

QObject* ChannelApi::resolve(const QString &handleUrl) {
    ChannelDetails *d = qobject_cast<ChannelDetails *>(m_details.data());
    if (!d) { d = new ChannelDetails(this); m_details = d; }
    d->loadByUrl(handleUrl);
    return d;
}

QObject* ChannelApi::searchChannels(const QString &query) {
    ChannelModel *m = qobject_cast<ChannelModel *>(m_search.data());
    if (!m) { m = new ChannelModel(this); m_search = m; }
    m->search(query);
    return m;
}

QObject* ChannelApi::videos(const QString &channelId) {
    VideoModel *m = qobject_cast<VideoModel *>(m_videos.data());
    if (!m) { m = new VideoModel(this); m_videos = m; }
    // The Videos tab explicitly — the default browse lands on the shelf-shaped
    // Home tab. Stable base64 protobuf (docs/INNERTUBE_API.md §12).
    m->list(channelId, QLatin1String("EgZ2aWRlb3PyBgQKAjoA"));
    return m;
}

void ChannelApi::subscribe(const QString &channelId)   { fireChannelAction(core::Subscribe, channelId); }
void ChannelApi::unsubscribe(const QString &channelId) { fireChannelAction(core::Unsubscribe, channelId); }

}

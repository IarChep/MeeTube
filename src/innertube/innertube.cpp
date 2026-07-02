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

#include "innertube.h"

namespace yt {

Innertube *Innertube::self = 0;

Innertube::Innertube(QObject *parent)
    : QObject(parent), m_client(this), m_store(QString(), this), m_manager(&m_client, &m_store, this),
      m_video(0), m_channel(0), m_playlist(0), m_accountApi(0) {
    if (!self) self = this;
    connect(&m_manager, SIGNAL(bearerChanged()), this, SLOT(applyBearer()));
}

VideoApi* Innertube::videoApi() {
    if (!m_video) m_video = new VideoApi(&m_client, this);
    return m_video;
}

ChannelApi* Innertube::channelApi() {
    if (!m_channel) m_channel = new ChannelApi(&m_client, this);
    return m_channel;
}

PlaylistApi* Innertube::playlistApi() {
    if (!m_playlist) m_playlist = new PlaylistApi(&m_client, this);
    return m_playlist;
}

AccountApi* Innertube::accountApi() {
    if (!m_accountApi) m_accountApi = new AccountApi(&m_client, &m_store, this);
    return m_accountApi;
}

void Innertube::applyBearer() {
    m_client.session().bearer = m_manager.currentBearer();
}

QVariantList Innertube::authedFeeds() const {
    QVariantList out;
    struct { const char *label; const char *id; } feeds[] = {
        { "Subscriptions", "FEsubscriptions" },
        { "History",       "FEhistory" },
        { "Library",       "FElibrary" } };
    for (int i = 0; i < 3; ++i) {
        QVariantMap m;
        m["label"] = QString::fromLatin1(feeds[i].label);
        m["kind"]  = QString::fromLatin1("video");
        m["id"]    = QString::fromLatin1(feeds[i].id);
        out << m;
    }
    return out;
}

// Lazy singleton. The app may construct Innertube explicitly early in main().
Innertube* Innertube::instance() { return self ? self : self = new Innertube; }

void Innertube::applySettings(const QString &region, const QString &language) {
    if (!region.isEmpty())   m_client.session().gl = region;
    if (!language.isEmpty()) m_client.session().hl = language;
}

QVariantList Innertube::navEntries() const {
    QVariantList out;
    struct { const char *label; const char *kind; const char *id; } nav[] = {
        { "News",     "video", "FEnews_destination" },
        { "Learning", "video", "UCtFRv9O2AHqOZjjynzrv-xg" },
        { "Live",     "video", "UC4R8DWoMoI7CAwX8_LjQHig" },
        { "Sports",   "video", "UCEgdi0XIXXZ-qJOFPf4JSKw" } };
    for (int i = 0; i < 4; ++i) {
        QVariantMap m;
        m["label"] = QString::fromLatin1(nav[i].label);
        m["kind"]  = QString::fromLatin1(nav[i].kind);
        m["id"]    = QString::fromLatin1(nav[i].id);
        out << m;
    }
    return out;
}

static QVariantMap order(const char *label, const char *value) {
    QVariantMap m;
    m["label"] = QString::fromLatin1(label);
    m["value"] = QString::fromLatin1(value);
    return m;
}

QVariantList Innertube::searchTypes() const {
    QVariantList out;

    QVariantList videoOrders;
    videoOrders << order("Relevance", "relevance") << order("Date", "date")
                << order("Views", "views")         << order("Rating", "rating");
    QVariantMap videos;
    videos["label"]  = QString("Videos");
    videos["kind"]   = QString("video");
    videos["orders"] = videoOrders;
    out << videos;

    QVariantList channelOrders;
    channelOrders << order("Relevance", "relevance");
    QVariantMap channels;
    channels["label"]  = QString("Channels");
    channels["kind"]   = QString("user");
    channels["orders"] = channelOrders;
    out << channels;

    QVariantList playlistOrders;
    playlistOrders << order("Relevance", "relevance");
    QVariantMap playlists;
    playlists["label"]  = QString("Playlists");
    playlists["kind"]   = QString("playlist");
    playlists["orders"] = playlistOrders;
    out << playlists;

    return out;
}

}

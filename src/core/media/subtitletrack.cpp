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

#include "media/subtitletrack.h"
#include "core/debuglog.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QXmlStreamReader>
#include <QRegExp>

namespace yt { namespace media {

SubtitleTrack::SubtitleTrack(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent), m_nam(nam), m_reply(0), m_pos(0) {}

SubtitleTrack::~SubtitleTrack()
{
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); }
}

void SubtitleTrack::clear()
{
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); m_reply = 0; }
    m_cues.clear();
    if (!m_text.isEmpty()) { m_text.clear(); emit textChanged(); }
}

void SubtitleTrack::load(const QString &url)
{
    clear();
    if (url.isEmpty() || !m_nam) return;
    // Force the simple srv1 <transcript><text start dur> format: the caption
    // baseUrl often already carries fmt=srv3 (the verbose per-word format), so
    // strip any existing fmt before appending. applyData still parses both shapes.
    // fromEncoded: QUrl(QString) double-encodes the server url's %-escapes and
    // corrupts the signed timedtext url -> 403 (see CLAUDE.md QUrl gotcha).
    QString u = url;
    const int f = u.indexOf(QLatin1String("&fmt="));
    if (f >= 0) { const int amp = u.indexOf(QLatin1Char('&'), f + 1);
                  u = u.left(f) + (amp >= 0 ? u.mid(amp) : QString()); }
    u += QLatin1String("&fmt=srv1");
    m_reply = m_nam->get(QNetworkRequest(QUrl::fromEncoded(u.toUtf8())));
    connect(m_reply, SIGNAL(finished()), this, SLOT(onFetched()));
    PLOG() << "SubtitleTrack: fetch" << qPrintable(u.left(90));
}

void SubtitleTrack::onFetched()
{
    if (!m_reply) return;
    QNetworkReply *r = m_reply; m_reply = 0;
    if (r->error() != QNetworkReply::NoError) {
        PLOG() << "SubtitleTrack: fetch failed:" << qPrintable(r->errorString());
        r->deleteLater();
        return;
    }
    const QByteArray body = r->readAll();
    r->deleteLater();
    applyData(body);
}

void SubtitleTrack::applyData(const QByteArray &body)
{
    m_cues.clear();
    QXmlStreamReader xml(body);
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) continue;
        const QStringRef name = xml.name();
        // srv1: <text start="S.SS" dur="S.SS"> (seconds). srv3: <p t="MS" d="MS">
        // (milliseconds, text possibly split across <s> word segments).
        // simplified() (not trimmed()): timedtext text carries stray newlines,
        // tabs and double spaces (srv3 <s> word segments especially) — collapse
        // every internal whitespace run to one space so the overlay wraps cleanly
        // instead of rendering rogue line breaks that shove the text off-centre.
        if (name == QLatin1String("text")) {
            const double start = xml.attributes().value(QLatin1String("start")).toString().toDouble();
            const double dur   = xml.attributes().value(QLatin1String("dur")).toString().toDouble();
            const QString t = htmlUnescape(
                xml.readElementText(QXmlStreamReader::IncludeChildElements)).simplified();
            if (!t.isEmpty()) {
                Cue c; c.start = (int)(start * 1000.0); c.end = (int)((start + dur) * 1000.0); c.text = t;
                m_cues.append(c);
            }
        } else if (name == QLatin1String("p")) {
            const int start = xml.attributes().value(QLatin1String("t")).toString().toInt();
            const int dur   = xml.attributes().value(QLatin1String("d")).toString().toInt();
            const QString t = htmlUnescape(
                xml.readElementText(QXmlStreamReader::IncludeChildElements)).simplified();
            if (!t.isEmpty()) {
                Cue c; c.start = start; c.end = start + (dur > 0 ? dur : 2000); c.text = t;
                m_cues.append(c);
            }
        }
    }
    PLOG() << "SubtitleTrack: parsed" << m_cues.size() << "cues";
    updateText();
    emit loaded();
}

void SubtitleTrack::setPosition(int ms)
{
    if (m_pos == ms) return;
    m_pos = ms;
    emit positionChanged();
    updateText();
}

// Active cue for the current position (server order is chronological; a plain
// scan is ample for a few hundred cues at the player's ~2 Hz position ticks).
void SubtitleTrack::updateText()
{
    QString t;
    for (int i = 0; i < m_cues.size(); ++i)
        if (m_pos >= m_cues[i].start && m_pos < m_cues[i].end) { t = m_cues[i].text; break; }
    if (t != m_text) { m_text = t; emit textChanged(); }
}

// Decode the HTML entities left in timedtext (YouTube often double-encodes, so
// QXmlStreamReader's own one-level decode leaves a second layer, e.g. &amp;#39;
// -> &#39; -> '). Numeric refs first, then the named ones with &amp; last so it
// doesn't resurrect an entity.
QString SubtitleTrack::htmlUnescape(const QString &s)
{
    QString r = s;
    QRegExp num(QLatin1String("&#(\\d+);"));
    int pos = 0;
    while ((pos = num.indexIn(r, pos)) != -1) {
        r.replace(pos, num.matchedLength(), QChar(num.cap(1).toInt()));
        pos += 1;
    }
    r.replace(QLatin1String("&quot;"), QLatin1String("\""));
    r.replace(QLatin1String("&lt;"),   QLatin1String("<"));
    r.replace(QLatin1String("&gt;"),   QLatin1String(">"));
    r.replace(QLatin1String("&amp;"),  QLatin1String("&"));
    return r;
}

}} // namespace yt::media

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

#ifndef YT_MEDIA_SUBTITLETRACK_H
#define YT_MEDIA_SUBTITLETRACK_H
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QVector>
class QNetworkAccessManager;
class QNetworkReply;
namespace yt { namespace media {

// One selected subtitle track. Fetches a YouTube timedtext URL through the
// injected libcurl NAM (Qt 4.7.4 does no TLS, so QML's own XMLHttpRequest can't
// reach the https endpoint), parses the srv1 <transcript> cues, and exposes the
// caption line for the current playback position. The player writes position(ms);
// QML binds a text overlay to text() ("" = no caption right now). Exposed to QML
// as the `subtitles` context property.
class SubtitleTrack : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    Q_PROPERTY(int position READ position WRITE setPosition NOTIFY positionChanged)
public:
    explicit SubtitleTrack(QNetworkAccessManager *nam, QObject *parent = 0);
    ~SubtitleTrack();

    Q_INVOKABLE void load(const QString &url);   // fetch + parse a timedtext url
    Q_INVOKABLE void clear();                    // drop cues + text (subtitles off)
    // Parse a timedtext body and index it as the active track. The network-free
    // seam load()'s reply handler funnels through (and the host test drives).
    Q_INVOKABLE void applyData(const QByteArray &body);

    QString text() const     { return m_text; }
    int     position() const { return m_pos; }
    void    setPosition(int ms);
Q_SIGNALS:
    void textChanged();
    void positionChanged();
    void loaded();
private Q_SLOTS:
    void onFetched();
private:
    struct Cue { int start; int end; QString text; };   // ms
    void updateText();
    static QString htmlUnescape(const QString &s);
    QNetworkAccessManager *m_nam;
    QNetworkReply *m_reply;      // at most one fetch in flight
    QVector<Cue> m_cues;         // sorted by start (server order)
    QString m_text;              // active cue text
    int m_pos;                   // current position (ms)
};
}}
#endif

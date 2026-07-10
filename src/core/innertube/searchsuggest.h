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

#ifndef YT_SEARCHSUGGEST_H
#define YT_SEARCHSUGGEST_H
#include <QObject>
#include <QStringList>
#include "core/job.h"
#include "innertube/apiref.h"

namespace yt {

// Query-suggestion feeder for the search field. Empty query → the persisted
// recent-search history (no network); non-empty → live YouTube suggestions
// (debounce upstream in QML). Reaches the backend via apiRef() like StreamSet.
class SearchSuggest : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(bool        live    READ live    NOTIFY resultsChanged)
public:
    explicit SearchSuggest(QObject *parent = 0);
    ~SearchSuggest();

    Q_INVOKABLE void query(const QString &q);   // cancels the previous in-flight query
    Q_INVOKABLE void record(const QString &q);  // prepend to capped, de-duped history

    QStringList results() const { return m_results; }
    bool        live()    const { return m_live; }

    // Chain delivery sink. Plain public method (meta-object stays frozen).
    void applySuggestions(const QStringList &s);

Q_SIGNALS:
    void resultsChanged();

protected:
    virtual ApiRef apiRef() const;   // test seam (see StreamSet::apiRef)

private:
    void cancelJob();
    QStringList history() const;

    core::JobToken m_job;
    QStringList    m_results;
    bool           m_live;
};

}
#endif

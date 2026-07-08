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

#ifndef COMMENTMODEL_H
#define COMMENTMODEL_H

#include "servicelistmodel.h"
#include "core/chains.h"
#include "core/job.h"
#include "innertube/apiref.h"

// Comments for one video. A Ready status with count 0 means comments are disabled
// (or there are none) — the chain reports ok with empty items, distinct from Failed.
class CommentModel : public ServiceListModel {
    Q_OBJECT
    // True when comments are turned OFF for this video (no comments panel/token — e.g.
    // made-for-kids content), distinct from a video that merely has zero comments.
    Q_PROPERTY(bool disabled READ disabled NOTIFY disabledChanged)
public:
    explicit CommentModel(QObject *parent = 0);
    ~CommentModel();

    bool disabled() const { return m_disabled; }

    Q_INVOKABLE void list(const QString &videoId);
    Q_INVOKABLE void fetchMore();
    // Post a top-level comment. Guarded by signedIn() (else needsSignIn()); ignores
    // empty text. Optimistically PREPENDs a locally-built comment at row 0 and reverts
    // it if the create_comment post fails.
    Q_INVOKABLE void post(const QString &text);

    // The chain's delivery sink — APPENDs (ok+empty ⇒ Ready = comments disabled).
    // Plain public method (not a slot) so the meta-object stays frozen.
    void applyComments(const yt::core::Outcome<yt::core::CommentPage> &r);

public Q_SLOTS:
    void cancel();

Q_SIGNALS:
    // Raised by post() when not signed in — the UI opens the auth sheet.
    void needsSignIn();
    // The `disabled` (comments-off) state settled — fires from list() + applyComments.
    void disabledChanged();

protected:
    // Test seam (see VideoModel::apiRef()).
    virtual yt::ApiRef apiRef() const;
    // Sign-in gate — reads the global AccountManager. Overridable in tests.
    virtual bool signedIn() const;

    // Typed row storage — answers reads with a zero-alloc switch(roleIdx).
    int itemCount() const;
    QVariant roleData(int row, int roleIdx) const;
    void dropItems();

private:
    void cancelJob();

    QList<CT::Comment> m_rows;
    yt::core::JobToken m_job;      // list/fetchMore
    yt::core::JobToken m_postJob;  // post() — dtor-canceled; the revert closure gates on it (R8)
    QString m_videoId;
    QString m_createCommentParams; // the create-comment box's submit token (R4)
    bool m_disabled = false;       // comments turned off for this video (set by applyComments)
};

#endif // COMMENTMODEL_H

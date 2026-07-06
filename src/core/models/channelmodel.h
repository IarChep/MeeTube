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

#ifndef CHANNELMODEL_H
#define CHANNELMODEL_H

#include "servicelistmodel.h"
#include "core/chains.h"
#include "core/job.h"
#include "innertube/apiref.h"

// A list of channels (channel-search results) for a ListView. A single channel's
// header is a plain ChannelDetails object, not this list.
class ChannelModel : public ServiceListModel {
    Q_OBJECT
public:
    explicit ChannelModel(QObject *parent = 0);
    ~ChannelModel();

    Q_INVOKABLE void search(const QString &query);

    // Browse a channel-list feed (FEchannels — the manage-subscriptions grid). Like
    // search() but over core::fetchChannelList; applyUsers RESETs the rows.
    Q_INVOKABLE void list(const QString &browseId);

    // Optimistically REMOVE the row whose channel id matches, then fire a
    // fire-and-forget subscription/unsubscribe (empty done — no self-capture, no
    // token needed; a failed unsubscribe simply reappears on the next reload).
    Q_INVOKABLE void unsubscribe(const QString &channelId);

    // The chain's delivery sink — RESETS the rows. Plain public method (not a slot)
    // so the meta-object stays frozen.
    void applyUsers(const yt::core::Outcome<yt::core::UserPage> &r);

public Q_SLOTS:
    void cancel();

protected:
    virtual yt::ApiRef apiRef() const;

    // Typed row storage — answers reads with a zero-alloc switch(roleIdx).
    int itemCount() const;
    QVariant roleData(int row, int roleIdx) const;
    void dropItems();

private:
    void cancelJob();

    QList<CT::User> m_rows;
    yt::core::JobToken m_job;
};

#endif // CHANNELMODEL_H

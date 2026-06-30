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

#include "categoryrequest.h"

namespace yt {

void CategoryRequest::list(const QString &) {
    setStatus(Loading);
    QList<CT::Category> out;
    struct { const char *id; const char *title; } cats[] = {
        {"10","Music"}, {"20","Gaming"}, {"25","News"}, {"30","Movies"}, {"live","Live"} };
    for (int i = 0; i < 5; ++i) { CT::Category c; c.id = cats[i].id; c.title = cats[i].title; out << c; }
    deliver(out);
}

void CategoryRequest::deliver(const QList<CT::Category> &c) { setStatus(Ready); emit ready(c); }

}

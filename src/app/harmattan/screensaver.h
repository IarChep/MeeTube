/*
 * Copyright (C) 2016 Stuart Howarth <showarth@marxoft.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 3, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef SCREENSAVER_H
#define SCREENSAVER_H

#include <QObject>
#include <QTimer>
#if defined(BUILD_N9)
#include <qmsystem2/qmdisplaystate.h>
#endif

// Keeps the display awake (no auto-dim / blank) while the user is on the player page.
// On device it pauses MCE display blanking via QmDisplayState::setBlankingPause(), which
// only holds for ~60 s, so a 60 s timer renews it; cancelBlankingPause() restores normal
// blanking. The host / Simulator build (BUILD_N9 off) is a no-op — the desktop never blanks.
// Ported from cuteTube2 (app/src/harmattan/screensaver.*); the device guard is renamed
// HARMATTAN_DEVICE -> BUILD_N9 to match MeeTube's convention. Exposed to QML as `screenSaver`.
class ScreenSaver : public QObject
{
    Q_OBJECT

public:
    explicit ScreenSaver(QObject *parent = 0);

public Q_SLOTS:
    // prevent=true  -> pause blanking now + keep renewing every 60 s (PlayerPage enter).
    // prevent=false -> cancel the pause + stop renewing (PlayerPage leave).
    void preventBlanking(bool prevent = true);

private:
#if defined(BUILD_N9)
    MeeGo::QmDisplayState m_displayState;
#endif
    QTimer m_timer;
};

#endif // SCREENSAVER_H

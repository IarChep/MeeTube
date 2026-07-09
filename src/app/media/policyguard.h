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

#ifndef YT_MEDIA_POLICYGUARD_H
#define YT_MEDIA_POLICYGUARD_H
#include "media/ipolicy.h"
#if defined(BUILD_N9)
#include <QList>
// Pull the real ResourcePolicy declarations (ResourceSet + the unscoped enum
// ResourceType) rather than forward-declaring: a bare `enum ResourceType;` is an
// opaque-enum-declaration, invalid in C++ without a fixed underlying type, and the
// real enum has none. resources.h is a small, moc-safe Qt header (no raw string
// literals), so AUTOMOC digests it fine; the slot below needs the type visible.
#include <policy/resources.h>
#endif
namespace yt { namespace media {

// IPolicy backed by the Harmattan resource-policy manager (libresourceqt).
// Acquires AudioPlaybackType (+ VideoPlaybackType in VideoMode) as the "player"
// application class; forwards grant/deny/loss to the IPolicy signals. Host build:
// a stub that grants immediately and no-ops the rest.
class PolicyGuard : public IPolicy {
    Q_OBJECT
public:
    explicit PolicyGuard(QObject *parent = 0);
    ~PolicyGuard();
    void acquire(PlaybackMode mode);
    void release();
#if defined(BUILD_N9)
private slots:
    void onGrantedRaw(const QList<ResourcePolicy::ResourceType> &granted);
private:
    ResourcePolicy::ResourceSet *m_set;
    PlaybackMode m_mode;
    // StreamPlayer calls release() on every terminal path (stop, error, EOS, dtor),
    // so release() must be idempotent: only forward to ResourceSet::release() while
    // actually acquired. Set on acquire(), cleared on release() — a second release()
    // (or one before any acquire()) is then a no-op against the live policy manager.
    bool m_acquired;
#endif
};
}}
#endif

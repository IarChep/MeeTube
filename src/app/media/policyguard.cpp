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

#include "media/policyguard.h"

#if defined(BUILD_N9)
#include <policy/resource-set.h>
#include <policy/resources.h>
#include <policy/audio-resource.h>
#include <QCoreApplication>

namespace yt { namespace media {

PolicyGuard::PolicyGuard(QObject *parent) : IPolicy(parent), m_set(0), m_mode(AudioMode), m_acquired(false)
{
    // "player" application class: the policy manager uses it for priority ordering.
    m_set = new ResourcePolicy::ResourceSet(QLatin1String("player"), this);
    connect(m_set, SIGNAL(resourcesGranted(QList<ResourcePolicy::ResourceType>)),
            this,  SLOT(onGrantedRaw(QList<ResourcePolicy::ResourceType>)));
    connect(m_set, SIGNAL(resourcesDenied()),            this, SIGNAL(denied()));
    connect(m_set, SIGNAL(lostResources()),              this, SIGNAL(lost()));
    connect(m_set, SIGNAL(resourcesReleasedByManager()), this, SIGNAL(releasedByManager()));
}

PolicyGuard::~PolicyGuard() { if (m_set && m_acquired) m_set->release(); }

void PolicyGuard::acquire(PlaybackMode mode)
{
    m_mode = mode;
    m_acquired = true;     // owns the set from here; release() will hand it back
    // A bare AudioPlaybackType grants the resource but Harmattan's SYSTEM pulse
    // (module-policy) still can't tell WHICH stream is ours, so it stays in the
    // unclassified group and plays MUTED (device symptom: pipeline PLAYING, pads
    // linked, zero sound). The documented libresourceqt pattern: an AudioResource
    // carrying our PID + a wildcard stream tag lets policy match the pulsesink
    // stream and unmute it. addResourceObject replaces any prior audio resource.
    ResourcePolicy::AudioResource *audio =
        new ResourcePolicy::AudioResource(QLatin1String("player"));
    audio->setProcessID((quint32) QCoreApplication::applicationPid());
    audio->setStreamTag(QLatin1String("media.name"), QLatin1String("*"));
    m_set->addResourceObject(audio);   // set takes ownership
    if (mode == VideoMode) m_set->addResource(ResourcePolicy::VideoPlaybackType);
    m_set->update();       // register the modified set
    m_set->acquire();      // -> resourcesGranted() (or resourcesDenied())
}

// Idempotent: StreamPlayer calls this on stop / error / EOS / dtor, so guard the
// live ResourceSet::release() with m_acquired — a repeat (or a call before any
// acquire()) is a no-op, avoiding redundant releases against the policy manager.
void PolicyGuard::release()
{
    if (m_set && m_acquired) m_set->release();
    m_acquired = false;
}

void PolicyGuard::onGrantedRaw(const QList<ResourcePolicy::ResourceType> &) { emit granted(); }

}} // namespace yt::media

#else   // ---- host stub ----
#include <QTimer>
namespace yt { namespace media {
PolicyGuard::PolicyGuard(QObject *parent) : IPolicy(parent) {}
PolicyGuard::~PolicyGuard() {}
// Grant asynchronously so the host play flow proceeds to the (device-only) pipeline.
void PolicyGuard::acquire(PlaybackMode) { QTimer::singleShot(0, this, SIGNAL(granted())); }
void PolicyGuard::release() {}
}}
#endif

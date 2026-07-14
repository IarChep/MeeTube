#ifndef YT_MEDIA_IPOLICY_H
#define YT_MEDIA_IPOLICY_H
#include <QObject>
#include <QTimer>
#include "media/playbackmode.h"
namespace yt { namespace media {

// Harmattan resource-policy seam. The real impl (PolicyGuard, src/app/media/)
// wraps ResourcePolicy::ResourceSet on device; tests inject a fake. acquire()
// is async: the player plays only after granted(); pauses on lost(); resumes on
// the next granted(); hard-stops on releasedByManager().
class IPolicy : public QObject {
    Q_OBJECT
public:
    explicit IPolicy(QObject *parent = 0) : QObject(parent) {}
    virtual ~IPolicy() {}
    virtual void acquire(PlaybackMode mode) = 0;
    virtual void release() = 0;
Q_SIGNALS:
    void granted();             // initial grant AND re-grant after a loss
    void denied();              // mandatory resource unavailable
    void lost();                // preempted (call/alarm) — stop using resources
    void releasedByManager();   // terminal: must re-acquire to play again
};

// Always-grant policy for backends that manage resources THEMSELVES: the
// QtMultimediaKit engine acquires its own ResourceSet internally, and a second
// app-side set of the same "player" class makes the policy manager revoke one
// of them mid-playback (device-observed: "policy released by manager" right
// after the first frame). Grants async to keep the StreamPlayer flow identical.
class NullPolicy : public IPolicy {
    Q_OBJECT
public:
    explicit NullPolicy(QObject *parent = 0) : IPolicy(parent) {}
    void acquire(PlaybackMode) { QTimer::singleShot(0, this, SIGNAL(granted())); }
    void release() {}
};
}}
#endif

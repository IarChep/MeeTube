#include <QtTest/QtTest>
#include "innertube/streamset.h"
#include "core/chains.h"        // PlayerOutcome
#include "servicedatatypes.h"
using namespace yt;

class TestStreamSet : public QObject { Q_OBJECT
private slots:
    void exposesBestVideoAndAudio() {
        StreamSet ss;
        core::PlayerOutcome o; o.streamsOk = true;
        CT::Stream v; v.id="137"; v.url="uV"; v.mimeType="video/mp4; codecs=\"avc1\""; v.width=1920; v.height=1080; v.hasAudio=false;
        CT::Stream a; a.id="140"; a.url="uA"; a.mimeType="audio/mp4; codecs=\"mp4a\""; a.hasAudio=true;
        CT::Stream p; p.id="18";  p.url="uP"; p.mimeType="video/mp4"; p.width=640; p.height=360; p.hasAudio=true;
        o.streams << v << a << p;
        ss.applyPlayer(o);
        QCOMPARE(ss.bestVideoUrl(), QString("uV"));
        QCOMPARE(ss.bestAudioUrl(), QString("uA"));
        QCOMPARE(ss.progressiveUrl(), QString("uP"));
    }
};
QTEST_MAIN(TestStreamSet)
#include "tst_meetube_streamset.moc"

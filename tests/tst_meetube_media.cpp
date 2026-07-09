#include <QtTest/QtTest>
#include "media/ipipeline.h"
#include "media/ipolicy.h"

// Compile-only anchor for Task 1: proves the media/ seams compile and moc.
class tst_meetube_media : public QObject {
    Q_OBJECT
private slots:
    void seamsCompile() {
        QVERIFY(yt::media::AudioMode == 0);
        QVERIFY(yt::media::VideoMode == 1);
    }
};

QTEST_MAIN(tst_meetube_media)
#include "tst_meetube_media.moc"

#include <QtTest/QtTest>
#include "jsc/basejs.h"
#include "testutil.h"        // loadFixtureRaw
#include <string>

using namespace yt::jsc;

static QString fixtureQt(const char *name) {
    const std::string s = loadFixtureRaw(name);
    return QString::fromUtf8(s.data(), (int)s.size());
}

class TestBaseJs : public QObject { Q_OBJECT
private slots:
    void hashFromIframeApi() {
        QCOMPARE(playerHashFromIframeApi(fixtureQt("iframe_api_sample.js")), QString("deadbeef"));
    }
    void baseUrlBuilt() {
        QCOMPARE(baseJsUrl("deadbeef"),
                 QString("https://www.youtube.com/s/player/deadbeef/player_ias.vflset/en_US/base.js"));
    }
    void stsExtracted() {
        QCOMPARE(extractSts(fixtureQt("base_js_sample.js")), 19834);
    }
    void sigSetupContainsCallable() {
        const std::string s = extractSigSetup(fixtureQt("base_js_sample.js"));
        QVERIFY(!s.empty());
        QVERIFY(s.find("__descramble") != std::string::npos);
        QVERIFY(s.find("Ap") != std::string::npos);   // helper object pulled in
    }
    void nSetupContainsCallable() {
        const std::string s = extractNSetup(fixtureQt("base_js_sample.js"));
        QVERIFY(!s.empty());
        QVERIFY(s.find("__nsig") != std::string::npos);
    }
};
QTEST_MAIN(TestBaseJs)
#include "tst_meetube_basejs.moc"

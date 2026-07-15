#include <QtTest/QtTest>
#include "jsc/solver.h"
#include "jsc/basejs.h"
#include "testutil.h"
#include <string>

using namespace yt::jsc;

class TestSolver : public QObject { Q_OBJECT
private slots:
    void deciphersKnownPermutation() {
        Solver s;
        QVERIFY(s.init(extractSigSetup(baseJs()), extractNSetup(baseJs())));
        QVERIFY(s.ready());
        QCOMPARE(s.decipherSignature("abcdef"), QString("bfcea"));  // see fixture maths
    }
    void solvesKnownN() {
        Solver s; s.init(extractSigSetup(baseJs()), extractNSetup(baseJs()));
        QCOMPARE(s.solveN("hello"), QString("olleh"));
    }
    void cacheReturnsSameValue() {
        Solver s; s.init(extractSigSetup(baseJs()), extractNSetup(baseJs()));
        QCOMPARE(s.decipherSignature("abcdef"), s.decipherSignature("abcdef"));
    }
    void buildsPlayerJsFromBaseJs() {
        PlayerJs *pj = buildPlayerJs("http://x/base.js", baseJs());
        QVERIFY(pj != 0);
        QCOMPARE(pj->sts, 19834);
        QVERIFY(pj->solver.ready());
        QCOMPARE(pj->solver.solveN("hello"), QString("olleh"));
        delete pj;
    }
    void buildReturnsNullOnGarbage() {
        QVERIFY(buildPlayerJs("http://x/base.js", "not base.js at all") == 0);
    }
private:
    static QString baseJs() {
        const std::string s = loadFixtureRaw("base_js_sample.js");
        return QString::fromUtf8(s.data(), (int)s.size());
    }
};
QTEST_MAIN(TestSolver)
#include "tst_meetube_solver.moc"

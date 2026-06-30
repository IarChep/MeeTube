#include <QtTest/QtTest>
#include "parsers/continuation.h"
#include "parsers/jsonutil.h"
#include "testutil.h"

using namespace yt;

class TestParsers : public QObject { Q_OBJECT
private slots:
    void jintParses() {
        nlohmann::json j1 = {{"s", "1234"}};
        QCOMPARE((qint64)jint(j1, "s"), (qint64)1234);

        nlohmann::json j2 = {{"i", 42}};
        QCOMPARE((qint64)jint(j2, "i"), (qint64)42);

        nlohmann::json j3 = {{"bad", "abc"}};
        QCOMPARE((qint64)jint(j3, "bad"), (qint64)0);

        nlohmann::json j4 = {{"other", 1}};
        QCOMPARE((qint64)jint(j4, "missing"), (qint64)0);
    }
    void jstrBehavior() {
        nlohmann::json j = {{"s", "hi"}, {"n", 5}};
        QCOMPARE(jstr(j, "s"), QString("hi"));
        QCOMPARE(jstr(j, "n"), QString());
        QCOMPARE(jstr(j, "missing"), QString());
    }
    void continuationFound() {
        nlohmann::json node = {
          {"continuationItemRenderer", {
            {"continuationEndpoint", {{"continuationCommand", {{"token", "CTOKEN123"}}}}}}}};
        QCOMPARE(findContinuationToken(node), QString("CTOKEN123"));
    }
    void continuationLegacy() {
        nlohmann::json node = {{"nextContinuationData", {{"continuation", "LEG456"}}}};
        QCOMPARE(findContinuationToken(node), QString("LEG456"));
    }
    void continuationAbsent() {
        nlohmann::json node = {{"foo", {{"bar", 1}}}};
        QCOMPARE(findContinuationToken(node), QString());
    }
};
QTEST_MAIN(TestParsers)
#include "tst_meetube_parsers.moc"

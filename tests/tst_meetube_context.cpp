#include <QtTest/QtTest>
#include "innertube/contextbuilder.h"

using namespace yt;

class TestContext : public QObject { Q_OBJECT
private slots:
    void iosContext() {
        Session s; s.visitorData = "VD";
        nlohmann::json c = ContextBuilder::context(ClientId::IOS, s);
        QCOMPARE(QString::fromStdString(c["client"]["clientName"].get<std::string>()), QString("IOS"));
        QCOMPARE(QString::fromStdString(c["client"]["clientVersion"].get<std::string>()), QString("20.49.6"));
        QCOMPARE(QString::fromStdString(c["client"]["hl"].get<std::string>()), QString("en"));
        QCOMPARE(QString::fromStdString(c["client"]["visitorData"].get<std::string>()), QString("VD"));
        // user + request sub-contexts (minimum-viable shape)
        QCOMPARE(c["user"]["lockedSafetyMode"].get<bool>(), false);
        QVERIFY(c["request"]["useSsl"].get<bool>());
    }
    void webContextHasUserRequest() {
        Session s;
        nlohmann::json c = ContextBuilder::context(ClientId::WEB, s);
        QVERIFY(c.contains("user") && c.contains("request"));
        QCOMPARE(QString::fromStdString(c["client"]["clientVersion"].get<std::string>()), QString("2.20260626.01.00"));
    }
    void headersHaveConsentCookie() {
        Session s;
        const QList<QPair<QByteArray,QByteArray> > h = ContextBuilder::headers(ClientId::WEB, s);
        bool sawCookie=false;
        for (int i=0;i<h.size();++i) if (h[i].first=="Cookie") { sawCookie=true; QVERIFY(h[i].second.contains("SOCS=")); }
        QVERIFY(sawCookie);
    }
    void iosHeaders() {
        Session s;
        const QList<QPair<QByteArray,QByteArray> > h = ContextBuilder::headers(ClientId::IOS, s);
        bool sawName=false, sawVer=false, sawCt=false;
        for (int i=0;i<h.size();++i) {
            if (h[i].first=="X-YouTube-Client-Name")    { sawName=true; QCOMPARE(h[i].second, QByteArray("5")); }
            if (h[i].first=="X-YouTube-Client-Version") { sawVer=true;  QCOMPARE(h[i].second, QByteArray("20.49.6")); }
            if (h[i].first=="Content-Type")             { sawCt=true;   QCOMPARE(h[i].second, QByteArray("application/json")); }
        }
        QVERIFY(sawName && sawVer && sawCt);
    }
    void bearerWhenSet() {
        Session s; s.bearer = "tok";
        const QList<QPair<QByteArray,QByteArray> > h = ContextBuilder::headers(ClientId::TVHTML5, s);
        bool sawAuth=false;
        for (int i=0;i<h.size();++i) if (h[i].first=="Authorization") { sawAuth=true; QCOMPARE(h[i].second, QByteArray("Bearer tok")); }
        QVERIFY(sawAuth);
    }
};
QTEST_MAIN(TestContext)
#include "tst_meetube_context.moc"

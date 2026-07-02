#include <QtTest/QtTest>
#include "innertube/contextbuilder.h"

using namespace yt;

class TestContext : public QObject { Q_OBJECT
private slots:
    // contextJson emits minified deterministic JSON — assert on substrings.
    void iosContext() {
        Session s; s.visitorData = "VD";
        const QString c = QString::fromStdString(ContextBuilder::contextJson(ClientId::IOS, s));
        QVERIFY(c.contains("\"clientName\":\"IOS\""));
        QVERIFY(c.contains("\"clientVersion\":\"20.49.6\""));
        QVERIFY(c.contains("\"hl\":\"en\""));
        QVERIFY(c.contains("\"visitorData\":\"VD\""));
        // user + request sub-contexts (minimum-viable shape)
        QVERIFY(c.contains("\"user\":{\"lockedSafetyMode\":false}"));
        QVERIFY(c.contains("\"useSsl\":true"));
        QVERIFY(c.contains("\"internalExperimentFlags\":[]"));
        // IOS device identity present
        QVERIFY(c.contains("\"deviceMake\":\"Apple\""));
    }
    void webContextHasUserRequest() {
        Session s;
        const QString c = QString::fromStdString(ContextBuilder::contextJson(ClientId::WEB, s));
        QVERIFY(c.contains("\"user\":") && c.contains("\"request\":"));
        QVERIFY(c.contains("\"clientVersion\":\"2.20260626.01.00\""));
        // no visitorData key when the session has none (skip_null_members)
        QVERIFY(!c.contains("\"visitorData\""));
        // no android/ios extras on WEB
        QVERIFY(!c.contains("\"deviceMake\"") && !c.contains("\"androidSdkVersion\""));
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
    // The bearer is minted with the TV client credentials and ONLY the TVHTML5
    // client honors it — every other client (incl. WEB /next, /browse, comments)
    // rejects it with 400 INVALID_ARGUMENT (live-verified 2026-07-02). Signed-in
    // WEB requests must therefore stay anonymous.
    void bearerOnlyOnTv() {
        Session s; s.bearer = "tok";
        QVERIFY(hasAuth(ContextBuilder::headers(ClientId::TVHTML5, s)));
        QVERIFY(!hasAuth(ContextBuilder::headers(ClientId::WEB, s)));
        QVERIFY(!hasAuth(ContextBuilder::headers(ClientId::WEB_SAFARI, s)));
        QVERIFY(!hasAuth(ContextBuilder::headers(ClientId::IOS, s)));
        QVERIFY(!hasAuth(ContextBuilder::headers(ClientId::ANDROID, s)));
        QVERIFY(!hasAuth(ContextBuilder::headers(ClientId::ANDROID_VR, s)));
    }
private:
    static bool hasAuth(const QList<QPair<QByteArray,QByteArray> > &h) {
        for (int i = 0; i < h.size(); ++i) if (h[i].first == "Authorization") return true;
        return false;
    }
};
QTEST_MAIN(TestContext)
#include "tst_meetube_context.moc"

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QFile>
#include <QDir>
#include "testutil.h"
#include "innertube/accountstore.h"
#include "innertube/accountmanager.h"
#include "innertube/catalog.h"

using namespace yt;

// Polls immediately instead of arming a timer, so the FakeTransport drains the whole
// device-code -> poll -> token chain in a single flush().
class TestAccountManager : public AccountManager {
public:
    TestAccountManager(ITransport *t, AccountStore *s) : AccountManager(t, s) {}
protected:
    void schedulePoll() { poll(); }
};

class TestAccount : public QObject { Q_OBJECT
    QString iniPath() const { return QDir::tempPath() + "/meetube_test_accounts.ini"; }
private slots:
    void init() { QFile::remove(iniPath()); }
    void cleanup() { QFile::remove(iniPath()); }

    // ---- AccountStore round-trip ----
    void storeRoundTrip() {
        AccountStore store(iniPath());
        QVERIFY(store.isEmpty());
        CT::Account a; a.id = "ch1"; a.username = "Alice";
        store.save(a, "REFRESH1");
        QCOMPARE(store.refreshToken("ch1"), QString("REFRESH1"));
        QCOMPARE(store.activeId(), QString("ch1"));   // first account becomes active
        QList<CT::Account> all = store.accounts();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all.first().username, QString("Alice"));
        store.remove("ch1");
        QVERIFY(store.isEmpty());
    }

    void storePersistsAcrossInstances() {
        { AccountStore s1(iniPath()); CT::Account a; a.id = "x"; a.username = "U"; s1.save(a, "RT"); }
        AccountStore s2(iniPath());           // fresh instance, same file
        QCOMPARE(s2.refreshToken("x"), QString("RT"));
    }

    // ---- AccountManager device-code flow ----
    void deviceCodeFlowSucceeds() {
        FakeTransport t;
        AccountStore store(iniPath());
        t.queue(QString::fromLatin1(Catalog::kDeviceCodeUrl), nlohmann::json{
            {"device_code", "DC"}, {"user_code", "ABCD-EFGH"},
            {"verification_url", "https://youtube.com/activate"}, {"interval", 5}});
        t.queue(QString::fromLatin1(Catalog::kTokenUrl), nlohmann::json{
            {"access_token", "AT"}, {"refresh_token", "RT"}});

        TestAccountManager mgr(&t, &store);
        QSignalSpy codeSpy(&mgr, SIGNAL(userCodeReady(QString,QString)));
        QSignalSpy authSpy(&mgr, SIGNAL(authenticated()));
        QSignalSpy bearerSpy(&mgr, SIGNAL(bearerChanged()));

        mgr.signIn();
        t.flush();      // device/code -> (schedulePoll==poll) -> token

        QCOMPARE(bearerSpy.count(), 1);   // engine copies this into the session
        QCOMPARE(codeSpy.count(), 1);
        QCOMPARE(codeSpy.at(0).at(0).toString(), QString("https://youtube.com/activate"));
        QCOMPARE(codeSpy.at(0).at(1).toString(), QString("ABCD-EFGH"));
        QCOMPARE(authSpy.count(), 1);
        QCOMPARE(mgr.currentBearer(), QString("AT"));
        QCOMPARE(store.refreshToken("default"), QString("RT"));
        QVERIFY(mgr.isSignedIn());
        // device/code POST + the token POST
        QCOMPARE(t.sentForm.size(), 2);
        QCOMPARE(t.sentForm.at(0).first, QString::fromLatin1(Catalog::kDeviceCodeUrl));
    }

    void deviceCodePollsWhilePending() {
        FakeTransport t;
        AccountStore store(iniPath());
        t.queue(QString::fromLatin1(Catalog::kDeviceCodeUrl), nlohmann::json{
            {"device_code", "DC"}, {"user_code", "WXYZ"}, {"interval", 5}});
        t.queue(QString::fromLatin1(Catalog::kTokenUrl), nlohmann::json{{"error", "authorization_pending"}});
        t.queue(QString::fromLatin1(Catalog::kTokenUrl), nlohmann::json{
            {"access_token", "AT2"}, {"refresh_token", "RT2"}});

        TestAccountManager mgr(&t, &store);
        QSignalSpy authSpy(&mgr, SIGNAL(authenticated()));
        QSignalSpy failSpy(&mgr, SIGNAL(authFailed(QString)));
        mgr.signIn();
        t.flush();      // device -> poll(pending) -> poll(success)

        QCOMPARE(failSpy.count(), 0);
        QCOMPARE(authSpy.count(), 1);
        QCOMPARE(mgr.currentBearer(), QString("AT2"));
        QCOMPARE(t.sentForm.size(), 3);   // device + 2 token polls
    }

    void deviceCodeDenied() {
        FakeTransport t;
        AccountStore store(iniPath());
        t.queue(QString::fromLatin1(Catalog::kDeviceCodeUrl), nlohmann::json{
            {"device_code", "DC"}, {"user_code", "WXYZ"}, {"interval", 5}});
        t.queue(QString::fromLatin1(Catalog::kTokenUrl), nlohmann::json{{"error", "access_denied"}});
        TestAccountManager mgr(&t, &store);
        QSignalSpy authSpy(&mgr, SIGNAL(authenticated()));
        QSignalSpy failSpy(&mgr, SIGNAL(authFailed(QString)));
        mgr.signIn();
        t.flush();
        QCOMPARE(authSpy.count(), 0);
        QCOMPARE(failSpy.count(), 1);
        QVERIFY(!mgr.isSignedIn());
    }
};
QTEST_MAIN(TestAccount)
#include "tst_meetube_account.moc"

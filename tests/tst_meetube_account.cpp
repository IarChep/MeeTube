#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QFile>
#include <QDir>
#include "testutil.h"
#include "innertube/accountstore.h"
#include "innertube/accountmanager.h"
#include "innertube/catalog.h"
#include "requests/accountrequest.h"
#include "innertube/accountdetails.h"

using namespace yt;

// Polls immediately instead of arming a timer, so the FakeTransport drains the whole
// device-code -> poll -> token chain in a single flush().
class TestAccountManager : public AccountManager {
public:
    TestAccountManager(ITransport *t, AccountStore *s) : AccountManager(t, s) {}
protected:
    void schedulePoll() { poll(); }
};

// Injects a FakeTransport-backed AccountRequest through the newRequest() seam.
class TestAccountDetails : public AccountDetails {
public:
    TestAccountDetails(ITransport *t, AccountStore *s) : AccountDetails(s), m_t(t) {}
protected:
    AccountRequest* newRequest() { return new AccountRequest(m_t, this); }
private:
    ITransport *m_t;
};

class TestAccount : public QObject { Q_OBJECT
    QString iniPath() const { return QDir::tempPath() + "/meetube_test_accounts.ini"; }
private slots:
    void initTestCase() { qRegisterMetaType<CT::Account>("CT::Account"); }
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

    void storeUpdateActiveKeepsToken() {
        AccountStore store(iniPath());
        CT::Account a; a.id = "default"; a.username = "YouTube";
        store.save(a, "RT");
        CT::Account real;
        real.username = "Ivan Petrov"; real.thumbnailUrl = "https://t/a.jpg";
        real.handle = "@ivanpetrov"; real.channelId = "UC123";
        store.updateActive(real);
        QCOMPARE(store.refreshToken("default"), QString("RT"));  // token untouched
        CT::Account out = store.active();
        QCOMPARE(out.id, QString("default"));                    // id untouched
        QCOMPARE(out.username, QString("Ivan Petrov"));
        QCOMPARE(out.handle, QString("@ivanpetrov"));
        QCOMPARE(out.channelId, QString("UC123"));
        QCOMPARE(out.thumbnailUrl, QString("https://t/a.jpg"));
    }

    void storePersistsAcrossInstances() {
        { AccountStore s1(iniPath()); CT::Account a; a.id = "x"; a.username = "U"; s1.save(a, "RT"); }
        AccountStore s2(iniPath());           // fresh instance, same file
        QCOMPARE(s2.refreshToken("x"), QString("RT"));
    }

    // ---- AccountRequest (accounts_list identity fetch) ----
    void accountRequestFetchesIdentity() {
        FakeTransport t;
        t.queue("account/accounts_list", loadFixture("accounts_list.json"));
        AccountRequest req(&t);
        QSignalSpy readySpy(&req, SIGNAL(ready(CT::Account)));
        QSignalSpy failSpy(&req, SIGNAL(failed(QString)));
        req.list();
        t.flush();
        QCOMPARE(failSpy.count(), 0);
        QCOMPARE(readySpy.count(), 1);
        const CT::Account a = readySpy.at(0).at(0).value<CT::Account>();
        QCOMPARE(a.username, QString("Ivan Petrov"));
        QCOMPARE(a.channelId, QString("UCabc123def"));
        QVERIFY(t.sent.at(0).contains("accountReadMask"));   // read mask in the body
    }

    // ---- AccountDetails (identity detail object) ----
    void detailsLoadsAndWritesThrough() {
        FakeTransport t;
        AccountStore store(iniPath());
        CT::Account placeholder; placeholder.id = "default"; placeholder.username = "YouTube";
        store.save(placeholder, "RT");
        t.queue("account/accounts_list", loadFixture("accounts_list.json"));
        TestAccountDetails d(&t, &store);
        QCOMPARE(d.username(), QString("YouTube"));    // seeded from the store cache
        QSignalSpy loadedSpy(&d, SIGNAL(loaded()));
        d.load();
        t.flush();
        QCOMPARE(loadedSpy.count(), 1);
        QCOMPARE(d.username(), QString("Ivan Petrov"));
        QCOMPARE(d.handle(), QString("@ivanpetrov"));
        QCOMPARE(d.channelId(), QString("UCabc123def"));
        QCOMPARE(store.active().username, QString("Ivan Petrov"));  // write-through
        QCOMPARE(store.refreshToken("default"), QString("RT"));     // token intact
    }

    void detailsKeepsCacheOnFailure() {
        FakeTransport t;   // nothing queued -> the post fails
        AccountStore store(iniPath());
        CT::Account cached; cached.id = "default"; cached.username = "Ivan";
        store.save(cached, "RT");
        TestAccountDetails d(&t, &store);
        d.load();
        t.flush();
        QCOMPARE(d.status(), (int) ServiceRequest::Failed);
        QCOMPARE(d.username(), QString("Ivan"));       // cached identity survives
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

    void signedInPropertyNotifies() {
        FakeTransport t;
        AccountStore store(iniPath());
        t.queue(QString::fromLatin1(Catalog::kDeviceCodeUrl), nlohmann::json{
            {"device_code", "DC"}, {"user_code", "ABCD"}, {"interval", 5}});
        t.queue(QString::fromLatin1(Catalog::kTokenUrl), nlohmann::json{
            {"access_token", "AT"}, {"refresh_token", "RT"}});
        TestAccountManager mgr(&t, &store);
        QSignalSpy sSpy(&mgr, SIGNAL(signedInChanged()));
        QVERIFY(!mgr.property("signedIn").toBool());
        mgr.signIn();
        t.flush();
        QCOMPARE(sSpy.count(), 1);
        QVERIFY(mgr.property("signedIn").toBool());
        mgr.signOut();
        QCOMPARE(sSpy.count(), 2);
        QVERIFY(!mgr.property("signedIn").toBool());
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

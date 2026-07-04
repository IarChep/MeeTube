#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QFile>
#include <QDir>
#include "testutil.h"
#include "innertube/accountstore.h"
#include "innertube/accountmanager.h"
#include "innertube/catalog.h"
#include "innertube/accountdetails.h"
#include "core/status.h"
#include "innertube/apiref.h"
#include "threading/workerhost.h"

using namespace yt;

// AccountManager is NOT rewired in this task (Task 13) — it still drives the
// FakeTransport ITransport seam, so these helpers and their tests are unchanged.
// Polls immediately instead of arming a timer, so the FakeTransport drains the whole
// device-code -> poll -> token chain in a single flush().
class TestAccountManager : public AccountManager {
public:
    TestAccountManager(ITransport *t, AccountStore *s) : AccountManager(t, s) {}
protected:
    void schedulePoll() { poll(); }
};

// AccountDetails IS rewired (fetchAccount chain) — inject an inline WorkerHost +
// FakeHttp through the apiRef() seam (mirrors tst_meetube_model). The FakeHttp is
// public so the tests can queue fixtures and flush(). (The accounts_list fetch itself
// is covered directly in tst_meetube_chains::accountFetchesIdentity.)
class TestAccountDetails : public AccountDetails {
public:
    TestAccountDetails(AccountStore *s) : AccountDetails(s) {}
    WorkerHost m_host; FakeHttp m_fake;
protected:
    ApiRef apiRef() const { return ApiRef(const_cast<WorkerHost *>(&m_host),
                                          const_cast<FakeHttp *>(&m_fake)); }
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

    // The anonymous session id must survive restarts — a fresh visitorData on every
    // launch is the cheapest "new bot" signal YouTube sees.
    void storeVisitorDataPersists() {
        { AccountStore s1(iniPath()); QVERIFY(s1.visitorData().isEmpty()); s1.setVisitorData("VD_PERSISTED"); }
        AccountStore s2(iniPath());           // fresh instance, same file
        QCOMPARE(s2.visitorData(), QString("VD_PERSISTED"));
    }

    void storePersistsAcrossInstances() {
        { AccountStore s1(iniPath()); CT::Account a; a.id = "x"; a.username = "U"; s1.save(a, "RT"); }
        AccountStore s2(iniPath());           // fresh instance, same file
        QCOMPARE(s2.refreshToken("x"), QString("RT"));
    }

    // ---- AccountDetails (identity detail object, now on the fetchAccount chain) ----
    void detailsLoadsAndWritesThrough() {
        AccountStore store(iniPath());
        CT::Account placeholder; placeholder.id = "default"; placeholder.username = "YouTube";
        store.save(placeholder, "RT");
        TestAccountDetails d(&store);
        d.m_fake.queue("account/accounts_list", loadFixtureRaw("accounts_list.json"));
        QCOMPARE(d.username(), QString("YouTube"));    // seeded from the store cache
        QSignalSpy loadedSpy(&d, SIGNAL(loaded()));
        d.load();
        d.m_fake.flush();
        QCOMPARE(loadedSpy.count(), 1);
        QCOMPARE(d.username(), QString("Ivan Petrov"));
        QCOMPARE(d.handle(), QString("@ivanpetrov"));
        QCOMPARE(d.channelId(), QString("UCabc123def"));
        QCOMPARE(store.active().username, QString("Ivan Petrov"));  // write-through
        QCOMPARE(store.refreshToken("default"), QString("RT"));     // token intact
    }

    void detailsKeepsCacheOnFailure() {
        AccountStore store(iniPath());
        CT::Account cached; cached.id = "default"; cached.username = "Ivan";
        store.save(cached, "RT");
        TestAccountDetails d(&store);   // nothing queued -> the post fails
        d.load();
        d.m_fake.flush();
        QCOMPARE(d.status(), (int) core::Failed);
        QCOMPARE(d.username(), QString("Ivan"));       // cached identity survives
    }

    // ---- AccountManager device-code flow ----
    void deviceCodeFlowSucceeds() {
        FakeTransport t;
        AccountStore store(iniPath());
        t.queue(QString::fromLatin1(Catalog::kDeviceCodeUrl),
                "{\"device_code\":\"DC\",\"user_code\":\"ABCD-EFGH\","
                "\"verification_url\":\"https://youtube.com/activate\",\"interval\":5}");
        t.queue(QString::fromLatin1(Catalog::kTokenUrl),
                "{\"access_token\":\"AT\",\"refresh_token\":\"RT\"}");

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
        t.queue(QString::fromLatin1(Catalog::kDeviceCodeUrl),
                "{\"device_code\":\"DC\",\"user_code\":\"WXYZ\",\"interval\":5}");
        t.queue(QString::fromLatin1(Catalog::kTokenUrl), "{\"error\":\"authorization_pending\"}");
        t.queue(QString::fromLatin1(Catalog::kTokenUrl),
                "{\"access_token\":\"AT2\",\"refresh_token\":\"RT2\"}");

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
        t.queue(QString::fromLatin1(Catalog::kDeviceCodeUrl),
                "{\"device_code\":\"DC\",\"user_code\":\"ABCD\",\"interval\":5}");
        t.queue(QString::fromLatin1(Catalog::kTokenUrl),
                "{\"access_token\":\"AT\",\"refresh_token\":\"RT\"}");
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
        t.queue(QString::fromLatin1(Catalog::kDeviceCodeUrl),
                "{\"device_code\":\"DC\",\"user_code\":\"WXYZ\",\"interval\":5}");
        t.queue(QString::fromLatin1(Catalog::kTokenUrl), "{\"error\":\"access_denied\"}");
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

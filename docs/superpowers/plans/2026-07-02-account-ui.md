# Account UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Signed-in identity surface: person ToolIcon in the main toolbar → AuthorisationSheet (OAuth device-code + QR) when signed out, AccountPage (YouTube "You"-tab clone: identity header, History carousel, Subscriptions/Library/Playlists rows) when signed in.

**Architecture:** `AccountManager` stays pure OAuth and gains a QML-readable `signedIn` property. Identity lives in a new `AccountDetails` detail object (ChannelDetails idiom) under a new `AccountApi` tree node (`innertube.account()`; the manager moves to `innertube.auth()`). A new `AccountRequest` posts `account/accounts_list`; results write through to `AccountStore` so the header is instant on relaunch. QML adds FeedPage (generic video list), PlaylistsPage, AccountPage, AuthorisationSheet.

**Tech Stack:** Qt 4.7.4 / QtQuick 1.1 / com.nokia.meego, nlohmann/json, CMake+Conan (`build-sim`), QTestLib with `FakeTransport`.

## Global Constraints

- Qt 4.7.4: NO `foreach`/`Q_FOREACH` (range-for is fine), NO lambdas / new-style `connect` — string `SIGNAL`/`SLOT` only, NO `QByteArray::fromStdString`.
- QML is QtQuick 1.1: `var`/`function(){}` only, no `let`/`const`/arrow-fns/`Qt.binding`. **Invoke the `nokia-n9-qml` skill before writing/editing any `.qml`**; every touched `.qml` must pass `python3 ~/.claude/skills/nokia-n9-qml/scripts/validate_qml.py <file>` with **0 ERROR** (WARN for `MeeTube 1.0` types and brand colors is acceptable).
- UI strings are English. Sizes/colors/fonts from `js/UIConstants.js` (`UI.` prefix).
- Build: `make -C build-sim -j"$(nproc)"`. Tests: `source simulator_env.sh && (cd build-sim && ctest --output-on-failure)`.
- Commit after every task (message style: `feat(...)` / `fix(...)` as in `git log`).
- License header: every new `.h`/`.cpp` starts with the same 15-line GPLv3 comment block as `src/innertube/channeldetails.h`.

---

### Task 1: `signedIn` property on AccountManager + startup restore

**Files:**
- Modify: `src/innertube/accountmanager.h`
- Modify: `src/innertube/accountmanager.cpp`
- Modify: `src/main.cpp:59` (restore call)
- Test: `tests/tst_meetube_account.cpp`

**Interfaces:**
- Produces: `Q_PROPERTY(bool signedIn ...)` + signal `signedInChanged()` on `yt::AccountManager` — QML reads `innertube.auth().signedIn`.

- [ ] **Step 1: Write the failing test** — add to the `TestAccount` class in `tests/tst_meetube_account.cpp` (after `deviceCodeDenied()`):

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make -C build-sim -j"$(nproc)" 2>&1 | tail -5`
Expected: compile FAILS — `no matching function for call to 'QSignalSpy::QSignalSpy'` / unknown signal `signedInChanged()` (moc rejects the spy on a nonexistent signal at runtime; the property() check would return invalid). If it compiles, the ctest run must FAIL.

- [ ] **Step 3: Implement** — `src/innertube/accountmanager.h`: add the property + signal.

```cpp
class AccountManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool signedIn READ isSignedIn NOTIFY signedInChanged)
```

and in `Q_SIGNALS:` (after `bearerChanged();`):

```cpp
    // isSignedIn() flipped (token grant / sign-out) — QML gates the account entry
    // point on this (sheet when signed out, AccountPage when signed in).
    void signedInChanged();
```

`src/innertube/accountmanager.cpp` — in `onToken()`, success branch, after `emit bearerChanged();`:

```cpp
        emit signedInChanged();
```

and in `signOut()`, after `emit bearerChanged();`:

```cpp
    emit signedInChanged();
```

`src/main.cpp` — after the `setContextProperty("innertube", ...)` line:

```cpp
    // Mint a bearer from the stored refresh token (no-op when signed out) so the
    // authed feeds work right after launch.
    yt::Innertube::instance()->accountManager()->restore();
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make -C build-sim -j"$(nproc)" 2>&1 | tail -3 && source simulator_env.sh && (cd build-sim && ctest -R tst_meetube_account --output-on-failure)`
Expected: PASS (all account tests).

- [ ] **Step 5: Commit**

```bash
git add src/innertube/accountmanager.h src/innertube/accountmanager.cpp src/main.cpp tests/tst_meetube_account.cpp
git commit -m "feat(auth): expose signedIn to QML + restore the session at startup"
```

---

### Task 2: CT::Account identity fields + AccountStore updateActive/active

**Files:**
- Modify: `src/types/servicedatatypes.h:45`
- Modify: `src/models/servicemetatypes.cpp`
- Modify: `src/innertube/accountstore.h`, `src/innertube/accountstore.cpp`
- Test: `tests/tst_meetube_account.cpp`

**Interfaces:**
- Produces: `CT::Account { id, username, thumbnailUrl, handle, channelId }`; `AccountStore::updateActive(const CT::Account&)` (fields only — id/refreshToken untouched); `CT::Account AccountStore::active() const`.

- [ ] **Step 1: Write the failing test** — add to `TestAccount`:

```cpp
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
```

- [ ] **Step 2: Run to verify it fails** — compile error: `'class yt::AccountStore' has no member named 'updateActive'`.

- [ ] **Step 3: Implement.** `src/types/servicedatatypes.h` — replace line 45:

```cpp
struct Account { QString id, username, thumbnailUrl, handle, channelId; };
```

and add with the other metatype declarations at the bottom:

```cpp
Q_DECLARE_METATYPE(CT::Account)
```

`src/models/servicemetatypes.cpp` — add inside `registerMeeTubeMetaTypes()`:

```cpp
    qRegisterMetaType<CT::Account>("CT::Account");
```

`src/innertube/accountstore.h` — after `void save(...)`:

```cpp
    // accounts_list write-through: refresh the ACTIVE record's identity fields.
    // The record id and refresh token are deliberately untouched.
    void updateActive(const CT::Account &account);
    CT::Account active() const;
```

`src/innertube/accountstore.cpp` — in `save()`, after the `thumbnailUrl` line:

```cpp
    m_s->setValue("accounts/" + account.id + "/handle", account.handle);
    m_s->setValue("accounts/" + account.id + "/channelId", account.channelId);
```

in `accounts()`, after the `thumbnailUrl` read:

```cpp
        a.handle = m_s->value("accounts/" + a.id + "/handle").toString();
        a.channelId = m_s->value("accounts/" + a.id + "/channelId").toString();
```

new methods (before `activeId()`):

```cpp
void AccountStore::updateActive(const CT::Account &account) {
    const QString id = activeId();
    if (id.isEmpty()) return;
    m_s->setValue("accounts/" + id + "/username", account.username);
    m_s->setValue("accounts/" + id + "/thumbnailUrl", account.thumbnailUrl);
    m_s->setValue("accounts/" + id + "/handle", account.handle);
    m_s->setValue("accounts/" + id + "/channelId", account.channelId);
    m_s->sync();
    emit accountsChanged();
}

CT::Account AccountStore::active() const {
    CT::Account a;
    const QString id = activeId();
    if (id.isEmpty()) return a;
    a.id = id;
    a.username = m_s->value("accounts/" + id + "/username").toString();
    a.thumbnailUrl = m_s->value("accounts/" + id + "/thumbnailUrl").toString();
    a.handle = m_s->value("accounts/" + id + "/handle").toString();
    a.channelId = m_s->value("accounts/" + id + "/channelId").toString();
    return a;
}
```

- [ ] **Step 4: Build + `ctest -R tst_meetube_account`** — PASS.

- [ ] **Step 5: Commit** — `feat(auth): identity fields on CT::Account + store updateActive/active`.

---

### Task 3: parseAccountsList + fixture

**Files:**
- Create: `tests/fixtures/accounts_list.json`
- Modify: `src/parsers/rendererparser.h`, `src/parsers/rendererparser.cpp`
- Test: `tests/tst_meetube_parsers.cpp`

**Interfaces:**
- Produces: `CT::Account yt::parseAccountsList(const nlohmann::json &response)` — the active (selected) account; empty `CT::Account` when none found.

- [ ] **Step 1: Create the fixture** `tests/fixtures/accounts_list.json`:

```json
{
  "actions": [{
    "getMultiPageMenuAction": {
      "menu": { "multiPageMenuRenderer": { "sections": [{
        "accountSectionListRenderer": { "contents": [{
          "accountItemSectionRenderer": { "contents": [{
            "accountItem": {
              "accountName": { "simpleText": "Ivan Petrov" },
              "channelHandle": { "simpleText": "@ivanpetrov" },
              "accountPhoto": { "thumbnails": [
                { "url": "https://yt3.example/small.jpg", "width": 32, "height": 32 },
                { "url": "https://yt3.example/big.jpg", "width": 88, "height": 88 }
              ] },
              "isSelected": true,
              "serviceEndpoint": { "selectActiveIdentityEndpoint": { "supportedTokens": [
                { "accountStateToken": { "hasChannel": true } },
                { "offlineCacheKeyToken": { "clientCacheKey": "abc123def" } }
              ] } }
            }
          }] }
        }] }
      }] } }
  }]
}
```

- [ ] **Step 2: Write the failing test** — add to `TestParsers` in `tests/tst_meetube_parsers.cpp`:

```cpp
    void parsesAccountsList() {
        const nlohmann::json j = loadFixture("accounts_list.json");
        QVERIFY(!j.is_discarded());
        const CT::Account a = parseAccountsList(j);
        QCOMPARE(a.username, QString("Ivan Petrov"));
        QCOMPARE(a.handle, QString("@ivanpetrov"));
        QCOMPARE(a.thumbnailUrl, QString("https://yt3.example/big.jpg"));
        QCOMPARE(a.channelId, QString("UCabc123def"));
        // graceful on garbage
        QVERIFY(parseAccountsList(nlohmann::json::object()).username.isEmpty());
    }
```

- [ ] **Step 3: Run — compile FAILS** (`parseAccountsList` not declared).

- [ ] **Step 4: Implement.** `src/parsers/rendererparser.h` — after `parseUserList`:

```cpp
// account/accounts_list (authed) → the active account's identity. The channel id is
// reconstructed as "UC" + offlineCacheKeyToken.clientCacheKey (the youtubei.js trick —
// the response carries no plain channelId).
CT::Account parseAccountsList(const nlohmann::json &response);
```

`src/parsers/rendererparser.cpp` — add:

```cpp
// accountItem objects sit ~7 renderer levels deep and the wrapper chain has shifted
// before (getMultiPageMenuAction vs openPopupAction); scan recursively instead of
// hard-coding the path.
static void collectAccountItems(const nlohmann::json &n, QList<const nlohmann::json *> &out) {
    if (n.is_object()) {
        if (n.contains("accountItem") && n["accountItem"].is_object())
            out << &n["accountItem"];
        for (nlohmann::json::const_iterator it = n.begin(); it != n.end(); ++it)
            collectAccountItems(it.value(), out);
    } else if (n.is_array()) {
        for (size_t i = 0; i < n.size(); ++i)
            collectAccountItems(n[i], out);
    }
}

CT::Account parseAccountsList(const nlohmann::json &response) {
    CT::Account out;
    QList<const nlohmann::json *> items;
    collectAccountItems(response, items);
    const nlohmann::json *pick = 0;
    for (int i = 0; i < items.size(); ++i)
        if (items.at(i)->value("isSelected", false)) { pick = items.at(i); break; }
    if (!pick && !items.isEmpty()) pick = items.first();
    if (!pick) return out;
    const nlohmann::json &it = *pick;
    if (it.contains("accountName")) out.username = parseText(it["accountName"]);
    if (it.contains("channelHandle")) out.handle = parseText(it["channelHandle"]);
    if (it.contains("accountPhoto") && it["accountPhoto"].contains("thumbnails")
        && it["accountPhoto"]["thumbnails"].is_array()
        && !it["accountPhoto"]["thumbnails"].empty()) {
        const nlohmann::json &ths = it["accountPhoto"]["thumbnails"];
        out.thumbnailUrl = jstr(ths[ths.size() - 1], "url");   // last = largest
    }
    if (it.contains("serviceEndpoint")
        && it["serviceEndpoint"].contains("selectActiveIdentityEndpoint")
        && it["serviceEndpoint"]["selectActiveIdentityEndpoint"].contains("supportedTokens")) {
        const nlohmann::json &tokens =
            it["serviceEndpoint"]["selectActiveIdentityEndpoint"]["supportedTokens"];
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (!tokens[i].contains("offlineCacheKeyToken")) continue;
            const QString key = jstr(tokens[i]["offlineCacheKeyToken"], "clientCacheKey");
            if (!key.isEmpty())
                out.channelId = key.startsWith("UC") ? key : "UC" + key;
        }
    }
    return out;
}
```

- [ ] **Step 5: Build + `ctest -R tst_meetube_parsers`** — PASS.

- [ ] **Step 6: Commit** — `feat(parser): accounts_list → CT::Account (name/handle/photo/channel id)`.

---

### Task 4: AccountRequest

**Files:**
- Create: `requests/accountrequest.h`, `requests/accountrequest.cpp` (under `src/`)
- Modify: `src/CMakeLists.txt` (add `requests/accountrequest.cpp` after `requests/actionrequest.cpp`)
- Test: `tests/tst_meetube_account.cpp`

**Interfaces:**
- Consumes: `parseAccountsList` (Task 3), `ServiceRequest` base (`setStatus`/`fail`/`aborted`).
- Produces: `yt::AccountRequest(ITransport*, QObject*)`, slot `list()`, signal `ready(const CT::Account&)`.

- [ ] **Step 1: Write the failing test** — in `tests/tst_meetube_account.cpp` add includes + registration + test:

```cpp
#include "requests/accountrequest.h"
```

add as the FIRST private slot of `TestAccount`:

```cpp
    void initTestCase() { qRegisterMetaType<CT::Account>("CT::Account"); }
```

add test:

```cpp
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
```

- [ ] **Step 2: Run — compile FAILS** (no `requests/accountrequest.h`).

- [ ] **Step 3: Implement.** `src/requests/accountrequest.h` (GPL header + guard):

```cpp
#ifndef ACCOUNTREQUEST_H
#define ACCOUNTREQUEST_H
#include "servicerequest.h"
#include "servicedatatypes.h"
#include "innertube/itransport.h"

namespace yt {

// The signed-in user's identity: account/accounts_list (WEB — the session Bearer
// rides in automatically). Fails when the response carries no account (signed out
// or auth expired) so the UI can fall back to the cached identity.
class AccountRequest : public ServiceRequest {
    Q_OBJECT
public:
    explicit AccountRequest(ITransport *t, QObject *parent = 0)
        : ServiceRequest(parent), m_t(t) {}
public Q_SLOTS:
    void list();
    void cancel();
Q_SIGNALS:
    void ready(const CT::Account &account);
private Q_SLOTS:
    void onFinished();
private:
    ITransport *m_t;
};

}
#endif
```

`src/requests/accountrequest.cpp`:

```cpp
#include "accountrequest.h"
#include "parsers/rendererparser.h"

namespace yt {

void AccountRequest::list() {
    setStatus(Loading);
    nlohmann::json body{ {"accountReadMask", nlohmann::json{ {"returnOwner", true} }} };
    connect(m_t->post("account/accounts_list", ClientId::WEB, body, this),
            SIGNAL(finished()), this, SLOT(onFinished()));
}

void AccountRequest::cancel() { setStatus(Canceled); }

void AccountRequest::onFinished() {
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep) return;
    const Reply r = rep->result();
    rep->deleteLater();
    if (aborted(r)) return;
    const CT::Account a = parseAccountsList(r.json);
    if (a.username.isEmpty() && a.channelId.isEmpty()) {
        fail(QString::fromLatin1("account unavailable"));
        return;
    }
    setStatus(Ready);
    emit ready(a);
}

}
```

`src/CMakeLists.txt`: add `requests/accountrequest.cpp` after `requests/actionrequest.cpp`.

- [ ] **Step 4: Build + `ctest -R tst_meetube_account`** — PASS.

- [ ] **Step 5: Commit** — `feat(api): AccountRequest — accounts_list identity fetch`.

---

### Task 5: AccountDetails + AccountApi + Innertube wiring (auth()/account())

**Files:**
- Create: `src/innertube/accountdetails.h`, `src/innertube/accountdetails.cpp`
- Create: `src/innertube/accountapi.h`, `src/innertube/accountapi.cpp`
- Modify: `src/innertube/innertube.h`, `src/innertube/innertube.cpp`, `src/CMakeLists.txt`
- Test: `tests/tst_meetube_account.cpp`

**Interfaces:**
- Consumes: `AccountRequest` (Task 4), `AccountStore::active()/updateActive()` (Task 2).
- Produces: QML `innertube.auth()` → AccountManager, `innertube.account()` → AccountApi; `AccountApi::details()` → `AccountDetails*` with props `username/handle/avatarUrl/channelId/status/errorString`, `load()`, signals `loaded()/statusChanged()`; C++ `Innertube::accountApi()`, `AccountApi::newAccountRequest()`.

- [ ] **Step 1: Write the failing tests** — in `tests/tst_meetube_account.cpp` add include + subclass + tests:

```cpp
#include "innertube/accountdetails.h"
```

after `TestAccountManager`:

```cpp
// Injects a FakeTransport-backed AccountRequest through the newRequest() seam.
class TestAccountDetails : public AccountDetails {
public:
    TestAccountDetails(ITransport *t, AccountStore *s) : AccountDetails(s), m_t(t) {}
protected:
    AccountRequest* newRequest() { return new AccountRequest(m_t, this); }
private:
    ITransport *m_t;
};
```

tests:

```cpp
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
```

- [ ] **Step 2: Run — compile FAILS** (no `innertube/accountdetails.h`).

- [ ] **Step 3: Implement.** `src/innertube/accountdetails.h`:

```cpp
#ifndef YT_ACCOUNTDETAILS_H
#define YT_ACCOUNTDETAILS_H
#include <QObject>
#include <QPointer>
#include "servicedatatypes.h"
#include "requests/accountrequest.h"

namespace yt {

class AccountStore;

// The signed-in user's identity header — plain detail object (ChannelDetails idiom).
// Seeds from the AccountStore cache at construction (instant header on relaunch),
// then load() refreshes via accounts_list and writes the result back to the store.
// On failure the cached identity is kept — the page falls back gracefully.
class AccountDetails : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString username    READ username    NOTIFY loaded)
    Q_PROPERTY(QString handle      READ handle      NOTIFY loaded)
    Q_PROPERTY(QString avatarUrl   READ avatarUrl   NOTIFY loaded)
    Q_PROPERTY(QString channelId   READ channelId   NOTIFY loaded)
    Q_PROPERTY(int     status      READ status      NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
public:
    explicit AccountDetails(AccountStore *store = 0, QObject *parent = 0);
    Q_INVOKABLE void load();
    QString username()    const { return m_account.username; }
    QString handle()      const { return m_account.handle; }
    QString avatarUrl()   const { return m_account.thumbnailUrl; }
    QString channelId()   const { return m_account.channelId; }
    int     status()      const { return m_status; }
    QString errorString() const { return m_error; }
public Q_SLOTS:
    void cancel();
Q_SIGNALS:
    void loaded();
    void statusChanged();
protected:
    virtual yt::AccountRequest* newRequest();
private Q_SLOTS:
    void onReady(const CT::Account &account);
    void onFailed(const QString &error);
private:
    yt::AccountRequest* request();
    QPointer<yt::AccountRequest> m_request;
    AccountStore *m_store;
    CT::Account m_account;
    int m_status;
    QString m_error;
};

}
#endif
```

`src/innertube/accountdetails.cpp`:

```cpp
#include "accountdetails.h"
#include "innertube/accountstore.h"
#include "innertube/innertube.h"

namespace yt {

AccountDetails::AccountDetails(AccountStore *store, QObject *parent)
    : QObject(parent), m_store(store), m_status(ServiceRequest::Null) {
    if (m_store) m_account = m_store->active();   // cached identity: instant header
}

AccountRequest* AccountDetails::newRequest() {
    Innertube *e = Innertube::instance();
    return e ? e->accountApi()->newAccountRequest() : 0;
}

AccountRequest* AccountDetails::request() {
    if (!m_request) {
        m_request = newRequest();
        if (m_request) {
            connect(m_request, SIGNAL(ready(CT::Account)), this, SLOT(onReady(CT::Account)));
            connect(m_request, SIGNAL(failed(QString)), this, SLOT(onFailed(QString)));
        }
    }
    return m_request;
}

void AccountDetails::load() {
    if (!request()) { m_error = "not supported"; m_status = ServiceRequest::Failed; emit statusChanged(); return; }
    m_status = ServiceRequest::Loading;
    emit statusChanged();
    m_request->list();
}

void AccountDetails::cancel() {
    if (m_request) m_request->cancel();
    m_status = ServiceRequest::Canceled;
    emit statusChanged();
}

void AccountDetails::onReady(const CT::Account &account) {
    m_account = account;
    if (m_store) m_store->updateActive(m_account);   // persist for the next launch
    m_status = ServiceRequest::Ready;
    emit loaded();
    emit statusChanged();
}

void AccountDetails::onFailed(const QString &error) {
    // Keep m_account — the cached header stays usable when the refresh fails.
    m_error = error;
    m_status = ServiceRequest::Failed;
    emit statusChanged();
}

}
```

`src/innertube/accountapi.h`:

```cpp
#ifndef YT_ACCOUNTAPI_H
#define YT_ACCOUNTAPI_H
#include <QObject>
#include <QPointer>

namespace yt {

class InnertubeClient;
class AccountStore;
class AccountRequest;

// The `account` node of the API tree — innertube.account(). Identity only; the
// OAuth flow itself lives on AccountManager (innertube.auth()).
class AccountApi : public QObject {
    Q_OBJECT
public:
    explicit AccountApi(InnertubeClient *client, AccountStore *store, QObject *parent = 0);

    Q_INVOKABLE QObject* details();     // AccountDetails* (cached; re-load()s per call)

    AccountRequest* newAccountRequest();

private:
    InnertubeClient *m_client;
    AccountStore *m_store;
    QPointer<QObject> m_details;   // reused AccountDetails
};

}
#endif
```

`src/innertube/accountapi.cpp`:

```cpp
#include "accountapi.h"
#include "innertube/innertubeclient.h"
#include "innertube/accountdetails.h"
#include "requests/accountrequest.h"

namespace yt {

AccountApi::AccountApi(InnertubeClient *client, AccountStore *store, QObject *parent)
    : QObject(parent), m_client(client), m_store(store) {}

AccountRequest* AccountApi::newAccountRequest() { return new AccountRequest(m_client, this); }

QObject* AccountApi::details() {
    AccountDetails *d = qobject_cast<AccountDetails *>(m_details.data());
    if (!d) { d = new AccountDetails(m_store, this); m_details = d; }
    d->load();
    return d;
}

}
```

`src/innertube/innertube.h`: add `#include "innertube/accountapi.h"` after the playlistapi include; replace the `account()` block (lines 65-66) with:

```cpp
    // OAuth manager — QML: innertube.auth().signIn()/.signedIn.
    Q_INVOKABLE QObject* auth()      { return &m_manager; }
```

extend the tree accessors:

```cpp
    VideoApi*    videoApi();
    ChannelApi*  channelApi();
    PlaylistApi* playlistApi();
    AccountApi*  accountApi();
    Q_INVOKABLE QObject* video()    { return videoApi(); }
    Q_INVOKABLE QObject* channel()  { return channelApi(); }
    Q_INVOKABLE QObject* playlist() { return playlistApi(); }
    Q_INVOKABLE QObject* account()  { return accountApi(); }
```

and the member list:

```cpp
    VideoApi    *m_video;
    ChannelApi  *m_channel;
    PlaylistApi *m_playlist;
    AccountApi  *m_accountApi;
```

`src/innertube/innertube.cpp`: ctor init list gets `, m_accountApi(0)`; after `playlistApi()`:

```cpp
AccountApi* Innertube::accountApi() {
    if (!m_accountApi) m_accountApi = new AccountApi(&m_client, &m_store, this);
    return m_accountApi;
}
```

`src/CMakeLists.txt`: add `innertube/accountdetails.cpp` and `innertube/accountapi.cpp` after `innertube/channeldetails.cpp`.

- [ ] **Step 4: Build + full `ctest`** — all 6 binaries PASS.

- [ ] **Step 5: Commit** — `feat(api): AccountDetails + AccountApi tree node; manager moves to innertube.auth()`.

---

### Task 6: VideoApi::feed() — per-browse-id model cache

The single `m_feed` QPointer means the home feed, the History carousel, and any pushed FeedPage would all re-list ONE shared VideoModel. Key the cache by browse id.

**Files:**
- Modify: `src/innertube/videoapi.h` (member `m_feed` → `m_feeds` map; add `#include <QMap>` and `#include <QString>` if missing)
- Modify: `src/innertube/videoapi.cpp:40-45`
- Test: `tests/tst_meetube_model.cpp`

**Interfaces:**
- Produces: unchanged signature `QObject* VideoApi::feed(const QString &navId)` — now one cached VideoModel PER navId.

- [ ] **Step 1: Write the failing test** — in `tests/tst_meetube_model.cpp` add includes:

```cpp
#include "innertube/videoapi.h"
#include "innertube/innertubeclient.h"
```

and the test slot (note: `feed()` triggers a list() whose request goes to the real engine seam; without an event loop no network I/O actually runs — the test only asserts identity):

```cpp
    void feedCachesPerBrowseId() {
        InnertubeClient client;
        VideoApi api(&client);
        QObject *history = api.feed("FEhistory");
        QObject *news = api.feed("FEnews_destination");
        QVERIFY(history != 0);
        QVERIFY(history != news);                  // distinct ids -> distinct models
        QCOMPARE(api.feed("FEhistory"), history);  // same id -> same cached model
    }
```

- [ ] **Step 2: Run `ctest -R tst_meetube_model`** — FAILS on `QVERIFY(history != news)`.

- [ ] **Step 3: Implement.** `src/innertube/videoapi.h`: replace `QPointer<QObject> m_feed;` with:

```cpp
    // One cached model per feed id — the home feed, the History carousel and any
    // pushed feed page must not re-list each other's model.
    QMap<QString, QPointer<QObject> > m_feeds;
```

`src/innertube/videoapi.cpp`:

```cpp
QObject* VideoApi::feed(const QString &navId) {
    VideoModel *m = qobject_cast<VideoModel *>(m_feeds.value(navId).data());
    if (!m) { m = new VideoModel(this); m_feeds.insert(navId, QPointer<QObject>(m)); }
    m->list(navId);
    return m;
}
```

- [ ] **Step 4: Build + full `ctest`** — PASS.

- [ ] **Step 5: Commit** — `fix(api): key the feed() model cache by browse id`.

---

### Task 7: FeedPage.qml (generic video list)

**REQUIRED:** invoke the `nokia-n9-qml` skill before this and every following QML task.

**Files:**
- Create: `resources/qml/pages/FeedPage.qml`
- Modify: `resources/resources.qrc` (add `<file>qml/pages/FeedPage.qml</file>` in the pages block)

**Interfaces:**
- Consumes: `innertube.video().feed(feedId)` (per-id cache from Task 6), `VideoDelegate`, `BusyOverlay`/`EmptyState`/`ListFooter`, `appWindow.stdHeaderBackground`, `headerBar.height`.
- Produces: page pushed as `pageStack.push(Qt.resolvedUrl(".../FeedPage.qml"), { pageTitle: "History", feedId: "FEhistory" })` or `{ pageTitle: t, feedModel: m }`.

- [ ] **Step 1: Write** `resources/qml/pages/FeedPage.qml`:

```qml
import QtQuick 1.1
import com.nokia.meego 1.0
import "../components/delegates"
import "../components/ui"
import "../js/UIConstants.js" as UI
import "../js/Status.js" as Status

// Generic titled video list over a VideoModel from the API tree. Push with
// { pageTitle, feedId } (feeds: FEhistory/FEsubscriptions/FElibrary/...) or hand
// in a ready model via feedModel (playlist videos).
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait

    property string pageTitle: ""
    property string feedId: ""
    property variant feedModel

    tools: feedTools

    Component.onCompleted: {
        if (!page.feedModel && page.feedId !== "")
            page.feedModel = innertube.video().feed(page.feedId);
    }

    property Component pageHeader: Component {
        Item {
            anchors.fill: parent
            Text {
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                text: page.pageTitle
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
                font.bold: true
                elide: Text.ElideRight
            }
        }
    }
    property Component pageHeaderBackground: appWindow.stdHeaderBackground

    ListView {
        id: list
        anchors {
            top: parent.top
            topMargin: headerBar.height
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        cacheBuffer: 1000
        boundsBehavior: Flickable.StopAtBounds
        model: page.feedModel
        delegate: VideoDelegate {}

        footer: ListFooter {
            hasMore: page.feedModel ? page.feedModel.canFetchMore : false
            active: page.feedModel ? (page.feedModel.status === Status.Loading) : false
        }

        onAtYEndChanged: {
            if (atYEnd && page.feedModel && page.feedModel.canFetchMore)
                page.feedModel.fetchMore();
        }

        ScrollDecorator { flickableItem: list }
    }

    BusyOverlay {
        running: page.feedModel
                 ? (page.feedModel.status === Status.Loading && page.feedModel.count === 0)
                 : false
        text: "Loading videos…"
    }

    EmptyState {
        property bool failed: page.feedModel ? (page.feedModel.status === Status.Failed) : false
        visible: page.feedModel
                 ? (page.feedModel.count === 0
                    && (page.feedModel.status === Status.Ready || failed))
                 : false
        title: failed ? "Couldn't load videos" : "Nothing here yet"
        hint: failed ? page.feedModel.errorString : ""
        showRetry: failed
        onRetry: {
            if (page.feedId !== "")
                page.feedModel = innertube.video().feed(page.feedId);
        }
    }

    ToolBarLayout {
        id: feedTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
```

- [ ] **Step 2: Validate**

Run: `python3 ~/.claude/skills/nokia-n9-qml/scripts/validate_qml.py resources/qml/pages/FeedPage.qml`
Expected: 0 ERROR.

- [ ] **Step 3: Add to qrc + build** — `make -C build-sim -j"$(nproc)" 2>&1 | tail -3` → `Built target meetube`.

- [ ] **Step 4: Commit** — `feat(ui): generic FeedPage for titled video feeds`.

---

### Task 8: NavRow + HistoryCardDelegate + PlaylistDelegate + PlaylistsPage

**Files:**
- Create: `resources/qml/components/ui/NavRow.qml`
- Create: `resources/qml/components/delegates/HistoryCardDelegate.qml`
- Create: `resources/qml/components/delegates/PlaylistDelegate.qml`
- Create: `resources/qml/pages/PlaylistsPage.qml`
- Modify: `resources/resources.qrc` (all four files)

**Interfaces:**
- Consumes: VideoModel roles (id/title/thumbnailUrl/largeThumbnailUrl/duration/username/viewCount/viewText/date/description/avatarUrl), PlaylistModel roles (id/title/thumbnailUrl/videoCount), `innertube.playlist().byChannel()/videos()`, FeedPage (Task 7).
- Produces: `NavRow { iconSource; label; signal clicked }`; `PlaylistsPage` pushed with `{ channelId }`.

- [ ] **Step 1: Write** `resources/qml/components/ui/NavRow.qml`:

```qml
import QtQuick 1.1
import "../../js/UIConstants.js" as UI

// Tappable navigation row (icon + label + drilldown chevron) with the standard
// press feedback and a bottom hairline. AccountPage's Subscriptions/Library/... rows.
Item {
    id: row

    property alias iconSource: icon.source
    property alias label: labelText.text
    signal clicked

    width: parent ? parent.width : 480
    height: UI.LIST_ITEM_HEIGHT_DEFAULT

    Rectangle {
        anchors.fill: parent
        color: UI.COLOR_INVERTED_FOREGROUND
        opacity: rowMouse.pressed ? 0.15 : 0.0
        Behavior on opacity { NumberAnimation { duration: UI.ANIM_FAST } }
    }

    Image {
        id: icon
        anchors {
            left: parent.left; leftMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
    }

    Text {
        id: labelText
        anchors {
            left: icon.right; leftMargin: UI.PADDING_XLARGE
            right: chevron.left; rightMargin: UI.PADDING_LARGE
            verticalCenter: parent.verticalCenter
        }
        color: UI.COLOR_INVERTED_FOREGROUND
        font.pixelSize: UI.FONT_LARGE
        font.family: UI.FONT_FAMILY
        elide: Text.ElideRight
    }

    Image {
        id: chevron
        source: "image://theme/icon-m-common-drilldown-arrow-inverse"
        anchors {
            right: parent.right; rightMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        onClicked: row.clicked()
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: UI.COLOR_DIVIDER
    }
}
```

If the validator reports `icon-m-common-drilldown-arrow-inverse` missing, check variants with
`find /home/iarchep/QtSDK/Simulator/Qt/gcc/harmattanthemes/blanco -name 'icon-m-common-drilldown*'`
and use the `-inverse` one that exists (fallback: `icon-m-common-drilldown-arrow` + white tint is NOT possible in QtQuick 1.1 — pick an existing white/inverse asset).

- [ ] **Step 2: Write** `resources/qml/components/delegates/HistoryCardDelegate.qml`:

```qml
import QtQuick 1.1
import "../../js/UIConstants.js" as UI

// Compact card for the AccountPage History carousel: 16:9 thumbnail + duration
// badge + a 2-line title. Pushes VideoPage with the standard videoData payload.
Item {
    id: card
    width: 220
    height: 186

    Rectangle {
        id: thumbBox
        width: 220
        height: 124
        color: "#1a1a1a"   // skeleton while the thumbnail streams in

        Image {
            anchors.fill: parent
            source: thumbnailUrl ? thumbnailUrl : ""
            fillMode: Image.PreserveAspectCrop
            clip: true
        }

        // Duration badge (bottom-right), YouTube style.
        Rectangle {
            visible: duration && duration !== ""
            anchors { right: parent.right; rightMargin: 6; bottom: parent.bottom; bottomMargin: 6 }
            width: durationText.width + 10
            height: durationText.height + 4
            radius: 2
            color: "#cc000000"
            Text {
                id: durationText
                anchors.centerIn: parent
                text: duration ? duration : ""
                color: "#ffffff"
                font.pixelSize: UI.FONT_XXSMALL
                font.family: UI.FONT_FAMILY
                font.bold: true
            }
        }
    }

    Text {
        anchors {
            top: thumbBox.bottom; topMargin: UI.PADDING_SMALL
            left: parent.left; right: parent.right
        }
        text: title ? title : ""
        color: UI.COLOR_INVERTED_FOREGROUND
        font.pixelSize: UI.FONT_SMALL
        font.family: UI.FONT_FAMILY
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    Rectangle {
        anchors.fill: parent
        color: UI.COLOR_INVERTED_FOREGROUND
        opacity: cardMouse.pressed ? 0.15 : 0.0
        Behavior on opacity { NumberAnimation { duration: UI.ANIM_FAST } }
    }

    MouseArea {
        id: cardMouse
        anchors.fill: parent
        onClicked: {
            pageStack.push(Qt.resolvedUrl("../../pages/VideoPage.qml"), {
                videoData: {
                    title: title,
                    username: username,
                    viewCount: viewCount,
                    viewText: viewText,
                    date: date,
                    duration: duration,
                    description: description,
                    thumbnailUrl: thumbnailUrl,
                    largeThumbnailUrl: largeThumbnailUrl,
                    avatarUrl: avatarUrl,
                    id: id
                }
            });
        }
    }
}
```

- [ ] **Step 3: Write** `resources/qml/components/delegates/PlaylistDelegate.qml`:

```qml
import QtQuick 1.1
import "../../js/UIConstants.js" as UI

// Playlist row: 16:9 thumbnail + video-count badge, title + count. Opens the
// playlist's videos in a FeedPage (innertube.playlist().videos(id)).
Item {
    id: root
    width: ListView.view ? ListView.view.width : 480
    height: 90 + UI.PADDING_XLARGE * 2

    Rectangle {
        id: thumbBox
        width: 160
        height: 90
        anchors {
            left: parent.left; leftMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
        color: "#1a1a1a"

        Image {
            anchors.fill: parent
            source: thumbnailUrl ? thumbnailUrl : ""
            fillMode: Image.PreserveAspectCrop
            clip: true
        }

        // Count strip on the right edge, YouTube style.
        Rectangle {
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            width: 48
            color: "#cc000000"
            Text {
                anchors.centerIn: parent
                text: videoCount ? videoCount : ""
                color: "#ffffff"
                font.pixelSize: UI.FONT_XSMALL
                font.family: UI.FONT_FAMILY
                font.bold: true
            }
        }
    }

    Column {
        anchors {
            left: thumbBox.right; leftMargin: UI.PADDING_XLARGE
            right: parent.right; rightMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
        spacing: UI.PADDING_SMALL

        Text {
            width: parent.width
            text: title ? title : ""
            color: UI.COLOR_INVERTED_FOREGROUND
            font.pixelSize: UI.FONT_LSMALL
            font.family: UI.FONT_FAMILY
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: videoCount ? videoCount + " videos" : ""
            color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
            font.pixelSize: UI.FONT_XSMALL
            font.family: UI.FONT_FAMILY
            elide: Text.ElideRight
        }
    }

    Rectangle {
        anchors.fill: parent
        color: UI.COLOR_INVERTED_FOREGROUND
        opacity: rootMouse.pressed ? 0.15 : 0.0
        Behavior on opacity { NumberAnimation { duration: UI.ANIM_FAST } }
    }

    MouseArea {
        id: rootMouse
        anchors.fill: parent
        onClicked: {
            pageStack.push(Qt.resolvedUrl("../../pages/FeedPage.qml"), {
                pageTitle: title,
                feedModel: innertube.playlist().videos(id)
            });
        }
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: UI.COLOR_DIVIDER
    }
}
```

- [ ] **Step 4: Write** `resources/qml/pages/PlaylistsPage.qml`:

```qml
import QtQuick 1.1
import com.nokia.meego 1.0
import "../components/delegates"
import "../components/ui"
import "../js/UIConstants.js" as UI
import "../js/Status.js" as Status

// The signed-in user's playlists (playlist().byChannel(own channel id)). Rows open
// each playlist's videos in a FeedPage.
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait

    property string channelId: ""
    property variant playlists

    tools: playlistTools

    Component.onCompleted: {
        if (page.channelId !== "")
            page.playlists = innertube.playlist().byChannel(page.channelId);
    }

    property Component pageHeader: Component {
        Item {
            anchors.fill: parent
            Text {
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                text: "Playlists"
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
                font.bold: true
            }
        }
    }
    property Component pageHeaderBackground: appWindow.stdHeaderBackground

    ListView {
        id: list
        anchors {
            top: parent.top
            topMargin: headerBar.height
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: page.playlists
        delegate: PlaylistDelegate {}

        footer: ListFooter {
            hasMore: page.playlists ? page.playlists.canFetchMore : false
            active: page.playlists ? (page.playlists.status === Status.Loading) : false
        }

        onAtYEndChanged: {
            if (atYEnd && page.playlists && page.playlists.canFetchMore)
                page.playlists.fetchMore();
        }

        ScrollDecorator { flickableItem: list }
    }

    BusyOverlay {
        running: page.playlists
                 ? (page.playlists.status === Status.Loading && page.playlists.count === 0)
                 : false
        text: "Loading playlists…"
    }

    EmptyState {
        property bool failed: page.playlists ? (page.playlists.status === Status.Failed) : false
        visible: page.playlists
                 ? (page.playlists.count === 0
                    && (page.playlists.status === Status.Ready || failed))
                 : false
        title: failed ? "Couldn't load playlists" : "No playlists yet"
        hint: failed ? page.playlists.errorString : ""
        showRetry: failed
        onRetry: page.playlists = innertube.playlist().byChannel(page.channelId)
    }

    ToolBarLayout {
        id: playlistTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
```

- [ ] **Step 5: Validate all four files** (0 ERROR each), add all four to `resources/resources.qrc` (delegates in the delegates block, NavRow in the ui block, page in the pages block), build.

- [ ] **Step 6: Commit** — `feat(ui): NavRow, history/playlist delegates, PlaylistsPage`.

---

### Task 9: AccountPage.qml

**Files:**
- Create: `resources/qml/pages/AccountPage.qml`
- Modify: `resources/resources.qrc`

**Interfaces:**
- Consumes: `innertube.account().details()` (Task 5), `innertube.video().feed("FEhistory")` (Task 6), `NavRow`/`HistoryCardDelegate` (Task 8), FeedPage/PlaylistsPage, `Avatar`, `innertube.auth().signOut()`.
- Produces: page pushed by `main.qml` (Task 11) and by AuthorisationSheet (Task 10).

- [ ] **Step 1: Write** `resources/qml/pages/AccountPage.qml`:

```qml
import QtQuick 1.1
import com.nokia.meego 1.0
import "../components"
import "../components/delegates"
import "../components/ui"
import "../js/UIConstants.js" as UI
import "../js/Status.js" as Status

// The YouTube mobile "You" tab adapted to N9: identity header (squircle avatar +
// name + @handle + Sign out), a horizontal History carousel, then Subscriptions /
// Library / Playlists rows. Signed-in entry point only (main.qml routes to the
// AuthorisationSheet when signed out).
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait

    tools: accountTools

    // AccountDetails (cached identity shows instantly; load() refreshes) + the
    // History feed — both C++-owned API-tree objects.
    property variant details
    property variant historyModel

    Component.onCompleted: {
        page.details = innertube.account().details();
        page.historyModel = innertube.video().feed("FEhistory");
    }

    property Component pageHeader: Component {
        Item {
            anchors.fill: parent
            Text {
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                textFormat: Text.RichText
                text: "<b>You</b>"
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
            }
        }
    }
    property Component pageHeaderBackground: appWindow.stdHeaderBackground

    Flickable {
        id: flick
        anchors {
            top: parent.top
            topMargin: headerBar.height
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        contentHeight: content.height + UI.PADDING_XLARGE
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content
            width: flick.width

            // --- Identity header: squircle avatar + name + @handle + Sign out.
            Item {
                width: parent.width
                height: 96 + UI.PADDING_XLARGE * 2

                Avatar {
                    id: avatar
                    width: 96
                    height: 96
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    source: (page.details && page.details.avatarUrl) ? page.details.avatarUrl : ""
                }

                Column {
                    anchors {
                        left: avatar.right; leftMargin: UI.PADDING_XLARGE
                        right: signOutButton.left; rightMargin: UI.PADDING_LARGE
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: UI.PADDING_SMALL

                    Text {
                        width: parent.width
                        text: (page.details && page.details.username !== "")
                              ? page.details.username : "YouTube account"
                        color: UI.COLOR_INVERTED_FOREGROUND
                        font.pixelSize: UI.FONT_XLARGE
                        font.family: UI.FONT_FAMILY
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        visible: page.details ? page.details.handle !== "" : false
                        text: page.details ? page.details.handle : ""
                        color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                        font.pixelSize: UI.FONT_LSMALL
                        font.family: UI.FONT_FAMILY
                        elide: Text.ElideRight
                    }
                }

                Button {
                    id: signOutButton
                    width: 150
                    anchors {
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    text: "Sign out"
                    onClicked: signOutDialog.open()
                }
            }

            Rectangle { width: parent.width; height: 1; color: UI.COLOR_DIVIDER }

            // --- History section header, "View all" opens the full feed.
            Item {
                width: parent.width
                height: UI.LIST_ITEM_HEIGHT_SMALL

                Text {
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    text: "History"
                    color: UI.COLOR_INVERTED_FOREGROUND
                    font.pixelSize: UI.FONT_LARGE
                    font.family: UI.FONT_FAMILY
                    font.bold: true
                }
                Text {
                    anchors {
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    text: "View all ›"
                    color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                    font.pixelSize: UI.FONT_SMALL
                    font.family: UI.FONT_FAMILY
                    opacity: viewAllMouse.pressed ? UI.OPACITY_DISABLED : UI.OPACITY_ENABLED
                }
                MouseArea {
                    id: viewAllMouse
                    anchors.fill: parent
                    onClicked: pageStack.push(Qt.resolvedUrl("FeedPage.qml"),
                                              { pageTitle: "History", feedId: "FEhistory" })
                }
            }

            // --- History carousel (collapses to a hint when empty/unavailable).
            Item {
                width: parent.width
                height: 196

                ListView {
                    id: historyList
                    anchors {
                        fill: parent
                        leftMargin: UI.DEFAULT_MARGIN
                    }
                    orientation: ListView.Horizontal
                    clip: true
                    spacing: UI.PADDING_LARGE
                    boundsBehavior: Flickable.StopAtBounds
                    model: page.historyModel
                    delegate: HistoryCardDelegate {}
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    visible: running
                    running: page.historyModel
                             ? (page.historyModel.status === Status.Loading
                                && page.historyModel.count === 0)
                             : false
                }
                Text {
                    anchors.centerIn: parent
                    visible: page.historyModel
                             ? (page.historyModel.count === 0
                                && (page.historyModel.status === Status.Ready
                                    || page.historyModel.status === Status.Failed))
                             : false
                    text: "No history yet"
                    color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                    font.pixelSize: UI.FONT_SMALL
                    font.family: UI.FONT_FAMILY
                }
            }

            Rectangle { width: parent.width; height: 1; color: UI.COLOR_DIVIDER }

            NavRow {
                iconSource: "image://theme/icon-m-content-feed-inverse"
                label: "Subscriptions"
                onClicked: pageStack.push(Qt.resolvedUrl("FeedPage.qml"),
                                          { pageTitle: "Subscriptions", feedId: "FEsubscriptions" })
            }
            NavRow {
                iconSource: "image://theme/icon-m-common-favorite-mark-inverse"
                label: "Library"
                onClicked: pageStack.push(Qt.resolvedUrl("FeedPage.qml"),
                                          { pageTitle: "Library", feedId: "FElibrary" })
            }
            NavRow {
                visible: page.details ? page.details.channelId !== "" : false
                iconSource: "image://theme/icon-m-content-playlist-inverse"
                label: "Playlists"
                onClicked: pageStack.push(Qt.resolvedUrl("PlaylistsPage.qml"),
                                          { channelId: page.details.channelId })
            }
        }
    }

    ScrollDecorator { flickableItem: flick }

    QueryDialog {
        id: signOutDialog
        titleText: "Sign out"
        message: "Sign out of MeeTube on this device?"
        acceptButtonText: "Sign out"
        rejectButtonText: "Cancel"
        onAccepted: {
            innertube.auth().signOut();
            pageStack.pop();
        }
    }

    ToolBarLayout {
        id: accountTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
```

- [ ] **Step 2: Validate (0 ERROR), add to qrc, build.**

- [ ] **Step 3: Commit** — `feat(ui): AccountPage — identity header, history carousel, feed rows`.

---

### Task 10: AuthorisationSheet.qml

**Files:**
- Create: `resources/qml/components/sheets/AuthorisationSheet.qml`
- Modify: `resources/resources.qrc`

**Interfaces:**
- Consumes: `innertube.auth()` — `signIn()/cancel()`, signals `userCodeReady(verificationUrl, userCode)`, `authenticated()`, `authFailed(error)`; `image://qr/` provider; AccountPage (Task 9).
- Produces: `AuthorisationSheet { id: authSheet }` opened by `main.qml` (Task 11).

- [ ] **Step 1: Write** `resources/qml/components/sheets/AuthorisationSheet.qml`:

```qml
import QtQuick 1.1
import com.nokia.meego 1.0
import "../../js/UIConstants.js" as UI

// OAuth TV device-code sign-in (innertube.auth()): requests a code, shows it plus
// a QR of the verification URL, then waits while the manager polls for approval.
// Auto-closes and opens AccountPage once authenticated.
Sheet {
    id: sheet

    // "request" (fetching a code) | "wait" (user enters it elsewhere) | "error"
    property string phase: "request"
    property string userCode: ""
    property string verificationUrl: ""
    property string errorText: ""

    rejectButtonText: "Cancel"
    onRejected: innertube.auth().cancel()

    onStatusChanged: {
        if (status === DialogStatus.Opening)
            sheet.begin();
    }

    function begin() {
        sheet.phase = "request";
        sheet.userCode = "";
        sheet.verificationUrl = "";
        sheet.errorText = "";
        innertube.auth().signIn();
    }

    Connections {
        target: innertube.auth()
        onUserCodeReady: {
            sheet.verificationUrl = verificationUrl;
            sheet.userCode = userCode;
            sheet.phase = "wait";
        }
        onAuthenticated: {
            sheet.close();
            pageStack.push(Qt.resolvedUrl("../../pages/AccountPage.qml"));
        }
        onAuthFailed: {
            sheet.errorText = error;
            sheet.phase = "error";
        }
    }

    content: Item {
        anchors.fill: parent

        // --- phase: request
        Column {
            visible: sheet.phase === "request"
            anchors.centerIn: parent
            spacing: UI.PADDING_XLARGE

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: sheet.phase === "request"
                platformStyle: BusyIndicatorStyle { size: "large" }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Requesting code…"
                color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                font.pixelSize: UI.FONT_LARGE
                font.family: UI.FONT_FAMILY
            }
        }

        // --- phase: wait (code + QR)
        Column {
            visible: sheet.phase === "wait"
            anchors {
                top: parent.top; topMargin: UI.PADDING_XXLARGE
                left: parent.left
                right: parent.right
            }
            spacing: UI.PADDING_XLARGE

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Sign in with YouTube"
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
                font.bold: true
            }
            Text {
                width: parent.width - UI.DEFAULT_MARGIN * 2
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
                text: "On your phone or computer, open youtube.com/activate and enter this code:"
                color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                font.pixelSize: UI.FONT_SMALL
                font.family: UI.FONT_FAMILY
                wrapMode: Text.WordWrap
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: sheet.userCode
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: 64
                font.family: UI.FONT_FAMILY
                font.bold: true
                font.letterSpacing: 4
            }
            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 220
                height: 220
                sourceSize.width: 220
                source: sheet.verificationUrl !== ""
                        ? "image://qr/" + encodeURIComponent(sheet.verificationUrl)
                        : ""
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: UI.PADDING_LARGE

                BusyIndicator {
                    anchors.verticalCenter: parent.verticalCenter
                    running: sheet.phase === "wait"
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Waiting for confirmation…"
                    color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                    font.pixelSize: UI.FONT_SMALL
                    font.family: UI.FONT_FAMILY
                }
            }
        }

        // --- phase: error
        Column {
            visible: sheet.phase === "error"
            anchors.centerIn: parent
            spacing: UI.PADDING_XLARGE

            Text {
                width: sheet.width - UI.DEFAULT_MARGIN * 2
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
                text: "Sign-in failed"
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
                font.bold: true
            }
            Text {
                width: sheet.width - UI.DEFAULT_MARGIN * 2
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
                text: sheet.errorText
                color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                font.pixelSize: UI.FONT_SMALL
                font.family: UI.FONT_FAMILY
                wrapMode: Text.WordWrap
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Retry"
                onClicked: sheet.begin()
            }
        }
    }
}
```

Note: if the validator flags `BusyIndicatorStyle` (needs `import com.nokia.meego 1.0`, already present) leave it; if it flags `font.letterSpacing` as unknown in QtQuick 1.1, drop that line.

- [ ] **Step 2: Validate (0 ERROR), add to qrc, build.**

- [ ] **Step 3: Commit** — `feat(ui): AuthorisationSheet — device-code + QR sign-in flow`.

---

### Task 11: Entry point — toolbar ToolIcon + openAccount()

**Files:**
- Modify: `resources/qml/main.qml` (openAccount + AuthorisationSheet instance + sheets import)
- Modify: `resources/qml/pages/MainPage.qml:109-115` (second ToolIcon)

**Interfaces:**
- Consumes: `innertube.auth().signedIn` (Task 1), AuthorisationSheet (Task 10), AccountPage (Task 9).

- [ ] **Step 1: Implement.** `resources/qml/main.qml` — add to the imports:

```qml
import "components/sheets"
```

after the `openCategoryDialog()` function:

```qml
    // Account entry point (person icon in the main toolbar): straight to the
    // account page when signed in, otherwise the device-code sign-in sheet.
    function openAccount() {
        if (innertube.auth().signedIn)
            pageStack.push(Qt.resolvedUrl("pages/AccountPage.qml"));
        else
            authSheet.open();
    }
```

after the `SelectionDialog { ... }` block:

```qml
    AuthorisationSheet {
        id: authSheet
    }
```

`resources/qml/pages/MainPage.qml` — extend the ToolBarLayout:

```qml
    ToolBarLayout {
        id: mainTools
        ToolIcon {
            iconId: "toolbar-view-menu"
            onClicked: mainMenu.open()
        }
        ToolIcon {
            iconId: "toolbar-contact"
            onClicked: appWindow.openAccount()
        }
    }
```

- [ ] **Step 2: Validate both files (0 ERROR), build, run full ctest** — all PASS.

- [ ] **Step 3: Commit** — `feat(ui): account ToolIcon — sheet when signed out, AccountPage when signed in`.

---

### Task 12: Simulator verification + screenshots

The headless smoke covers the home page only — verify the new surfaces visually.

- [ ] **Step 1: Signed-out flow.** `source simulator_env.sh && build-sim/meetube` — screenshot (python-xlib `get_image` on the Qt Simulator window): home page shows the person icon on the right of the toolbar. Tap it → AuthorisationSheet opens (real device code from Google, or the error state + Retry when offline — both are valid outcomes). Screenshot the sheet.

- [ ] **Step 2: Signed-in page (cached identity, no real login needed).** Pre-fill the store, run, screenshot AccountPage:

```bash
mkdir -p ~/.config/MeeTube && cat > ~/.config/MeeTube/MeeTube.conf <<'EOF'
[accounts]
active=default
default/username=Ivan Petrov
default/handle=@ivanpetrov
default/channelId=UC123
default/refreshToken=FAKE
EOF
```

Launch, tap the person icon → AccountPage: header shows "Ivan Petrov" + @ivanpetrov + placeholder squircle; History/feeds show empty/failed states (the fake token can't authenticate — expected). Verify the Playlists row is visible (channelId set), Sign out → QueryDialog → returns to home; tapping the person icon again now opens the sheet. Screenshot each. Clean up: `rm ~/.config/MeeTube/MeeTube.conf`.

- [ ] **Step 3: Real-login probe (requires the user).** The FElibrary/FEhistory parse check needs a real signed-in account — ask the user to complete the QR login once and report/screenshot what History, Subscriptions and Library return. If FElibrary comes back shelf-shaped (empty model on a non-empty library), file it as the known follow-up from the spec.

- [ ] **Step 4: Commit any fixes; final full `ctest` + validate pass.**

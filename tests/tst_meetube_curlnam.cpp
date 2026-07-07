#include <QtTest/QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QSignalSpy>
#include "net/curlnetworkaccessmanager.h"

// Minimal one-shot HTTP server: accepts a connection, reads the request, replies
// with a fixed 200 body, then records the raw request for POST/header assertions.
class LoopServer : public QObject {
    Q_OBJECT
public:
    LoopServer() : status(200), m_sock(0) {
        m_srv.listen(QHostAddress::LocalHost, 0);
        connect(&m_srv, SIGNAL(newConnection()), this, SLOT(onConn()));
    }
    int port() const { return m_srv.serverPort(); }
    QByteArray lastRequest;
    QByteArray responseBody;    // set by the test
    QByteArray extraHeaders;    // e.g. "X-Foo: bar\r\n"
    int status;
private slots:
    void onConn() {
        QTcpSocket *s = m_srv.nextPendingConnection();
        connect(s, SIGNAL(readyRead()), this, SLOT(onData()));
        m_sock = s;
    }
    void onData() {
        lastRequest += m_sock->readAll();
        if (!lastRequest.contains("\r\n\r\n")) return;   // headers not complete
        QByteArray body = responseBody;
        QByteArray resp = "HTTP/1.1 " + QByteArray::number(status) + " OK\r\n"
                          "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                          + extraHeaders + "\r\n" + body;
        m_sock->write(resp);
        m_sock->flush();
    }
private:
    QTcpServer m_srv;
    QTcpSocket *m_sock;
};

class tst_meetube_curlnam : public QObject {
    Q_OBJECT
private slots:
    void getReturnsBodyAndStatus() {
        LoopServer srv;
        srv.status = 200;
        srv.responseBody = "{\"dislikes\":42}";
        yt::net::CurlNetworkAccessManager nam;
        QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1/votes").arg(srv.port())));
        QNetworkReply *r = nam.get(req);
        QEventLoop loop;
        connect(r, SIGNAL(finished()), &loop, SLOT(quit()));
        QTimer::singleShot(5000, &loop, SLOT(quit()));
        loop.exec();
        QCOMPARE(r->error(), QNetworkReply::NoError);
        QCOMPARE(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
        QCOMPARE(r->readAll(), QByteArray("{\"dislikes\":42}"));
    }

    // A file:// URL must be refused by CURLOPT_PROTOCOLS (Fix 3: SSRF/local-file guard),
    // AND that instant failure must still deliver finished() on a later event-loop turn
    // rather than during the ctor (Fix 4: async initial kick). If finished() were lost we
    // would fall through to the 5 s fallback and then see NoError / non-empty body.
    void blockedSchemeFailsGracefully() {
        yt::net::CurlNetworkAccessManager nam;
        QNetworkReply *r = nam.get(QNetworkRequest(QUrl("file:///etc/hostname")));
        QEventLoop loop;
        connect(r, SIGNAL(finished()), &loop, SLOT(quit()));
        QTimer::singleShot(5000, &loop, SLOT(quit()));
        loop.exec();
        QVERIFY(r->error() != QNetworkReply::NoError);   // scheme blocked -> curl error
        QVERIFY(r->readAll().isEmpty());                 // no bytes leaked from disk
    }

    // POST must reach the wire as a POST with the caller's body and Content-Type
    // (the youtubei/v1 endpoints are POST-only JSON): the reply ctor drains
    // outgoingData into m_post and passes it via CURLOPT_POSTFIELDS, and the
    // request's raw headers become CURLOPT_HTTPHEADER lines.
    void postSendsBodyAndContentType() {
        LoopServer srv;
        srv.responseBody = "ok";
        yt::net::CurlNetworkAccessManager nam;
        QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1/youtubei/v1/browse").arg(srv.port())));
        req.setRawHeader("Content-Type", "application/json");
        QNetworkReply *r = nam.post(req, QByteArray("{\"context\":{}}"));
        QEventLoop loop;
        connect(r, SIGNAL(finished()), &loop, SLOT(quit()));
        QTimer::singleShot(5000, &loop, SLOT(quit()));
        loop.exec();
        QCOMPARE(r->error(), QNetworkReply::NoError);
        QVERIFY(srv.lastRequest.startsWith("POST /youtubei/v1/browse"));
        QVERIFY(srv.lastRequest.contains("Content-Type: application/json"));
        QVERIFY(srv.lastRequest.contains("{\"context\":{}}"));
    }

    // Response headers curl parses (via headerCb -> setRawHeader) must be readable
    // through QNetworkReply::rawHeader() — the envelope scan pulls X-Goog-Visitor-Id
    // off exactly this path.
    void exposesResponseHeaders() {
        LoopServer srv;
        srv.responseBody = "x";
        srv.extraHeaders = "X-Goog-Visitor-Id: abc123\r\n";
        yt::net::CurlNetworkAccessManager nam;
        QNetworkReply *r = nam.get(QNetworkRequest(QUrl(QString("http://127.0.0.1:%1/").arg(srv.port()))));
        QEventLoop loop;
        connect(r, SIGNAL(finished()), &loop, SLOT(quit()));
        QTimer::singleShot(5000, &loop, SLOT(quit()));
        loop.exec();
        QCOMPARE(r->rawHeader("X-Goog-Visitor-Id"), QByteArray("abc123"));
    }

    // A failed TCP connect (nothing listening on port 1) must surface as a non-NoError
    // QNetworkReply error, not a silent empty-body success (mapCurl: CURLE_COULDNT_CONNECT
    // -> ConnectionRefusedError).
    void connectionRefusedIsAnError() {
        yt::net::CurlNetworkAccessManager nam;
        // Port 1 is not listening -> curl fails to connect.
        QNetworkReply *r = nam.get(QNetworkRequest(QUrl("http://127.0.0.1:1/")));
        QEventLoop loop;
        connect(r, SIGNAL(finished()), &loop, SLOT(quit()));
        QTimer::singleShot(5000, &loop, SLOT(quit()));
        loop.exec();
        QVERIFY(r->error() != QNetworkReply::NoError);
    }

    // abort() (called before the deferred first kick has driven the handle) must set
    // OperationCanceledError and emit finished() exactly once, synchronously — the
    // JobToken/dtor teardown path relies on that single terminal signal.
    void abortFinishesWithCancelError() {
        LoopServer srv;
        srv.responseBody = "later";
        yt::net::CurlNetworkAccessManager nam;
        QNetworkReply *r = nam.get(QNetworkRequest(QUrl(QString("http://127.0.0.1:%1/").arg(srv.port()))));
        QSignalSpy spy(r, SIGNAL(finished()));
        r->abort();
        QCOMPARE(r->error(), QNetworkReply::OperationCanceledError);
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(tst_meetube_curlnam)
#include "tst_meetube_curlnam.moc"

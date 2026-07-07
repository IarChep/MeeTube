#include <QtTest/QtTest>
#include <curl/curl.h>

class tst_meetube_curlnam : public QObject {
    Q_OBJECT
private slots:
    void curlLinks() {
        const char *v = curl_version();
        QVERIFY(v != 0);
        QVERIFY(QByteArray(v).contains("curl"));
    }
};

QTEST_MAIN(tst_meetube_curlnam)
#include "tst_meetube_curlnam.moc"

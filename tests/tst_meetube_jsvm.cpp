#include <QtTest/QtTest>
#include "jsc/jsvm.h"

using namespace yt::jsc;

class TestJsVm : public QObject { Q_OBJECT
private slots:
    void evalsArithmetic() {
        JsVm vm; QVERIFY(vm.ok());
        std::optional<std::string> r = vm.evalToString("1+1");
        QVERIFY(r.has_value());
        QCOMPARE(QString::fromStdString(*r), QString("2"));
    }
    void definesThenCallsFunction() {
        JsVm vm;
        QVERIFY(vm.evalToString("var f=function(a){return a.split('').reverse().join('')};").has_value());
        std::optional<std::string> r = vm.evalToString("f('abc')");
        QVERIFY(r.has_value());
        QCOMPARE(QString::fromStdString(*r), QString("cba"));
    }
    void syntaxErrorReturnsNullopt() {
        JsVm vm;
        QVERIFY(!vm.evalToString("this is not js {{{").has_value());
    }
    void infiniteLoopIsInterrupted() {
        JsVm vm;
        // Must not hang the test: the interrupt handler kills it after the budget.
        std::optional<std::string> r = vm.evalToString("for(;;){}");
        QVERIFY(!r.has_value());
    }
};
QTEST_MAIN(TestJsVm)
#include "tst_meetube_jsvm.moc"

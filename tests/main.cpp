// =============================================================================
// main.cpp — 测试入口：依次运行全部测试类，任一失败返回非零退出码
// 每个测试类输出到独立文件 run_<类名>.txt（避免互相覆盖），同时输出到 stdout
// =============================================================================
#include <QCoreApplication>
#include <QtTest/QtTest>
#include <QByteArray>
#include <QVector>
#include "core/models.h"
#include "tst_runners.h"

namespace {

struct TestArgs {
    QVector<QByteArray> bytes;
    QVector<char *> argv;
};

// 为单个测试类构造参数: 输出到独立文件
TestArgs makeArgs(const char *tag)
{
    TestArgs ta;
    auto push = [&ta](const QString &s) {
        ta.bytes.append(s.toLocal8Bit());
        ta.argv.append(ta.bytes.last().data());
    };
    push("tst_xgz");
    push("-o");
    push(QString("run_%1.txt,txt").arg(tag));
    push("-silent");
    return ta;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // 跨线程队列投递自定义类型（与主程序 main.cpp 注册一致）
    qRegisterMetaType<QVector<PassEvent>>("QVector<PassEvent>");
    qRegisterMetaType<PassEvent>("PassEvent");

    struct Runner { const char *tag; int (*fn)(int, char *[]); };
    const Runner runners[] = {
        { "tleparser",      runTstTleParser },
        { "tleblocks",      runTstTleBlocks },
        { "coordtransform", runTstCoordTransform },
        { "sunposition",    runTstSunPosition },
        { "nutation",       runTstNutation },
        { "earthshadow",    runTstEarthShadow },
        { "propagator",     runTstPropagator },
        { "orbitcalc",      runTstOrbitCalc },
        { "magnitude",      runTstMagnitude },
        { "doppler",        runTstDoppler },
        { "errorhandler",   runTstErrorHandler },
        { "passworker",     runTstPassWorker },
        { "passservice",    runTstPassService },
    };

    int failures = 0;
    for (const Runner &r : runners) {
        TestArgs a = makeArgs(r.tag);
        failures += r.fn(a.argv.size(), a.argv.data());
    }

    qInfo("==================================================");
    qInfo("TOTAL TEST FAILURES: %d", failures);
    return failures ? 1 : 0;
}

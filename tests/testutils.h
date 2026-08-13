#ifndef TESTS_TESTUTILS_H
#define TESTS_TESTUTILS_H
// =============================================================================
// testutils.h — 测试共享工具（数据文件定位 + 儒略日计算）
// =============================================================================
#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <cstdint>

// 在多个候选路径中查找测试数据文件（覆盖从 build 目录/源码目录/任意 cwd 运行）
inline QString findTestData(const QString &rel)
{
    const QStringList roots = {
        QCoreApplication::applicationDirPath() + "/data",
        QCoreApplication::applicationDirPath() + "/../data",
        QCoreApplication::applicationDirPath() + "/../../data",
        QDir::currentPath() + "/data",
        QDir::currentPath() + "/../data",
    };
    for (const QString &r : roots) {
        const QFileInfo fi(r + "/" + rel);
        if (fi.exists()) return fi.absoluteFilePath();
    }
    return QString();
}

// 测试数据目录（存在则返回其绝对路径，否则用 cwd/data 兜底）
inline QString testDataDir()
{
    const QString rel = findTestData("SGP4-VER.TLE");
    if (rel.isEmpty()) return QDir::currentPath() + "/data";
    return QFileInfo(rel).absolutePath();
}

// 儒略日 → UTC 毫秒（与 OrbitCalculator::jdToIntMs 同语义, 截断取整）
inline int64_t jdToMs(double jd)
{
    return static_cast<int64_t>(jd * 86400000.0);
}

// UTC 年月日时 → 儒略日（Hinnant days_from_civil 算法）
inline double jdFromUtc(int y, int mo, int d, int h = 0, int mi = 0, double s = 0.0)
{
    y -= (mo <= 2) ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const qint64 days = static_cast<qint64>(era) * 146097
                      + static_cast<qint64>(doe) - 719468;
    return 2440587.5 + days + (h * 3600 + mi * 60 + s) / 86400.0;
}

#endif // TESTS_TESTUTILS_H

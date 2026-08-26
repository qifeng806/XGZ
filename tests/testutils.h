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
#include <QtGlobal>
#include <cmath>
#include <limits>
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
    // 检查输入值的有效性
    if (!std::isfinite(jd)) {
        return 0;
    }
    
    // 检查极端值，防止溢出
    // int64_t 的范围大约是 ±9.2e18，86400000.0 * jd 必须在这个范围内
    const double maxSafeJd = 9.2e18 / 86400000.0;
    const double minSafeJd = -9.2e18 / 86400000.0;
    
    if (jd > maxSafeJd) {
        return std::numeric_limits<int64_t>::max();
    }
    if (jd < minSafeJd) {
        return std::numeric_limits<int64_t>::min();
    }
    
    // 使用更精确的转换算法
    // 将 jd 分解为整数和小数部分，分别处理以减少精度损失
    int64_t jdInt = static_cast<int64_t>(std::floor(jd));
    double jdFrac = jd - jdInt;
    
    // 计算整数部分和小数部分的毫秒数
    int64_t intMs = jdInt * 86400000LL;
    int64_t fracMs = static_cast<int64_t>(std::round(jdFrac * 86400000.0));
    
    // 处理四舍五入导致的进位（归一后 fracMs ∈ [0, 86400000)）
    if (fracMs >= 86400000LL) {
        intMs += 86400000LL;
        fracMs -= 86400000LL;
    } else if (fracMs < 0) {
        intMs -= 86400000LL;
        fracMs += 86400000LL;
    }

    return intMs + fracMs;
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

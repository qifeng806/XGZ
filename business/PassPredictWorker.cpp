// =============================================================================
// PassPredictWorker.cpp — Worker 实现（集成 perturb 库版本）
// =============================================================================
#include "PassPredictWorker.h"
#include "core/TleParser.h"
#include "core/Propagator.h"
#include "core/CoordTransform.h"
#include "core/Observer.h"
#include "core/OrbitCalculator.h"
#include <cmath>

PassPredictWorker::PassPredictWorker(QObject *parent)
    : QObject(parent) {}

void PassPredictWorker::run(Job job)
{
    m_cancelled = false;

    // ---- Step 1: 解析 TLE（获取卫星名称和轨道信息） ----
    TleParser::ParseError parseErr;
    std::optional<TleData> tleOpt;
    if (!job.tleLine0.isEmpty()) {
        tleOpt = TleParser::parse(
            job.tleLine0.toStdString(),
            job.tleLine1.toStdString(),
            job.tleLine2.toStdString(), &parseErr);
    } else {
        tleOpt = TleParser::parse(
            job.tleLine1.toStdString(),
            job.tleLine2.toStdString(), &parseErr);
    }

    if (!tleOpt) {
        QString msg;
        switch (parseErr.code) {
        case TleParser::ParseError::BAD_LENGTH:     msg = "TLE 行长度不足"; break;
        case TleParser::ParseError::BAD_LINE_NUMBER: msg = "行号前缀错误"; break;
        case TleParser::ParseError::BAD_NORAD:       msg = "无效的 NORAD 编号"; break;
        case TleParser::ParseError::BAD_EPOCH:       msg = "无效的历元格式"; break;
        case TleParser::ParseError::BAD_ELEMENTS:    msg = "轨道根数越界"; break;
        case TleParser::ParseError::BAD_BS:          msg = "B* 格式错误"; break;
        default:                                     msg = "未知解析错误"; break;
        }
        emit errorOccurred(msg);
        return;
    }

    const auto &tle = *tleOpt;

    // ---- Step 2: 初始化 Propagator（封装 perturb::Satellite） ----
    Propagator prop;
    std::string errMsg;
    bool ok = prop.initialize(
        job.tleLine1.toStdString(),
        job.tleLine2.toStdString(), errMsg);

    if (!ok) {
        emit errorOccurred(QString::fromStdString(errMsg));
        return;
    }

    // 设置卫星名称
    prop.setSatelliteName(tle.name);

    // ---- Step 3: 构建传播 Lambda ----
    auto propagateFn = [&prop, this](double jd) -> std::optional<EciState> {
        if (m_cancelled) return std::nullopt;
        return prop.propagate(jd);
    };

    // ---- Step 4: 创建 Observer ----
    Observer obs(job.observerLatDeg, job.observerLonDeg,
                 job.observerAltKm, job.minElevDeg);

    // ---- Step 5: 创建 OrbitCalculator 并执行 ----
    OrbitCalculator calc(job.calcCfg);
    calc.setSatelliteName(tle.name);  // 设置卫星名称到引擎

    // QDateTime(UTC) → double JD
    double jdStart = (job.startUtc.toMSecsSinceEpoch() / 86400000.0) + 2440587.5;

    auto passes = calc.predict(propagateFn, obs, jdStart, job.hoursAhead, &m_cancelled);

    if (m_cancelled) return;

    // ---- Step 6: 更新状态栏信息（通过信号传递） ----
    // 将模型信息附加到第一条过境的卫星名称中
    QString modelStr;
    switch (tle.ephemerisType) {
    case Sgp4Type::LEO_SGP4:    modelStr = "SGP4"; break;
    case Sgp4Type::MEO_SDP4:    modelStr = "SDP4"; break;
    case Sgp4Type::GEO_SDP4_DS: modelStr = "SDP4(DS)"; break;
    }
    if (!passes.empty() && !modelStr.isEmpty()) {
        std::string suffix = " [" + modelStr.toStdString() + "]";
        // 只在卫星名称不含模型信息时才追加
        for (auto &p : passes) {
            if (p.satelliteName.find(" [") == std::string::npos) {
                p.satelliteName += suffix;
            }
        }
    }

    emit resultReady(QVector<PassEvent>(passes.begin(), passes.end()));
}

void PassPredictWorker::cancel()
{
    m_cancelled = true;
}

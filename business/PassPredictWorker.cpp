// =============================================================================
// PassPredictWorker.cpp — Worker 实现（集成 perturb 库版本）
// =============================================================================
#include "business/PassPredictWorker.h"
#include "core/TleParser.h"
#include "core/Propagator.h"
#include "core/CoordTransform.h"
#include "core/Observer.h"
#include "core/OrbitCalculator.h"
#include "core/ErrorHandler.h"
#include <QTimeZone>
#include <cmath>
#include <chrono>
#include <QDebug>

PassPredictWorker::PassPredictWorker(QObject *parent)
    : QObject(parent), m_cancelFlag(makeAtomicCancelFlag()) {}

void PassPredictWorker::run(Job job)
{
    runJobs({job});
}

void PassPredictWorker::runJobs(const QVector<Job> &jobs)
{
    // 注意: worker 每次任务新建且仅 run 一次，不得在此复位 m_cancelFlag ——
    // 否则"启动前已被 cancel() 置位"的取消请求会被冲掉（取消失效竞态）
    for (int i = 0; i < jobs.size(); ++i) {
        const auto &job = jobs[i];
        if (m_cancelFlag->isCancelled()) break;   // 取消: 剩余切片任务不再执行（调用方已放弃等待）
        
        try {
            runOneJob(job);
        } catch (const std::exception &e) {
            // 防御: 子线程未捕获异常会导致 std::terminate 整个进程
            QString satelliteId = job.tleLine0.isEmpty() ? job.tleLine1.left(10) : job.tleLine0;
            QString errorMsg = QString("卫星 %1 计算异常 (任务 %2/%3): %4")
                .arg(satelliteId)
                .arg(i + 1)
                .arg(jobs.size())
                .arg(QString::fromUtf8(e.what()));
            
            // 添加上下文信息
            if (!job.tleLine0.isEmpty()) {
                errorMsg += QString("\n卫星名称: %1").arg(job.tleLine0);
            }
            errorMsg += QString("\n观测站位置: 纬度 %.6f°, 经度 %.6f°, 高度 %.3f km")
                .arg(job.observerLatDeg)
                .arg(job.observerLonDeg)
                .arg(job.observerAltKm);
            
            emit errorOccurred(errorMsg);
        } catch (...) {
            QString satelliteId = job.tleLine0.isEmpty() ? job.tleLine1.left(10) : job.tleLine0;
            QString errorMsg = QString("卫星 %1 计算异常（未知错误） (任务 %2/%3)")
                .arg(satelliteId)
                .arg(i + 1)
                .arg(jobs.size());
            
            emit errorOccurred(errorMsg);
        }
    }
}

void PassPredictWorker::runOneJob(const Job &job)
{
    // ---- Step 1: 解析 TLE（获取卫星名称和轨道信息） ----
    TleParser::ParseError parseErr;
    std::optional<TleData> tleOpt;
    
    // 记录解析开始时间，用于错误上下文
    auto parseStartTime = std::chrono::steady_clock::now();
    
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
        // 计算解析耗时
        auto parseDuration = std::chrono::steady_clock::now() - parseStartTime;
        auto parseMs = std::chrono::duration_cast<std::chrono::milliseconds>(parseDuration).count();
        
        // 使用错误处理系统记录TLE解析错误
        QString satelliteId = job.tleLine0.isEmpty() ? job.tleLine1.left(10) : job.tleLine0;
        QString details;
        QString suggestion;
        
        switch (parseErr.code) {
        case TleParser::ParseError::BAD_LENGTH: {
            details = QString("TLE行长度不足: 行1 %1字符, 行2 %2字符 (期望至少69字符)")
                .arg(job.tleLine1.length())
                .arg(job.tleLine2.length());
            suggestion = "请确保TLE数据完整，每行至少69字符";
            break;
        }
        case TleParser::ParseError::BAD_LINE_NUMBER: {
            QString line1Prefix = QString(job.tleLine1.mid(0, 1));
            QString line2Prefix = QString(job.tleLine2.mid(0, 1));
            details = QString("行号前缀错误: 行1 '%1', 行2 '%2' (期望行1='1', 行2='2')")
                .arg(line1Prefix)
                .arg(line2Prefix);
            suggestion = "请检查TLE格式，确保行号正确";
            break;
        }
        case TleParser::ParseError::CHECKSUM_FAILED: {
            bool line1Valid = TleParser::verifyChecksum(job.tleLine1.toStdString());
            bool line2Valid = TleParser::verifyChecksum(job.tleLine2.toStdString());
            details = QString("校验和错误: 行1 %1, 行2 %2").arg(line1Valid ? "通过" : "失败").arg(line2Valid ? "通过" : "失败");
            suggestion = "校验和错误可能影响计算精度，建议使用正确的TLE数据";
            break;
        }
        case TleParser::ParseError::BAD_NORAD: {
            QString noradStr = job.tleLine1.mid(2, 5);
            details = QString("无效的NORAD编号: '%1'").arg(noradStr);
            suggestion = "请检查NORAD编号格式，应为5位数字";
            break;
        }
        case TleParser::ParseError::BAD_EPOCH: {
            QString epochStr = job.tleLine1.mid(18, 14);
            details = QString("无效的历元格式: '%1'").arg(epochStr);
            suggestion = "请检查历元时间格式，应为YYDDD.DDDDDDDD格式";
            break;
        }
        case TleParser::ParseError::BAD_ELEMENTS: {
            details = "轨道根数越界，可能原因：倾角不在[0°,180°]范围内，偏心率≥1，或平运动非正数";
            suggestion = "请检查轨道参数是否在合理范围内";
            break;
        }
        case TleParser::ParseError::BAD_BS: {
            QString bstarStr = job.tleLine1.mid(53, 8);
            details = QString("B*格式错误: '%1'").arg(bstarStr);
            suggestion = "请检查B*阻力项格式，应为SmmmmmmEE格式";
            break;
        }
        case TleParser::ParseError::DECAYED: {
            details = "卫星已再入大气层，轨道已失效";
            suggestion = "请使用更新的TLE数据，或检查卫星状态";
            break;
        }
        default: {
            details = QString("未知解析错误，错误代码: %1").arg(static_cast<int>(parseErr.code));
            suggestion = "请检查TLE格式或联系技术支持";
            break;
        }
        }
        
        // 使用错误处理系统
        ErrorInfo errorInfo = ErrorHandler::createTleParseError(
            satelliteId, details, suggestion);
        errorInfo.detailedContext = QString("解析耗时: %1 ms\n观测站: 纬度 %.6f°, 经度 %.6f°, 高度 %.3f km")
            .arg(parseMs)
            .arg(job.observerLatDeg)
            .arg(job.observerLonDeg)
            .arg(job.observerAltKm);
        
        // 记录错误并尝试恢复
        bool recoverySuccess = ErrorHandler::instance().handleError(errorInfo);
        
        // 恢复成功: 自动跳过该任务（如 WARNING 级校验和错误）——仍须通知调度层:
        // 批量 completed 计数靠信号驱动, 缺失会让 resultReady 永不发出（悬挂）;
        // 单星模式 UI 也会永久停留在"计算中"
        if (recoverySuccess) {
            emit jobSkipped(satelliteId, details);
            return;
        }
        // 恢复失败，发送错误信号
        emit errorOccurred(errorInfo.message + "\n" + errorInfo.detailedContext + "\n" + errorInfo.suggestedAction);
        
        return;
    }

    const auto &tle = *tleOpt;
    
    // 记录解析成功信息
    #ifdef QT_DEBUG
    auto parseDuration = std::chrono::steady_clock::now() - parseStartTime;
    auto parseMs = std::chrono::duration_cast<std::chrono::milliseconds>(parseDuration).count();
    
    // 输出解析成功的调试信息（仅在调试模式下）
    qDebug() << QString("TLE解析成功: %1, NORAD: %2, 解析耗时: %3 ms")
        .arg(QString::fromStdString(tle.name))
        .arg(tle.noradID)
        .arg(parseMs);
    #endif

    // ---- Step 2: 初始化 Propagator（封装 perturb::Satellite） ----
    Propagator prop;
    std::string errMsg;
    
    // 记录初始化开始时间
    auto initStartTime = std::chrono::steady_clock::now();
    
    bool ok = prop.initialize(
        job.tleLine1.toStdString(),
        job.tleLine2.toStdString(), errMsg);

    if (!ok) {
        // 计算初始化耗时
        auto initDuration = std::chrono::steady_clock::now() - initStartTime;
        auto initMs = std::chrono::duration_cast<std::chrono::milliseconds>(initDuration).count();
        
        QString satelliteId = QString::fromStdString(tle.name);
        
        // 使用错误处理系统记录Propagator错误
        QString details = QString("Propagator初始化失败: %1").arg(QString::fromStdString(errMsg));
        QString suggestion;
        
        if (errMsg.find("TLE 行长度不足") != std::string::npos) {
            suggestion = "确保TLE数据完整（每行至少69字符）";
        } else if (errMsg.find("TLE 格式无效") != std::string::npos) {
            suggestion = "检查TLE格式是否符合规范";
        } else if (errMsg.find("卫星已衰减") != std::string::npos) {
            suggestion = "使用更新的TLE数据，或检查卫星状态";
        } else if (errMsg.find("历元数据无效") != std::string::npos) {
            suggestion = "检查历元时间格式";
        } else if (errMsg.find("奇异状态") != std::string::npos) {
            suggestion = "检查轨道参数，或尝试不同的传播算法";
        } else {
            suggestion = "检查输入数据或联系技术支持";
        }
        
        ErrorInfo errorInfo = ErrorHandler::createPropagatorError(
            satelliteId, details, suggestion);
        errorInfo.detailedContext = QString("初始化耗时: %1 ms\n模型类型: %2\n观测站: 纬度 %.6f°, 经度 %.6f°")
            .arg(initMs)
            .arg(static_cast<int>(prop.modelType()))
            .arg(job.observerLatDeg)
            .arg(job.observerLonDeg);
        
        // 记录错误并尝试恢复
        bool recoverySuccess = ErrorHandler::instance().handleError(errorInfo);
        
        // 恢复成功: 自动跳过（如传播器可重试场景）——通知调度层计数防悬挂
        if (recoverySuccess) {
            emit jobSkipped(satelliteId, details);
            return;
        }
        // 恢复失败，发送错误信号
        emit errorOccurred(errorInfo.message + "\n" + errorInfo.detailedContext + "\n" + errorInfo.suggestedAction);
        
        return;
    }
    
    // 记录初始化成功信息
    #ifdef QT_DEBUG
    auto initDuration = std::chrono::steady_clock::now() - initStartTime;
    auto initMs = std::chrono::duration_cast<std::chrono::milliseconds>(initDuration).count();
    
    qDebug() << QString("Propagator初始化成功: %1, 模型类型: %2, 初始化耗时: %3 ms")
        .arg(QString::fromStdString(tle.name))
        .arg(static_cast<int>(prop.modelType()))
        .arg(initMs);
    #endif

    // ---- Step 1.5: 上报 TLE 元信息（模型/历元/龄期，供 UI 展示与过期警告）----
    // 模型标签取 perturb 实际使用的深空分类(no_unkozai 周期), 与 TLE 行 2
    // 的 Kozai 平运动分类在 225min 边界附近可能不一致——以实际传播模型为准
    QString modelStr;
    switch (prop.modelType()) {
    case Sgp4Type::LEO_SGP4:    modelStr = "SGP4"; break;
    case Sgp4Type::MEO_SDP4:    modelStr = "SDP4"; break;
    case Sgp4Type::GEO_SDP4_DS: modelStr = "SDP4(DS)"; break;
    }
    // 历元 = 年首 + (epochDay − 1) 天（epochDay 含小数，UTC 时标）
    // 使用更精确的毫秒转换，避免精度损失
    const qint64 epochMs = QDateTime(QDate(tle.epochYear, 1, 1), QTime(0, 0), QTimeZone::UTC)
        .toMSecsSinceEpoch() + static_cast<qint64>((tle.epochDay - 1.0) * 86400000.0 + 0.5);
    double ageDays = (job.startUtc.toMSecsSinceEpoch() - epochMs) / 86400000.0;
    if (ageDays < 0.0) ageDays = 0.0;   // 未来历元（时钟/数据异常）按 0 处理
    emit metaInfo(modelStr,
                  QDateTime::fromMSecsSinceEpoch(epochMs, QTimeZone::UTC)
                      .toString("yyyy-MM-dd HH:mm:ss 'UTC'"),
                  ageDays);

    // 设置卫星名称
    prop.setSatelliteName(tle.name);

    // ---- Step 3: 构建传播 Lambda ----
    auto propagateFn = [&prop, this](double jd) -> std::optional<EciState> {
        if (m_cancelFlag->isCancelled()) return std::nullopt;
        return prop.propagate(jd);
    };

    // ---- Step 4: 创建 Observer ----
    Observer obs(job.observerLatDeg, job.observerLonDeg,
                 job.observerAltKm, job.minElevDeg);

    // ---- Step 5: 创建 OrbitCalculator 并执行 ----
    OrbitCalculator calc(job.calcCfg);
    calc.setSatelliteName(tle.name);  // 设置卫星名称到引擎

    // QDateTime(UTC) → double JD
    // 使用更高精度的转换，避免精度损失
    double jdStart = (job.startUtc.toMSecsSinceEpoch() / 86400000.0) + 2440587.5;
    
    // 记录计算开始时间
    auto calcStartTime = std::chrono::steady_clock::now();
    
    std::vector<PassEvent> passes;
    try {
        passes = calc.predict(propagateFn, obs, jdStart, job.hoursAhead, m_cancelFlag);

        if (m_cancelFlag->isCancelled()) return;
        
        // 计算总耗时
        #ifdef QT_DEBUG
        auto calcDuration = std::chrono::steady_clock::now() - calcStartTime;
        auto calcMs = std::chrono::duration_cast<std::chrono::milliseconds>(calcDuration).count();
        
        qDebug() << QString("轨道计算完成: %1, 过境数量: %2, 计算耗时: %3 ms")
            .arg(QString::fromStdString(tle.name))
            .arg(passes.size())
            .arg(calcMs);
        #endif
        
        // 如果没有找到过境，提供信息性消息（不作为错误）
        if (passes.empty()) {
            #ifdef QT_DEBUG
            qDebug() << QString("卫星 %1 在指定时间窗口内无可见过境")
                .arg(QString::fromStdString(tle.name));
            #endif
        }
        
    } catch (const std::exception &e) {
        // 计算耗时
        auto calcDuration = std::chrono::steady_clock::now() - calcStartTime;
        auto calcMs = std::chrono::duration_cast<std::chrono::milliseconds>(calcDuration).count();
        
        QString satelliteId = QString::fromStdString(tle.name);
        
        // 使用错误处理系统记录轨道计算错误
        QString details = QString("轨道计算失败: %1").arg(QString::fromUtf8(e.what()));
        QString suggestion;
        
        if (QString(e.what()).contains("传播")) {
            suggestion = "检查TLE数据时效性，或使用更新的TLE";
        } else if (QString(e.what()).contains("数值") || QString(e.what()).contains("范围")) {
            suggestion = "检查观测站坐标和仰角阈值是否合理";
        } else if (QString(e.what()).contains("时间")) {
            suggestion = "缩短预测时间窗口，或检查起始时间设置";
        } else if (QString(e.what()).contains("内存") || QString(e.what()).contains("溢出")) {
            suggestion = "减少同时计算的卫星数量，或缩短预测时间";
        } else {
            suggestion = "尝试缩短预测时间窗口，或检查输入参数";
        }
        
        ErrorInfo errorInfo = ErrorHandler::createOrbitCalcError(
            satelliteId, details, suggestion);
        errorInfo.detailedContext = QString("计算耗时: %1 ms\n时间窗口: %2 小时\n观测站: 纬度 %.6f°, 经度 %.6f°, 高度 %.3f km\n掩星角: %.1f°")
            .arg(calcMs)
            .arg(job.hoursAhead)
            .arg(job.observerLatDeg)
            .arg(job.observerLonDeg)
            .arg(job.observerAltKm)
            .arg(job.minElevDeg);
        
        // 记录错误并尝试恢复
        bool recoverySuccess = ErrorHandler::instance().handleError(errorInfo);
        
        // 恢复成功: 自动跳过（防御: 当前计算错误均为 ERROR 级不会命中, 防未来改级）
        if (recoverySuccess) {
            emit jobSkipped(satelliteId, details);
            return;
        }
        // 恢复失败，发送错误信号
        emit errorOccurred(errorInfo.message + "\n" + errorInfo.detailedContext + "\n" + errorInfo.suggestedAction);
        
        return;
    } catch (...) {
        // 计算耗时
        auto calcDuration = std::chrono::steady_clock::now() - calcStartTime;
        auto calcMs = std::chrono::duration_cast<std::chrono::milliseconds>(calcDuration).count();
        
        QString satelliteId = QString::fromStdString(tle.name);
        
        // 使用错误处理系统记录未知错误
        ErrorInfo errorInfo = ErrorHandler::createOrbitCalcError(
            satelliteId, "轨道计算失败（未知错误）", 
            "请检查系统资源或联系技术支持");
        errorInfo.detailedContext = QString("计算耗时: %1 ms\n时间窗口: %2 小时")
            .arg(calcMs)
            .arg(job.hoursAhead);
        
        // 记录错误并尝试恢复
        bool recoverySuccess = ErrorHandler::instance().handleError(errorInfo);
        
        // 如果恢复失败，发送错误信号
        if (!recoverySuccess) {
            emit errorOccurred(errorInfo.message + "\n" + errorInfo.detailedContext + "\n" + errorInfo.suggestedAction);
        }
        
        return;
    }

    // ---- Step 6: 更新状态栏信息（通过信号传递） ----
    // 将模型信息附加到第一条过境的卫星名称中（modelStr 已在 Step 1.5 计算）
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
    m_cancelFlag->set(true);
}

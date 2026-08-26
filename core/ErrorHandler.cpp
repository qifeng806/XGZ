// =============================================================================
// ErrorHandler.cpp — 统一错误处理和恢复机制实现
// =============================================================================
#include "ErrorHandler.h"
#include <QDateTime>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>

ErrorHandler& ErrorHandler::instance()
{
    static ErrorHandler instance;
    return instance;
}

ErrorHandler::ErrorHandler()
    : m_autoRecoveryEnabled(true)
{
    // 统一恢复契约: 仅 WARNING 级自动「跳过」（如校验和警告继续计算）;
    // ERROR/CRITICAL 一律返回 false 走 errorOccurred 上报——
    // 恢复函数是纯谓词不做任何实际动作, 把 ERROR/CRITICAL 判为
    // 「恢复成功」会让 worker 静默 jobSkipped / 严重度倒置为跳过
    const auto warningOnlySkip = [](const ErrorInfo& error) -> bool {
        return error.severity == ErrorSeverity::WARNING;   // 与 shouldSkip 一致
    };

    registerRecoveryFunction(ErrorType::TLE_PARSE_ERROR, warningOnlySkip);
    registerRecoveryFunction(ErrorType::PROPAGATOR_ERROR, warningOnlySkip);
    registerRecoveryFunction(ErrorType::ORBIT_CALC_ERROR, warningOnlySkip);
    registerRecoveryFunction(ErrorType::NETWORK_ERROR, warningOnlySkip);
    registerRecoveryFunction(ErrorType::FILE_IO_ERROR, warningOnlySkip);
    registerRecoveryFunction(ErrorType::CONFIG_ERROR, warningOnlySkip);
    registerRecoveryFunction(ErrorType::UNKNOWN_ERROR, warningOnlySkip);
}

ErrorHandler::~ErrorHandler()
{
    // 清理资源
    m_errorHistory.clear();
    m_recoveryFunctions.clear();
}

void ErrorHandler::registerRecoveryFunction(ErrorType type, RecoveryFunction func)
{
    QMutexLocker locker(&m_mutex);
    m_recoveryFunctions[type] = func;
    qDebug() << "Registered recovery function for error type:" << static_cast<int>(type);
}

bool ErrorHandler::handleError(const ErrorInfo& error)
{
    // 记录错误
    logError(error);

    // 发送错误信号（错误必然被上报，恢复结果只决定是否自动处理）
    emit errorOccurred(error);

    // 如果启用自动恢复，尝试自动处理
    bool autoRecovery;
    {
        QMutexLocker locker(&m_mutex);
        autoRecovery = m_autoRecoveryEnabled;
    }
    if (autoRecovery) {
        return internalHandleError(error);
    }

    // 否则返回false，表示需要人工处理
    return false;
}

void ErrorHandler::logError(const ErrorInfo& error)
{
    QMutexLocker locker(&m_mutex);

    // 添加到历史记录
    m_errorHistory.append(error);

    // 更新错误计数
    m_errorCounts[error.type]++;

    // 限制历史记录大小，保留最近1000条
    if (m_errorHistory.size() > 1000) {
        m_errorHistory = m_errorHistory.mid(m_errorHistory.size() - 1000);
    }

    // 输出调试信息
    qDebug() << "Error logged:" << error.message
             << "(Type:" << static_cast<int>(error.type)
             << "Severity:" << static_cast<int>(error.severity)
             << "Satellite:" << error.satelliteId << ")";
}

QList<ErrorInfo> ErrorHandler::getErrorHistory(ErrorType type, int maxCount) const
{
    QMutexLocker locker(&m_mutex);

    QList<ErrorInfo> result;

    if (type == ErrorType::UNKNOWN_ERROR) {
        // 返回所有类型的错误
        result = m_errorHistory;
    } else {
        // 返回指定类型的错误
        for (const auto& error : m_errorHistory) {
            if (error.type == type) {
                result.append(error);
            }
        }
    }

    // 限制返回数量
    if (result.size() > maxCount) {
        result = result.mid(result.size() - maxCount);
    }

    return result;
}

void ErrorHandler::clearErrorHistory(ErrorType type)
{
    QMutexLocker locker(&m_mutex);

    if (type == ErrorType::UNKNOWN_ERROR) {
        // 清除所有错误历史
        m_errorHistory.clear();
        m_errorCounts.clear();
    } else {
        // 清除指定类型的错误历史
        for (int i = m_errorHistory.size() - 1; i >= 0; i--) {
            if (m_errorHistory[i].type == type) {
                m_errorHistory.removeAt(i);
                m_errorCounts[type]--;
                if (m_errorCounts[type] <= 0) {
                    m_errorCounts.remove(type);
                }
            }
        }
    }
}

QMap<ErrorType, int> ErrorHandler::getErrorStatistics() const
{
    QMutexLocker locker(&m_mutex);
    return m_errorCounts;
}

void ErrorHandler::setAutoRecoveryEnabled(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_autoRecoveryEnabled = enabled;
    qDebug() << "Auto recovery enabled:" << enabled;
}

bool ErrorHandler::isAutoRecoveryEnabled() const
{
    QMutexLocker locker(&m_mutex);
    return m_autoRecoveryEnabled;
}

ErrorInfo ErrorHandler::createTleParseError(const QString& satelliteId,
                                           const QString& details,
                                           const QString& suggestion)
{
    QString message = "TLE解析失败";
    if (!satelliteId.isEmpty()) {
        message += QString(" (卫星: %1)").arg(satelliteId);
    }

    QString context = details;
    if (context.isEmpty()) {
        context = "TLE数据格式不符合规范要求";
    }

    QString action = suggestion;
    if (action.isEmpty()) {
        action = "建议检查TLE格式或使用更新的TLE数据";
    }

    // 根据错误内容判断严重程度
    ErrorSeverity severity = ErrorSeverity::ERROR;
    if (details.contains("校验和错误")) {
        severity = ErrorSeverity::WARNING; // 校验和错误通常是警告
    } else if (details.contains("卫星已再入")) {
        severity = ErrorSeverity::CRITICAL; // 卫星再入是严重错误
    }

    return ErrorInfo(ErrorType::TLE_PARSE_ERROR, severity, message,
                    context, action, RecoveryAction::SKIP, satelliteId);
}

ErrorInfo ErrorHandler::createPropagatorError(const QString& satelliteId,
                                             const QString& details,
                                             const QString& suggestion)
{
    QString message = "轨道传播器初始化失败";
    if (!satelliteId.isEmpty()) {
        message += QString(" (卫星: %1)").arg(satelliteId);
    }

    QString context = details;
    if (context.isEmpty()) {
        context = "SGP4模型初始化失败";
    }

    QString action = suggestion;
    if (action.isEmpty()) {
        action = "建议检查TLE数据时效性或尝试不同的传播模型";
    }

    return ErrorInfo(ErrorType::PROPAGATOR_ERROR, ErrorSeverity::ERROR,
                    message, context, action, RecoveryAction::RETRY, satelliteId);
}

ErrorInfo ErrorHandler::createOrbitCalcError(const QString& satelliteId,
                                           const QString& details,
                                           const QString& suggestion)
{
    QString message = "轨道计算失败";
    if (!satelliteId.isEmpty()) {
        message += QString(" (卫星: %1)").arg(satelliteId);
    }

    QString context = details;
    if (context.isEmpty()) {
        context = "轨道参数计算出现异常";
    }

    QString action = suggestion;
    if (action.isEmpty()) {
        action = "建议缩短预测时间窗口或检查观测站参数";
    }

    return ErrorInfo(ErrorType::ORBIT_CALC_ERROR, ErrorSeverity::ERROR,
                    message, context, action, RecoveryAction::RETRY, satelliteId);
}

ErrorInfo ErrorHandler::createNetworkError(const QString& url,
                                         const QString& details,
                                         const QString& suggestion)
{
    QString message = QString("网络请求失败: %1").arg(url);

    QString context = details;
    if (context.isEmpty()) {
        context = "无法连接到远程服务器";
    }

    QString action = suggestion;
    if (action.isEmpty()) {
        action = "建议检查网络连接或稍后重试";
    }

    return ErrorInfo(ErrorType::NETWORK_ERROR, ErrorSeverity::ERROR,
                    message, context, action, RecoveryAction::RETRY);
}

ErrorInfo ErrorHandler::createFileIoError(const QString& filePath,
                                        const QString& details,
                                        const QString& suggestion)
{
    QString message = QString("文件操作失败: %1").arg(filePath);

    QString context = details;
    if (context.isEmpty()) {
        context = "文件读写操作出现异常";
    }

    QString action = suggestion;
    if (action.isEmpty()) {
        action = "建议检查文件权限或文件路径是否正确";
    }

    return ErrorInfo(ErrorType::FILE_IO_ERROR, ErrorSeverity::ERROR,
                    message, context, action, RecoveryAction::SKIP);
}

ErrorInfo ErrorHandler::createConfigError(const QString& configKey,
                                        const QString& details,
                                        const QString& suggestion)
{
    QString message = QString("配置错误: %1").arg(configKey);

    QString context = details;
    if (context.isEmpty()) {
        context = "配置参数无效或缺少";
    }

    QString action = suggestion;
    if (action.isEmpty()) {
        action = "建议检查配置文件或使用默认设置";
    }

    return ErrorInfo(ErrorType::CONFIG_ERROR, ErrorSeverity::CRITICAL,
                    message, context, action, RecoveryAction::MANUAL_INTERVENTION);
}

bool ErrorHandler::internalHandleError(const ErrorInfo& error)
{
    // 在锁内取出恢复函数，锁外调用（避免持有锁期间执行用户回调）
    RecoveryFunction func;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_recoveryFunctions.contains(error.type)) {
            return false;
        }
        func = m_recoveryFunctions[error.type];
    }

    // 尝试恢复
    bool success = func(error);

    // 发送恢复信号
    emit errorRecoveryAttempt(error, error.recommendedAction);
    emit errorRecoveryResult(error, success);

    if (success) {
        qDebug() << "Error recovery successful:" << error.message;
        return true;
    } else {
        qDebug() << "Error recovery failed:" << error.message;
    }

    // 如果没有恢复函数或恢复失败，返回false
    return false;
}

bool ErrorHandler::shouldRetry(const ErrorInfo& error) const
{
    // 简单的重试策略：对于网络错误和临时性错误，可以重试
    if (error.type == ErrorType::NETWORK_ERROR) {
        return true;
    }

    if (error.type == ErrorType::TLE_PARSE_ERROR &&
        error.severity == ErrorSeverity::WARNING) {
        return true;
    }

    if (error.type == ErrorType::PROPAGATOR_ERROR) {
        return true;
    }

    if (error.type == ErrorType::ORBIT_CALC_ERROR) {
        return true;
    }

    return false;
}

bool ErrorHandler::shouldSkip(const ErrorInfo& error) const
{
    // 仅 WARNING 级可跳过（与构造函数注册的恢复策略一致——
    // 此前 TLE_PARSE_ERROR 不看严重度一律 true, 会把「卫星已再入」
    // 等 CRITICAL 错误误判为可跳过）
    return error.severity == ErrorSeverity::WARNING;
}

bool ErrorHandler::needsManualIntervention(const ErrorInfo& error) const
{
    // 对于严重错误或需要特殊处理的错误，需要人工干预
    if (error.severity == ErrorSeverity::CRITICAL) {
        return true;
    }

    if (error.type == ErrorType::CONFIG_ERROR) {
        return true;
    }

    return false;
}

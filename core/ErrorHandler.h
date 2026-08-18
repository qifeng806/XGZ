// =============================================================================
// ErrorHandler.h — 统一错误处理和恢复机制
// =============================================================================
#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QDateTime>
#include <QObject>
#include <QMutex>
#include <functional>
#include <memory>

// 错误类型枚举
enum class ErrorType {
    TLE_PARSE_ERROR,        // TLE解析错误
    PROPAGATOR_ERROR,       // 轨道传播器错误
    ORBIT_CALC_ERROR,       // 轨道计算错误
    NETWORK_ERROR,          // 网络错误
    FILE_IO_ERROR,          // 文件IO错误
    CONFIG_ERROR,           // 配置错误
    UNKNOWN_ERROR           // 未知错误
};

// 错误严重级别
enum class ErrorSeverity {
    INFO,           // 信息性提示
    WARNING,        // 警告，可以继续
    ERROR,          // 错误，部分功能受限
    CRITICAL        // 严重错误，功能完全不可用
};

// 错误恢复操作类型
enum class RecoveryAction {
    RETRY,          // 重试操作
    SKIP,           // 跳过当前操作
    FALLBACK,       // 使用备用方案
    ABORT,          // 中止操作
    MANUAL_INTERVENTION  // 需要人工干预
};

// 错误信息结构
struct ErrorInfo {
    ErrorType type;
    ErrorSeverity severity;
    QString message;
    QString detailedContext;
    QString suggestedAction;
    RecoveryAction recommendedAction;
    QString timestamp;
    QString sourceFile;
    int sourceLine;
    QString satelliteId;  // 相关的卫星ID（如果有）

    // 默认构造（QVariant 转换路径要求类型默认可构造）
    ErrorInfo() = default;
    
    // 构造函数
    ErrorInfo(ErrorType t, ErrorSeverity s, const QString& msg, 
              const QString& context = "", const QString& action = "",
              RecoveryAction recAction = RecoveryAction::ABORT,
              const QString& satId = "")
        : type(t), severity(s), message(msg), detailedContext(context),
          suggestedAction(action), recommendedAction(recAction),
          satelliteId(satId) {
        timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    }
};

// 错误恢复函数类型
using RecoveryFunction = std::function<bool(const ErrorInfo&)>;

Q_DECLARE_METATYPE(ErrorInfo)
Q_DECLARE_METATYPE(ErrorType)
Q_DECLARE_METATYPE(ErrorSeverity)
Q_DECLARE_METATYPE(RecoveryAction)

// 错误处理器类
class ErrorHandler : public QObject {
    Q_OBJECT
    
public:
    static ErrorHandler& instance();
    
    // 注册错误类型和对应的恢复函数
    void registerRecoveryFunction(ErrorType type, RecoveryFunction func);
    
    // 处理错误
    bool handleError(const ErrorInfo& error);
    
    // 记录错误（不立即处理）
    void logError(const ErrorInfo& error);
    
    // 获取错误历史
    QList<ErrorInfo> getErrorHistory(ErrorType type = ErrorType::UNKNOWN_ERROR, 
                                     int maxCount = 100) const;
    
    // 清除错误历史
    void clearErrorHistory(ErrorType type = ErrorType::UNKNOWN_ERROR);
    
    // 获取错误统计
    QMap<ErrorType, int> getErrorStatistics() const;
    
    // 设置是否启用自动恢复
    void setAutoRecoveryEnabled(bool enabled);

    // 获取是否启用自动恢复
    bool isAutoRecoveryEnabled() const;

    // 查询恢复策略（供 UI 层决定是否提示重试等）
    bool shouldRetry(const ErrorInfo& error) const;

    // 检查是否应该跳过（供 UI 层决定是否提示跳过）
    bool shouldSkip(const ErrorInfo& error) const;

    // 检查是否需要人工干预（供 UI 层决定是否提示人工处理）
    bool needsManualIntervention(const ErrorInfo& error) const;
    
    // 创建错误信息（辅助函数）
    static ErrorInfo createTleParseError(const QString& satelliteId, 
                                        const QString& details,
                                        const QString& suggestion = "");
    
    static ErrorInfo createPropagatorError(const QString& satelliteId,
                                          const QString& details,
                                          const QString& suggestion = "");
    
    static ErrorInfo createOrbitCalcError(const QString& satelliteId,
                                        const QString& details,
                                        const QString& suggestion = "");
    
    static ErrorInfo createNetworkError(const QString& url,
                                      const QString& details,
                                      const QString& suggestion = "");
    
    static ErrorInfo createFileIoError(const QString& filePath,
                                     const QString& details,
                                     const QString& suggestion = "");
    
    static ErrorInfo createConfigError(const QString& configKey,
                                     const QString& details,
                                     const QString& suggestion = "");

signals:
    // 错误信号
    void errorOccurred(const ErrorInfo& error);
    
    // 错误恢复信号
    void errorRecoveryAttempt(const ErrorInfo& error, RecoveryAction action);
    
    // 错误恢复结果信号
    void errorRecoveryResult(const ErrorInfo& error, bool success);

private:
    ErrorHandler();
    ~ErrorHandler();
    
    // 禁止拷贝和赋值
    ErrorHandler(const ErrorHandler&) = delete;
    ErrorHandler& operator=(const ErrorHandler&) = delete;
    
    // 错误历史记录
    QList<ErrorInfo> m_errorHistory;
    
    // 恢复函数映射
    QMap<ErrorType, RecoveryFunction> m_recoveryFunctions;
    
    // 是否启用自动恢复
    bool m_autoRecoveryEnabled;
    
    // 错误计数
    QMap<ErrorType, int> m_errorCounts;

    // 跨线程互斥: worker 线程 handleError/logError 与主线程查询并发访问
    mutable QMutex m_mutex;

    // 内部处理错误
    bool internalHandleError(const ErrorInfo& error);
};

#endif // ERROR_HANDLER_H
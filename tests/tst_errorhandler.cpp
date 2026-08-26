// =============================================================================
// tst_errorhandler.cpp — 错误处理系统单元测试
// =============================================================================
#include <QtTest/QtTest>
#include "core/ErrorHandler.h"
#include <QSignalSpy>
#include <QDebug>

class TstErrorHandler : public QObject {
    Q_OBJECT
private slots:
    // 每个用例后恢复 NETWORK_ERROR 默认策略——理由同 tst_passworker/
    // tst_passservice 的 cleanup(): ErrorHandler 是进程级单例, 本套件注册的
    // 自定义恢复函数会跨用例/跨测试类留存; testCustomRecoveryFunction 注册的
    // lambda 捕获栈局部变量, 套件结束后若仍留存, 后续任何触发 NETWORK_ERROR
    // 恢复路径的代码都会写入已销毁的栈对象（UB）
    void cleanup();

    void testSingletonInstance();
    void testErrorCreation();
    void testErrorHandling();
    void testErrorRecovery();
    void testErrorHistory();
    void testErrorStatistics();
    void testAutoRecovery();
    void testCustomRecoveryFunction();
};

void TstErrorHandler::testSingletonInstance()
{
    // 测试单例模式
    ErrorHandler& instance1 = ErrorHandler::instance();
    ErrorHandler& instance2 = ErrorHandler::instance();
    
    QVERIFY(&instance1 == &instance2);
    
    qDebug() << "Singleton instance test passed";
}

void TstErrorHandler::testErrorCreation()
{
    // 测试错误信息创建
    ErrorInfo error = ErrorHandler::createTleParseError(
        "ISS (ZARYA)",
        "TLE行长度不足",
        "请确保TLE数据完整");
    
    QCOMPARE(error.type, ErrorType::TLE_PARSE_ERROR);
    QCOMPARE(error.severity, ErrorSeverity::ERROR);
    QCOMPARE(error.satelliteId, QString("ISS (ZARYA)"));
    QVERIFY(error.message.contains("TLE解析失败"));
    QVERIFY(error.suggestedAction.contains("请确保TLE数据完整"));
    
    // 测试其他错误类型的创建
    ErrorInfo networkError = ErrorHandler::createNetworkError(
        "https://celestrak.com",
        "连接超时",
        "检查网络连接");
    
    QCOMPARE(networkError.type, ErrorType::NETWORK_ERROR);
    QCOMPARE(networkError.severity, ErrorSeverity::ERROR);
    QVERIFY(networkError.message.contains("网络请求失败"));
    
    qDebug() << "Error creation test passed";
}

void TstErrorHandler::testErrorHandling()
{
    ErrorHandler& handler = ErrorHandler::instance();
    
    // 创建信号监视器
    QSignalSpy errorSpy(&handler, &ErrorHandler::errorOccurred);
    QSignalSpy recoverySpy(&handler, &ErrorHandler::errorRecoveryResult);
    
    // 创建测试错误
    ErrorInfo error = ErrorHandler::createTleParseError(
        "TEST-SAT",
        "测试错误",
        "测试建议");
    
    // 处理错误
    bool success = handler.handleError(error);
    
    // 验证信号发射
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(recoverySpy.count(), 1);
    
    // 验证错误信息
    QList<QVariant> errorArgs = errorSpy.takeFirst();
    QCOMPARE(errorArgs.size(), 1);
    QCOMPARE(errorArgs[0].value<ErrorInfo>().message, error.message);
    
    // 验证恢复结果
    QList<QVariant> recoveryArgs = recoverySpy.takeFirst();
    QCOMPARE(recoveryArgs.size(), 2);
    QCOMPARE(recoveryArgs[0].value<ErrorInfo>().message, error.message);
    QCOMPARE(recoveryArgs[1].toBool(), success);
    
    qDebug() << "Error handling test passed";
}

void TstErrorHandler::testErrorRecovery()
{
    ErrorHandler& handler = ErrorHandler::instance();
    
    // 测试重试策略
    ErrorInfo retryError = ErrorHandler::createNetworkError(
        "https://celestrak.com",
        "连接超时",
        "检查网络连接");
    
    bool shouldRetry = handler.shouldRetry(retryError);
    QVERIFY(shouldRetry);
    
    // 测试跳过策略
    ErrorInfo skipError = ErrorHandler::createTleParseError(
        "TEST-SAT",
        "校验和错误",
        "继续计算可能不准确");
    
    bool shouldSkip = handler.shouldSkip(skipError);
    QVERIFY(shouldSkip);
    
    // 测试人工干预
    ErrorInfo manualError = ErrorHandler::createConfigError(
        "观测站位置",
        "配置参数无效",
        "需要人工检查配置");
    
    bool needsManual = handler.needsManualIntervention(manualError);
    QVERIFY(needsManual);
    
    qDebug() << "Error recovery test passed";
}

void TstErrorHandler::testErrorHistory()
{
    ErrorHandler& handler = ErrorHandler::instance();
    
    // 清除历史记录
    handler.clearErrorHistory();
    
    // 创建一些测试错误
    ErrorInfo error1 = ErrorHandler::createTleParseError("SAT1", "错误1", "建议1");
    ErrorInfo error2 = ErrorHandler::createNetworkError("url", "错误2", "建议2");
    ErrorInfo error3 = ErrorHandler::createTleParseError("SAT2", "错误3", "建议3");
    
    // 记录错误
    handler.logError(error1);
    handler.logError(error2);
    handler.logError(error3);
    
    // 获取所有错误
    QList<ErrorInfo> allErrors = handler.getErrorHistory();
    QCOMPARE(allErrors.size(), 3);
    
    // 获取特定类型的错误
    QList<ErrorInfo> tleErrors = handler.getErrorHistory(ErrorType::TLE_PARSE_ERROR);
    QCOMPARE(tleErrors.size(), 2);
    
    // 获取网络错误
    QList<ErrorInfo> networkErrors = handler.getErrorHistory(ErrorType::NETWORK_ERROR);
    QCOMPARE(networkErrors.size(), 1);
    
    // 清除特定类型的错误
    handler.clearErrorHistory(ErrorType::TLE_PARSE_ERROR);
    
    QList<ErrorInfo> remainingErrors = handler.getErrorHistory();
    QCOMPARE(remainingErrors.size(), 1);
    QCOMPARE(remainingErrors[0].type, ErrorType::NETWORK_ERROR);
    
    qDebug() << "Error history test passed";
}

void TstErrorHandler::testErrorStatistics()
{
    ErrorHandler& handler = ErrorHandler::instance();
    
    // 清除历史记录
    handler.clearErrorHistory();
    
    // 创建一些测试错误
    ErrorInfo error1 = ErrorHandler::createTleParseError("SAT1", "错误1", "建议1");
    ErrorInfo error2 = ErrorHandler::createNetworkError("url", "错误2", "建议2");
    ErrorInfo error3 = ErrorHandler::createTleParseError("SAT2", "错误3", "建议3");
    
    // 记录错误
    handler.logError(error1);
    handler.logError(error2);
    handler.logError(error3);
    
    // 获取统计信息
    QMap<ErrorType, int> stats = handler.getErrorStatistics();
    
    QCOMPARE(stats[ErrorType::TLE_PARSE_ERROR], 2);
    QCOMPARE(stats[ErrorType::NETWORK_ERROR], 1);
    
    qDebug() << "Error statistics test passed";
}

void TstErrorHandler::testAutoRecovery()
{
    ErrorHandler& handler = ErrorHandler::instance();

    // handleError/logError 会累积历史记录（前序用例已产生条目）→ 先清空
    handler.clearErrorHistory();

    // 测试自动恢复开关
    QVERIFY(handler.isAutoRecoveryEnabled());
    
    handler.setAutoRecoveryEnabled(false);
    QVERIFY(!handler.isAutoRecoveryEnabled());
    
    handler.setAutoRecoveryEnabled(true);
    QVERIFY(handler.isAutoRecoveryEnabled());
    
    // 创建测试错误
    ErrorInfo error = ErrorHandler::createNetworkError(
        "https://celestrak.com",
        "连接超时",
        "检查网络连接");
    
    // 记录错误但不处理
    handler.logError(error);
    
    // 获取错误历史
    QList<ErrorInfo> errors = handler.getErrorHistory();
    QCOMPARE(errors.size(), 1);
    
    qDebug() << "Auto recovery test passed";
}

void TstErrorHandler::testCustomRecoveryFunction()
{
    ErrorHandler& handler = ErrorHandler::instance();
    
    // 创建自定义恢复函数
    bool customRecoveryCalled = false;
    handler.registerRecoveryFunction(ErrorType::NETWORK_ERROR,
        [&customRecoveryCalled](const ErrorInfo&) -> bool {
            customRecoveryCalled = true;
            return true; // 模拟恢复成功
        });
    
    // 创建测试错误
    ErrorInfo error = ErrorHandler::createNetworkError(
        "https://celestrak.com",
        "连接超时",
        "检查网络连接");
    
    // 处理错误
    bool success = handler.handleError(error);
    
    // 验证自定义恢复函数被调用
    QVERIFY(customRecoveryCalled);
    QVERIFY(success);
    
    qDebug() << "Custom recovery function test passed";
}

void TstErrorHandler::cleanup()
{
    // 恢复 NETWORK_ERROR 默认策略（与 ErrorHandler 构造函数注册的
    // warningOnlySkip 契约一致: 仅 WARNING 级自动跳过）
    ErrorHandler::instance().registerRecoveryFunction(
        ErrorType::NETWORK_ERROR,
        [](const ErrorInfo &e) { return e.severity == ErrorSeverity::WARNING; });
}

int runTstErrorHandler(int argc, char *argv[])
{
    TstErrorHandler t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_errorhandler.moc"
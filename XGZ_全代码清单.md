# XGZ 卫星 TLE 过境预报系统 — 最终版全代码清单

- **工程**：Qt 6.11 + qmake + C++17 + MinGW-w64（Qt Widgets / Network / Concurrent）
- **生成时间**：2026-09-19 10:59（第 7 轮审查修复后重新生成）
- **构建**：qmake XGZ.pro && mingw32-make -j8 → release/XGZ.exe（全量构建通过，离屏启动冒烟测试通过）
- **本清单包含**：XGZ 自研源码 50 个文本文件中的全部代码与构建描述文件，合计 10,779 行
- **不含**：perturb-master/（第三方 SGP4 引擎，gunvirranu/perturb，MIT，随源码包提供，未在清单内展开）
  与 resources/land110m.txt（72 KB 海岸线数据）、resources/app.ico（图标，二进制）
- **说明**：单元测试工程（原 tests/）已按要求移除；tools/stk_compare/ 为 XGZ↔STK 数值一致性验证工具（不参与主程序构建）
- **审查报告**：任务表1_代码核查与修改建议.md（首轮逐项核查）、代码审查_新一轮.md（第 7 轮审查与修复明细）

## 文件清单

| 文件 | 行数 | 字节 |
|---|---:|---:|
| main.cpp | 176 | 6,982 |
| XGZ.pro | 105 | 3,414 |
| .gitignore | 40 | 943 |
| core/AtomicCancelFlag.h | 121 | 4,265 |
| core/CoordTransform.cpp | 752 | 29,177 |
| core/CoordTransform.h | 93 | 4,473 |
| core/DataTypes.h | 246 | 11,845 |
| core/DopplerCalc.cpp | 60 | 2,737 |
| core/DopplerCalc.h | 27 | 1,152 |
| core/EarthShadow.cpp | 73 | 3,633 |
| core/EarthShadow.h | 47 | 2,574 |
| core/ErrorHandler.cpp | 360 | 10,977 |
| core/ErrorHandler.h | 183 | 6,129 |
| core/Nutation.cpp | 164 | 6,105 |
| core/Nutation.h | 27 | 975 |
| core/Observer.cpp | 83 | 3,938 |
| core/Observer.h | 33 | 1,196 |
| core/OrbitCalculator.cpp | 927 | 46,278 |
| core/OrbitCalculator.h | 102 | 4,819 |
| core/Propagator.cpp | 252 | 10,787 |
| core/Propagator.h | 63 | 2,330 |
| core/SunPosition.cpp | 195 | 9,077 |
| core/SunPosition.h | 19 | 852 |
| core/TleParser.cpp | 580 | 23,322 |
| core/TleParser.h | 70 | 2,820 |
| core/VisualMagnitude.cpp | 160 | 6,775 |
| core/VisualMagnitude.h | 33 | 1,987 |
| business/PassPredictorService.cpp | 465 | 22,410 |
| business/PassPredictorService.h | 81 | 3,550 |
| business/PassPredictWorker.cpp | 470 | 21,191 |
| business/PassPredictWorker.h | 66 | 2,855 |
| business/TleBatchParser.cpp | 48 | 1,966 |
| business/TleBatchParser.h | 22 | 970 |
| business/TleCache.cpp | 106 | 3,306 |
| business/TleCache.h | 49 | 2,074 |
| business/TleFetcher.cpp | 267 | 12,699 |
| business/TleFetcher.h | 74 | 3,119 |
| ui/CatmullRomSpline.h | 50 | 1,769 |
| ui/DopplerPlotWidget.cpp | 202 | 7,628 |
| ui/DopplerPlotWidget.h | 37 | 1,370 |
| ui/GroundTrackWidget.cpp | 284 | 11,466 |
| ui/GroundTrackWidget.h | 55 | 2,344 |
| ui/MainWindow.cpp | 2,680 | 120,674 |
| ui/MainWindow.h | 219 | 10,377 |
| ui/PassTableModel.cpp | 261 | 11,064 |
| ui/PassTableModel.h | 49 | 2,003 |
| ui/PolarPlotWidget.cpp | 222 | 8,917 |
| ui/PolarPlotWidget.h | 44 | 1,714 |
| resources/resources.qrc | 9 | 325 |
| resources/app.rc | 28 | 667 |

> 另有第三方目录 perturb-master/（include/ + src/ 参与编译；original/、tests/、docs/ 为上游原样保留）

---

## main.cpp

```cpp
// =============================================================================
// main.cpp — 卫星 TLE 过境预报系统 (XGZ) 入口
// Qt 6.11.0, C++17, qmake
// =============================================================================
#include <QApplication>
#include <QSurfaceFormat>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QDir>
#include <QFont>
#include "ui/MainWindow.h"
#include "core/DataTypes.h"

// 注册跨线程信号槽的自定义类型（必须在 QApplication 构造前完成）
static void registerMetaTypes()
{
    qRegisterMetaType<QVector<PassEvent>>("QVector<PassEvent>");
    qRegisterMetaType<PassEvent>("PassEvent");
}

int main(int argc, char *argv[])
{
    registerMetaTypes();

    // 单实例检查所需的提示弹窗也是 QWidget——QApplication 必须先于任何
    // 弹窗构造, 否则 Qt 6 下第二个实例启动即崩溃（M 必修项）
    QApplication app(argc, argv);

    // 单实例保护：同一用户会话内仅允许一个 XGZ 实例（锁随 main 栈对象持有至进程退出）
    // 注意: 不使用 setStaleLockTime(0)——那会禁用陈旧锁检测, 崩溃/强杀后残留的
    // 锁文件将永久拦截启动; 保留默认 30 秒, 崩溃后等待 30 秒即自动接管
    QLockFile lock(QDir::temp().filePath("xgz-" + qEnvironmentVariable("USERNAME", "user") + ".lock"));
    if (!lock.tryLock(10)) {
        QMessageBox::information(nullptr, "XGZ",
            "XGZ 已在运行，本实例即将退出。\n"
            "(若确认没有其他实例，请删除该锁文件后重试)",
            QMessageBox::Ok);
        return 0;
    }

    app.setApplicationName("XGZ");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("XGZ");

    // 设置默认字体（兼容中文显示）
    QFont defaultFont = app.font();
    defaultFont.setPointSize(10);
    app.setFont(defaultFont);

    // 深色主题风格
    app.setStyleSheet(R"(
        /* 兜底: 中央 QWidget 容器/嵌套容器默认继承系统浅灰白——
           仅设 QMainWindow 背景时大面积区域仍是浅色; 具体控件规则
           （更具体的选择器）在其后覆盖本规则 */
        QWidget { background-color: #2b2b2b; }
        QMainWindow { background-color: #2b2b2b; }
        QGroupBox {
            color: #e0e0e0;
            background-color: #2b2b2b;
            border: 1px solid #555;
            border-radius: 4px;
            margin-top: 8px;
            padding-top: 16px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
        }
        QTableWidget {
            background-color: #1e1e1e;
            alternate-background-color: #252525;
            color: #d0d0d0;
            gridline-color: #444;
        }
        QHeaderView::section {
            background-color: #333;
            color: #ccc;
            border: 1px solid #555;
            padding: 4px;
        }
        QPushButton {
            background-color: #3a3a3a;
            color: #e0e0e0;
            border: 1px solid #555;
            border-radius: 3px;
            padding: 5px 14px;
        }
        QPushButton:hover { background-color: #4a4a4a; }
        QPushButton:pressed { background-color: #2a2a2a; }
        QPushButton:disabled { color: #666; }
        QDoubleSpinBox, QSpinBox, QComboBox, QPlainTextEdit, QDateTimeEdit {
            background-color: #1e1e1e;
            color: #d0d0d0;
            border: 1px solid #555;
        }
        QLabel { color: #ccc; }
        QTabWidget::pane { background-color: #2b2b2b; border: 1px solid #555; }
        QTabBar::tab {
            background-color: #333;
            color: #aaa;
            padding: 6px 16px;
            border: 1px solid #555;
        }
        QTabBar::tab:selected { background-color: #2b2b2b; color: #fff; }
        QCheckBox { color: #ccc; }
        QProgressBar {
            border: 1px solid #555;
            background-color: #333;
            color: #e0e0e0;
        }
        QProgressBar::chunk { background-color: #4488cc; }
        /* 状态栏: Windows 默认浅白底, 深色主题下须与 QMainWindow 背景一致 */
        QStatusBar { background-color: #2b2b2b; color: #ccc; }
        QStatusBar::item { border: none; }
        /* 滚动条: 默认浅色轨道在深色主题下扎眼, 与表格/列表底色保持一致 */
        QScrollBar:vertical, QScrollBar:horizontal {
            background-color: #2b2b2b;
            width: 14px; height: 14px;
            margin: 0;
        }
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
            background-color: #555;
            border-radius: 4px;
            min-height: 30px; min-width: 30px;
        }
        QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {
            background-color: #666;
        }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: none; }
        /* 弹窗/下拉层: 默认浅色底 + 全局 QLabel{color:#ccc} 会形成低对比,
           须整体深色化（QMessageBox 按钮复用上方 QPushButton 规则） */
        QMessageBox, QDialog {
            background-color: #2b2b2b;
        }
        QMessageBox QLabel, QDialog QLabel { color: #e0e0e0; }
        /* QMessageBox 展开详情时的内部文本框: 无具体规则会深底黑字 */
        QMessageBox QTextEdit {
            background-color: #1e1e1e;
            color: #d0d0d0;
            border: 1px solid #555;
        }
        /* 文件对话框: 全局 QWidget 兜底会给它深色背景, 但内部视图/列表/
           侧栏/输入行文字仍是 palette 黑色（深底黑字不可读）——补齐配色。
           QSidebar 继承 QListView、路径栏继承 QComboBox, 均被下列规则命中 */
        QFileDialog {
            background-color: #2b2b2b;
            color: #d0d0d0;
        }
        QFileDialog QTreeView, QFileDialog QListView,
        QFileDialog QLineEdit, QFileDialog QComboBox {
            background-color: #1e1e1e;
            color: #d0d0d0;
            border: 1px solid #555;
        }
        QFileDialog QTreeView::item:selected, QFileDialog QListView::item:selected {
            background-color: #4488cc;
            color: #ffffff;
        }
        QFileDialog QLabel { color: #ccc; }
        QComboBox QAbstractItemView {
            background-color: #2b2b2b;
            color: #d0d0d0;
            border: 1px solid #555;
            selection-background-color: #4488cc;
            selection-color: #ffffff;
        }
    )");

    MainWindow mainWindow;
    mainWindow.setWindowIcon(QIcon(":/app.ico"));
    mainWindow.show();

    return app.exec();
}
```

---

## XGZ.pro

```qmake
# =============================================================================
# XGZ.pro — Satellite TLE Pass Predictor
# Qt 6.11.0 + qmake + C++17 + MinGW-w64
# 集成 perturb 库 (gunvirranu/perturb, MIT) 作为 SGP4/SDP4 引擎
# =============================================================================
QT       += core gui widgets network concurrent
CONFIG   += c++17
TARGET    = XGZ
TEMPLATE  = app

RESOURCES += resources/resources.qrc

# ---- Source Files ----
SOURCES += \
    main.cpp \
    core/TleParser.cpp \
    core/Propagator.cpp \
    core/CoordTransform.cpp \
    core/Observer.cpp \
    core/SunPosition.cpp \
    core/Nutation.cpp \
    core/EarthShadow.cpp \
    core/DopplerCalc.cpp \
    core/VisualMagnitude.cpp \
    core/OrbitCalculator.cpp \
    core/ErrorHandler.cpp \
    business/PassPredictWorker.cpp \
    business/PassPredictorService.cpp \
    business/TleFetcher.cpp \
    business/TleCache.cpp \
    business/TleBatchParser.cpp \
    ui/MainWindow.cpp \
    ui/PolarPlotWidget.cpp \
    ui/GroundTrackWidget.cpp \
    ui/DopplerPlotWidget.cpp \
    ui/PassTableModel.cpp \
    perturb-master/src/perturb.cpp \
    perturb-master/src/sgp4.cpp \
    perturb-master/src/tle.cpp

HEADERS += \
    core/DataTypes.h \
    core/Propagator.h \
    core/TleParser.h \
    core/CoordTransform.h \
    core/Observer.h \
    core/SunPosition.h \
    core/Nutation.h \
    core/EarthShadow.h \
    core/DopplerCalc.h \
    core/VisualMagnitude.h \
    core/OrbitCalculator.h \
    core/AtomicCancelFlag.h \
    core/ErrorHandler.h \
    business/PassPredictWorker.h \
    business/PassPredictorService.h \
    business/TleFetcher.h \
    business/TleCache.h \
    business/TleBatchParser.h \
    ui/MainWindow.h \
    ui/PolarPlotWidget.h \
    ui/GroundTrackWidget.h \
    ui/DopplerPlotWidget.h \
    ui/CatmullRomSpline.h \
    ui/PassTableModel.h \
    perturb-master/include/perturb/perturb.hpp \
    perturb-master/include/perturb/sgp4.hpp \
    perturb-master/include/perturb/tle.hpp

# ---- Include Paths ----
INCLUDEPATH += . core business ui perturb-master/include

# ---- 输出目录 ----
# 相对路径写法: qmake 在影子构建（Qt Creator 的 build\... 目录）下把
# 这些相对路径解析为相对 shadow 目录, 就地构建时解析为相对工程目录——
# 两层构建各自自洽。禁止写 $$PWD 绝对路径: qmake 对 OBJECTS_DIR 的
# 相对化不完整, 影子构建的 Makefile 会引用不存在的 release/obj（链接失败）;
# 禁止在 release/ 等子目录内运行 make（路径基准错位）
DESTDIR     = release
OBJECTS_DIR = release/obj
MOC_DIR     = release/moc
INCLUDEPATH += release/moc   # `#include "MainWindow.moc"` 的解析路径

# ---- Compiler Flags ----
win32-g++ {
    QMAKE_CXXFLAGS += -Wall -Wextra -std=c++17
    # 抑制 perturb 内部的 magic-number 警告
    QMAKE_CXXFLAGS += -Wno-missing-braces
}
win32-msvc* {
    QMAKE_CXXFLAGS += /utf-8 /MP /W3 /std:c++17
}
# 说明: qmake 的 Makefile 生成器不支持按源文件作用域设置编译选项，
# 故 perturb 源文件内的历史遗留未使用变量/参数已直接在源码上用
# [[maybe_unused]] 标注（保持行为不变，全局 -Wall -Wextra 不受影响）

# ---- Debug/Release ----
CONFIG(debug, debug|release) {
    TARGET = $${TARGET}_d
} else {
    DEFINES += QT_NO_DEBUG_OUTPUT
}

# ---- Windows RC ----
RC_FILE = resources/app.rc
```

---

## .gitignore

```text
# =============================================================================
# .gitignore — 商业子代理重构草稿（交付物, 不纳入版本库）
# 均为未接线/未构建的参考实现; 若后续采纳其中部分, 从本清单移除后再提交
# =============================================================================

# business 层草稿
business/BusinessLogicController.cpp
business/BusinessLogicController.h
business/ErrorManager.cpp
business/ErrorManager.h
business/ExportManager.cpp
business/ExportManager.h

# ui 层草稿
ui/Configuration.cpp
ui/Configuration.h
ui/UIController.cpp
ui/UIController.h
ui/MainWindowRefactored.cpp
ui/MainWindowRefactored.h

# 文档与脚本
docs/
重构方案.md
测试覆盖改进报告.md
原子操作修复报告.md

# 构建产物与运行日志
release/
debug/
Makefile
Makefile.Release
Makefile.Debug
.qmake.stash
.qtcreator/
target_wrapper.bat
run_*.txt
*.o
*.obj
*.exe
```

---

## core/AtomicCancelFlag.h

```cpp
#ifndef CORE_ATOMICCANCELFLAG_H
#define CORE_ATOMICCANCELFLAG_H
// =============================================================================
// AtomicCancelFlag.h — 线程安全的取消标志管理
// =============================================================================
#include <atomic>
#include <memory>
#include <functional>

/**
 * @brief 线程安全的取消标志管理类
 * 
 * 提供了一个线程安全的取消标志，支持 RAII 管理和回调函数。
 * 主要用于 OrbitCalculator 中的协作式取消机制。
 *
 * ⚠ 使用边界（审查结论, 2026-09）:
 *   - 生产代码只使用 set()/get()/isCancelled() 三个成员（标志本身是原子的）;
 *   - setCancelCallback()/reset()/swap() **当前无任何调用者**, 且按现状启用是
 *     不安全的使用方式:
 *       * set() 会在**调用者线程**直接执行 m_cancelCallback, 而
 *         setCancelCallback() 对该 std::function 的赋值没有任何同步——两个线程
 *         分别 set/setCancelCallback 就是数据竞争（未定义行为）;
 *       * reset() 会连带清空回调, 语义上把"标志"和"回调注册"耦合在一起,
 *         多线程下会让回调在不同线程看到不一致的状态;
 *       * swap() 对 m_flag 用了 acq_rel 但只交换标志与回调, 不保证与
 *         其他成员的关联状态一致。
 *     如确需回调能力: 把 Callback 换成 std::atomic<std::shared_ptr<Callback>>
 *     （或加互斥锁保护), 并在 set() 中先取局部副本再调用。
 *   - 否则请勿在生产路径引入这三者, 也不要因为"看起来线程安全"而直接启用。
 */
class AtomicCancelFlag {
public:
    using Callback = std::function<void()>;
    
    /**
     * @brief 构造函数
     * @param initialValue 初始值（默认为 false）
     */
    explicit AtomicCancelFlag(bool initialValue = false) 
        : m_flag(initialValue) {}
    
    /**
     * @brief 设置取消状态
     * @param value true 表示取消，false 表示未取消
     * @param memoryOrder 内存序，默认为 release
     */
    void set(bool value, std::memory_order memoryOrder = std::memory_order_release) {
        m_flag.store(value, memoryOrder);
        if (value && m_cancelCallback) {
            m_cancelCallback();
        }
    }
    
    /**
     * @brief 获取取消状态
     * @param memoryOrder 内存序，默认为 acquire
     * @return true 表示已取消，false 表示未取消
     */
    bool get(std::memory_order memoryOrder = std::memory_order_acquire) const {
        return m_flag.load(memoryOrder);
    }
    
    /**
     * @brief 检查是否已取消
     * @return true 表示已取消，false 表示未取消
     */
    bool isCancelled() const {
        return get();
    }
    
    /**
     * @brief 设置取消回调函数
     * @param callback 回调函数
     */
    void setCancelCallback(Callback callback) {
        m_cancelCallback = callback;
    }
    
    /**
     * @brief 重置取消标志
     */
    void reset() {
        m_flag.store(false, std::memory_order_release);
        m_cancelCallback = nullptr;
    }
    
    /**
     * @brief 交换取消状态
     * @param other 另一个取消标志
     */
    void swap(AtomicCancelFlag& other) noexcept {
        // 双向对换: 单次 exchange 只把 other 拷入 this 并丢弃旧值,
        // 必须再写回 other 才是交换语义
        bool mine = m_flag.load(std::memory_order_relaxed);
        bool theirs = other.m_flag.exchange(mine, std::memory_order_acq_rel);
        m_flag.store(theirs, std::memory_order_release);
        std::swap(m_cancelCallback, other.m_cancelCallback);
    }
    
private:
    std::atomic<bool> m_flag;
    Callback m_cancelCallback;
};

/**
 * @brief 取消标志的共享指针类型
 * 
 * 使用 shared_ptr 确保取消标志的生命周期安全
 */
using AtomicCancelFlagPtr = std::shared_ptr<AtomicCancelFlag>;

/**
 * @brief 创建取消标志的工厂函数
 * @param initialValue 初始值
 * @return 取消标志的共享指针
 */
inline AtomicCancelFlagPtr makeAtomicCancelFlag(bool initialValue = false) {
    return std::make_shared<AtomicCancelFlag>(initialValue);
}

#endif // CORE_ATOMICCANCELFLAG_H
```

---

## core/CoordTransform.cpp

```cpp
// =============================================================================
// CoordTransform.cpp — 坐标转换完整实现
// =============================================================================
#include "CoordTransform.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
    constexpr double PI = M_PI;
    constexpr double TWOPI = 2.0 * PI;
    constexpr double DEG2RAD = PI / 180.0;
    constexpr double SEC_PER_DAY = 86400.0;

    inline double deg2rad(double d) { return d * DEG2RAD; }
    inline double rad2deg(double r) { return r / DEG2RAD; }
    
    // GMST 记忆化: 以"当天 0h 的 JD"为键的**单条目**缓存（O(1)，无线性扫描）。
    // 时间扫掠（粗搜 15s 步长 + 二分/采样回访）下相邻查询的 jd0 大量重复，
    // 命中即省去多项式；不命中只重算一次。
    //
    // 历史教训（性能回归修复）: 曾用 1000 项 FIFO + 容差 1e-8 天的 vector
    // 线性扫描缓存。扫掠时相邻 JD 相差 ~1.7e-4 天（≫ 容差）→ 几乎全部未命中，
    // 而每次未命中仍需扫满 1000 项；实测比不缓存慢 25~27 倍，占粗搜单步
    // 耗时约 39%。单条目按日键命中率高且无扫描成本。
    thread_local double memoJd0 = std::numeric_limits<double>::quiet_NaN();
    thread_local double memoGmstSec0 = 0.0;

    void clearGmstMemo() {
        memoJd0 = std::numeric_limits<double>::quiet_NaN();
        memoGmstSec0 = 0.0;
    }

    bool gmstMemoValid() {
        return std::isfinite(memoJd0);
    }
}

// ============================================================================
// gmstRad — IAU-1982 格林尼治平恒星时（弧度）
// 输入为 UT1 儒略日；可用 UTC 直接近似（UT1−UTC ≤ 0.9s），
// 切勿加 ΔT（ΔT = TT−UT1，加了得到 TT，地球自转角会系统性偏大 ~69s 恒星时）
//
// 为什么**不能**"升级"为 IAU-2006:
//   TEME 基于赤道/春分点框架，其定义与 SGP4 输出一致，必须用 IAU-1982 GMST
//   （Vallado"Revisiting Spacetrack Report #3"/AIAA 2006-6753 中 TOD↔PEF 即用
//   θ_GMST82；本工程内置 perturb 的 gstime_SGP4 亦用同一组系数）。
//   若误用 IAU-2006 的 ERA（基于 CIO）会引入 ERA−GMST ≈ 4612.156534″·T 的
//   偏差——2025 年即 1153″ ≈ 0.32° ≈ 地面上 36 km；即便只用 GMST06 表达式，
//   与 GMST82 的差异也仅 0.055″（1.7 m），收益远小于框架不一致的风险。
// ============================================================================
double CoordTransform::gmstRad(double jdUt1)
{
    // 输入验证（直接返回 NaN, 与相邻 eciDirToEcef 的高效模式一致,
    // 避免热路径中 throw/catch 的 10-100x 性能开销）
    if (!std::isfinite(jdUt1)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // 当天 0h UT1 的儒略日
    double jd0 = std::floor(jdUt1 - 0.5) + 0.5;

    // 记忆化: jd0 由 jdUt1 确定性导出, 精确相等才复用（无需容差）
    double gmstSec = 0.0;
    if (memoJd0 == jd0) {
        gmstSec = memoGmstSec0;
    } else {
        // J2000.0 起算的儒略世纪
        double T = (jd0 - 2451545.0) / 36525.0;

        // 检查T的有效性
        if (!std::isfinite(T)) {
            return std::numeric_limits<double>::quiet_NaN();
        }

        // 当天 0h 的 GMST（秒）: IAU-1982 公式
        gmstSec = 24110.54841
                + 8640184.812866 * T
                + 0.093104 * T * T
                - 6.2e-6 * T * T * T;

        // 检查gmstSec的有效性
        if (!std::isfinite(gmstSec)) {
            return std::numeric_limits<double>::quiet_NaN();
        }

        memoJd0 = jd0;
        memoGmstSec0 = gmstSec;
    }

    // 加上当天 UT1 小数的贡献（恒星日 / 太阳日 ≈ 1.00273790935）
    double fracDay = (jdUt1 - jd0);
    gmstSec += SEC_PER_DAY * fracDay * 1.00273790935;

    // 检查fracDay的有效性
    if (!std::isfinite(fracDay)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // 取模 → 转为弧度 → 正规化到 [0, 2π)
    gmstSec = std::fmod(gmstSec, SEC_PER_DAY);
    if (gmstSec < 0.0) gmstSec += SEC_PER_DAY;

    double gmstRadVal = gmstSec * TWOPI / SEC_PER_DAY;

    // 确保结果在有效范围内
    if (!std::isfinite(gmstRadVal)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // 规范化到[0, 2π)范围
    gmstRadVal = std::fmod(gmstRadVal, TWOPI);
    if (gmstRadVal < 0.0) gmstRadVal += TWOPI;

    return gmstRadVal;
}

// ============================================================================
// deltaTSeconds — ΔT = TT − UT1（秒）
// Espenak & Meeus (2006) 分段多项式（NASA deltatpoly2004）:
//   1900~2050 各分段 + 2050~2150 段（含 −0.5628·(2150−y) 连续性修正项）
//   + ≥2150 长期抛物线; <1900 取 1900 边界值
// 应用内历元范围之外时取边界值，保证连续
// ============================================================================
double CoordTransform::deltaTSeconds(double jdUtc)
{
    // 输入验证
    if (!std::isfinite(jdUtc)) {
        return std::numeric_limits<double>::quiet_NaN();  // NaN 哨兵
    }

    // 儒略日 → 年份（近似，足够 ΔT 使用）
    double year = 2000.0 + (jdUtc - 2451545.0) / 365.2425;
    
    // 检查年份的有效性
    if (!std::isfinite(year)) {
        return std::numeric_limits<double>::quiet_NaN();  // NaN 哨兵
    }

    if (year < 1900.0) {
        // 取 1900 年处边界值（t = y − 1900 = 0），保证连续
        return -2.79;
    }
    if (year < 1920.0) {
        // Espenak & Meeus 标准定义 t 以"年"计（t = y − 1900）,
        // 不做世纪缩放——误除 100 会让段内值停在 −2.79 附近,
        // 与下一段起点 21.20s 形成 ~24s 边界跳变
        double t = year - 1900.0;
        if (!std::isfinite(t)) return -2.79;  // 无效t值返回边界值
        return -2.79 + 1.494119 * t - 0.0598939 * t * t
             + 0.0061966 * t * t * t - 0.000197 * t * t * t * t;
    }
    if (year < 1941.0) {
        double t = year - 1920.0;
        if (!std::isfinite(t)) return 21.20;  // 无效t值返回边界值
        return 21.20 + 0.84493 * t - 0.076100 * t * t
             + 0.0020936 * t * t * t;
    }
    if (year < 1961.0) {
        double t = year - 1950.0;
        if (!std::isfinite(t)) return 29.07;  // 无效t值返回边界值
        double term1 = 29.07 + 0.407 * t;
        double term2 = -t * t / 233.0;
        double term3 = t * t * t / 2547.0;
        if (!std::isfinite(term2) || !std::isfinite(term3)) return term1;  // 无效项返回部分结果
        return term1 + term2 + term3;
    }
    if (year < 1986.0) {
        double t = year - 1975.0;
        if (!std::isfinite(t)) return 45.45;  // 无效t值返回边界值
        double term1 = 45.45 + 1.067 * t;
        double term2 = -t * t / 260.0;
        double term3 = -t * t * t / 718.0;
        if (!std::isfinite(term2) || !std::isfinite(term3)) return term1;  // 无效项返回部分结果
        return term1 + term2 + term3;
    }
    if (year < 2005.0) {
        double t = year - 2000.0;
        if (!std::isfinite(t)) return 63.86;  // 无效t值返回边界值
        double term1 = 63.86 + 0.3345 * t - 0.060374 * t * t;
        double term2 = 0.0017275 * t * t * t;
        double term3 = 0.000651814 * t * t * t * t;
        double term4 = 0.00002373599 * t * t * t * t * t;
        if (!std::isfinite(term2) || !std::isfinite(term3) || !std::isfinite(term4)) return term1;  // 无效项返回部分结果
        return term1 + term2 + term3 + term4;
    }
    if (year < 2050.0) {
        double t = year - 2000.0;
        if (!std::isfinite(t)) return 62.92;  // 无效t值返回边界值
        double term1 = 62.92 + 0.32217 * t;
        double term2 = 0.005589 * t * t;
        if (!std::isfinite(term2)) return term1;  // 无效项返回部分结果
        return term1 + term2;
    }
    // 2050 ~ 2150: Espenak & Meeus (2006) 分段式（NASA deltatpoly2004）
    //   ΔT = −20 + 32·u² − 0.5628·(2150 − year),  u = (year − 1820)/100
    // 末项用于消除 2050 处的不连续: 本段在 2050.0 处与 2005~2050 段同为
    // 93.00 s, 在 2150 处与长期式同为 328.48 s（缺该项会在 2050 跳变 +56.28 s）
    // ≥ 2150: 长期抛物线 ΔT = −20 + 32·u²（即下式的 term2 = 0）
    const double t = (year - 1820.0) / 100.0;
    if (!std::isfinite(t)) return -20.0;  // 无效t值返回边界值
    const double term1 = -20.0 + 32.0 * t * t;
    const double term2 = (year < 2150.0) ? -0.5628 * (2150.0 - year) : 0.0;
    if (!std::isfinite(term1) || !std::isfinite(term2)) return -20.0;
    return term1 + term2;
}

// ============================================================================
// eciToEcef — TEME → ECEF（绕 Z 轴旋转 GMST 角）
// ============================================================================
std::array<double, 3> CoordTransform::eciToEcef(
    const std::array<double, 3> &posTeme, double jdUt1)
{
    // 输入验证（直接返回 NaN, 避免热路径中 throw/catch 性能开销）
    if (!std::isfinite(jdUt1)) {
        return {NAN, NAN, NAN};
    }
    
    // 检查位置矢量
    if (!std::isfinite(posTeme[0]) || !std::isfinite(posTeme[1]) || !std::isfinite(posTeme[2])) {
        return {NAN, NAN, NAN};
    }

    double theta = gmstRad(jdUt1);
    if (!std::isfinite(theta)) {
        return {NAN, NAN, NAN};
    }
    
    double c = std::cos(theta);
    double s = std::sin(theta);
    
    // 检查三角函数值的有效性
    if (!std::isfinite(c) || !std::isfinite(s)) {
        return {NAN, NAN, NAN};
    }

    double x = posTeme[0] * c + posTeme[1] * s;
    double y = -posTeme[0] * s + posTeme[1] * c;
    double z = posTeme[2];

    // 检查结果的有效性
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return {NAN, NAN, NAN};
    }

    return {x, y, z};
}

// ============================================================================
// eciDirToEcef — 方向单位矢量 ECI → ECEF（仅旋转 GMST）
// ============================================================================
std::array<double, 3> CoordTransform::eciDirToEcef(
    const std::array<double, 3> &dirTeme, double jdUt1)
{
    // 输入验证（失败一律 NaN 哨兵: 与真实方向矢量可区分, NaN 比较自然拦截）
    if (!std::isfinite(jdUt1)) {
        return {NAN, NAN, NAN};  // 无效输入返回 NaN
    }
    
    // 检查方向矢量
    if (!std::isfinite(dirTeme[0]) || !std::isfinite(dirTeme[1]) || !std::isfinite(dirTeme[2])) {
        return {NAN, NAN, NAN};  // 无效方向返回 NaN
    }

    double theta = gmstRad(jdUt1);
    if (!std::isfinite(theta)) {
        return {NAN, NAN, NAN};  // 无效角度返回 NaN
    }
    
    double c = std::cos(theta);
    double s = std::sin(theta);
    
    // 检查三角函数值的有效性
    if (!std::isfinite(c) || !std::isfinite(s)) {
        return {NAN, NAN, NAN};  // 无效三角函数返回 NaN
    }

    double x = dirTeme[0] * c + dirTeme[1] * s;
    double y = -dirTeme[0] * s + dirTeme[1] * c;
    double z = dirTeme[2];

    // 检查结果的有效性
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return {NAN, NAN, NAN};  // 无效结果返回 NaN
    }

    return {x, y, z};
}

// ============================================================================
// eciStateToEcef — TEME 位置+速度 → ECEF（含地球自转哥里奥利项）
// ============================================================================
std::pair<std::array<double, 3>, std::array<double, 3>>
CoordTransform::eciStateToEcef(const std::array<double, 3> &posTeme,
                               const std::array<double, 3> &velTeme,
                               double jdUt1)
{
    // 输入验证（失败一律 NaN 哨兵: 与同文件 eciToEcef/eciDirToEcef 的
    // 策略一致——{0,0,0} 是合法地心坐标, 零值哨兵会被下游当真实数据
    // 计算出看似有效的错误结果; NaN 经 isfinite/NaN 比较自然拦截）
    if (!std::isfinite(jdUt1)) {
        return {{NAN, NAN, NAN}, {NAN, NAN, NAN}};
    }
    
    // 检查位置和速度矢量
    if (!std::isfinite(posTeme[0]) || !std::isfinite(posTeme[1]) || !std::isfinite(posTeme[2]) ||
        !std::isfinite(velTeme[0]) || !std::isfinite(velTeme[1]) || !std::isfinite(velTeme[2])) {
        return {{NAN, NAN, NAN}, {NAN, NAN, NAN}};
    }

    double theta = gmstRad(jdUt1);
    if (!std::isfinite(theta)) {
        return {{NAN, NAN, NAN}, {NAN, NAN, NAN}};
    }
    
    double c = std::cos(theta);
    double s = std::sin(theta);
    
    // 检查三角函数值的有效性
    if (!std::isfinite(c) || !std::isfinite(s)) {
        return {{NAN, NAN, NAN}, {NAN, NAN, NAN}};
    }

    std::array<double, 3> posEcef = {
        posTeme[0] * c + posTeme[1] * s,
       -posTeme[0] * s + posTeme[1] * c,
         posTeme[2]
    };

    // 惯性速度先旋转到地面系参考，再减去地球自转牵连速度:
    // v_ECEF = R(θ)·v_TEME − ω×r_ECEF
    // ω×r = (0, 0, ω)×(rx, ry, rz) = (−ω·ry, ω·rx, 0)
    // 与 perturb SGP4 保持一致的地球自转速率 (rad/s)
    constexpr double OMEGA_EARTH_RAD_S = 7.292115e-5;
    std::array<double, 3> velEcef = {
        velTeme[0] * c + velTeme[1] * s + OMEGA_EARTH_RAD_S * posEcef[1],
       -velTeme[0] * s + velTeme[1] * c - OMEGA_EARTH_RAD_S * posEcef[0],
         velTeme[2]
    };
    
    // 检查结果的有效性
    for (const auto& vec : {posEcef, velEcef}) {
        if (!std::isfinite(vec[0]) || !std::isfinite(vec[1]) || !std::isfinite(vec[2])) {
            return {{NAN, NAN, NAN}, {NAN, NAN, NAN}};  // 无效结果返回 NaN 哨兵
        }
    }

    return { posEcef, velEcef };
}

// ============================================================================
// ecefToGeodetic — Bowring 1976 闭式解法: ECEF → WGS84 经纬高
// ============================================================================
GeoPos CoordTransform::ecefToGeodetic(const std::array<double, 3> &p)
{
    double x = p[0], y = p[1], z = p[2];
    
    // 输入验证
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return GeoPos(std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN());
    }

    double r = std::sqrt(x * x + y * y);

    // WGS84 椭球参数
    double a  = WGS84_A_KM;
    double f  = 1.0 / WGS84_F_INV;
    double b  = a * (1.0 - f);
    double e2 = WGS84_E2;
    double ep2 = e2 / (1.0 - e2);  // e'² = e²/(1−e²)

    // 经度（直接解析公式）
    double lonDeg = std::atan2(y, x) * 180.0 / PI;
    
    // 确保经度在有效范围内（保留 ±180 原值, 仅回绕越界值）
    if (lonDeg > 180.0) lonDeg -= 360.0;
    else if (lonDeg < -180.0) lonDeg += 360.0;

    // 纬度 — Bowring 1976 闭式解（参数纬度 θ 一次性修正，精度 < 1e-9°）
    //   θ = atan(z·a / (r·b))          参数纬度
    //   φ = atan2(z + e'²·b·sin³θ, r − e²·a·cos³θ)
    if (r < 1e-12) {
        double latDeg = (z >= 0.0) ? 90.0 : -90.0;
        double altKm = std::fabs(z) - b;
        return GeoPos(latDeg, lonDeg, altKm);
    }

    double theta = std::atan2(z * a, r * b);
    double sinT = std::sin(theta), cosT = std::cos(theta);
    
    // 检查三角函数值的有效性
    if (!std::isfinite(sinT) || !std::isfinite(cosT)) {
        return GeoPos(std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN());
    }

    double lat = std::atan2(z + ep2 * b * sinT * sinT * sinT,
                            r - e2 * a * cosT * cosT * cosT);
    
    // 检查纬度计算结果
    if (!std::isfinite(lat)) {
        return GeoPos(std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN());
    }

    // 高度/纬度固定点迭代（Bowring 闭式解做初值，2~3 次即收敛）
    // 极区 cos(φ)→0 时 r/cos(φ) 除零 → 改用 z 反解: h = z/sinφ − N(1−e²)
    for (int i = 0; i < 3; ++i) {
        double sinL = std::sin(lat);
        double cosL = std::cos(lat);
        
        // 检查三角函数值的有效性
        if (!std::isfinite(sinL) || !std::isfinite(cosL)) {
            break;  // 无效计算退出迭代
        }
        
        double Nl = a / std::sqrt(1.0 - e2 * sinL * sinL);
        
        // 检查Nl的有效性
        if (!std::isfinite(Nl) || Nl <= 0.0) {
            break;  // 无效计算退出迭代
        }
        
        double h;
        if (std::fabs(cosL) > 1e-9) {
            h = r / cosL - Nl;
        } else {
            // 极点处理
            if (std::fabs(sinL) < 1e-12) {
                break;  // 极点附近数值不稳定
            }
            h = z / sinL - Nl * (1.0 - e2);   // 极点: z 与 sinφ 同号
        }
        
        // 检查高度计算的有效性
        if (!std::isfinite(h)) {
            break;  // 无效计算退出迭代
        }
        
        double new_lat = std::atan2(z, r * (1.0 - e2 * Nl / (Nl + h)));
        
        // 检查新纬度的有效性
        if (!std::isfinite(new_lat)) {
            break;  // 无效计算退出迭代
        }
        
        // 检查收敛性
        if (std::fabs(new_lat - lat) < 1e-12) {
            lat = new_lat;
            break;
        }
        
        lat = new_lat;
    }

    double sinLat = std::sin(lat);
    double N = a / std::sqrt(1.0 - e2 * sinLat * sinLat);
    double altKm;
    
    // 检查N的有效性
    if (!std::isfinite(N) || N <= 0.0) {
        altKm = 0.0;  // 无效N值使用零高度
    } else if (std::fabs(std::cos(lat)) > 1e-9) {
        altKm = r / std::cos(lat) - N;
    } else {
        // 极点处理
        if (std::fabs(sinLat) < 1e-12) {
            altKm = 0.0;  // 极点高度设为0
        } else {
            altKm = z / sinLat - N * (1.0 - e2);
        }
    }
    
    // 确保高度有效
    if (!std::isfinite(altKm)) {
        altKm = 0.0;
    }

    // 确保纬度在有效范围内
    double latDeg = lat * 180.0 / PI;
    latDeg = std::clamp(latDeg, -90.0, 90.0);

    return GeoPos(latDeg, lonDeg, altKm);
}

// ============================================================================
// geodeticToEcef — 大地经纬高 → ECEF
// ============================================================================
std::array<double, 3> CoordTransform::geodeticToEcef(const GeoPos &geo)
{
    // 输入验证
    if (!std::isfinite(geo.latDeg) || !std::isfinite(geo.lonDeg) || !std::isfinite(geo.altKm)) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    // 确保纬度在有效范围内
    double latDeg = std::clamp(geo.latDeg, -90.0, 90.0);
    double lonDeg = geo.lonDeg;
    
    // 规范化经度到[-180, 180]范围（保留 ±180 原值, 仅回绕越界值）
    if (lonDeg > 180.0) lonDeg -= 360.0;
    else if (lonDeg < -180.0) lonDeg += 360.0;

    double lat = deg2rad(latDeg);
    double lon = deg2rad(lonDeg);
    double alt = geo.altKm;

    double sinLat = std::sin(lat);
    double cosLat = std::cos(lat);
    
    // 检查三角函数值的有效性
    if (!std::isfinite(sinLat) || !std::isfinite(cosLat)) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    // 计算卯酉圈曲率半径N，避免数值不稳定
    double e2_sin2 = WGS84_E2 * sinLat * sinLat;
    if (e2_sin2 >= 1.0) {
        // 纬度过大导致数值不稳定
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }
    
    double N = WGS84_A_KM / std::sqrt(1.0 - e2_sin2);
    
    // 检查N的有效性
    if (!std::isfinite(N) || N <= 0.0) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    // 计算各分量
    double cosLon = std::cos(lon);
    double sinLon = std::sin(lon);
    
    // 检查经度三角函数值的有效性
    if (!std::isfinite(cosLon) || !std::isfinite(sinLon)) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    double N_plus_alt = N + alt;
    double x = N_plus_alt * cosLat * cosLon;
    double y = N_plus_alt * cosLat * sinLon;
    double z = ((1.0 - WGS84_E2) * N + alt) * sinLat;

    // 检查最终结果的有效性
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    return {x, y, z};
}

// ============================================================================
// ecefToEnuComponents — ECEF 相对矢量 → 站心 (East, North, Up) 分量
// ============================================================================
std::array<double, 3> CoordTransform::ecefToEnuComponents(
    const std::array<double, 3> &d,
    double staLatDeg, double staLonDeg)
{
    // 输入验证
    if (!std::isfinite(staLatDeg) || !std::isfinite(staLonDeg)) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }
    
    // 检查相对矢量
    if (!std::isfinite(d[0]) || !std::isfinite(d[1]) || !std::isfinite(d[2])) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    // 确保纬度在有效范围内
    double latDeg = std::clamp(staLatDeg, -90.0, 90.0);
    double lonDeg = staLonDeg;
    
    // 规范化经度到[-180, 180]范围（保留 ±180 原值, 仅回绕越界值）
    if (lonDeg > 180.0) lonDeg -= 360.0;
    else if (lonDeg < -180.0) lonDeg += 360.0;

    double lat = deg2rad(latDeg);
    double lon = deg2rad(lonDeg);

    double sLat = std::sin(lat), cLat = std::cos(lat);
    double sLon = std::sin(lon), cLon = std::cos(lon);
    
    // 检查三角函数值的有效性
    if (!std::isfinite(sLat) || !std::isfinite(cLat) || 
        !std::isfinite(sLon) || !std::isfinite(cLon)) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    // 计算各分量
    double east = -sLon * d[0] + cLon * d[1];
    double north = -sLat * cLon * d[0] - sLat * sLon * d[1] + cLat * d[2];
    double up = cLat * cLon * d[0] + cLat * sLon * d[1] + sLat * d[2];

    // 检查结果的有效性
    if (!std::isfinite(east) || !std::isfinite(north) || !std::isfinite(up)) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    return {east, north, up};
}

// ============================================================================
// computeLookAngle — 一站式计算仰角、方位角、斜距
// ============================================================================
LookAngle CoordTransform::computeLookAngle(
    const std::array<double, 3> &satECEF,
    const std::array<double, 3> &staECEF,
    double staLatDeg, double staLonDeg)
{
    // 输入验证（失败返回 NaN: 与 eciToEcef 策略对齐, 调用方
    // elevationAt 通过 !std::isfinite 检查自然拦截）
    if (!std::isfinite(staLatDeg) || !std::isfinite(staLonDeg)) {
        return LookAngle(std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::quiet_NaN());
    }

    // 相对矢量
    std::array<double, 3> d = {
        satECEF[0] - staECEF[0],
        satECEF[1] - staECEF[1],
        satECEF[2] - staECEF[2]
    };

    // 检查输入坐标的有效性
    for (const auto& coord : {satECEF, staECEF}) {
        if (!std::isfinite(coord[0]) || !std::isfinite(coord[1]) || !std::isfinite(coord[2])) {
            return LookAngle(std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN());
        }
    }

    auto enu = ecefToEnuComponents(d, staLatDeg, staLonDeg);
    double e = enu[0], n = enu[1], u = enu[2];
    double range = std::sqrt(e * e + n * n + u * u);

    if (range < 1e-9) {
        return LookAngle(90.0, 0.0, 0.0);  // 卫星在头顶（理想情况）
    }

    // 检查ENU分量是否有效
    if (!std::isfinite(e) || !std::isfinite(n) || !std::isfinite(u)) {
        return LookAngle(std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::quiet_NaN());
    }

    // 计算归一化的高度分量，并进行严格范围检查
    double u_normalized = u / range;
    
    // 防止浮点误差导致超出[-1, 1]范围
    if (u_normalized < -1.0) u_normalized = -1.0;
    else if (u_normalized > 1.0) u_normalized = 1.0;
    
    // 检查结果是否为有效数值
    if (!std::isfinite(u_normalized)) {
        return LookAngle(std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::quiet_NaN());
    }

    double elDeg = std::asin(u_normalized) * 180.0 / PI;
    
    // 计算方位角
    double azDeg = std::atan2(e, n) * 180.0 / PI;    // atan2(E,N): 0°=N, 90°=E
    if (azDeg < 0.0) azDeg += 360.0;

    // 确保结果在有效范围内
    elDeg = std::clamp(elDeg, -90.0, 90.0);
    azDeg = std::fmod(azDeg, 360.0);
    if (azDeg < 0.0) azDeg += 360.0;

    return LookAngle(elDeg, azDeg, range);
}

// ============================================================================
// applyRefraction — Saastamoinen 大气折射修正（支持气象参数）
// 公式: R(arcmin) = 1.02 / tan(h + 10.3/(h + 5.11)) × P/(1013.25 - 0.254×e)
// e = 水汽压(hPa), 由 Buck 1981 公式从温度/湿度计算
// 原式输出单位为【角分】——转度需 /60。此前注释误标「角秒」并再 /60,
// 使折射量偏小 60 倍（地平仅 ~29″, 应为 ~29′ = 0.48°），与 STK 11 明显不符。
// ============================================================================
double CoordTransform::applyRefraction(double elGeomDeg,
                                       double tempC, double pressHpa, double humidityPct)
{
    if (!std::isfinite(elGeomDeg)) return elGeomDeg;
    if (elGeomDeg < 0.0) return elGeomDeg;     // 地平以下不修正（掩星角 ≥ 0）
    if (elGeomDeg >= 88.0) return elGeomDeg;   // 近天顶原式数值失稳

    // 水汽压修正 (Buck 1981): e_s = 6.1121·exp(17.502·T/(240.97+T)) hPa
    double e = 6.1121 * std::exp(17.502 * tempC / (240.97 + tempC)) * humidityPct / 100.0;

    // Saastamoinen 折射量（角分）
    // 注: 原式含 cos(2φ) 重力修正项，此处省略（无纬度参数）
    double h = elGeomDeg;
    double R_arcmin = (1.02 / std::tan(deg2rad(h + 10.3 / (h + 5.11))))
                    * (pressHpa / (1013.25 - 0.254 * e));

    double result = elGeomDeg + R_arcmin / 60.0;   // 角分 → 度

    if (!std::isfinite(result) || result < elGeomDeg)
        return elGeomDeg;

    return result;
}

// ============================================================================
// 缓存管理函数
// 注意: GMST 记忆化为 thread_local（每线程独立）——clearCache()/getCacheSize()
// 只作用/反映当前调用线程; worker 池各线程的 memo 不受主线程清理影响,
// 属设计内行为（缓存隔离避免并发 UB 的代价）。
// 条目数为 0（空）或 1（已缓存"当天 0h 的 GMST"）
// ============================================================================
void CoordTransform::clearCache() {
    clearGmstMemo();
}

size_t CoordTransform::getCacheSize() {
    return gmstMemoValid() ? 1u : 0u;
}
```

---

## core/CoordTransform.h

```cpp
#ifndef CORE_COORDTRANSFORM_H
#define CORE_COORDTRANSFORM_H
// =============================================================================
// CoordTransform.h — 坐标转换工具集
// GMST | TEME(ECI) ↔ ECEF | ECEF ↔ 大地经纬高 | ENU 站心 | 大气折射
// 所有函数为纯静态方法，无状态
// =============================================================================
#include "DataTypes.h"
#include <array>
#include <utility>

class CoordTransform {
public:
    // WGS-84 椭球参数
    static constexpr double WGS84_A_KM       = 6378.137;       // 半长轴 km
    static constexpr double WGS84_F_INV      = 298.257223563;  // 扁率倒数
    static constexpr double WGS84_E2         = 2.0 / WGS84_F_INV
                                             - 1.0 / (WGS84_F_INV * WGS84_F_INV);

    // ---- GMST 记忆化 ----
    // 实现为 thread_local 单条目缓存（键 = "当天 0h 的儒略日"），O(1) 无扫描。
    // 旧实现为 1000 项 FIFO + 容差 1e-8 天的线性扫描：扫掠调用下几乎全不命中
    // 且每次未命中都要扫满 1000 项，实测比不计算更慢（详见 .cpp 注释）

    // 清除当前线程的记忆化条目
    static void clearCache();

    // 当前线程记忆化条目数（0 = 空, 1 = 已缓存"当天 0h 的 GMST"）
    static size_t getCacheSize();

    // ---- GMST 格林尼治平恒星时 ----
    // jdUt1: 儒略日（UT1，可用 UTC 近似，差值 < 0.9s）
    // 返回: GMST 弧度，范围 [0, 2π)
    static double gmstRad(double jdUt1);

    // ---- ΔT = TT − UT1（秒）----
    // Espenak & Meeus (2006)/NASA deltatpoly2004 分段多项式:
    //   1900~2050 各分段 + 2050~2150 段（含 −0.5628·(2150−y) 连续性修正项）
    //   + ≥2150 长期抛物线; <1900 取 1900 边界值（−2.79 s）
    // 仅用于太阳/行星历表（TT 时标）: jdTT = jdUtc + ΔT/86400
    // 注意: GMST/地球自转需要 UT1，而 UT1 ≈ UTC（≤ 0.9s），不可加 ΔT
    static double deltaTSeconds(double jdUtc);

    // ---- TEME(ECI) → ECEF ----
    // 绕 Z 轴旋转 +GMST 角（将惯性系旋转到与地球同步）
    // 输入: posTeme — TEME 系位置 km; jdUt1 — 儒略日
    // 返回: ECEF 位置 km
    static std::array<double, 3> eciToEcef(
        const std::array<double, 3> &posTeme, double jdUt1);

    // ---- 方向矢量 ECI → ECEF（仅旋转 GMST，不平移） ----
    // 用于太阳方向等单位矢量：必须与卫星位置处于同一坐标系才能做点积
    static std::array<double, 3> eciDirToEcef(
        const std::array<double, 3> &dirTeme, double jdUt1);

    // ---- TEME 位置+速度 → ECEF（含地球自转哥里奥利项） ----
    // velECEF = R(θ)·velTeme − ω×rECEF，ω = 地球自转角速度
    // 返回 {posECEF, velECEF}，供多普勒等需要地面系速度的场合
    static std::pair<std::array<double, 3>, std::array<double, 3>>
        eciStateToEcef(const std::array<double, 3> &posTeme,
                       const std::array<double, 3> &velTeme, double jdUt1);

    // ---- ECEF → 大地经纬高（Bowring 1976 迭代法） ----
    static GeoPos ecefToGeodetic(const std::array<double, 3> &ecef);

    // ---- 大地经纬高 → ECEF ----
    static std::array<double, 3> geodeticToEcef(const GeoPos &geo);

    // ---- 站心 ENU 分量 ----
    // relVec = satECEF − staECEF，输入为差值矢量
    // 返回 (East, North, Up) 分量 km
    static std::array<double, 3> ecefToEnuComponents(
        const std::array<double, 3> &relVec,
        double staLatDeg, double staLonDeg);

    // ---- 整站: 卫星 ECEF + 站 ECEF + 站经纬 → LookAngle ----
    static LookAngle computeLookAngle(
        const std::array<double, 3> &satECEF,
        const std::array<double, 3> &staECEF,
        double staLatDeg, double staLonDeg);

    // ---- 大气折射修正 ----
    // Saastamoinen 1972:
    //   R(arcmin) = 1.02/tan(h + 10.3/(h + 5.11)) × P/(1013.25 - 0.254×e)
    // e = 水汽压(hPa), 由 Buck 1981 公式从温度/湿度计算; 原式输出为角分
    // elGeomDeg: 几何仰角（度），必须 ≥ 0°
    // 返回: 视仰角（度），已施加大气折射
    // 标准大气(与 STK 11 一致): T=10°C, P=1013.25 hPa, 湿度=40%
    static double applyRefraction(double elGeomDeg,
                                  double tempC, double pressHpa, double humidityPct);
};

#endif // CORE_COORDTRANSFORM_H
```

---

## core/DataTypes.h

```cpp
#ifndef CORE_MODELS_H
#define CORE_MODELS_H
// =============================================================================
// DataTypes.h — 卫星过境预报系统 全部数据结构定义
// 纯 C++17，零 Qt 依赖，可独立编译和单元测试
// =============================================================================
#include <cstdint>
#include <limits>
#include <string>
#include <array>
#include <vector>
#include <cmath>

// ---------------------------------------------------------------------------
// JD epoch 偏移量: Unix epoch (1970-01-01T00:00:00Z) - JD0 (1858-11-17T00:00:00Z)
// 即 JD0 到 Unix epoch 的毫秒数: 2440587.5 * 86400000 = 210866760000000
// ---------------------------------------------------------------------------
constexpr int64_t kJdEpochOffsetMs = 210866760000000LL;

// ---------------------------------------------------------------------------
// 轨道模型类别
// ---------------------------------------------------------------------------
enum class Sgp4Type {
    LEO_SGP4    = 1,   // 近地：周期 < 225 min（平运动 > 6.4 rev/day）
    MEO_SDP4    = 2,   // 深空/中轨：周期 225 min ~ 1400 min（平运动 1.03 ~ 6.4 rev/day）
    GEO_SDP4_DS = 3    // 深空/同步带：周期 ≥ 1400 min（平运动 ≤ 1.03 rev/day），
                       // 含同步带 1436 min 及其上下漂移轨道、超同步处置轨道
};

// ---------------------------------------------------------------------------
// 重力模型选择（SGP4/SDP4 内部常数差异）
// ---------------------------------------------------------------------------
enum class GravityModel {
    WGS72     = 0,  // WGS-72 标准（NORAD TLE 默认）
    WGS84     = 1   // WGS-84（部分特殊 TLE 使用）
};

// ---------------------------------------------------------------------------
// TLE 解析结果
// ---------------------------------------------------------------------------
struct TleData {
    std::string name;         // 卫星名称（来自 TLE 行0 或 NORAD 编号）
    int    noradID   = 0;     // NORAD 编号
    int    epochYear = 0;     // 历元年（4 位，如 2024）
    double epochDay  = 0.0;   // 历元日 + 小数（如 83.56167013）
    double bstar     = 0.0;   // B* 大气阻力项（1/地球半径）
    double inclinationDeg  = 0.0;   // 倾角（度）
    double raanDeg         = 0.0;   // 升交点赤经（度）
    double eccentricity    = 0.0;   // 偏心率
    double argPerigeeDeg   = 0.0;   // 近地点辐角（度）
    double meanAnomalyDeg  = 0.0;   // 平近点角（度）
    double meanMotionRevDay = 0.0;  // 平运动（rev/day）
    int    revNumber = 0;           // 圈号（历元时刻）
    int    elsetNum  = 0;           // 元素集编号
    Sgp4Type ephemerisType = Sgp4Type::LEO_SGP4;
    bool   checksumValid = false;   // 校验和是否通过
    bool   isDecayed     = false;   // 是否已衰减

    // 计算轨道周期（分钟）
    double periodMinutes() const {
        if (meanMotionRevDay <= 0.0) return 0.0;
        return 1440.0 / meanMotionRevDay;
    }
};

// ---------------------------------------------------------------------------
// TEME 系（准惯性系）瞬时状态
// ---------------------------------------------------------------------------
struct EciState {
    std::array<double, 3> posKm  = {0, 0, 0};   // 位置 (x, y, z) km
    std::array<double, 3> velKms = {0, 0, 0};   // 速度 (vx, vy, vz) km/s
};

// ---------------------------------------------------------------------------
// WGS84 大地坐标
// ---------------------------------------------------------------------------
struct GeoPos {
    double latDeg = 0.0;   // 纬度（度），北正南负，范围 [-90, +90]
    double lonDeg = 0.0;   // 经度（度），东正西负，范围 [-180, +180]
    double altKm  = 0.0;   // 椭球面以上高度（km）

    GeoPos() = default;
    GeoPos(double lat, double lon, double alt)
        : latDeg(lat), lonDeg(lon), altKm(alt) {}
};

// ---------------------------------------------------------------------------
// 站心观测量（仰角/方位角/斜距）
// ---------------------------------------------------------------------------
struct LookAngle {
    double elevDeg = 0.0;   // 仰角（度），地平 0°，天顶 90°
    double azDeg   = 0.0;   // 方位角（度），北 0°，顺时针（东 90°，南 180°，西 270°）
    double rangeKm = 0.0;   // 斜距（km）

    LookAngle() = default;
    LookAngle(double el, double az, double rng)
        : elevDeg(el), azDeg(az), rangeKm(rng) {}
};

// ---------------------------------------------------------------------------
// 太阳位置（地面站所见）
// ---------------------------------------------------------------------------
struct SunPosition {
    double azDeg      = 0.0;   // 太阳方位角（度），北 0°，顺时针
    double altDeg     = 0.0;   // 太阳高度角（度），地平 0°，地平下为负
    double raDeg      = 0.0;   // 太阳赤经（度）
    double decDeg     = 0.0;   // 太阳赤纬（度）
    double distanceAU = 1.0;   // 日地距离（天文单位）

    // 太阳地心方向单位矢量（赤道系，随当日历元）
    // 用于卫星地影判定前，必须先经 CoordTransform::eciDirToEcef
    // 旋转到 ECEF 地固系与卫星位置同系（EarthShadow::checkShadow 要求）
    std::array<double, 3> dirECI = {1, 0, 0};
};

// ---------------------------------------------------------------------------
// 卫星地影状态
// ---------------------------------------------------------------------------
enum class ShadowStatus {
    FullSunlight = 0,   // 完全日照
    InPenumbra   = 1,   // 半影（部分日照）
    InUmbra      = 2    // 本影（完全无光照）
};

// ---------------------------------------------------------------------------
// 过境可见性
// ---------------------------------------------------------------------------
enum class PassVisibility {
    Visible   = 0,   // 肉眼可见（暗天 + 卫星被照亮）
    RadioOnly = 1,   // 仅无线电可用（暗天 + 卫星在地影中）
    Daylight  = 2    // 白天过境（天未暗）
};

// ---------------------------------------------------------------------------
// 过境质量
// ---------------------------------------------------------------------------
enum class PassQuality {
    Excellent = 0,   // 最大仰角 ≥ 50°
    Good      = 1,   // 最大仰角 20°~50°
    Poor      = 2    // 最大仰角 < 20°
};

// ---------------------------------------------------------------------------
// JD ↔ intMs 转换（详情视图与导出共用）
// ---------------------------------------------------------------------------
inline int64_t jdToMs(double jd) {
    if (!std::isfinite(jd)) return 0;
    const double maxSafeJd = 9.2e18 / 86400000.0;
    const double minSafeJd = -9.2e18 / 86400000.0;
    if (jd > maxSafeJd) return std::numeric_limits<int64_t>::max();
    if (jd < minSafeJd) return std::numeric_limits<int64_t>::min();
    int64_t jdInt = static_cast<int64_t>(std::floor(jd));
    double jdFrac = jd - jdInt;
    int64_t intMs = jdInt * 86400000LL;
    int64_t fracMs = static_cast<int64_t>(std::round(jdFrac * 86400000.0));
    if (fracMs >= 86400000LL) { intMs += 86400000LL; fracMs -= 86400000LL; }
    else if (fracMs < 0)      { intMs -= 86400000LL; fracMs += 86400000LL; }
    return intMs + fracMs;
}
inline double msToJd(int64_t ms) {
    const int64_t msPerDay = 86400000LL;
    int64_t days = ms / msPerDay;
    int64_t rem  = ms % msPerDay;
    if (ms < 0 && rem != 0) { days--; rem += msPerDay; }
    return static_cast<double>(days) + static_cast<double>(rem) / 86400000.0;
}

// ---------------------------------------------------------------------------
// 过境弧段采样点（绘图/导出用，由 OrbitCalculator 在过境区间内等步长采集）
// ---------------------------------------------------------------------------
struct PassSample {
    int64_t  jdMs = 0;    // 时刻（毫秒儒略日）
    LookAngle look;       // 站心观测角（仰角/方位角/斜距）
    GeoPos    geo;        // 星下点（经纬高）
    double    dopplerKHz = 0.0;   // 该时刻多普勒频移（enableDoppler 时有效）
};

// ---------------------------------------------------------------------------
// 一次完整过境事件（共 22 个字段 + 光照比例 + 弧段采样）
// 时刻统一用 int64_t 毫秒儒略日，参考点为 JD 原点（JD×86400000 取整），
// 与 Unix epoch 毫秒（1970-01-01）不同，转换公式：
//   epoch_ms = jdMs − 210866760000000LL   （即减去 2440587.5 × 86400000）
// ---------------------------------------------------------------------------
struct PassEvent {
    std::string satelliteName;       // 卫星名称

    // ── AOS（入境/升起）时刻 ──
    int64_t     aosJdMs      = 0;
    LookAngle   aosLook;
    GeoPos      aosSatGeo;
    double      aosSunAlt    = 0.0;      // AOS 时地面站太阳高度角（度）
    ShadowStatus aosSatShadow = ShadowStatus::FullSunlight;

    // ── TCA（最大仰角）时刻 ──
    int64_t     tcaJdMs      = 0;
    double      maxElevDeg   = 0.0;      // 最大仰角（度）
    LookAngle   tcaLook;
    GeoPos      tcaSatGeo;
    double      tcaSunAlt    = 0.0;
    ShadowStatus tcaSatShadow = ShadowStatus::FullSunlight;

    // ── LOS（出境/落下）时刻 ──
    int64_t     losJdMs      = 0;
    LookAngle   losLook;
    GeoPos      losSatGeo;
    double      losSunAlt    = 0.0;
    ShadowStatus losSatShadow = ShadowStatus::FullSunlight;

    // ── 统计信息 ──
    double       durationSec     = 0.0;  // 持续时长（秒）
    int          passType        = 0;    // 0 = 升轨，1 = 降轨
    PassQuality  quality         = PassQuality::Poor;
    PassVisibility visibility   = PassVisibility::Daylight;
    double       dopplerAtTcaKHz = 0.0;  // TCA 时刻多普勒频移（kHz）

    // ── 太阳光照比例（本影=0，全日照=1；半影带内按几何比例 f 取 f^0.7，
    //     近似太阳临边变暗，**非线性**——详见 EarthShadow::umbraFactor）──
    double aosIllum = 1.0;   // AOS 时刻卫星被太阳照亮的圆面比例
    double tcaIllum = 1.0;
    double losIllum = 1.0;

    // ── 可见弧段子区间（Heavens-Above 语义: 几何过境内同时满足
    //     "地面暗天 + 卫星被照亮" 的时间范围, 观测者实际可见的时间窗口）;
    //     单段过境为最大连续子区间; 合并过境（mergeGapSec）时取各段并集,
    //     可能跨过两段之间低于掩星角的间隙, 属合并语义而非真实连续可见
    int64_t visAosJdMs   = 0;   // 可见弧段起点（毫秒 JD, hasVisibleArc=false 时无效）
    int64_t visLosJdMs   = 0;   // 可见弧段终点
    bool    hasVisibleArc = false;

    // ── 星等估算（Vallerie 1963 相位模型, enableMagnitude 时有效）──
    double magAtTca = 0.0;      // TCA 时刻视星等（越小越亮; magValid=false 时无效）
    bool   magValid = false;    // 星等可用（卫星未处本影且几何参数有效）

    // 过境延伸至预测窗口外（窗口终点截断）: LOS 各字段为截断时刻瞬时值,
    // 并非真实出境值; durationSec 为截断后时长（UI 需注明）
    bool truncated = false;

    // ── TLE 持久存储（用于按需重新传播详情视图）──
    std::string tleLine1;   // TLE 第一行（~69 字符）
    std::string tleLine2;   // TLE 第二行（~69 字符）

    // 过境弧段采样（AOS→LOS 等步长采样，供极坐标/地面轨迹绘图与 KML 导出）
    std::vector<PassSample> samples;
};

#endif // CORE_MODELS_H
```

---

## core/DopplerCalc.cpp

```cpp
// =============================================================================
// DopplerCalc.cpp — 多普勒频移实现
// =============================================================================
#include "DopplerCalc.h"
#include "CoordTransform.h"
#include <cmath>
#include <limits>

namespace {
/// 失败/无效输入的统一返回值: NaN 哨兵（与 0.0 kHz 的真实过零值可区分）
constexpr double kInvalidKHz = std::numeric_limits<double>::quiet_NaN();
}

double DopplerCalc::compute(const std::array<double, 3> &rTeme,
                              const std::array<double, 3> &vTeme,
                              double jd,
                              const std::array<double, 3> &staECEF,
                              double carrierFreqMHz)
{
    // ---- Step 0: 输入校验（NaN 哨兵） ----
    // 载频必须为正: 负值会反转头文件"正=靠近"的符号契约;
    // 非有限输入若放行, NaN 会沿结果流入 PassSample 导出
    if (!std::isfinite(jd) || !std::isfinite(carrierFreqMHz)
        || carrierFreqMHz <= 0.0) {
        return kInvalidKHz;
    }
    for (const auto v : {rTeme, vTeme, staECEF}) {
        if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]))
            return kInvalidKHz;
    }

    // ---- Step 1: TEME → ECEF 位置+速度（含地球自转哥里奥利修正） ----
    // 旋转角需要 UT1；UT1 ≈ UTC（≤ 0.9s），直接使用 UTC 儒略日，
    // 不加 ΔT（ΔT = TT−UT1，加了会把地球自转角系统性偏大 ~0.29°）
    const auto ecef = CoordTransform::eciStateToEcef(rTeme, vTeme, jd);
    const auto &rECEF = ecef.first;
    const auto &vECEF = ecef.second;

    // 转换失败透传: eciStateToEcef 失败返回 NaN 哨兵, 此处显式拦截,
    // 不与下方 dist<1e-9 的"重合保护"（合法零输出）混淆
    for (const auto v : {rECEF, vECEF}) {
        if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]))
            return kInvalidKHz;
    }

    // ---- Step 2: LOS 单位矢量 ----
    double dx = rECEF[0] - staECEF[0];
    double dy = rECEF[1] - staECEF[1];
    double dz = rECEF[2] - staECEF[2];
    double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 1e-9) return 0.0;   // 星站重合: 无意义几何, 返回真实 0
    const double lx = dx / dist, ly = dy / dist, lz = dz / dist;

    // ---- Step 3: 径向速度（远离为正, 靠近为负 → 多普勒频移 = −v_radial / c × f） ----
    double vRadial = vECEF[0] * lx + vECEF[1] * ly + vECEF[2] * lz;

    // ---- Step 4: 多普勒频移（kHz） ----
    // 正 = 靠近（频率升高）
    return -vRadial / SPEED_OF_LIGHT_KM_S * carrierFreqMHz * 1000.0;
}
```

---

## core/DopplerCalc.h

```cpp
#ifndef CORE_DOPPLERCALC_H
#define CORE_DOPPLERCALC_H
// =============================================================================
// DopplerCalc.h — 多普勒频移计算
// 给定 TEME 位置/速度 + JD + 地面站 ECEF + 载波频率 → 输出频移 (kHz)
// 含地球自转项修正（将惯性速度转为 ECEF 速度）
// =============================================================================
#include <array>

class DopplerCalc {
public:
    static constexpr double SPEED_OF_LIGHT_KM_S = 299792.458;

    // rTeme: 卫星 TEME 位置 (km)
    // vTeme: 卫星 TEME 速度 (km/s)
    // jd: 儒略日
    // staECEF: 地面站 ECEF 坐标 (km)
    // carrierFreqMHz: 载波频率 (MHz)，默认 437.800 (ISS UHF)
    // 返回: 多普勒频移 (kHz)，正=靠近（频率升高），负=远离（频率降低）
    static double compute(const std::array<double, 3> &rTeme,
                          const std::array<double, 3> &vTeme,
                          double jd,
                          const std::array<double, 3> &staECEF,
                          double carrierFreqMHz = 437.800);
};

#endif // CORE_DOPPLERCALC_H
```

---

## core/EarthShadow.cpp

```cpp
// =============================================================================
// EarthShadow.cpp — 圆锥形地影判定实现
// =============================================================================
#include "EarthShadow.h"
#include <cmath>

namespace {
    // 夹取到 [-1, 1]（防 asin/acos 越界 → NaN）
    inline double clampUnit(double x)
    {
        if (x > 1.0) return 1.0;
        if (x < -1.0) return -1.0;
        return x;
    }
}

double EarthShadow::umbraFactor(const std::array<double, 3> &satECEF,
                                const std::array<double, 3> &sunDirECEF,
                                double sunDistAU)
{
    // NaN/Inf 防御: 非有限输入若放行, 三分支比较对 NaN 全 false 会被
    // checkShadow 误判为 InPenumbra/半影——按「失败即不可见」原则返回
    // 完全遮挡（与 propagate 失败点的消费语义一致; 纵深防御, 生产链路
    // 上游 propagate/eciToEcef 已过滤非有限值）
    if (!std::isfinite(satECEF[0]) || !std::isfinite(satECEF[1]) || !std::isfinite(satECEF[2])
        || !std::isfinite(sunDirECEF[0]) || !std::isfinite(sunDirECEF[1]) || !std::isfinite(sunDirECEF[2])
        || !std::isfinite(sunDistAU))
        return 0.0;

    // ---- 日侧判定: 卫星到太阳方向分量为正 → 夹角 < 90° → 全日照 ----
    const double dotProduct = satECEF[0] * sunDirECEF[0]
                            + satECEF[1] * sunDirECEF[1]
                            + satECEF[2] * sunDirECEF[2];

    const double r = std::sqrt(satECEF[0] * satECEF[0]
                             + satECEF[1] * satECEF[1]
                             + satECEF[2] * satECEF[2]);
    if (r < 1e-9) return 1.0;   // 地心点无意义，按日照

    // cos(theta): theta = 地心-卫星矢量与反太阳方向的夹角（夜侧为 0°）
    // theta = 90° ↔ 卫星在地平圆上与太阳连线垂直
    const double cosTheta = clampUnit(-dotProduct / r);
    if (cosTheta <= 0.0) return 1.0;   // 日侧

    // ---- 视半径 ----
    const double dsun = (sunDistAU > 0.0 ? sunDistAU : 1.0) * AU_KM;
    const double rhoEarth = std::asin(clampUnit(EARTH_RADIUS_KM / r));
    const double rhoSun   = std::asin(clampUnit(SUN_RADIUS_KM / dsun));

    // ---- 半影带内临边变暗过渡（theta ∈ [ρe−ρs, ρe+ρs]） ----
    // 线性几何比例 f → 临边变暗修正 I = f^(1−u/2), u=0.6 (Spencer, 1972)
    // 物理含义: 太阳盘面边缘亮度低于中心，卫星进入半影时光照非线性衰减
    const double theta = std::acos(cosTheta);
    const double f = (theta - (rhoEarth - rhoSun)) / (2.0 * rhoSun);
    if (f <= 0.0) return 0.0;
    if (f >= 1.0) return 1.0;
    // u = 0.6: 太阳临边变暗系数（可见光波段典型值）
    // 1 − u/2 = 0.7: 指数使过渡在 umbra 边界更陡峭、在全日照边界更平缓
    // 注: 调用方 OrbitCalculator 用 >0.5 判"被照亮", 故等效边界在半影带内
    // 偏向本影一侧（0.5^1.4 ≈ 0.38 的带内位置, LEO 约 3 s）——设计内取舍
    return std::pow(f, 0.7);
}

ShadowStatus EarthShadow::checkShadow(const std::array<double, 3> &satECEF,
                                       const std::array<double, 3> &sunDirECEF,
                                       double sunDistAU)
{
    const double f = umbraFactor(satECEF, sunDirECEF, sunDistAU);
    // 1e-9 阈值复刻圆锥边界的精确语义: f 恰为 0/1 时分别判本影/全日照
    if (f <= 1e-9) return ShadowStatus::InUmbra;
    if (f >= 1.0 - 1e-9) return ShadowStatus::FullSunlight;
    return ShadowStatus::InPenumbra;
}
```

---

## core/EarthShadow.h

```cpp
#ifndef CORE_EARTHSHADOW_H
#define CORE_EARTHSHADOW_H
// =============================================================================
// EarthShadow.h — 卫星地影判定（圆锥形模型）
// 用太阳视半径与地球视半径的几何关系区分本影/半影/全日照
// 参考 Vallado, "Fundamentals of Astrodynamics" §3.7（圆锥地影）
// =============================================================================
#include "DataTypes.h"
#include <array>

class EarthShadow {
public:
    // WGS84 赤道半径 km（圆锥母线半径）
    static constexpr double EARTH_RADIUS_KM  = 6378.137;
    // 太阳半径 km
    static constexpr double SUN_RADIUS_KM    = 695700.0;
    // 1 天文单位 km
    static constexpr double AU_KM            = 149597870.7;

    // satECEF: 卫星 ECEF 位置 (km)
    // sunDirECEF: 太阳地心方向单位矢量（ECEF 地固系）
    //             注意：必须先经 CoordTransform::eciDirToEcef 将太阳方向
    //             旋转到地固系，与卫星位置同系才能做点积（差一个 GMST 旋转，
    //             对 MEO/GEO 可差数十度，不能忽略）
    // sunDistAU: 日地距离（天文单位），用于计算太阳视半径；≤0 时按 1.0
    //
    // 模型几何（theta = 地心-卫星矢量与反太阳方向的夹角）:
    //   视地球半径 ρe = asin(Re/|r|)，视太阳半径 ρs = asin(Rs/dsun)
    //   theta < ρe − ρs        → 本影（地球完全遮住太阳）
    //   ρe − ρs ≤ theta < ρe + ρs → 半影（部分遮住）
    //   theta ≥ ρe + ρs（或日侧） → 全日照
    static ShadowStatus checkShadow(const std::array<double, 3> &satECEF,
                                    const std::array<double, 3> &sunDirECEF,
                                    double sunDistAU = 1.0);

    // 太阳圆面可见比例 [0, 1]：1=全日照，0=本影，
    // 半影带内随夹角 theta 由本影界到半影界过渡——几何比例再取 f^0.7
    // 近似太阳临边变暗（u=0.6, Spencer 1972），故为**非线性**过渡；
    // 调用方以 >0.5 判"被照亮"时，等效边界在半影带内偏向本影一侧
    // 约 0.198°（LEO 约 3 s）——这是既有的、自洽的设计取舍
    // 参数含义与 checkShadow 相同；用于光照强度/星上电源等细化场景
    static double umbraFactor(const std::array<double, 3> &satECEF,
                              const std::array<double, 3> &sunDirECEF,
                              double sunDistAU = 1.0);
};

#endif // CORE_EARTHSHADOW_H
```

---

## core/ErrorHandler.cpp

```cpp
// =============================================================================
// ErrorHandler.cpp — 统一错误处理和恢复机制实现
// =============================================================================
#include "ErrorHandler.h"
#include <QDateTime>
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
        return true;
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
```

---

## core/ErrorHandler.h

```cpp
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
```

---

## core/Nutation.cpp

```cpp
// =============================================================================
// Nutation.cpp — 章动完整实现（Meeus 表 22.A，63 项主级数）
// 参数: D=日月平角距, M=太阳平近点角, M'=月球平近点角,
//       F=月球平辐角, Ω=月球升交点平黄经
// Δψ = Σ (a + bT) sin(和), Δε = Σ (c + dT) cos(和), 单位 0.0001 角秒
// =============================================================================
#include "Nutation.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

constexpr double ARC_SEC_TO_RAD = M_PI / (180.0 * 3600.0);
constexpr double DEG2RAD = M_PI / 180.0;

struct NutTerm {
    int D, M, Mp, F, Om;   // 五个基本量倍数
    double a, b;           // Δψ 正弦系数（0.0001″ 单位，b 随 T 线性）
    double c, d;           // Δε 余弦系数
};

// Meeus 表 22.A（63 项）
const NutTerm TERMS[] = {
    { 0, 0, 0, 0, 1, -171996, -174.2, 92025, 8.9 },
    { -2, 0, 0, 2, 2, -13187, -1.6, 5736, -3.1 },
    { 0, 0, 0, 2, 2, -2274, -0.2, 977, -0.5 },
    { 0, 0, 0, 0, 2, 2062, 0.2, -895, 0.5 },
    { 0, 1, 0, 0, 0, 1426, -3.4, 54, -0.1 },
    { 0, 0, 1, 0, 0, 712, 0.1, -7, 0.0 },
    { -2, 1, 0, 2, 2, -517, 1.2, 224, -0.6 },
    { 0, 0, 0, 2, 1, -386, -0.4, 200, 0.0 },
    { 0, 0, 1, 2, 2, -301, 0.0, 129, -0.1 },
    { -2, -1, 0, 2, 2, 217, -0.5, -95, 0.3 },
    { -2, 0, 1, 0, 0, -158, 0.0, 0, 0.0 },
    { -2, 0, 0, 2, 1, 129, 0.1, -70, 0.0 },
    { 0, 0, -1, 2, 2, 123, 0.0, -53, 0.0 },
    { 2, 0, 0, 0, 0, 63, 0.0, 0, 0.0 },
    { 0, 0, 1, 0, 1, 63, 0.1, -33, 0.0 },
    { 2, 0, -1, 2, 2, -59, 0.0, 26, 0.0 },
    { 0, 0, -1, 0, 1, -58, -0.1, 32, 0.0 },
    { 0, 0, 1, 2, 1, -51, 0.0, 27, 0.0 },
    { -2, 0, 2, 0, 0, 48, 0.0, 0, 0.0 },
    { 0, 0, -2, 2, 1, 46, 0.0, -24, 0.0 },
    { 2, 0, 0, 2, 2, -38, 0.0, 16, 0.0 },
    { 0, 0, 2, 2, 2, -31, 0.0, 13, 0.0 },
    { 0, 0, 2, 0, 0, 29, 0.0, 0, 0.0 },
    { -2, 0, 1, 2, 2, 29, 0.0, -12, 0.0 },
    { 0, 0, 0, 2, 0, 26, 0.0, 0, 0.0 },
    { -2, 0, 0, 2, 0, -22, 0.0, 0, 0.0 },
    { 0, 0, -1, 2, 1, 21, 0.0, -10, 0.0 },
    { 0, 2, 0, 0, 0, 17, -0.1, 0, 0.0 },
    { 2, 0, -1, 0, 1, 16, 0.0, -8, 0.0 },
    { -2, 2, 0, 2, 2, -16, 0.1, 7, 0.0 },
    { 0, 1, 0, 0, 1, -15, 0.0, 9, 0.0 },
    { -2, 0, 1, 0, 1, -13, 0.0, 7, 0.0 },
    { 0, -1, 0, 0, 1, -12, 0.0, 6, 0.0 },
    { 0, 0, 2, -2, 0, 11, 0.0, 0, 0.0 },
    { 2, 0, -1, 2, 1, -10, 0.0, 5, 0.0 },
    { 2, 0, 1, 2, 2, -8, 0.0, 3, 0.0 },
    { 0, 1, 0, 2, 2, 7, 0.0, -3, 0.0 },
    { -2, 1, 1, 0, 0, -7, 0.0, 0, 0.0 },
    { 0, -1, 0, 2, 2, -7, 0.0, 3, 0.0 },
    { 2, 0, 0, 2, 1, -7, 0.0, 3, 0.0 },
    { 2, 0, 1, 0, 0, 6, 0.0, 0, 0.0 },
    { -2, 0, 2, 2, 2, 6, 0.0, -3, 0.0 },
    { -2, 0, 1, 2, 1, 6, 0.0, -3, 0.0 },
    { 2, 0, -2, 0, 1, -6, 0.0, 3, 0.0 },
    { 2, 0, 0, 0, 1, -6, 0.0, 3, 0.0 },
    { 0, -1, 1, 0, 0, 5, 0.0, 0, 0.0 },
    { -2, -1, 0, 2, 1, -5, 0.0, 3, 0.0 },
    { -2, 0, 0, 0, 1, -5, 0.0, 3, 0.0 },
    { 0, 0, 2, 2, 1, -5, 0.0, 3, 0.0 },
    { -2, 0, 2, 0, 1, 4, 0.0, 0, 0.0 },
    { -2, 1, 0, 2, 1, 4, 0.0, 0, 0.0 },
    { 0, 0, 1, -2, 0, 4, 0.0, 0, 0.0 },
    { -1, 0, 1, 0, 0, -4, 0.0, 0, 0.0 },
    { -2, 1, 0, 0, 0, -4, 0.0, 0, 0.0 },
    { 1, 0, 0, 0, 0, -4, 0.0, 0, 0.0 },
    { 0, 0, 1, 2, 0, 3, 0.0, 0, 0.0 },
    { 0, 0, -2, 2, 2, -3, 0.0, 0, 0.0 },
    { -1, -1, 1, 0, 0, -3, 0.0, 0, 0.0 },
    { 0, 1, 1, 0, 0, -3, 0.0, 0, 0.0 },
    { 0, -1, 1, 2, 2, -3, 0.0, 0, 0.0 },
    { 2, -1, -1, 2, 2, -3, 0.0, 0, 0.0 },
    { 0, 0, 3, 0, 0, 3, 0.0, 0, 0.0 },
    { 2, 0, -1, 0, 2, -3, 0.0, 0, 0.0 },
};

// 平黄赤交角 ε0（度，Meeus 22.2）
inline double meanObliquityDegImpl(double jdTT)
{
    const double T = (jdTT - 2451545.0) / 36525.0;
    return 23.43929111 - 0.01300416667 * T - 1.6389e-7 * T * T
         + 5.0361e-7 * T * T * T;
}

} // namespace

namespace Nutation {

// 按日缓存: 章动日内变化实测界约 ±0.5″（主 D 项 1.32″ 与 (−2,0,0,2,2) 项
// 合成角速率所致）——对太阳高度引入 ≤0.00014° 偏差, 远低于本应用
// 0.01° 级精度需求, 换取省去高频路径的 63 项级数求值。
// thread_local 保证多线程（预报 worker 并行）安全。
// 初值 -1.0 防与 floor(jdTT)=0 的合法查询碰撞
struct DayCache {
    double jdDay = -1.0;
    Angles value;
};
thread_local DayCache g_cache;

double meanObliquityDeg(double jdTT)
{
    return meanObliquityDegImpl(jdTT);
}

double trueObliquityDeg(double jdTT)
{
    return meanObliquityDegImpl(jdTT) + compute(jdTT).dEps / DEG2RAD;
}

Angles compute(double jdTT)
{
    const double day = std::floor(jdTT);
    if (g_cache.jdDay == day)
        return g_cache.value;

    const double T  = (jdTT - 2451545.0) / 36525.0;

    // 基本量（度）— IAU 1980 章动级数（Meeus 22.A，T 为 J2000 起算儒略世纪）
    // D=日月平距角, M=太阳平近点角, M'=月球平近点角,
    // F=月球平辐角(L_moon−Ω), Ω=月球升交点平黄经
    const double D  = 297.85036 + 445267.11148 * T
                    - 0.0019142 * T * T + T * T * T / 189474.0;
    const double M  = 357.52772 + 35999.05034 * T
                    - 0.0001603 * T * T - T * T * T / 300000.0;
    const double Mp = 134.96298 + 477198.867398 * T
                    + 0.0086972 * T * T + T * T * T / 56250.0;
    const double F  = 93.27191 + 483202.017538 * T
                    - 0.0036825 * T * T + T * T * T / 327270.0;
    const double Om = 125.04452 - 1934.136261 * T
                    + 0.0020708 * T * T + T * T * T / 450000.0;

    double dPsi = 0.0;   // 0.0001″
    double dEps = 0.0;
    for (const NutTerm &t : TERMS) {
        const double argDeg = t.D * D + t.M * M + t.Mp * Mp
                            + t.F * F + t.Om * Om;
        const double arg = argDeg * DEG2RAD;
        dPsi += (t.a + t.b * T) * std::sin(arg);
        dEps += (t.c + t.d * T) * std::cos(arg);
    }
    dPsi *= 1e-4 * ARC_SEC_TO_RAD;   // → 弧度
    dEps *= 1e-4 * ARC_SEC_TO_RAD;

    g_cache.jdDay = day;
    g_cache.value = { dPsi, dEps };
    return g_cache.value;
}

} // namespace Nutation
```

---

## core/Nutation.h

```cpp
#ifndef CORE_NUTATION_H
#define CORE_NUTATION_H
// =============================================================================
// Nutation.h — 章动（黄经章动 Δψ 与交角章动 Δε）
// Meeus "Astronomical Algorithms" 第 22 章表 22.A 完整 63 项主级数
// 输入 TT 儒略日 → 输出弧度；精度 ≈ 0.5″（VSOP/IAU 2006 以下级别的经典精度）
// =============================================================================
namespace Nutation {

// jdTT: 儒略日（TT 时标，调用方须已从 UTC 加 ΔT）
// 返回 {Δψ, Δε}，单位弧度
struct Angles {
    double dPsi = 0.0;   // 黄经章动（弧度）
    double dEps = 0.0;   // 交角章动（弧度）
};

Angles compute(double jdTT);

// 平黄赤交角 ε0（度，Meeus 22.2 三阶多项式）
double meanObliquityDeg(double jdTT);

// 真黄赤交角 ε0 + Δε（度）
double trueObliquityDeg(double jdTT);

} // namespace Nutation

#endif // CORE_NUTATION_H
```

---

## core/Observer.cpp

```cpp
// =============================================================================
// Observer.cpp — 地面站实现
// =============================================================================
#include "Observer.h"
#include <limits>
#include "CoordTransform.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

Observer::Observer(double latDeg, double lonDeg, double altKm, double minEl)
    : mLatDeg(latDeg), mLonDeg(lonDeg), mAltKm(altKm), mMinEl(minEl)
{
    try {
        // 参数验证
        if (!std::isfinite(latDeg) || !std::isfinite(lonDeg) || 
            !std::isfinite(altKm) || !std::isfinite(minEl)) {
            throw std::invalid_argument("观测站参数无效");
        }
        
        // 构造时预计算 ECEF 坐标，后续所有 lookAngle 调用复用此值
        // 参数防御: 越界经纬度/负海拔会静默产生失真的站心几何——
        // 纬度钳到 [-90,90], 经度回绕到 [-180,180], 海拔钳到 [0,100]
        mLatDeg = std::clamp(mLatDeg, -90.0, 90.0);
        mLonDeg = std::fmod(mLonDeg + 180.0, 360.0);
        if (mLonDeg < 0.0) mLonDeg += 360.0;
        mLonDeg -= 180.0;
        mAltKm  = std::clamp(mAltKm, -1.0, 100.0);   // 下界与 UI spinbox 一致:
                                                      // 负海拔（死海/洼地）合法,
                                                      // horizonDipDeg 对负值钳 0 保守处理
        mMinEl  = std::clamp(mMinEl, 0.0, 90.0);
        
        // 检查转换后的ECEF坐标有效性
        mEcef = CoordTransform::geodeticToEcef(GeoPos(mLatDeg, mLonDeg, mAltKm));
        if (!std::isfinite(mEcef[0]) || !std::isfinite(mEcef[1]) || !std::isfinite(mEcef[2])) {
            throw std::invalid_argument("ECEF坐标转换失败");
        }
    } catch (const std::exception &e) {
        // 记录错误信息（在实际应用中可以使用日志系统）
        // std::cerr << "观测站初始化失败: " << e.what() << std::endl;

        // 回退到赤道 (0,0) 海平面站, 并由该回退参数【重新生成】ECEF——
        // 若直接置 mEcef={0,0,0} 会得到地心坐标, 与 (0,0,0) 站的真实
        // ECEF≈(6378.137,0,0) 不自洽, lookAngle 几何全部失真
        mLatDeg = 0.0;
        mLonDeg = 0.0;
        mAltKm = 0.0;
        mMinEl = 0.0;
        mEcef = CoordTransform::geodeticToEcef(GeoPos(mLatDeg, mLonDeg, mAltKm));
    }
}

LookAngle Observer::lookAngle(const std::array<double, 3> &satECEF) const
{
    // 检查输入坐标有效性（直接返回 NaN, 与 CoordTransform 高效模式一致）
    if (!std::isfinite(satECEF[0]) || !std::isfinite(satECEF[1]) || !std::isfinite(satECEF[2])) {
        return LookAngle(std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::quiet_NaN());
    }
    return CoordTransform::computeLookAngle(satECEF, mEcef, mLatDeg, mLonDeg);
}

GeoPos Observer::subSatPoint(const std::array<double, 3> &satECEF) const
{
    // 检查输入坐标有效性（直接返回 NaN, 与系统级 NaN 哨兵约定对齐）
    if (!std::isfinite(satECEF[0]) || !std::isfinite(satECEF[1]) || !std::isfinite(satECEF[2])) {
        return GeoPos(std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN());
    }

    GeoPos result = CoordTransform::ecefToGeodetic(satECEF);

    // 检查结果有效性（ecefToGeodetic 已内置 NaN 返回, 此处双重保险）
    if (!std::isfinite(result.latDeg) || !std::isfinite(result.lonDeg) || !std::isfinite(result.altKm)) {
        return GeoPos(std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::quiet_NaN());
    }

    return result;
}
```

---

## core/Observer.h

```cpp
#ifndef CORE_OBSERVER_H
#define CORE_OBSERVER_H
// =============================================================================
// Observer.h — 地面观测站
// 封装地面站 WGS84 坐标，预计算 ECEF 位置，提供站心观测接口
// =============================================================================
#include "DataTypes.h"
#include <array>

class Observer {
public:
    Observer(double latDeg, double lonDeg, double altKm = 0.0,
             double minElevationDeg = 10.0);

    // 访问器
    double latDeg()        const { return mLatDeg; }
    double lonDeg()        const { return mLonDeg; }
    double altKm()         const { return mAltKm; }
    double minElevDeg()    const { return mMinEl; }
    const std::array<double, 3>& ecef() const { return mEcef; }

    // 站心观测量（仰角、方位角、斜距）
    LookAngle lookAngle(const std::array<double, 3> &satECEF) const;

    // 星下点（从卫星 ECEF 直接解经纬高）
    GeoPos subSatPoint(const std::array<double, 3> &satECEF) const;

private:
    double mLatDeg, mLonDeg, mAltKm, mMinEl;
    std::array<double, 3> mEcef;    // 预计算的站 ECEF 坐标
};

#endif // CORE_OBSERVER_H
```

---

## core/OrbitCalculator.cpp

```cpp
// =============================================================================
// OrbitCalculator.cpp — 过境预报引擎完整实现
// =============================================================================
#include "OrbitCalculator.h"
#include "CoordTransform.h"
#include "SunPosition.h"
#include "EarthShadow.h"
#include "DopplerCalc.h"
#include "VisualMagnitude.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
    constexpr double TCA_REFINE_SEC = 3.0;     // TCA 三分搜索初始半宽（秒）
    constexpr double SEC_PER_DAY  = 86400.0;
    // 传播失败哨兵: 真实仰角物理下限为 -90°, 该值不可能由观测产生。
    // 调用方必须用它区分"传播失败"与"真实低于掩星角"——把失败点当真实
    // LOS/AOS 会把过境错误分裂或截断（瞬态失败时仰角其实仍在掩星角之上）
    constexpr double ELEV_FAILURE_SENTINEL = -1000.0;
    // 观测站海拔的地平线倾角（度）: dip = acos(r/(r+h)) ≈ 1.0152·sqrt(h_km)。
    // 高海拔站"视地平线"下倾，暗天判定的太阳高度阈值须按此值平移——
    // 否则海拔 1km 山地站在太阳尚高于其视地平时就被误判为暗天
    constexpr double DIP_COEF_DEG_PER_SQRT_KM = 1.0152;
    inline double horizonDipDeg(double altKm)
    {
        return DIP_COEF_DEG_PER_SQRT_KM * std::sqrt(std::max(0.0, altKm));
    }
    // 注: 地固系旋转（GMST）用 UT1 ≈ UTC（≤ 0.9s）的儒略日即可；
    // ΔT（TT−UT1）只用于太阳历表（SunPosition 内部自行转为 TT），切不可加到这里

    // ---- 采样点数换算（含端点 / 不含端点）----
    // 直接把"窗口 ÷ 步长"取整会踩到 jdLOS 自身的**表示舍入**：
    // JD≈2.46e6 时 ulp≈4.7e-10 天（≈40 µs），整除窗口可能被算成 59.999997 步，
    // floor 之后丢掉端点那一个采样点（表现为弧段采样/可见弧段网格少一点）。
    // 这里把 4×ulp 折算成"步"再加进去：既保留"含端点"的语义，
    // 又不会凭空多采（该补偿量远小于 1 步，除极小步长外不会引入额外采样）。
    inline double ulpDaysAt(double jd)
    {
        return 4.0 * std::numeric_limits<double>::epsilon() * std::fabs(jd);
    }
    inline long long stepCountInclusive(double jdA, double jdB, double stepDays)
    {
        const double eps = ulpDaysAt(std::max(std::fabs(jdA), std::fabs(jdB))) / stepDays;
        return static_cast<long long>(std::floor((jdB - jdA) / stepDays + eps + 1e-12));
    }
    inline long long stepCountExclusive(double jdA, double jdB, double stepDays)
    {
        const double eps = ulpDaysAt(std::max(std::fabs(jdA), std::fabs(jdB))) / stepDays;
        return static_cast<long long>(std::floor((jdB - jdA) / stepDays - eps + 1e-12));
    }
}

// ========== 辅助：填充一次过境的完整详情 ==========
static void fillPassDetails(PassEvent &ev,
    OrbitCalculator::PropagateFn &propagate, const Observer &observer,
    const OrbitCalculator::Config &cfg,
    double jdAOS, double jdTCA, double jdLOS)
{
    try {
        // 优化：批量获取所有需要的轨道状态，避免重复调用 propagate
        std::optional<EciState> eciA, eciT, eciL;
        std::optional<SunPosition> sunT;   // TCA 时刻太阳位置（光照/星等共用）
        
        // 仅在需要时才进行轨道传播
        if (cfg.enableSunAnalysis && cfg.enableShadowCheck) {
            // 需要所有三个时刻的数据
            eciA = propagate(jdAOS);
            eciT = propagate(jdTCA);
            eciL = propagate(jdLOS);
        } else if (cfg.enableShadowCheck || cfg.enableMagnitude || cfg.enableDoppler) {
            // 只需要 TCA 时刻的数据
            eciT = propagate(jdTCA);
        }
        
        // 光照分析
        if (cfg.enableSunAnalysis) {
            const auto sunA = SunPositionCalc::compute(jdAOS, observer.latDeg(), observer.lonDeg());
            sunT = SunPositionCalc::compute(jdTCA, observer.latDeg(), observer.lonDeg());
            const auto sunL = SunPositionCalc::compute(jdLOS, observer.latDeg(), observer.lonDeg());
            
            // 太阳位置计算失败（输入非法）: 与下方"传播失败"同样处理——
            // 由外层 try/catch 落为 Daylight/默认值, 绝不用伪造的 0 值参与判定
            if (!sunA || !sunT || !sunL) {
                throw std::runtime_error("太阳位置计算失败");
            }
            
            ev.aosSunAlt = sunA->altDeg;
            ev.tcaSunAlt = sunT->altDeg;
            ev.losSunAlt = sunL->altDeg;

            if (cfg.enableShadowCheck) {
                if (eciA) {
                    auto eA = CoordTransform::eciToEcef(eciA->posKm, jdAOS);
                    // 太阳方向必须旋转到 ECEF 与卫星位置同系
                    auto sunDirA = CoordTransform::eciDirToEcef(sunA->dirECI, jdAOS);
                    ev.aosSatShadow = EarthShadow::checkShadow(eA, sunDirA, sunA->distanceAU);
                    ev.aosIllum = EarthShadow::umbraFactor(eA, sunDirA, sunA->distanceAU);
                } else {
                    // 传播失败: 不得让 PassEvent 保持结构体默认值 FullSunlight/1.0
                    // —— 那会把失败时刻"默认为照亮", 暗天过境被误判为可见
                    ev.aosSatShadow = ShadowStatus::InUmbra;
                    ev.aosIllum = 0.0;
                }
                if (eciT) {
                    auto eT = CoordTransform::eciToEcef(eciT->posKm, jdTCA);
                    auto sunDirT = CoordTransform::eciDirToEcef(sunT->dirECI, jdTCA);
                    ev.tcaSatShadow = EarthShadow::checkShadow(eT, sunDirT, sunT->distanceAU);
                    ev.tcaIllum = EarthShadow::umbraFactor(eT, sunDirT, sunT->distanceAU);
                } else {
                    ev.tcaSatShadow = ShadowStatus::InUmbra;
                    ev.tcaIllum = 0.0;
                }
                if (eciL) {
                    auto eL = CoordTransform::eciToEcef(eciL->posKm, jdLOS);
                    auto sunDirL = CoordTransform::eciDirToEcef(sunL->dirECI, jdLOS);
                    ev.losSatShadow = EarthShadow::checkShadow(eL, sunDirL, sunL->distanceAU);
                    ev.losIllum = EarthShadow::umbraFactor(eL, sunDirL, sunL->distanceAU);
                } else {
                    ev.losSatShadow = ShadowStatus::InUmbra;
                    ev.losIllum = 0.0;
                }
            }

            // 光照判定以 TCA（用户实际观测时刻）为准 —— 与 Heavens-Above/Orbis
            // 的「峰值点燃亮」语义一致：仅 AOS/LOS 照亮、TCA 却在地影内的过境
            // 不标为可见（否则用户按 TCA 时刻观测看不到卫星）
            bool satLit = (ev.tcaSatShadow != ShadowStatus::InUmbra) && ev.tcaIllum > 0.5;
            if (ev.tcaSunAlt >= cfg.darkSkyThresholdDeg)
                ev.visibility = PassVisibility::Daylight;
            else if (!satLit)
                ev.visibility = PassVisibility::RadioOnly;
            else
                ev.visibility = PassVisibility::Visible;
        }

        // 星等估算: 独立于 enableSunAnalysis，仅需 TCA 时刻的太阳位置与卫星状态
        if (cfg.enableMagnitude) {
            if (eciT) {
                // 若 enableSunAnalysis 已算出 sunT 则复用, 否则按需计算
                if (!sunT)
                    sunT = SunPositionCalc::compute(jdTCA, observer.latDeg(), observer.lonDeg());
                if (sunT) {
                    auto satE = CoordTransform::eciToEcef(eciT->posKm, jdTCA);
                    auto sunDirE = CoordTransform::eciDirToEcef(sunT->dirECI, jdTCA);
                    std::array<double, 3> sunPosE;
                    for (int k = 0; k < 3; ++k)
                        sunPosE[k] = sunDirE[k] * (sunT->distanceAU * EarthShadow::AU_KM);
                    const double phase = VisualMagnitudeCalc::phaseAngleRad(satE, sunPosE,
                                                                          observer.ecef());
                    ev.magAtTca = VisualMagnitudeCalc::visualMagnitude(
                        VisualMagnitudeCalc::standardMagnitude(ev.satelliteName, cfg.stdMagFallback),
                        ev.tcaLook.rangeKm, phase);
                    // 本影中卫星不可见，公式前提（太阳无遮挡）不成立 → 标为无效
                    // 若已由 enableSunAnalysis 算出 tcaSatShadow 则直接复用,
                    // 否则按需计算
                    // 注: enableShadowCheck=false 时 tcaShadow 保持默认 FullSunlight
                    // → magValid=true; 这是合理设计取舍：无阴影数据时默认可见,
                    // 用户需自行判断是否启用阴影检查以获得准确的星等有效性
                    ShadowStatus tcaShadow = ev.tcaSatShadow;
                    if (!cfg.enableSunAnalysis && cfg.enableShadowCheck) {
                        auto eT = CoordTransform::eciToEcef(eciT->posKm, jdTCA);
                        auto sunDirT = CoordTransform::eciDirToEcef(sunT->dirECI, jdTCA);
                        tcaShadow = EarthShadow::checkShadow(eT, sunDirT, sunT->distanceAU);
                    }
                    ev.magValid = (tcaShadow != ShadowStatus::InUmbra)
                                  && std::isfinite(ev.magAtTca);
                }
            }
        }
        // 仰角由 computeMoment 直接计算（几何仰角）
        if (cfg.enableDoppler) {
            if (eciT) {
                ev.dopplerAtTcaKHz = DopplerCalc::compute(
                    eciT->posKm, eciT->velKms, jdTCA,
                    observer.ecef(), cfg.carrierFreqMHz);
            }
        }
        // 过境类型: 基于星下点纬度趋势判断升/降轨（比方位角启发式更可靠）
        // 注意: 合并过境时 passType 取第一段的值，若两段方向不同（极罕见）
        // 则合并后 passType 可能不准——既有边界条件，影响极小
        ev.passType = (ev.aosSatGeo.latDeg < ev.losSatGeo.latDeg) ? 0 : 1;
        // 质量等级
        if (ev.maxElevDeg >= 50.0)      ev.quality = PassQuality::Excellent;
        else if (ev.maxElevDeg >= 20.0) ev.quality = PassQuality::Good;
        else                            ev.quality = PassQuality::Poor;
    } catch (const std::exception &e) {
        // 异常情况下设置默认值，避免数据损坏
        ev.visibility = PassVisibility::Daylight;
        ev.quality = PassQuality::Poor;
        ev.passType = 0;
        
        #ifdef QT_DEBUG
        qDebug("fillPassDetails 异常 (sat=%s): %s", ev.satelliteName.c_str(), e.what());
        #endif
    }
}

int64_t OrbitCalculator::jdToIntMs(double jd) {
    // 委托给 DataTypes.h 的全局 jdToMs 消除重复实现
    return jdToMs(jd);
}
double OrbitCalculator::intMsToJd(int64_t ms) {
    // 委托给 DataTypes.h 的全局 msToJd 消除重复实现
    return msToJd(ms);
}

// ============================================================================
// propagate / search / bisect / collectVisibleArc / collectSamples
// ============================================================================

// ========== 单点仰角计算 ==========
double OrbitCalculator::elevationAt(PropagateFn &propagate,
                                     const Observer &obs, double jd) const
{
    try {
        // 检查输入参数有效性
        if (!std::isfinite(jd)) {
            return ELEV_FAILURE_SENTINEL;
        }
        
        auto eciOpt = propagate(jd);
        if (!eciOpt) return ELEV_FAILURE_SENTINEL;  // 传播失败 → 调用方按哨兵跳过, 不得视作真实 LOS

        auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, jd);
        auto look = obs.lookAngle(ecef);
        
        // 检查仰角数据有效性
        if (!std::isfinite(look.elevDeg)) {
            return ELEV_FAILURE_SENTINEL;
        }
        
        return look.elevDeg;
    } catch (const std::exception &e) {
        // 异常情况下返回失败哨兵值
        return ELEV_FAILURE_SENTINEL;
    }
}

// ========== 计算某一时刻的完整观测 + 星下点 ==========
void OrbitCalculator::computeMoment(PropagateFn &propagate,
                                     const Observer &obs, double jd,
                                     LookAngle &look, GeoPos &geo) const
{
    try {
        // 检查输入参数有效性
        if (!std::isfinite(jd)) {
            look = LookAngle(ELEV_FAILURE_SENTINEL, 0.0, 0.0);
            geo  = GeoPos(0.0, 0.0, 0.0);
            return;
        }
        
        auto eciOpt = propagate(jd);
        if (!eciOpt) {
            look = LookAngle(ELEV_FAILURE_SENTINEL, 0.0, 0.0);
            geo  = GeoPos(0.0, 0.0, 0.0);
            return;
        }
        
        auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, jd);
        look = obs.lookAngle(ecef);
        geo  = obs.subSatPoint(ecef);
        
        // 检查结果有效性
        if (!std::isfinite(look.elevDeg) || !std::isfinite(look.azDeg) || !std::isfinite(look.rangeKm) ||
            !std::isfinite(geo.latDeg) || !std::isfinite(geo.lonDeg) || !std::isfinite(geo.altKm)) {
            look = LookAngle(ELEV_FAILURE_SENTINEL, 0.0, 0.0);
            geo  = GeoPos(0.0, 0.0, 0.0);
        }
    } catch (const std::exception &e) {
        // 异常情况下设置默认值
        look = LookAngle(ELEV_FAILURE_SENTINEL, 0.0, 0.0);
        geo  = GeoPos(0.0, 0.0, 0.0);
    }
}

// ========== 过境弧段采样 ==========
void OrbitCalculator::collectSamples(PropagateFn &propagate,
                                       const Observer &obs,
                                       double jdAOS, double jdLOS,
                                       PassEvent &ev,
                                       const AtomicCancelFlagPtr cancel) const
{
    try {
        if (!m_cfg.enableSamples || m_cfg.sampleStepSec <= 0.0) return;
        
        // 检查时间范围有效性
        if (!std::isfinite(jdAOS) || !std::isfinite(jdLOS) || jdAOS >= jdLOS) {
            return;
        }
        
        const double step = m_cfg.sampleStepSec / 86400.0;
        ev.samples.clear();
        // 防御: 调用链保证 jdLOS > jdAOS, 但数值抖动/上游配置错误时负数会
        // 在 size_t 转换后变成天文数字触发 bad_alloc
        const double n = std::max(0.0, (jdLOS - jdAOS) / step);
        
        // 防止分配过多内存
        if (n > 100000.0) {  // 限制最大采样点数
            return;
        }
        
        // 预分配：基础点数 + 余量（Doppler 不增加采样数，仅附加计算）
        size_t estimatedSamples = static_cast<size_t>(n) + 2;
        estimatedSamples += estimatedSamples / 4;  // +25% 防重分配
        ev.samples.reserve(estimatedSamples);
        
        // 传播一次完成全部字段（含多普勒），避免对同一时刻二次传播
        // 采样时刻用"起点 + 整数索引 × 步长"：逐次 jd += step 会累积浮点误差
        // （JD≈2.46e6 时 ulp≈41 µs/步，24h/15s 窗口累计可达 ~5 ms ≈ 40 m 位置偏差），
        // 与 STK 等外部工具做逐点比对时该偏差不可忽略；点数含端点
        const long long nSamples = stepCountInclusive(jdAOS, jdLOS, step);
        for (long long i = 0; i <= nSamples; ++i) {
            const double jd = jdAOS + static_cast<double>(i) * step;
            if (isCancelled(cancel)) return;
            
            try {
                auto eciOpt = propagate(jd);
                LookAngle look;
                GeoPos geo;
                double dopplerKHz = 0.0;
                
                if (eciOpt) {
                    auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, jd);
                    look = obs.lookAngle(ecef);
                    geo = obs.subSatPoint(ecef);
                    
                    if (m_cfg.enableDoppler) {
                        dopplerKHz = DopplerCalc::compute(eciOpt->posKm, eciOpt->velKms, jd,
                                                          obs.ecef(), m_cfg.carrierFreqMHz);
                    }
                } else {
                    look = LookAngle(ELEV_FAILURE_SENTINEL, 0.0, 0.0);   // 传播失败占位（与 computeMoment 一致）
                    geo = GeoPos(0.0, 0.0, 0.0);
                }
                
                // 检查采样数据有效性（仰角必须在物理范围内，传播失败占位 -1000° 需排除）
                if (std::isfinite(look.elevDeg) && std::isfinite(look.azDeg) && 
                    std::isfinite(look.rangeKm) && std::isfinite(geo.latDeg) && 
                    std::isfinite(geo.lonDeg) && std::isfinite(geo.altKm)
                    && look.elevDeg > -90.0 && look.elevDeg <= 90.0) {
                    PassSample s;
                    s.jdMs = jdToIntMs(jd);
                    s.look = look;
                    s.geo = geo;
                    s.dopplerKHz = dopplerKHz;
                    ev.samples.push_back(s);
                }
            } catch (const std::exception &e) {
                // 单个采样点失败时跳过该点，继续处理其他点
                continue;
            }
        }
    } catch (const std::bad_alloc &e) {
        // 内存分配失败时清空采样数据
        ev.samples.clear();
    } catch (const std::exception &e) {
        // 其他异常时清空采样数据
        ev.samples.clear();
    }
}

// ========== 可见弧段子区间 ==========
// Heavens-Above 语义: 几何过境内"地面暗天（太阳高度 < 阈值）+ 卫星被照亮
// （太阳圆面可见比例 > 50%）"的最大连续子区间，即观测者实际能看到的时段。
// 粗扫描定位状态翻转区间 → 二分精化边界；TCA 作为采样点强制纳入网格，
// 保证与 TCA 时刻可见性分类（fillPassDetails）严格一致。
void OrbitCalculator::collectVisibleArc(PropagateFn &propagate,
                                         const Observer &obs,
                                         double jdAOS, double jdLOS,
                                         PassEvent &ev,
                                         const AtomicCancelFlagPtr cancel) const
{
    if (!m_cfg.enableVisArc || !m_cfg.enableSunAnalysis
        || !m_cfg.enableShadowCheck || m_cfg.sampleStepSec <= 0.0) return;
    if (jdLOS <= jdAOS) return;

    const double step  = m_cfg.sampleStepSec / 86400.0;
    const double dark  = m_cfg.darkSkyThresholdDeg - horizonDipDeg(obs.altKm());

    // 太阳位置: 直接计算。等步长网格 + 二分精化下每次查询的 JD 均不同,
    // 曾用「容差 1e-8 天 + 线性扫描 100 项」的缓存从不命中, 纯开销——删除
    auto getSunPosition = [&](double jd) -> std::optional<SunPosition> {
        return SunPositionCalc::compute(jd, obs.latDeg(), obs.lonDeg());
    };
    
    // t 时刻是否"可观测": 地面暗天 且 卫星被照亮（光照圆面 > 50%）
    auto visibleAt = [&](double jd) -> bool {
        if (isCancelled(cancel)) return false;
        
        auto sunOpt = getSunPosition(jd);
        if (!sunOpt) return false;
        const auto& sun = *sunOpt;
        
        if (sun.altDeg >= dark) return false;
        
        auto eciOpt = propagate(jd);
        if (!eciOpt) return false;
        
        auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, jd);
        auto sunDirE = CoordTransform::eciDirToEcef(sun.dirECI, jd);
        return EarthShadow::umbraFactor(ecef, sunDirE, sun.distanceAU) > 0.5;
    };

    // 二分精化状态翻转边界: [lo, hi] 端点可见性已知不同，收敛到区间内的翻转点。
    // 不变式: visibleAt(lo) == A, visibleAt(hi) == B（A ≠ B），收敛到翻转位置。
    // 防御: 若区间内存在多次翻转（半影边缘抖动/阈值边界叠加的罕见情况），
    // 二分会收敛到任意一个翻转点而非首/末边界——每轮重查端点, 一旦端点
    // 可见性相同即退化为返回区间端点（真实可见段的成员），不做错误精化
    auto refineBoundary = [&](double jdLo, double jdHi) -> double {
        for (int i = 0; i < m_cfg.bisectionIters; ++i) {
            if (isCancelled(cancel)) return jdLo;
            bool vLo = visibleAt(jdLo);
            bool vHi = visibleAt(jdHi);
            if (vLo == vHi) return jdLo;   // 多翻转: 不精化, 返回端点
            double mid = (jdLo + jdHi) * 0.5;
            if (visibleAt(mid)) jdHi = mid;
            else                jdLo = mid;
        }
        return (jdLo + jdHi) * 0.5;
    };

    // 采样网格: 等步长 + 强制纳入 TCA（保证相邻采样状态不同处边界精化正确，
    // 且 TCA 时刻可见性分类与弧段包含关系严格一致）
    std::vector<double> jds;
    const double nStep = (jdLOS - jdAOS) / step;
    if (nStep > 100000.0) {
        // 与 collectSamples 同款上限（GEO 长过境 + 极小步长下逐点
        // visibleAt 即 SunPosition+propagate+umbraFactor, 无界耗时）
        return;
    }
    jds.push_back(jdAOS);
    // 网格用整数索引步进（避免 jd += step 的累积漂移，见 collectSamples 说明）；
    // 内点严格早于 jdLOS，端点由下方显式压入（点数换算见 stepCountExclusive）
    {
        const long long nInner = stepCountExclusive(jdAOS, jdLOS, step);
        for (long long i = 1; i <= nInner; ++i) {
            const double jd = jdAOS + static_cast<double>(i) * step;
            if (jd >= jdLOS) break;
            jds.push_back(jd);
        }
    }
    jds.push_back(jdLOS);
    {
        double tca = intMsToJd(ev.tcaJdMs);
        if (tca > jdAOS && tca < jdLOS) {
            // 插入 TCA（若已与某采样点重合则跳过）
            bool dup = false;
            for (double jd : jds) {
                if (std::abs(jd - tca) * 86400.0 < 0.5) { dup = true; break; }
            }
            if (!dup) {
                jds.push_back(tca);
                std::sort(jds.begin(), jds.end());
            }
        }
    }

    // 逐采样点求可见性，收集连续 true 段
    std::vector<bool> vis(jds.size());
    for (size_t i = 0; i < jds.size(); ++i) vis[i] = visibleAt(jds[i]);

    struct Run { int s, e; };   // 闭区间 [s, e]
    std::vector<Run> runs;
    size_t i = 0;
    while (i < jds.size()) {
        if (!vis[i]) { ++i; continue; }
        size_t s = i;
        while (i < jds.size() && vis[i]) ++i;
        runs.push_back({ static_cast<int>(s), static_cast<int>(i - 1) });
    }
    if (runs.empty()) return;

    // 多段时优先取包含 TCA 的连续段（M-1: 可见弧段与可见性分类同以 TCA
    // 时刻为准，避免"弧段未含 TCA"与"标为可见"互相矛盾）；
    // 仅当 TCA 不在任何可见段内（理论矛盾，防御）时退化为取最长段
    size_t best = runs.size();
    const double tcaJd = intMsToJd(ev.tcaJdMs);
    for (size_t k = 0; k < runs.size(); ++k) {
        if (jds[runs[k].s] <= tcaJd && tcaJd <= jds[runs[k].e]) {
            best = k;
            break;
        }
    }
    if (best == runs.size()) {
        best = 0;
        for (size_t k = 1; k < runs.size(); ++k) {
            if (jds[runs[k].e] - jds[runs[k].s]
                > jds[runs[best].e] - jds[runs[best].s])
                best = k;
        }
    }
    // 该段前后边界: 内部采样直接取端点，否则二分精化翻转区间
    double b0 = (runs[best].s > 0)
        ? refineBoundary(jds[runs[best].s - 1], jds[runs[best].s])
        : jdAOS;
    double b1 = (jds.size() > 1 && runs[best].e < static_cast<int>(jds.size()) - 1)
        ? refineBoundary(jds[runs[best].e], jds[runs[best].e + 1])
        : jdLOS;

    ev.hasVisibleArc = true;
    ev.visAosJdMs = jdToIntMs(b0);
    ev.visLosJdMs = jdToIntMs(b1);
}

// ========== 二分精化 AOS ==========
double OrbitCalculator::bisectAOS(PropagateFn &propagate,
                                   const Observer &obs,
                                   double jdBelow, double jdAbove,
                                   const AtomicCancelFlagPtr cancel) const
{
    double maskEl = obs.minElevDeg();
    // 跨窗口过境: 下界处仰角已在掩星角之上（AOS 在窗口起点之前）→ 直接取窗口起点
    if (elevationAt(propagate, obs, jdBelow) >= maskEl)
        return jdBelow;
    double lo = jdBelow, hi = jdAbove;
    for (int i = 0; i < m_cfg.bisectionIters; ++i) {
        if (isCancelled(cancel)) return lo;   // 取消响应: 二分期间也保持敏感
        double mid = (lo + hi) * 0.5;
        double el = elevationAt(propagate, obs, mid);
        if (el == ELEV_FAILURE_SENTINEL) {
            // 传播失败: 向已知 below-horizon 端收缩，保证区间收缩
            hi = mid;
            continue;
        }
        if (el < maskEl) lo = mid;
        else             hi = mid;
    }
    return hi;
}

// ========== 二分精化 LOS ==========
double OrbitCalculator::bisectLOS(PropagateFn &propagate,
                                   const Observer &obs,
                                   double jdAbove, double jdBelow,
                                   const AtomicCancelFlagPtr cancel) const
{
    double maskEl = obs.minElevDeg();
    double lo = jdAbove, hi = jdBelow;
    for (int i = 0; i < m_cfg.bisectionIters; ++i) {
        if (isCancelled(cancel)) return lo;
        double mid = (lo + hi) * 0.5;
        double el = elevationAt(propagate, obs, mid);
        if (el == ELEV_FAILURE_SENTINEL) {
            // 传播失败: 向已知 above-horizon 端收缩，保证区间收缩
            lo = mid;
            continue;
        }
        if (el >= maskEl) lo = mid;
        else              hi = mid;
    }
    return lo;
}

// ========== TCA 扫描（最大仰角时刻） ==========
void OrbitCalculator::findTCA(PropagateFn &propagate,
                               const Observer &obs,
                               double aosJD, double losJD,
                               int64_t &outJdMs, double &outElDeg,
                               LookAngle &outLook, GeoPos &outGeo,
                               const AtomicCancelFlagPtr cancel) const
{
    double stepDays = m_cfg.maxElevScanStepSec / 86400.0;
    double bestJD = aosJD;
    double bestEl = ELEV_FAILURE_SENTINEL;
    outJdMs  = 0;       // 取消时保证不输出垃圾值
    outElDeg = ELEV_FAILURE_SENTINEL; // 取消时保证不输出垃圾值

    // 粗扫描（整数索引步进, 避免累加漂移; 点数含端点, 换算见 stepCountInclusive）
    {
        const long long nScan = stepCountInclusive(aosJD, losJD, stepDays);
        for (long long i = 0; i <= nScan; ++i) {
            const double jd = aosJD + static_cast<double>(i) * stepDays;
            if (isCancelled(cancel)) return;
            double el = elevationAt(propagate, obs, jd);
            if (el == ELEV_FAILURE_SENTINEL) continue;   // 失败点不参与峰值竞争
            if (el > bestEl) {
                bestEl = el;
                bestJD = jd;
            }
        }
    }

    // 标准三分搜索精化 TCA（单峰函数保证收敛，无平台发散问题）;
    // 半宽取 max(3s, 1.5×粗扫步长)——粗扫最优点与真峰可相距 ±1 步长,
    // 窗口过窄会收敛到次峰; 窗口再钳制到 [aos, los]: 跨窗/截断过境的峰
    // 可能落在窗口边界外, 不钳制会抖出 tcaJd < aosJd 的时序倒挂
    const double delta = std::max(TCA_REFINE_SEC,
                                  1.5 * m_cfg.maxElevScanStepSec) / 86400.0;
    double lo = std::max(bestJD - delta, aosJD);
    double hi = std::min(bestJD + delta, losJD);
    for (int i = 0; i < m_cfg.bisectionIters; ++i) {
        if (isCancelled(cancel)) return;
        double m1 = lo + (hi - lo) / 3.0;
        double m2 = hi - (hi - lo) / 3.0;
        double el1 = elevationAt(propagate, obs, m1);
        double el2 = elevationAt(propagate, obs, m2);
        bool fail1 = (el1 == ELEV_FAILURE_SENTINEL);
        bool fail2 = (el2 == ELEV_FAILURE_SENTINEL);
        if (fail1 && fail2) {
            // 双侧均失败: 向当前最优端收缩，保证窗口收缩
            lo = m1;
            hi = m2;
        } else if (fail1) {
            // m1 失败: 用 m2 收缩
            if (el2 >= bestEl) lo = m1;   // 峰在右侧
            else               hi = m2;   // 峰在左侧或当前位置
        } else if (fail2) {
            // m2 失败: 用 m1 收缩
            if (el1 >= bestEl) hi = m2;   // 峰在左侧
            else               lo = m1;   // 峰在右侧或当前位置
        } else if (el1 >= el2) {
            hi = m2;
        } else {
            lo = m1;
        }
    }

    bestJD = (lo + hi) * 0.5;
    bestJD = std::max(aosJD, std::min(bestJD, losJD));   // 数值误差兜底
    outElDeg = elevationAt(propagate, obs, bestJD);
    // 若精确点传播失败，使用搜索阶段找到的最佳仰角兜底（避免单点故障丢弃有效过境）
    if (outElDeg == ELEV_FAILURE_SENTINEL && bestEl > ELEV_FAILURE_SENTINEL) {
        outElDeg = bestEl;
    }
    computeMoment(propagate, obs, bestJD, outLook, outGeo);
    outJdMs = jdToIntMs(bestJD);
}

// ========== 主入口 ==========
std::vector<PassEvent> OrbitCalculator::predict(PropagateFn propagate,
                                                 const Observer &observer,
                                                 double jdStart,
                                                 double hoursAhead,
                                                 const AtomicCancelFlagPtr cancel) const
{
    // 入口防御: 非法配置/非法时间会导致下方主循环 `for(;;)` 永不死去
    // （stepDays<=0 不前进、jdStart=NaN 恒不满足 jd>=jdEnd）→ 拒绝并返回空
    const double stepDays = m_cfg.coarseStepSec / 86400.0;
    
    // 详细的输入验证和错误信息
    if (!(stepDays > 0.0)) {
        throw std::invalid_argument("OrbitCalculator配置错误: 粗搜索步长必须为正数。当前配置: " +
                                   std::to_string(m_cfg.coarseStepSec) + " 秒");
    }
    if (!std::isfinite(jdStart)) {
        throw std::invalid_argument("OrbitCalculator计算错误: 起始儒略日无效。当前值: " +
                                   std::to_string(jdStart));
    }
    if (!std::isfinite(hoursAhead)) {
        throw std::invalid_argument("OrbitCalculator计算错误: 预测时长无效。当前值: " +
                                   std::to_string(hoursAhead) + " 小时");
    }
    if (hoursAhead < 0.0) {
        throw std::invalid_argument("OrbitCalculator配置错误: 预测时长必须为非负数。当前值: " +
                                   std::to_string(hoursAhead) + " 小时");
    }
    // 上限防御: 超长窗口会让 maxSteps 溢出/预分配数百 GB（bad_alloc 未捕获
    // 即进程崩溃）。UI 限 24h, 一年(8760h)上限远高于真实用途
    if (hoursAhead > 8760.0) {
        throw std::invalid_argument("OrbitCalculator配置错误: 预测时长超过一年(8760h)上限。当前值: " +
                                   std::to_string(hoursAhead) + " 小时");
    }
    if (!std::isfinite(observer.minElevDeg())) {
        throw std::invalid_argument("OrbitCalculator配置错误: 掩星角无效。观测站掩星角: " +
                                   std::to_string(observer.minElevDeg()) + "°");
    }
    // 粗搜索步长不超过窗口的 1/4，否则会跳过整个过境
    const double windowSec = hoursAhead * 3600.0;
    if (windowSec > 0.0 && m_cfg.coarseStepSec > windowSec / 4.0) {
        throw std::invalid_argument("OrbitCalculator配置错误: 粗搜索步长超过窗口 1/4。步长: " +
                                   std::to_string(m_cfg.coarseStepSec) + " 秒, 窗口: " +
                                   std::to_string(static_cast<int>(windowSec)) + " 秒");
    }
    // 峰值扫描步长校验: findTCA 以此为 for 步进, ≤0 时条件恒真硬挂死 worker
    //（区别于 coarseStepSec 的"慢", 这是死循环）; 迭代次数下限 1 同理防御
    if (!(m_cfg.maxElevScanStepSec > 0.0) || !std::isfinite(m_cfg.maxElevScanStepSec)) {
        throw std::invalid_argument("OrbitCalculator配置错误: 峰值扫描步长必须为正数。当前值: " +
                                    std::to_string(m_cfg.maxElevScanStepSec) + " 秒");
    }
    if (m_cfg.bisectionIters < 1) {
        throw std::invalid_argument("OrbitCalculator配置错误: 二分迭代次数必须≥1。当前值: " +
                                    std::to_string(m_cfg.bisectionIters));
    }
    
    // 计算结束时间并验证合理性
    const double jdEnd    = jdStart + hoursAhead / 24.0;
    const double maskEl   = observer.minElevDeg();
    
    // 优化：预分配 PassEvent 向量容量，避免频繁重新分配
    // 估算最大可能的过境数量：假设每60分钟一个过境，每个过境持续10分钟，额外预留50%余量
    const double maxEstimatedPasses = std::ceil(hoursAhead * 60.0 / (60.0 + 10.0));
    std::vector<PassEvent> passes;
    {
        size_t cap = maxEstimatedPasses > 0 ? static_cast<size_t>(maxEstimatedPasses) : 10;
        cap += cap / 2;
        passes.reserve(cap);
    }
    
    // 优化：使用移动语义避免 PassEvent 的不必要拷贝
    
    // 添加调试信息（仅在调试模式下）
    #ifdef QT_DEBUG
    qDebug() << QString("OrbitCalculator开始计算: 起始JD=%.6f, 预测时长=%.1f小时, 掩星角=%.1f°")
        .arg(jdStart, 0, 'f', 6)
        .arg(hoursAhead)
        .arg(maskEl);
    #endif

    // 初始状态: 若窗口起点已在掩星角之上，视为过境中（AOS 在窗口前，用窗口起点作下界）
    double el0 = elevationAt(propagate, observer, jdStart);
    bool   inPass     = (el0 >= maskEl);
    double prevEl     = el0;
    double candBelow  = jdStart;   // AOS 候选：仰角 < maskEl 的 JD
    double candAbove  = jdStart;   // AOS 候选：仰角 ≥ maskEl 的 JD

    // ---- 粗搜索 ----
    // 末次采样推进到窗口终点（jdEnd），防窗口末尾一个粗搜步长内的 AOS 漏检;
    // 对完全落在相邻两采样点之间的短过境做中点探测
    auto assemblePass = [&](double jdAOS, double jdLOS,
                            bool truncated = false) -> bool {
        double durSec = (jdLOS - jdAOS) * 86400.0;
        if (durSec < m_cfg.minDurationSec) return false;
        PassEvent ev;
        ev.truncated = truncated;   // 窗口终点截断: LOS 字段为瞬时值非真实出境
        computeMoment(propagate, observer, jdAOS, ev.aosLook, ev.aosSatGeo);
        ev.aosJdMs = jdToIntMs(jdAOS);
        // TCA（findTCA 内部取消时输出已知哨兵值，这里再检查一次取消）
        findTCA(propagate, observer, jdAOS, jdLOS,
                ev.tcaJdMs, ev.maxElevDeg, ev.tcaLook, ev.tcaSatGeo, cancel);
        if (isCancelled(cancel)) return false;
        // 防御: 精化峰值仍低于掩星角（数值异常/传播抖动）时整段丢弃，
        // 防止 findTCA 抖出 -1000 级别的垃圾过境（L-1）
        if (ev.maxElevDeg < maskEl) return false;
        computeMoment(propagate, observer, jdLOS, ev.losLook, ev.losSatGeo);
        ev.losJdMs = jdToIntMs(jdLOS);
        ev.durationSec = durSec;
        collectSamples(propagate, observer, jdAOS, jdLOS, ev, cancel);
        ev.satelliteName = m_satName;
        fillPassDetails(ev, propagate, observer, m_cfg, jdAOS,
                        intMsToJd(ev.tcaJdMs), jdLOS);
        collectVisibleArc(propagate, observer, jdAOS,
                          intMsToJd(ev.losJdMs), ev, cancel);
        passes.push_back(ev);
        return true;
    };

    // 兜底迭代上限: 正常路径由 jd>=jdEnd 退出; 此上限仅防御
    // stepDays 极小值（用户配置 0.001s 级）造成的亿级迭代
    const int64_t maxSteps = static_cast<int64_t>(std::ceil(
        hoursAhead * 86400.0 / std::max(m_cfg.coarseStepSec, 1e-6))) + 2;
    int64_t stepsDone = 0;
    // 采样时刻 = jdStart + 整数索引 × 步长（避免 jd += stepDays 的累积漂移:
    // 24h/15s 窗口累计约 5 ms ≈ 40 m 位置偏差, 会让与 STK 的逐点比对无法收敛）
    for (int64_t i = 0;; ++i) {
        const double jd     = jdStart + static_cast<double>(i) * stepDays;
        const double jdPrev = jdStart + static_cast<double>(i > 0 ? i - 1 : 0) * stepDays;
        if (isCancelled(cancel) || ++stepsDone > maxSteps) return passes;   // 协作式取消

        double el = elevationAt(propagate, observer, jd);
        const bool elFailed = (el == ELEV_FAILURE_SENTINEL);
        // 传播瞬时失败: 无可靠信息, 保持 prevEl 并跳过状态转移/边界检测——
        // 失败点仰角其实可能仍在掩星角之上, 直接当 LOS 会错误分裂或
        // 截断过境; 仅推进时间不丢采样
        if (!elFailed) {
            if (!inPass) {
                // 检测 AOS ▲
                if (prevEl < maskEl && el >= maskEl) {
                    inPass = true;
                    candBelow = jdPrev;
                    candAbove = jd;
                } else if (prevEl < maskEl && i > 0) {
                    // 步内短过境探测(4 等分子步): 原实现只探中点, 完全落在
                    // 一个步长内且不跨中点的 10~15s 级短过境会漏检;
                    // 现对 4 个子区间逐段检测连续 ≥掩星角的子段并逐段装配
                    //
                    // 守卫必须是 i > 0（存在"上一个样本"）: 早前写成
                    // jdPrev >= jdStart, 而 i=0 时 jdPrev == jdStart 也满足该式,
                    // 会在窗口第一个样本处探测 [jdStart-step, jdStart] 这段
                    // **窗口之前**的时间, 可能装配出 AOS 早于窗口起点的过境
                    const double sub = stepDays / 4.0;
                    double pb[5] = { prevEl, el, el, el, el }; // 下标0/4 必低于掩星角
                    for (int k = 1; k <= 3; ++k)
                        pb[k] = elevationAt(propagate, observer, jd - (4 - k) * sub);
                    for (int s = 1; s <= 3; ++s) {
                        if (pb[s] < maskEl) continue;
                        int t = s;
                        while (t < 3 && pb[t + 1] >= maskEl) ++t;
                        // 连续段 [s, t] 两端(t_{s-1}/t_{t+1})均低于掩星角 → 整段可装配
                        assemblePass(
                            bisectAOS(propagate, observer,
                                      jd - (4 - (s - 1)) * sub, jd - (4 - s) * sub,
                                      cancel),
                            bisectLOS(propagate, observer,
                                      jd - (4 - t) * sub, jd - (4 - (t + 1)) * sub,
                                      cancel));
                        s = t;
                    }
                }
            } else {
                // 检测 LOS ▼
                if (prevEl >= maskEl && el < maskEl) {
                    double jdAOS = bisectAOS(propagate, observer,
                                             candBelow, jd, cancel);
                    double jdLOS = bisectLOS(propagate, observer,
                                             candAbove, jd, cancel);
                    assemblePass(jdAOS, jdLOS);
                    inPass = false;
                }
            }
            prevEl = el;
        }
        if (jd >= jdEnd) break;
    }

    // ---- 跨窗口终点处理 ----
    if (inPass) {
        // 过境延伸至窗口终点之外: 按 STK Access 语义在窗口终点 (jdEnd) 截断。
        // LOS 字段/时长均为窗口终点瞬时值, 非真实出境值（truncated=true 标注）。
        double jdAOS = bisectAOS(propagate, observer, candBelow, jdEnd, cancel);
        assemblePass(jdAOS, jdEnd, true);
    }

    // ---- 合并相邻过境 ----
    if (!isCancelled(cancel) && passes.size() > 1 && m_cfg.mergeGapSec > 0.0) {
        const double gapDays = m_cfg.mergeGapSec / 86400.0;
        // 优化：预分配合并后的向量容量
        std::vector<PassEvent> merged;
        merged.reserve(passes.size());  // 最坏情况下不需要合并
        merged.push_back(std::move(passes[0]));
        for (size_t i = 1; i < passes.size(); ++i) {
            double prevLos = intMsToJd(merged.back().losJdMs);
            double curAos  = intMsToJd(passes[i].aosJdMs);
            if (curAos - prevLos < gapDays) {
                // 后段 AOS（ms）: 两段间隙（地平线以下）内的采样点须丢弃,
                // 否则绘出负仰角凹陷并破坏"采样仰角 ≥ 掩星角"的不变量
                const int64_t curAosMs = passes[i].aosJdMs;
                // 截断标记取并集: 任一段被截断则合并结果仍是截断过境
                if (passes[i].truncated) merged.back().truncated = true;
                // 合并：用较晚的 LOS 替换
                if (passes[i].losJdMs > merged.back().losJdMs) {
                    merged.back().losJdMs = passes[i].losJdMs;
                    merged.back().losLook = passes[i].losLook;
                    merged.back().losSatGeo = passes[i].losSatGeo;
                    merged.back().losSunAlt = passes[i].losSunAlt;
                    merged.back().losSatShadow = passes[i].losSatShadow;
                    merged.back().losIllum = passes[i].losIllum;
                    merged.back().durationSec = (intMsToJd(merged.back().losJdMs) - intMsToJd(merged.back().aosJdMs)) * 86400.0;
                    // 可见弧段: 合并后取两段并集区间
                    if (passes[i].hasVisibleArc && merged.back().hasVisibleArc) {
                        merged.back().visAosJdMs = std::min(merged.back().visAosJdMs,
                                                            passes[i].visAosJdMs);
                        merged.back().visLosJdMs = std::max(merged.back().visLosJdMs,
                                                            passes[i].visLosJdMs);
                    } else if (passes[i].hasVisibleArc) {
                        merged.back().hasVisibleArc = true;
                        merged.back().visAosJdMs = passes[i].visAosJdMs;
                        merged.back().visLosJdMs = passes[i].visLosJdMs;
                    }
                }
                // 取较高的最大仰角
                if (passes[i].maxElevDeg > merged.back().maxElevDeg) {
                    merged.back().maxElevDeg = passes[i].maxElevDeg;
                    merged.back().tcaJdMs = passes[i].tcaJdMs;
                    merged.back().tcaLook = passes[i].tcaLook;
                    merged.back().tcaSatGeo = passes[i].tcaSatGeo;
                    merged.back().tcaSunAlt = passes[i].tcaSunAlt;
                    merged.back().tcaSatShadow = passes[i].tcaSatShadow;
                    merged.back().tcaIllum = passes[i].tcaIllum;
                    merged.back().dopplerAtTcaKHz = passes[i].dopplerAtTcaKHz;
                    merged.back().magAtTca = passes[i].magAtTca;
                    merged.back().magValid = passes[i].magValid;
                }
                // L-2 修复: 弧段采样合并（无论 TCA 取自哪一段都执行），
                // 追加后半段的点并保持时间单调、跳过重叠区与两段间隙
                // （间隙点在 [mergeLosMs, curAosMs) 内, 地平线以下）——
                // 保证 TCA 峰值处的采样点不会从星表中消失
                auto &dst = merged.back().samples;
                const int64_t lastT = dst.empty() ? std::numeric_limits<int64_t>::min()
                                                  : dst.back().jdMs;
                for (const auto &s : passes[i].samples)
                    if (s.jdMs > lastT && s.jdMs >= curAosMs) dst.push_back(s);
            } else {
                merged.push_back(std::move(passes[i]));
            }
        }
        // M-2 修复: 合并可能抬高 maxElevDeg / 替换 TCA，
        // 质量等级与过境类型须按最终字段重算（公式与 fillPassDetails 保持一致）
        for (auto &ev : merged) {
            if (ev.maxElevDeg >= 50.0)      ev.quality = PassQuality::Excellent;
            else if (ev.maxElevDeg >= 20.0) ev.quality = PassQuality::Good;
            else                            ev.quality = PassQuality::Poor;
            // 与 fillPassDetails 对齐: 用纬度趋势（而非方位角差值）判断升/降轨
            // 合并后 aosSatGeo 取首段、losSatGeo 取末段，均为正确极值
            ev.passType = (ev.aosSatGeo.latDeg < ev.losSatGeo.latDeg) ? 0 : 1;
            // 可见性同以新 TCA 时刻的光照/太阳高度重推——TCA 可能已换为
            // 后段, 旧分类（前段 TCA）会与展示字段自相矛盾。
            // 前提守卫: 重推依赖 aos/tca/losSunAlt 与 Illum 字段有效——
            // enableSunAnalysis 关闭时它们是结构默认值（0/FullSunlight/1.0）,
            // 无条件重推会用假数据把分类翻转成 Visible, 与未合并段不一致
            if (m_cfg.enableSunAnalysis) {
                bool satLit = (ev.tcaSatShadow != ShadowStatus::InUmbra)
                              && ev.tcaIllum > 0.5;
                if (ev.tcaSunAlt >= m_cfg.darkSkyThresholdDeg)
                    ev.visibility = PassVisibility::Daylight;
                else if (!satLit)
                    ev.visibility = PassVisibility::RadioOnly;
                else
                    ev.visibility = PassVisibility::Visible;
            }
        }
        passes = std::move(merged);
    }

    // ---- 排序: 按 AOS 升序 ----
    std::sort(passes.begin(), passes.end(),
              [](const PassEvent &a, const PassEvent &b) {
                  return a.aosJdMs < b.aosJdMs;
              });

    return passes;
}
```

---

## core/OrbitCalculator.h

```cpp
#ifndef CORE_ORBITCALCULATOR_H
#define CORE_ORBITCALCULATOR_H
// =============================================================================
// OrbitCalculator.h — 过境预报引擎
// 完整流程: 粗搜(15s步) → 二分精化(18次) → TCA扫描 → 光照/多普勒 → 事件组装
// =============================================================================
#include "DataTypes.h"
#include "Observer.h"
#include "AtomicCancelFlag.h"
#include "CoordTransform.h"
#include <atomic>
#include <functional>
#include <optional>
#include <vector>

class OrbitCalculator {
public:
    // 传播函数: JD → TEME 状态（nullopt = 传播失败/衰减）
    using PropagateFn = std::function<std::optional<EciState>(double jd)>;

    struct Config {
        double coarseStepSec      = 15.0;   // 过境检测粗搜步长: 15s 可检出 1~2 分钟短过境
        int    bisectionIters     = 18;
        double maxElevScanStepSec = 3.0;
        double minDurationSec     = 10.0;
        double mergeGapSec        = 60.0;
        GravityModel gravityModel = GravityModel::WGS72;  // 重力模型
        bool   enableSunAnalysis   = true;
        bool   enableShadowCheck   = true;
        double darkSkyThresholdDeg = -6.0;
        bool   enableDoppler       = false;
        double carrierFreqMHz      = 437.800;
        bool   enableSamples       = true;   // 采集过境弧段采样点
        double sampleStepSec       = 15.0;   // 弧段采样步长（秒）
        bool   enableMagnitude     = true;   // 星等估算（Vallerie 1963 相位模型）
        double stdMagFallback      = 4.5;    // 无目录匹配时的固有星等（1000 km）
        bool   enableVisArc        = true;   // 可见弧段子区间分析（Heavens-Above 语义）
        Config() = default;
    };

    OrbitCalculator() : m_cfg() {}
    explicit OrbitCalculator(const Config &cfg) : m_cfg(cfg) {}

    // 主入口
    // cancel: 可选的协作式取消标志（工作线程中置位后，预测会在下一个步长提前返回）
    std::vector<PassEvent> predict(PropagateFn propagate,
                                   const Observer &observer,
                                   double jdStart,
                                   double hoursAhead = 24.0,
                                   const AtomicCancelFlagPtr cancel = nullptr) const;

    // 卫星名称（用于填充 PassEvent::satelliteName）
    void setSatelliteName(const std::string &name) { m_satName = name; }

private:
    Config m_cfg;
    mutable std::string m_satName;   // 卫星名称

    // 并发契约（勘误）: predict 参数化已使引擎计算逻辑无状态, 但 m_satName
    // 是 setSatelliteName 非原子写 + predict 内读——std::string 并发读写即
    // 数据竞争（UB）。同一实例仅允许单线程串行使用（业务层每 worker 独立
    // 实例, 满足契约）; 跨线程共享实例时必须在构造/启动后只读 predict
    static bool isCancelled(const AtomicCancelFlagPtr cancel) {
        return cancel && cancel->isCancelled();
    }

    double elevationAt(PropagateFn &propagate,
                       const Observer &obs, double jd) const;

    double bisectAOS(PropagateFn &propagate, const Observer &obs,
                     double jdBelow, double jdAbove,
                     const AtomicCancelFlagPtr cancel) const;
    double bisectLOS(PropagateFn &propagate, const Observer &obs,
                     double jdAbove, double jdBelow,
                     const AtomicCancelFlagPtr cancel) const;

    void findTCA(PropagateFn &propagate, const Observer &obs,
                 double aosJD, double losJD,
                 int64_t &outJdMs, double &outElDeg,
                 LookAngle &outLook, GeoPos &outGeo,
                 const AtomicCancelFlagPtr cancel) const;

    void computeMoment(PropagateFn &propagate, const Observer &obs,
                       double jd, LookAngle &look, GeoPos &geo) const;

    // 在 [AOS, LOS] 内等步长采集弧段采样点（填 PassEvent::samples，
    // enableDoppler 时同时填充各采样点多普勒）
    void collectSamples(PropagateFn &propagate, const Observer &obs,
                        double jdAOS, double jdLOS, PassEvent &ev,
                        const AtomicCancelFlagPtr cancel) const;

    // 可见弧段子区间: 在 [AOS, LOS] 内扫描"暗天 + 被照亮"的最大连续区间，
    // 边界做二分精化（填 visAosJdMs/visLosJdMs/hasVisibleArc）
    void collectVisibleArc(PropagateFn &propagate, const Observer &obs,
                            double jdAOS, double jdLOS, PassEvent &ev,
                            const AtomicCancelFlagPtr cancel) const;

    static int64_t jdToIntMs(double jd);
    static double intMsToJd(int64_t ms);
};

#endif // CORE_ORBITCALCULATOR_H
```

---

## core/Propagator.cpp

```cpp
// =============================================================================
// Propagator.cpp — perturb::Satellite 封装实现
// =============================================================================
#include "Propagator.h"
#include "TleParser.h"
#include "perturb-master/include/perturb/perturb.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cassert>
#include <stdexcept>

Propagator::Propagator() {}
Propagator::~Propagator() {}   // unique_ptr 自动释放（此处需完整类型，已在下方 include）

Propagator::Propagator(Propagator &&other) noexcept
    : m_satellite(std::move(other.m_satellite))
    , m_initialized(other.m_initialized)
    , m_epochJD(other.m_epochJD)
    , m_name(std::move(other.m_name))
    , m_noradID(other.m_noradID)
    , m_modelType(other.m_modelType)
{
    other.m_initialized = false;
}

Propagator &Propagator::operator=(Propagator &&other) noexcept
{
    if (this != &other) {
        m_satellite = std::move(other.m_satellite);
        m_initialized = other.m_initialized;
        m_epochJD = other.m_epochJD;
        m_name = std::move(other.m_name);
        m_noradID = other.m_noradID;
        m_modelType = other.m_modelType;
        other.m_initialized = false;
    }
    return *this;
}

bool Propagator::initialize(const std::string &line1, const std::string &line2,
                             std::string &errorMsg, GravityModel gravModel)
{
    // 先清理旧实例
    m_satellite.reset();
    m_initialized = false;

    // 非标准 TLE 规范化: 与 STK 宽容策略对齐
    // 处理额外空格(70+字符)、字母 NORAD 前缀、缺少隐含小数点等
    auto [norm1, norm2] = TleParser::normalizeTleLines(line1, line2);

    // 规范化后校验 69 字符列对齐
    if (norm1.length() != kTleLineLength || norm2.length() != kTleLineLength) {
        errorMsg = "TLE 行长度应为 69 字符";
        if (norm1.length() != kTleLineLength)
            errorMsg += " (行1: " + std::to_string(norm1.length()) + ")";
        if (norm2.length() != kTleLineLength)
            errorMsg += " (行2: " + std::to_string(norm2.length()) + ")";
        return false;
    }

    // perturb::Satellite::from_tle 接受非 const string 引用（底层会修改）
    std::string l1 = norm1;
    std::string l2 = norm2;

    // 重力模型转换
    auto toPerturbGravModel = [](GravityModel gm) -> perturb::GravModel {
        switch (gm) {
            case GravityModel::WGS72:     return perturb::GravModel::WGS72;
            case GravityModel::WGS84:     return perturb::GravModel::WGS84;
        }
        assert(false && "新增重力模型枚举值未更新 toPerturbGravModel");
        return perturb::GravModel::WGS72;
    };

    try {
        auto sat = std::make_unique<perturb::Satellite>(
            perturb::Satellite::from_tle(l1, l2, toPerturbGravModel(gravModel)));

        auto err = sat->last_error();
        if (err != perturb::Sgp4Error::NONE) {
            switch (err) {
            case perturb::Sgp4Error::INVALID_TLE:
                errorMsg = "TLE 格式无效: TLE 数据不符合规范要求";
                errorMsg += "\n可能原因: ";
                errorMsg += "- TLE字段格式错误（如非数字字符、字段越界）";
                errorMsg += "\n- 轨道根数超出物理范围（如偏心率≥1、倾角>180°）";
                errorMsg += "\n- 校验和错误导致数据损坏";
                errorMsg += "\n- TLE版本与SGP4模型不兼容";
                break;
            case perturb::Sgp4Error::DECAYED:
                errorMsg = "卫星已衰减: 卫星轨道已衰减至大气层内";
                errorMsg += "\n可能原因: ";
                errorMsg += "- 卫星大气阻力过大导致轨道快速衰减";
                errorMsg += "\n- 使用了过时的TLE数据（历元时间过早）";
                errorMsg += "\n- B*阻力项参数异常";
                errorMsg += "\n建议: 使用更新的TLE数据或检查卫星状态";
                break;
            case perturb::Sgp4Error::MEAN_ELEMENTS:
                errorMsg = "平根数异常: 使用平根数而非瞬时根数";
                errorMsg += "\n可能原因: ";
                errorMsg += "- TLE使用了平均轨道根数而非瞬时根数";
                errorMsg += "\n- SGP4模型版本与TLE格式不匹配";
                errorMsg += "\n建议: 确认TLE格式或尝试使用不同SGP4实现";
                break;
            default:
                errorMsg = "SGP4 初始化错误: 未知错误代码 " + std::to_string(static_cast<int>(err));
                errorMsg += "\n可能原因: ";
                errorMsg += "- 未知或未处理的SGP4错误类型";
                errorMsg += "\n- perturb库版本与TLE格式不兼容";
                errorMsg += "\n- 内存分配或数值计算异常";
                errorMsg += "\n建议: 更新perturb库或联系技术支持";
                break;
            }
            
            return false;   // unique_ptr 自动释放
        }

        // 获取历元
        perturb::JulianDate epoch = sat->epoch();
        if (!std::isfinite(epoch.jd) || !std::isfinite(epoch.jd_frac)) {
            errorMsg = "历元数据无效: 儒略日数据不正常";
            return false;
        }
        m_epochJD = epoch.jd + epoch.jd_frac;
        if (!std::isfinite(m_epochJD)) {
            errorMsg = "历元计算错误: 儒略日计算结果异常";
            return false;
        }

        // NORAD 编号（elsetrec.satnum）与轨道模型类别（由平运动推导周期）
        //   no_unkozai: 未作 Kozai 修正的平运动 (rad/min) ⇒ 周期 = 2π/no_unkozai
        const auto &rec = sat->sat_rec;
        
        // 防御性解析 NORAD 编号
        {
            const char *b = rec.satnum, *e = rec.satnum + 5;
            while (b < e && (*b == ' ' || *b == '\t')) ++b;
            while (e > b && (e[-1] == ' ' || e[-1] == '\t')) --e;
            int nid = 0;
            const auto res = std::from_chars(b, e, nid);
            if (res.ec == std::errc() && res.ptr == e) {
                m_noradID = nid;
                if (m_noradID <= 0) {
                    errorMsg = "NORAD 编号无效: 编号必须为正整数";
                    return false;
                }
            } else {
                m_noradID = 0;
                errorMsg = "NORAD 编号解析失败: 无法从 '" + std::string(rec.satnum) + "' 解析有效编号";
                return false;
            }
        }
        
        if (m_name.empty()) {
            m_name = rec.satnum;
            // 清理名称中的空白字符
            m_name.erase(std::remove_if(m_name.begin(), m_name.end(), 
                                      [](char c) { return std::isspace(c); }), m_name.end());
            if (m_name.empty()) {
                m_name = "Unknown Satellite";
            }
        }
        
        // 检查平运动数据有效性
        if (!std::isfinite(rec.no_unkozai)) {
            errorMsg = "平运动数据无效: 平运动值不是有限数值";
            return false;
        }
        
        const double periodMin = (rec.no_unkozai > 1e-12)
            ? (2.0 * M_PI) / rec.no_unkozai : 0.0;
            
        if (!std::isfinite(periodMin) || periodMin <= 0.0) {
            errorMsg = "轨道周期计算错误: 计算得到的周期无效";
            return false;
        }
        
        // 分类带: 阈值与 TleParser::classifyModel 对齐（225/1400 min）,
        // 但基准不同——此处用 Kozai 修正后平运动 no_unkozai 推周期（SGP4
        // 实际采用, 更准）, 解析器用目录平运动 meanMotionRevDay。近边界
        // 轨道两处标签可能相差一个 Kozai 修正量级; 仅影响展示字段
        //（ephemerisType 标签）, 不影响传播路径本身
        if (periodMin < 225.0)          m_modelType = Sgp4Type::LEO_SGP4;
        else if (periodMin < 1400.0)    m_modelType = Sgp4Type::MEO_SDP4;
        else                            m_modelType = Sgp4Type::GEO_SDP4_DS;

        m_satellite = std::move(sat);
    } catch (const std::invalid_argument &e) {
        errorMsg = "参数错误: " + std::string(e.what());
        return false;
    } catch (const std::out_of_range &e) {
        errorMsg = "范围错误: " + std::string(e.what());
        return false;
    } catch (const std::exception &e) {
        errorMsg = "perturb 标准异常: " + std::string(e.what());
        return false;
    } catch (...) {
        errorMsg = "perturb 未知异常: 未知类型的异常被抛出";
        return false;
    }

    m_initialized = true;
    return true;
}

std::optional<EciState> Propagator::propagate(double jd)
{
    if (!m_satellite || !m_initialized)
        return std::nullopt;

    auto *sat = m_satellite.get();

    // double JD → perturb::JulianDate
    perturb::JulianDate targetJD(jd);

    perturb::StateVector sv;
    perturb::Sgp4Error err = sat->propagate(targetJD, sv);

    if (err != perturb::Sgp4Error::NONE)
        return std::nullopt;

    // 防御: 数值异常（位置/速度非有限）时按失败处理,
    // 防止 NaN 污染后续过境几何/光照计算
    if (!std::isfinite(sv.position[0]) || !std::isfinite(sv.position[1])
        || !std::isfinite(sv.position[2])
        || !std::isfinite(sv.velocity[0]) || !std::isfinite(sv.velocity[1])
        || !std::isfinite(sv.velocity[2]))
        return std::nullopt;

    // 量值合理性界（宽松包络, 只拦截"数值合法但物理非法"的退化状态）:
    //   r ∈ [100, 5e5] km（地心距）, |v| ≤ 15 km/s
    // 阈值取宽松包络（与 Vallado SGP4-VER 验证数据集的不变量一致）:
    // 注意**不可**收紧到 10 km/s: 高偏心轨道（Molniya/HEO）近地点速度可达
    // 10.0~10.3 km/s, 自带 Vallado SGP4-VER 数据集里有 6 例近地点速度 >10 km/s
    //（其中 4 例被要求"窗口内传播必须成功"）, 收紧会误杀合法轨道。
    // NaN 比较恒假 → 落入 !(...) 分支, 与上方 finite 检查等效兜底
    const double rKm = std::sqrt(sv.position[0] * sv.position[0]
                               + sv.position[1] * sv.position[1]
                               + sv.position[2] * sv.position[2]);
    const double vKms = std::sqrt(sv.velocity[0] * sv.velocity[0]
                                + sv.velocity[1] * sv.velocity[1]
                                + sv.velocity[2] * sv.velocity[2]);
    if (!(rKm >= 100.0 && rKm <= 5.0e5) || !(vKms <= 15.0))
        return std::nullopt;

    EciState result;
    result.posKm  = sv.position;
    result.velKms = sv.velocity;
    return result;
}
```

---

## core/Propagator.h

```cpp
#ifndef CORE_PROPAGATOR_H
#define CORE_PROPAGATOR_H
// =============================================================================
// Propagator.h — perturb::Satellite 轻量封装
// 将双精度儒略日转换为 perturb::JulianDate，调用 SGP4/SDP4 传播
// 返回 EciState（TEME 系），失败时返回 nullopt
//
// 线程安全: 非线程安全。底层 SGP4 内核在每次传播时改写 satrec 的
// atime/xli/xni/error 等成员（深空积分状态随调用推进）——同一实例
// 必须由单线程独占，并行预报请每个任务/线程各自构造独立实例
// =============================================================================
#include "DataTypes.h"
#include <memory>
#include <optional>
#include <string>

namespace perturb { class Satellite; }

class Propagator {
public:
    Propagator();
    ~Propagator();

    // 禁止拷贝（持有动态分配的 perturb::Satellite）
    Propagator(const Propagator &) = delete;
    Propagator &operator=(const Propagator &) = delete;

    // 允许移动
    Propagator(Propagator &&other) noexcept;
    Propagator &operator=(Propagator &&other) noexcept;

    // 从 TLE 两行初始化（字符串会被修改，传入副本）
    bool initialize(const std::string &line1, const std::string &line2,
                    std::string &errorMsg,
                    GravityModel gravModel = GravityModel::WGS72);

    bool isInitialized() const { return m_initialized; }

    // 核心传播：双精度儒略日 → TEME 位置/速度
    // 成功返回 EciState，失败（衰减/异常）返回 nullopt
    std::optional<EciState> propagate(double jd);

    // 暴露历元儒略日
    double epochJD() const { return m_epochJD; }

    // 暴露卫星名称
    const std::string &satelliteName() const { return m_name; }
    void setSatelliteName(const std::string &name) { m_name = name; }

    int noradID() const { return m_noradID; }
    Sgp4Type modelType() const { return m_modelType; }

private:
    // 唯一持有 perturb::Satellite（类型安全，替代 void* 手动转换）
    std::unique_ptr<perturb::Satellite> m_satellite;
    bool  m_initialized = false;
    double m_epochJD = 0.0;
    std::string m_name;
    int    m_noradID = 0;
    Sgp4Type m_modelType = Sgp4Type::LEO_SGP4;
};

#endif // CORE_PROPAGATOR_H
```

---

## core/SunPosition.cpp

```cpp
// =============================================================================
// SunPosition.cpp — Meeus "Astronomical Algorithms" 第25章低精度太阳星历
// =============================================================================
#include "SunPosition.h"
#include "CoordTransform.h"
#include "Nutation.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
    constexpr double PI     = M_PI;
    constexpr double TWOPI  = 2.0 * PI;
    constexpr double DEG2RAD = PI / 180.0;
    constexpr double RAD2DEG = 180.0 / PI;
    constexpr double SEC_PER_DAY = 86400.0;

    inline double deg2rad(double d) { return d * DEG2RAD; }
    inline double rad2deg(double r) { return r * RAD2DEG; }

    // 正规化角度到 [0, 2π)
    double wrap2Pi(double ang) {
        ang = std::fmod(ang, TWOPI);
        if (ang < 0.0) ang += TWOPI;
        return ang;
    }
}

std::optional<SunPosition> SunPositionCalc::compute(const double jdUtc,
                                                    double obsLatDeg, double obsLonDeg)
{
    try {
        // 输入验证
        if (!std::isfinite(jdUtc) || !std::isfinite(obsLatDeg) || !std::isfinite(obsLonDeg)) {
            throw std::invalid_argument("无效的输入参数");
        }

        // UTC → TT（历表使用）：ΔT 秒级修正，远小于公式自身精度
        const double jdTT = jdUtc + CoordTransform::deltaTSeconds(jdUtc) / SEC_PER_DAY;
        if (!std::isfinite(jdTT)) {
            throw std::invalid_argument("TT时间计算结果无效");
        }

        const double obsLat = deg2rad(obsLatDeg);
        const double obsLon = deg2rad(obsLonDeg);
        SunPosition sp;

        // ---- Step 1: 儒略千年数（J2000.0 起算, TT 时标） ----
        // 注: T 以千年为单位(除数 365250), 而 Meeus 系数按世纪编制,
        // 后续多项式须以 (T×10) 折算——勿被注释误导改回世纪制
        double T = (jdTT - 2451545.0) / 365250.0;
        if (!std::isfinite(T)) {
            throw std::invalid_argument("儒略千年数计算结果无效");
        }

        // ---- Step 2: 太阳平黄经（度 → rad） ----
        double L0 = deg2rad(280.46646 + 36000.76983 * (T * 10.0)
                            + 0.0003032 * (T * 10.0) * (T * 10.0));
        L0 = wrap2Pi(L0);

        // ---- Step 3: 太阳平近点角 ----
        double M = deg2rad(357.52911 + 35999.05029 * (T * 10.0)
                           - 0.0001537 * (T * 10.0) * (T * 10.0));
        M = wrap2Pi(M);

        // ---- Step 4: 中心差（高精度6项，Meeus 25.9，精度 ~0.001°） ----
        double C = deg2rad((1.914602 - 0.004817 * (T * 10.0) - 0.000014 * (T * 10.0) * (T * 10.0)) * std::sin(M)
                         + (0.019993 - 0.000101 * (T * 10.0)) * std::sin(2.0 * M)
                         + 0.000289 * std::sin(3.0 * M)
                         + 0.000005 * std::sin(4.0 * M)
                         + 0.000003 * std::sin(5.0 * M)
                         + 0.000002 * std::sin(6.0 * M));

        // ---- Step 5: 太阳视黄经 λ ----
        // 几何黄经 λ_geo = L0 + C（平均轨道项）
        double lambda = wrap2Pi(L0 + C);

        // 光行差 −20.4898″/R（Meeus 25.9，R 为日地距离 AU，见 Step 8）
        // 章动 Δψ（Meeus 表 22.A 完整 63 项级数，精度 0.5″）
        const double distAU = 1.00014 - 0.01671 * std::cos(M)
                            - 0.00014 * std::cos(2.0 * M)
                            - 0.000003 * std::cos(3.0 * M);  // 高精度R³项
        if (!std::isfinite(distAU) || distAU <= 0.0) {
            throw std::invalid_argument("日地距离计算结果无效");
        }
        
        const double dPsi = Nutation::compute(jdTT).dPsi;
        // 视黄经 = 几何黄经 + 章动 Δψ − 光行差
        //
        // 注意（回归修复）: Meeus 25 章低精度式的经验式
        //   λ = Θ − 0.00569° − 0.00478·sinΩ
        // 中那两个常数项分别是**光行差**（0.00569° = 20.5″ ≈ 20.4898″）与
        // **章动的近似值**（0.00478° = 17.2″ = 主项 Δψ 振幅）。本实现已用
        // 63 项完整 Δψ 与 −20.4898″/R 的精确光行差，故二者**都不可再加**——
        // 同时保留会把章动与光行差各算两遍（合计 ~0.01° 量级的太阳位置偏差）。
        // FK5 修正（−0.09033″ ≈ 2.5e-5°）量级过小，此处省略
        lambda = wrap2Pi(lambda + dPsi - deg2rad(20.4898 / 3600.0 / distAU));

        // ---- Step 6: 真黄赤交角 ε = ε0 + Δε ----
        double eps = deg2rad(Nutation::trueObliquityDeg(jdTT));
        if (!std::isfinite(eps)) {
            throw std::invalid_argument("黄赤交角计算结果无效");
        }

        // ---- Step 7: 黄道 → 赤道 (RA, Dec) ----
        double sinLambda = std::sin(lambda);
        double cosLambda = std::cos(lambda);
        double sinEps    = std::sin(eps);
        double cosEps    = std::cos(eps);

        if (!std::isfinite(sinLambda) || !std::isfinite(cosLambda) || 
            !std::isfinite(sinEps) || !std::isfinite(cosEps)) {
            throw std::invalid_argument("三角函数计算结果无效");
        }

        double sinDec = sinEps * sinLambda;
        double cosDec = std::sqrt(std::max(0.0, 1.0 - sinDec * sinDec));  // Dec 在 ±23.5° 内，正值
        double dec    = std::atan2(sinDec, cosDec);

        double sinRA  = sinLambda * cosEps;  // 符号同 sinLambda
        double cosRA  = cosLambda;
        double ra     = std::atan2(sinRA, cosRA);
        ra = wrap2Pi(ra);

        sp.raDeg  = rad2deg(ra);
        sp.decDeg = rad2deg(dec);

        // ---- Step 8: 日地距离（AU） ----
        sp.distanceAU = distAU;

        // ---- Step 9: 太阳地心方向单位矢量（当日真赤道系, equinox-of-date——
        // 非J2000: 由视黄经+当日真黄赤交角构成; 与 TEME（近似当日真赤道系）
        // 卫星位置 + GMST 旋转配合在几何上自洽） ----
        sp.dirECI = {
            std::cos(dec) * std::cos(ra),
            std::cos(dec) * std::sin(ra),
            std::sin(dec)
        };

        // ---- Step 10: 赤道 → 地平（观测站） ----
        // 时角须配**视恒星时（GAST）**：上方 ra 已含章动（视赤经），若用 GMST
        // 会漏掉春分点方程 EE ≈ Δψ·cos ε（最大 ~15.8″ ≈ 0.0044°）。
        // EE 用标准一阶式 Δψ·cos(ε_true)（忽略 <0.002″ 的补充项）;
        // GMST 输入为 UT1 儒略日（UT1 ≈ UTC ≤ 0.9s），不加 ΔT
        const double gastRad = CoordTransform::gmstRad(jdUtc) + dPsi * std::cos(eps);
        if (!std::isfinite(gastRad)) return std::nullopt;  // GMST 失败 → 太阳位置无法计算
        double lha = gastRad + obsLon - ra;  // 地方时角
        lha = wrap2Pi(lha);

        double sinAlt = std::sin(obsLat) * std::sin(dec)
                      + std::cos(obsLat) * std::cos(dec) * std::cos(lha);
        // 钳制防 asin 越界（浮点舍入可致 |sinAlt| 略超 1）
        sinAlt = std::min(1.0, std::max(-1.0, sinAlt));
        double alt = std::asin(sinAlt);

        // 防除零: cos(alt)→0（太阳在天顶）与 cos(lat)→0（站在极点）时
        // 方位角无定义，返回 0（北），避免 0/0 → NaN
        const double cosAlt = std::max(1e-12, std::cos(alt));
        const double cosLat = std::max(1e-12, std::cos(obsLat));
        double sinAz = -std::sin(lha) * std::cos(dec) / cosAlt;
        double cosAz = (std::sin(dec) - std::sin(obsLat) * std::sin(alt))
                      / (cosLat * cosAlt);
        double az = std::atan2(sinAz, cosAz);
        az = wrap2Pi(az);

        // 方位角在两类退化位置下无物理意义且数值会任意跳变:
        // 1) 太阳在天顶/天底 (|alt|≈±90°)  2) 观测站在极点 (|lat|≈±90°)
        // → 稳定钳制为 0°（北），避免极区时段内方位角无意义的抖动
        constexpr double kPolarThresholdDeg = 89.9;
        const double absAltDeg  = std::abs(rad2deg(alt));
        const double absLatDeg  = std::abs(rad2deg(obsLat));
        if (absAltDeg >= kPolarThresholdDeg || absLatDeg >= kPolarThresholdDeg) az = 0.0;

        sp.azDeg  = rad2deg(az);     // 北=0°, 顺时针
        sp.altDeg = rad2deg(alt);    // 地平=0°

        // 最终结果验证
        if (!std::isfinite(sp.raDeg) || !std::isfinite(sp.decDeg) || 
            !std::isfinite(sp.azDeg) || !std::isfinite(sp.altDeg) ||
            !std::isfinite(sp.distanceAU)) {
            throw std::invalid_argument("太阳位置计算结果无效");
        }

        return sp;
    } catch (const std::exception &e) {
        // 计算失败（输入非法/中间值非有限）: 不吞错返回默认值——
        // 调用侧收到 nullopt 可区分"真失败"与"合法结果"，避免把
        // RA=0/Dec=0 的伪结果当真实数据使用
        return std::nullopt;
    }
}
```

---

## core/SunPosition.h

```cpp
#ifndef CORE_SUNPOSITION_H
#define CORE_SUNPOSITION_H
// =============================================================================
// SunPosition.h — Meeus 低精度太阳星历
// 给定 JD 和观测站经纬度 → 输出太阳方位角/高度角/RA/Dec/日地距离
// 精度约 0.01°（1950–2050 年范围），足够过境可见性判定
// =============================================================================
#include "DataTypes.h"
#include <optional>

class SunPositionCalc {
public:
    // 输入/中间值非法（NaN/Inf/距离非正等）→ 返回 nullopt，调用侧可区分"真失败"
    // 与"合法结果"，而非吞错返回默认值
    static std::optional<SunPosition> compute(const double jd,
                                              double obsLatDeg, double obsLonDeg);
};

#endif // CORE_SUNPOSITION_H
```

---

## core/TleParser.cpp

```cpp
// =============================================================================
// TleParser.cpp — TLE 两行根数解析实现
// =============================================================================
#include "TleParser.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

// 整段消费解析（拒绝 "nan"/"inf"/部分数字如 "12x3" → 12 的静默截断）
// 前后空白允许（TLE 数字字段固定宽度带前导空格）
bool parseStrict(const std::string &raw, double &out)
{
    std::string s = raw;
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    const size_t e = s.find_last_not_of(" \t\r\n");
    s = s.substr(b, e - b + 1);

    auto res = std::from_chars(s.data(), s.data() + s.size(), out);
    return (res.ec == std::errc() && res.ptr == s.data() + s.size()
            && std::isfinite(out));
}

bool parseStrict(const std::string &raw, int &out)
{
    std::string s = raw;
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    const size_t e = s.find_last_not_of(" \t\r\n");
    s = s.substr(b, e - b + 1);
    if (s.empty()) return false;

    // from_chars(C++17) 整数版不支持 '+' 符号（TLE B* 指数常见 "+0"）
    if (s[0] == '+')
        s.erase(0, 1);
    if (s.empty()) return false;

    auto res = std::from_chars(s.data(), s.data() + s.size(), out);
    return (res.ec == std::errc() && res.ptr == s.data() + s.size());
}

} // namespace

// 从三行格式解析 TLE
std::optional<TleData> TleParser::parse(
    const std::string &line0,
    const std::string &line1,
    const std::string &line2,
    ParseError *errOut,
    bool strictChecksum)
{
    auto result = parse(line1, line2, errOut, strictChecksum);
    if (result) {
        // 行0 作为卫星名称（去除首尾空白）
        std::string name = line0;
        size_t first = name.find_first_not_of(" \t\r\n");
        size_t last  = name.find_last_not_of(" \t\r\n");
        if (first != std::string::npos)
            result->name = name.substr(first, last - first + 1);
    }
    return result;
}

// 从两行格式解析 TLE
std::optional<TleData> TleParser::parse(
    const std::string &line1,
    const std::string &line2,
    ParseError *errOut,
    bool strictChecksum)
{
    TleData tle;
    ParseError err;

    // ---- 步骤 0: 规范化非标准 TLE ----
    // 仅在 Propagator::initialize() 中调用，parse() 不做规范化以保留原始校验语义
    auto stripCr = [](const std::string &s) {
        if (!s.empty() && s.back() == '\r')
            return s.substr(0, s.size() - 1);
        return s;
    };
    const std::string l1in = stripCr(line1);
    const std::string l2in = stripCr(line2);

    // ---- 步骤 1: 长度校验 ----
    if (l1in.length() < 69 || l2in.length() < 69) {
        err.code = ParseError::BAD_LENGTH;
        err.detail = "TLE line too short; minimum 69 characters per line";
        if (errOut) *errOut = err;
        return std::nullopt;
    }

    // ---- 步骤 2: 行号校验 ----
    if (l1in[0] != '1') {
        err.code = ParseError::BAD_LINE_NUMBER;
        err.detail = "Line 1 must start with '1'";
        if (errOut) *errOut = err;
        return std::nullopt;
    }
    if (l2in[0] != '2') {
        err.code = ParseError::BAD_LINE_NUMBER;
        err.detail = "Line 2 must start with '2'";
        if (errOut) *errOut = err;
        return std::nullopt;
    }

    // ---- 步骤 3: NORAD 编号（行1 列3-7） ----
    // 宽容处理: 部分 TLE 使用字母前缀（如 "A0370" 表示业余无线电编号），
    // 与 STK 对齐: 剥离前导字母后解析数字部分
    {
        int nid = 0;
        std::string noradStr = l1in.substr(2, 5);
        // 剥离前导非数字字符（字母前缀）
        size_t firstDigit = 0;
        while (firstDigit < noradStr.size() && !std::isdigit(static_cast<unsigned char>(noradStr[firstDigit])))
            ++firstDigit;
        if (firstDigit > 0)
            noradStr = noradStr.substr(firstDigit);
        if (!parseStrict(noradStr, nid)) {
            err.code = ParseError::BAD_NORAD;
            err.detail = "Invalid NORAD ID";
            if (errOut) *errOut = err;
            return std::nullopt;
        }
        tle.noradID = nid;
    }

    // 默认名称
    tle.name = "NORAD " + std::to_string(tle.noradID);

    // ---- 步骤 4: 历元（行1 列19-32） ----
    // 格式: YYDDD.DDDDDDDD（2位年 + 3位日 + 8位小数）
    {
        const std::string epochStr = l1in.substr(18, 14);
        int epochYr2 = 0;
        double epochDayVal = 0.0;
        
        if (!parseStrict(epochStr.substr(0, 2), epochYr2)) {
            err.code = ParseError::BAD_EPOCH;
            err.detail = "Invalid epoch year format: '" + epochStr.substr(0, 2) + 
                        "'. Expected 2-digit year (00-99).";
            if (errOut) *errOut = err;
            return std::nullopt;
        }
        
        if (!parseStrict(epochStr.substr(2), epochDayVal)) {
            err.code = ParseError::BAD_EPOCH;
            err.detail = "Invalid epoch day format: '" + epochStr.substr(2) + 
                        "'. Expected decimal day-of-year (1.0-366.0).";
            if (errOut) *errOut = err;
            return std::nullopt;
        }

        // 2位年份 → 4位年份: ≤56 → 2000+y, >56 → 1900+y
        if (epochYr2 <= 56)
            tle.epochYear = 2000 + epochYr2;
        else
            tle.epochYear = 1900 + epochYr2;

        // 年内天数上限按闰年判定（TLE 日计数 1 起, 上限 365/366）
        const int  y     = tle.epochYear;
        const bool leap  = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        const double dayMax = leap ? 367.0 : 366.0;   // 日计数 1..365/366 → < 366/367
        if (epochDayVal < 1.0 || epochDayVal >= dayMax) {
            err.code = ParseError::BAD_EPOCH;
            err.detail = "Invalid epoch day-of-year: " + std::to_string(epochDayVal) + 
                        ". Year " + std::to_string(y) + " is " + std::string(leap ? "leap" : "non-leap") + 
                        ", valid range: 1.0-" + std::to_string(dayMax - 1.0) + ".";
            if (errOut) *errOut = err;
            return std::nullopt;
        }

        tle.epochDay = epochDayVal;
    }

    // ---- 步骤 5: B* 阻力项（行1 列54-61） ----
    try {
        std::string bstarStr = l1in.substr(53, 8);
        // 格式: "-12345-3" → -0.12345e-3
        tle.bstar = parseScientific(bstarStr);
    } catch (...) {
        err.code = ParseError::BAD_BS;
        err.detail = "Invalid B* format";
        if (errOut) *errOut = err;
        return std::nullopt;
    }

    // ---- 步骤 6: 行2 各轨道根数 ----
    {
        auto fail = [&]() {
            err.code = ParseError::BAD_ELEMENTS;
            err.detail = "Invalid orbital element format";
            if (errOut) *errOut = err;
            return std::nullopt;
        };

        double v = 0.0;
        int    rv = 0;

        // 倾角（列9-16，度）
        if (!parseStrict(l2in.substr(8, 8), v)) return fail();
        tle.inclinationDeg = v;

        // 升交点赤经（列18-25，度）
        if (!parseStrict(l2in.substr(17, 8), v)) return fail();
        tle.raanDeg = v;

        // 偏心率（列27-33，省略前导"0."）
        if (!parseStrict("0." + l2in.substr(26, 7), v)) return fail();
        tle.eccentricity = v;

        // 近地点辐角（列35-42，度）
        if (!parseStrict(l2in.substr(34, 8), v)) return fail();
        tle.argPerigeeDeg = v;

        // 平近点角（列44-51，度）
        if (!parseStrict(l2in.substr(43, 8), v)) return fail();
        tle.meanAnomalyDeg = v;

        // 平运动（列53-63，rev/day）
        if (!parseStrict(l2in.substr(52, 11), v)) return fail();
        tle.meanMotionRevDay = v;

        // 圈号（列64-68）
        if (!parseStrict(l2in.substr(63, 5), rv)) return fail();
        tle.revNumber = rv;
    }

    // ---- 步骤 7: 范围检查 ----
    if (tle.inclinationDeg < 0.0 || tle.inclinationDeg > 180.0) {
        err.code = ParseError::BAD_ELEMENTS;
        err.detail = "Inclination out of range [0, 180]";
        if (errOut) *errOut = err;
        return std::nullopt;
    }
    if (tle.eccentricity < 0.0 || tle.eccentricity >= 1.0) {
        err.code = ParseError::BAD_ELEMENTS;
        err.detail = "Eccentricity out of range [0, 1)";
        if (errOut) *errOut = err;
        return std::nullopt;
    }
    if (tle.meanMotionRevDay <= 0.0) {
        err.code = ParseError::BAD_ELEMENTS;
        err.detail = "Mean motion must be positive";
        if (errOut) *errOut = err;
        return std::nullopt;
    }
    // 角度量 [0,360) 界: 定宽字段下越界需越界拼写（罕见）, 但缺失检查时
    // 错误会在更下层以 INVALID_TLE 暴露, 归类路径与 BAD_ELEMENTS 不一致
    if (tle.raanDeg < 0.0 || tle.raanDeg >= 360.0
        || tle.argPerigeeDeg < 0.0 || tle.argPerigeeDeg >= 360.0
        || tle.meanAnomalyDeg < 0.0 || tle.meanAnomalyDeg >= 360.0) {
        err.code = ParseError::BAD_ELEMENTS;
        err.detail = "RAAN/argPerigee/meanAnomaly out of range [0, 360)";
        if (errOut) *errOut = err;
        return std::nullopt;
    }

    // ---- 步骤 8: 模型分类 ----
    tle.ephemerisType = classifyModel(tle.meanMotionRevDay);

    // ---- 步骤 9: 校验和（可选/可强制） ----
    tle.checksumValid = verifyChecksum(l1in) && verifyChecksum(l2in);
    if (!tle.checksumValid) {
        err.code = ParseError::CHECKSUM_FAILED;
        if (strictChecksum) {
            // 强制模式: 阻断解析——损坏/篡改的根数不得进入传播计算
            err.detail = "Checksum mismatch (strict mode: rejected)";
            if (errOut) *errOut = err;
            return std::nullopt;
        }
        err.detail = "Checksum mismatch (proceeding anyway)";
        // 宽松模式: 不中断，仅记录（调用方据 checksumValid 提示用户）
    }

    // ---- 步骤 10: 元素集编号 ----
    {
        int n = 0;
        if (!parseStrict(l1in.substr(64, 4), n)) n = 0;   // 非致命，容错
        tle.elsetNum = n;
    }

    if (errOut) *errOut = err;
    return tle;
}

// 模型分类
// 与 Vallado SGP4/perturb 判据一致：轨道周期 ≥ 225 分钟（平运动 ≤ 6.4 rev/day）
// → 深空模型 SDP4；否则近地 SGP4
// 深空带内再细分: 周期 ≥ 1400 min（平运动 ≤ 1440/1400 ≈ 1.0286 rev/day）
// → 同步带/超同步 GEO（与 Propagator 的 no_unkozai 分类同阈值、同语义）
Sgp4Type TleParser::classifyModel(double meanMotionRevDay)
{
    if (meanMotionRevDay > 6.4)                        // 周期 < 225 分钟 → 近地
        return Sgp4Type::LEO_SGP4;
    else if (meanMotionRevDay >= 1440.0 / 1400.0)      // 周期 225 ~ 1400 分钟 → 深空/中轨
        return Sgp4Type::MEO_SDP4;
    else                                               // 周期 ≥ 1400 分钟 → 同步带/超同步
        return Sgp4Type::GEO_SDP4_DS;
}

// 科学记数法解析（如 " 12345-3" → 0.12345 × 10⁻³）
// 严格整段消费，指数超界（TLE 规范为 [-9, +9]）或非法字符 → 抛异常
double TleParser::parseScientific(const std::string &s)
{
    try {
        // 前6位是尾数（含前导空格或符号），后2位是指数
        // 格式: "Sm.mmmEE" 其中 S=符号/空格, m=尾数, E=指数符号, E=指数值
        if (s.length() < 8)
            throw std::invalid_argument("B* field too short: input length " + std::to_string(s.length()) + " < 8");

        std::string mantissaStr = s.substr(0, 6);
        std::string expStr = s.substr(6, 2);

        // 处理尾数中的空格
        for (char &c : mantissaStr) {
            if (c == ' ') c = '0';
        }
        // 确保开头有符号或数字
        if (mantissaStr[0] == ' ') mantissaStr[0] = '0';

        double mantissa = 0.0;
        int exponent = 0;
        
        if (!parseStrict(mantissaStr, mantissa)) {
            throw std::invalid_argument("B* mantissa parsing failed: '" + mantissaStr + "' -> " + std::to_string(mantissa));
        }
        
        if (!parseStrict(expStr, exponent)) {
            throw std::invalid_argument("B* exponent parsing failed: '" + expStr + "' -> " + std::to_string(exponent));
        }
        
        if (exponent < -9 || exponent > 9) {
            throw std::invalid_argument("B* exponent " + std::to_string(exponent) + " out of range [-9, +9]");
        }

        double result = mantissa * 1e-5 * std::pow(10.0, exponent);  // TLE格式: 隐含前导小数点
        
        if (!std::isfinite(result)) {
            throw std::invalid_argument("B* result is not finite: " + std::to_string(result));
        }
        
        return result;
    } catch (const std::exception &e) {
        throw std::invalid_argument("B* parsing failed for input '" + s + "': " + e.what());
    }
}

// 校验和验证（模 10）
bool TleParser::verifyChecksum(const std::string &line)
{
    if (line.length() < 69) return false;

    // 解析预期的校验和（第69列，即索引68）
    char checksumChar = line[68];
    int expected = (checksumChar >= '0' && checksumChar <= '9') ? (checksumChar - '0') : -1;
    if (expected < 0) return false;

    // 计算校验和（前68列）
    int sum = 0;
    for (size_t i = 0; i < 68; ++i) {
        char c = line[i];
        if (c >= '0' && c <= '9') {
            sum += (c - '0');
        } else if (c == '-') {
            sum += 1;
        }
        // 其他字符（字母、空格、小数点、'+'）贡献 0
    }
    return (sum % 10) == expected;
}

// ============================================================================
// normalizeTleLines — 将非标准 TLE 规范化为 69 字符列对齐格式
// 与 STK 宽容策略对齐: 处理额外空格、字母 NORAD 前缀、缺少小数点等
// ============================================================================
std::pair<std::string, std::string>
TleParser::normalizeTleLines(const std::string &line1, const std::string &line2)
{
    // 快速路径: 标准或扩展 TLE 无需任何处理，直接返回
    // - 标准: 69 字符，行首 '1'/'2'
    // - 扩展: >69 字符（如 SGP4-VER 格式含额外字段），截取前 69 字符
    // - 太短: <15 字符无法构成有效 TLE，返回让下游校验报错
    // 注意: 先 stripCr 再判断长度，避免 CRLF (69+1=70) 被截断后含 \r
    auto stripCr = [](const std::string &s) {
        if (!s.empty() && s.back() == '\r')
            return s.substr(0, s.size() - 1);
        return s;
    };
    {
        std::string a = stripCr(line1);
        std::string b = stripCr(line2);
        // 扩展格式: >69 字符，截取前 69（保留标准列对齐，忽略尾部扩展字段）
        if (a.size() > 69) a = a.substr(0, 69);
        if (b.size() > 69) b = b.substr(0, 69);
        // 标准 69 字符: 直接返回
        if (a.size() == 69 && b.size() == 69)
            return {a, b};
        // 太短: 不做规范化，让下游长度校验报 BAD_LENGTH
        if (a.size() < 15 || b.size() < 15)
            return {a, b};
    }

    // 辅助: 去除首尾空白
    auto trim = [](const std::string &s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        size_t e = s.find_last_not_of(" \t\r\n");
        return (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
    };
    // 辅助: 压缩连续空格为单个
    auto collapse = [](const std::string &s) {
        std::string out;
        bool lastSpace = false;
        for (char c : s) {
            if (c == ' ' || c == '\t') {
                if (!lastSpace) out += ' ';
                lastSpace = true;
            } else {
                out += c;
                lastSpace = false;
            }
        }
        return out;
    };
    // 辅助: 剥离前缀非数字字符
    auto stripAlphaPrefix = [](const std::string &s) {
        size_t i = 0;
        while (i < s.size() && !std::isdigit(static_cast<unsigned char>(s[i])))
            ++i;
        return (i > 0) ? s.substr(i) : s;
    };
    // 辅助: 右填充到指定宽度
    auto padRight = [](const std::string &s, size_t width) {
        return (s.size() >= width) ? s.substr(0, width) : s + std::string(width - s.size(), ' ');
    };

    const std::string raw1 = trim(line1);
    const std::string raw2 = trim(line2);

    // ---- 规范化行1 ----
    {
        const std::string c = collapse(raw1);
        // 按空格拆分为 token
        std::vector<std::string> tokens;
        {std::istringstream iss(c); std::string t; while (iss >> t) tokens.push_back(t);}

        // token[0] = "1", token[1] = NORAD+分类 (如 "A0370U"), token[2] = 国际编号 ...
        std::string norad = (tokens.size() > 1) ? stripAlphaPrefix(tokens[1]) : "00000";
        // 分类: 从原始 token 中提取第一个非数字字符
        char classification = 'U';
        if (tokens.size() > 1) {
            for (char ch : tokens[1]) {
                if (!std::isdigit(static_cast<unsigned char>(ch))) { classification = ch; break; }
            }
        }
        std::string intl = (tokens.size() > 2) ? tokens[2] : "000000000";
        std::string epochYr = (tokens.size() > 3) ? tokens[3].substr(0, std::min<size_t>(2, tokens[3].size())) : "00";
        std::string epochDay = (tokens.size() > 3) ? tokens[3].substr(2) : "00000.00000000";
        std::string nDot = (tokens.size() > 4) ? tokens[4] : "00000-0";
        std::string nDDot = (tokens.size() > 5) ? tokens[5] : "00000-0";
        std::string bstar = (tokens.size() > 6) ? tokens[6] : "00000-0";
        std::string ephType = (tokens.size() > 7) ? tokens[7] : "0";
        std::string elSet = (tokens.size() > 8) ? tokens[8] : "0";
        std::string checksum = (tokens.size() > 9) ? tokens[9] : "0";

        // 重新组装为标准 69 字符列对齐行
        // 格式: '1' S NNNNN S IIIIIIIII YYDDD.DDDDDDDDDD +NNNNNN+NN S NNNNEE S N.NNNNNNN S T NNNNN C
        //        0 1 2-6   7 8 9-17     18-19 20-31       32-42      43-50 51 52-59   60 61 62-66 67
        std::string result = "1";
        result += " ";                          // col 1: 分隔空格
        result += padRight(norad, 5);           // cols 2-6: NORAD 编号
        result += classification;               // col 7: 密级
        result += " ";                          // col 8: 分隔空格
        result += padRight(intl, 9);            // cols 9-17: 国际编号(9字符)
        result += padRight(epochYr, 2);         // cols 18-19: 历元年
        result += padRight(epochDay, 12);       // cols 20-31: 历元日
        result += padRight(nDot, 11);           // cols 32-42: 一阶导数
        // nDDot: 格式为 "mmmmmmEE" (6位尾数+2位指数), 需拆分
        {
            std::string ndd = nDDot;
            // 去掉可能的小数点
            size_t dp = ndd.find('.');
            if (dp != std::string::npos) ndd.erase(dp, 1);
            // 提取尾数和指数
            std::string mant, exp;
            if (ndd.size() >= 3) {
                mant = ndd.substr(0, ndd.size() - 2);
                exp = ndd.substr(ndd.size() - 2);
            } else {
                mant = ndd;
                exp = "00";
            }
            // 标准: 前导空格 + 5位尾数 + 2位指数 = 8字符
            if (mant.size() > 5) mant = mant.substr(0, 5);
            else if (mant.size() < 5) mant = std::string(5 - mant.size(), '0') + mant;
            // 格式: " mmmmEE"（前导空格 + 隐含小数点不打印）
            std::string field = " " + mant + exp;
            result += padRight(field, 8);           // cols 43-50
        }
        // B*: 格式同 nDDot
        result += " ";                          // col 51: 分隔空格
        {
            std::string bs = bstar;
            size_t dp = bs.find('.');
            if (dp != std::string::npos) bs.erase(dp, 1);
            std::string mant, exp;
            if (bs.size() >= 3) {
                mant = bs.substr(0, bs.size() - 2);
                exp = bs.substr(bs.size() - 2);
            } else {
                mant = bs;
                exp = "00";
            }
            if (mant.size() > 5) mant = mant.substr(0, 5);
            else if (mant.size() < 5) mant = std::string(5 - mant.size(), '0') + mant;
            std::string field = mant.substr(0, 1) + "." + mant.substr(1) + exp;
            result += padRight(field, 8);           // cols 52-59
        }
        result += " ";                              // col 60: 分隔空格
        result += padRight(ephType, 1);             // col 61: 星历类型
        result += padRight(elSet, 5);               // cols 62-66: 集号
        result += padRight(checksum, 1);            // col 67: 校验和

        // 确保恰好 69 字符
        if (result.size() > 69) result = result.substr(0, 69);
        else if (result.size() < 69) result += std::string(69 - result.size(), ' ');

        // ---- 规范化行2 ----
        const std::string c2 = collapse(raw2);
        std::vector<std::string> t2;
        {std::istringstream iss(c2); std::string t; while (iss >> t) t2.push_back(t);}

        std::string norad2 = (t2.size() > 1) ? stripAlphaPrefix(t2[1]) : norad;
        std::string incStr = (t2.size() > 2) ? t2[2] : "00.0000";
        std::string raanStr = (t2.size() > 3) ? t2[3] : "000.0000";
        std::string eccStr = (t2.size() > 4) ? t2[4] : "0000000";
        std::string argpStr = (t2.size() > 5) ? t2[5] : "000.0000";
        std::string maStr = (t2.size() > 6) ? t2[6] : "000.0000";
        std::string mmStr = (t2.size() > 7) ? t2[7] : "00.00000000";
        std::string revStr = (t2.size() > 8) ? t2[8] : "0000";

        std::string result2 = "2";
        result2 += padRight(norad2, 5);
        result2 += padRight(incStr, 9);
        result2 += padRight(raanStr, 9);
        // 偏心率: 去掉小数点后填充到 7 或 8 位
        {
            std::string ecc = eccStr;
            size_t dp = ecc.find('.');
            if (dp != std::string::npos) ecc.erase(dp, 1);
            // 决定使用 7 还是 8 位 (取决于均运动字段宽度)
            // 默认 8 位 (列25-32), 如果 mmStr > 10字符则用 7 位 (列25-31)
            bool mm11 = (mmStr.size() > 10);
            size_t eccWidth = mm11 ? 7 : 8;
            ecc = padRight(ecc, eccWidth);
            result2 += ecc;
        }
        result2 += padRight(argpStr, 9);
        result2 += padRight(maStr, 9);
        // 均运动: 10 或 11 字符
        {
            std::string mm = mmStr;
            size_t dp = mm.find('.');
            if (dp != std::string::npos) mm.erase(dp, 1);
            if (mm.size() > 11) mm = mm.substr(0, 11);
            result2 += padRight(mm, mm.size() <= 10 ? 10 : 11);
        }
        result2 += padRight(revStr, 5);

        // 确保恰好 69 字符
        if (result2.size() > 69) result2 = result2.substr(0, 69);
        else if (result2.size() < 69) result2 += std::string(69 - result2.size(), ' ');

        return {result, result2};
    }
}
```

---

## core/TleParser.h

```cpp
#ifndef CORE_TLEPARSER_H
#define CORE_TLEPARSER_H
// =============================================================================
// TleParser.h — TLE 两行根数解析器
// 按 CCSDS 标准解析 TLE，含校验和、范围检查和模型分类
// =============================================================================
#include "DataTypes.h"
#include <string>
#include <optional>

// TLE 标准行长度（CCSDS 两行根数格式）
constexpr int kTleLineLength = 69;

class TleParser {
public:
    struct ParseError {
        enum Code {
            OK = 0,
            BAD_LENGTH,
            BAD_LINE_NUMBER,
            CHECKSUM_FAILED,
            BAD_NORAD,
            BAD_EPOCH,
            BAD_ELEMENTS,
            BAD_BS,
            DECAYED   // 保留枚举: TleParser::parse 当前不产生（再入由
                      // Propagator/perturb 在初始化阶段报 DECAYED 错误码）
        };
        Code code = OK;
        std::string detail;
    };

    // 从三行解析（行0=名称，行1=TLE第一行，行2=TLE第二行）
    // strictChecksum=false（默认，宽松模式）: 校验和失败仍返回数据,
    //   仅在 errOut 里置 CHECKSUM_FAILED + checksumValid=false（调用方提示用）
    // strictChecksum=true（强制模式）: 校验和失败返回 nullopt（阻断后续计算）
    //
    // 调用约定（重要）: **返回值优先于 errOut**——宽松模式下 errOut 可能是
    // CHECKSUM_FAILED 而返回值仍然有效（非致命告警）。判定成败只看返回值,
    // errOut 仅用于取错误/告警细节, 切勿用 err.code != OK 作为失败判据。
    static std::optional<TleData>
        parse(const std::string &line0,
              const std::string &line1,
              const std::string &line2,
              ParseError *errOut = nullptr,
              bool strictChecksum = false);

    // 从两行解析（无名称行，自动用 NORAD ID 作为名称）
    static std::optional<TleData>
        parse(const std::string &line1,
              const std::string &line2,
              ParseError *errOut = nullptr,
              bool strictChecksum = false);

    // 根据平运动判定轨道模型
    static Sgp4Type classifyModel(double meanMotionRevDay);

    // 将科学记数法字符串（如 " 12345-3" → 0.12345e-3）转为 double
    static double parseScientific(const std::string &s);

    // 验证校验和（模 10）
    static bool verifyChecksum(const std::string &line);

    // 将非标准 TLE 行规范化为标准 69 字符列对齐格式
    // 处理: 额外空格、字母NORAD前缀、缺少小数点等非标准情况
    static std::pair<std::string, std::string>
        normalizeTleLines(const std::string &line1, const std::string &line2);
};

#endif // CORE_TLEPARSER_H
```

---

## core/VisualMagnitude.cpp

```cpp
// =============================================================================
// VisualMagnitude.cpp — 星等估算实现
// =============================================================================
#include "VisualMagnitude.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

// 常见亮卫星固有星等（1000 km、满相位视角）——近似值，参考 qsmag (Quicksat)
// 与 McCants 数据库；默认(未知)卫星回退值由调用方提供
struct StdMagEntry {
    const char *frag;   // 名称子串（卫星名通常为大写）
    double      mag;    // 固有星等（越小越亮）
};

const StdMagEntry kCatalog[] = {
    // ---- 空间站 ----
    { "ISS",           -1.8 },   // 国际空间站
    { "TIANGONG",      -0.9 },   // 天宫空间站
    { "CSS",           -0.9 },   // 天宫空间站（css 名称变体）
    { "MIR",            0.5 },   // 和平号（已退役）

    // ---- 大型科学卫星 ----
    { "HUBBLE",         1.4 },   // 哈勃太空望远镜
    { "ENVISAT",        3.9 },   // 环境卫星（大型）
    { "TERRA",          3.9 },   // EOS AM-1
    { "AQUA",           4.4 },   // EOS PM-1
    { "SUOMI",          4.6 },   // Suomi NPP
    { "JASON",          4.5 },   // Jason 系列测高卫星
    { "ALOS",           4.8 },   // 日本先进对地观测
    { "RADARSAT",       5.0 },   // 加拿大雷达卫星
    { "TANDEM",         5.0 },   // TanDEM-X (DLR)
    { "SAOCOM",         5.0 },   // 阿根廷 L 波段 SAR

    // ---- 气象卫星 ----
    { "NOAA-15",        4.8 },
    { "NOAA-18",        4.8 },
    { "NOAA-19",        4.8 },
    { "METOP",          4.0 },   // EUMETSAT 极轨
    { "METEOSAT",       6.5 },   // EUMETSAT 静止
    { "GOES",           6.5 },   // 美国静止气象
    { "FY-2",           6.5 },   // 风云二号静止
    { "FY-3",           4.8 },   // 风云三号极轨
    { "FY-4",           6.0 },   // 风云四号静止
    { "HIMAWARI",       6.0 },   // 日本向日葵静止
    { "INSAT",          6.5 },   // 印度静止气象
    { "MTSAT",          6.5 },   // 日本多功能运输卫星

    // ---- 陆地观测 ----
    { "LANDSAT",        5.5 },
    { "SENTINEL",       5.5 },
    { "COPERNICUS",     5.5 },   // Sentinel 别名
    { "WORLDVIEW",      5.0 },   // DigitalGlobe 商业
    { "GeoEye",         5.0 },
    { "PLEIADES",       5.0 },   // Airbus 军民两用
    { "RESURS",         5.5 },   // 俄罗斯资源卫星
    { "KOMPSAT",        5.0 },   // 韩国阿里郎
    { "CARTOSAT",       5.5 },   // 印度测绘
    { "RISAT",          5.0 },   // 印度雷达卫星
    { "SPOT",           5.5 },   // 法国对地观测
    { "EROS",           5.0 },   // 以色列高分

    // ---- 导航卫星 ----
    { "GPS",            5.0 },
    { "BEIDOU",         5.5 },
    { "GLONASS",        5.6 },
    { "GALILEO",        5.8 },
    { "QZSS",           5.5 },   // 日本准天顶

    // ---- 通信/宽带星座 ----
    { "STARLINK",       5.0 },
    { "ONEWEB",         5.8 },
    { "GLOBSTAR",       5.8 },   // Globalstar
    { "IRIDIUM",        5.6 },
    { "ORBCOMM",        6.0 },
    { "LEO",            5.8 },   // 泛 LEO 星座

    // ---- 激光测距/科学小卫星 ----
    { "LAGEOS",         6.0 },   // 激光测距
    { "ETALON",         6.5 },   // 激光测距
    { "STELLA",         6.0 },
    { "AJISAI",         6.5 },   // 日本实验
    { "HEAO",           6.5 },
    { "COSMOS",         5.5 },   // 俄罗斯系列

    // ---- 军事/侦察（公开编目） ----
    { "NOSS",           4.5 },   // 美国海洋监视
    { "KEYHOLE",        5.5 },   // KH 侦察系列
    { "LACROSSE",       5.0 },   // 雷达侦察
    { "MISTY",          5.5 },   // 隐身侦察
};

} // namespace

double VisualMagnitudeCalc::standardMagnitude(const std::string &satName, double fallback)
{
    if (satName.empty()) return fallback;
    // 大小写不敏感匹配（TLE 名称可能是 "starlink 1234" 等混写变体）
    std::string upper = satName;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    for (const auto &e : kCatalog) {
        size_t pos = upper.find(e.frag);
        while (pos != std::string::npos) {
            // 词边界匹配（两端）: 前缀不能是字母/数字, 后缀同样要求
            // 非字母数字——避免 "XGPS"/"LOGOES" 前缀误命中,
            // 也避免 "STARLINKERS-1" 这类后缀误命中
            bool atWordStart =
                (pos == 0) ||
                !std::isalnum(static_cast<unsigned char>(upper[pos - 1]));
            size_t end = pos + std::strlen(e.frag);
            bool atWordEnd =
                (end == upper.length()) ||
                !std::isalnum(static_cast<unsigned char>(upper[end]));
            if (atWordStart && atWordEnd) return e.mag;
            pos = upper.find(e.frag, pos + 1);
        }
    }
    return fallback;
}

double VisualMagnitudeCalc::phaseAngleRad(const std::array<double, 3> &satEcefKm,
                                    const std::array<double, 3> &sunEcefKm,
                                    const std::array<double, 3> &obsEcefKm)
{
    // 从卫星指向太阳 / 指向观测者的单位矢量
    double vSun[3] = { sunEcefKm[0] - satEcefKm[0],
                       sunEcefKm[1] - satEcefKm[1],
                       sunEcefKm[2] - satEcefKm[2] };
    double vObs[3] = { obsEcefKm[0] - satEcefKm[0],
                       obsEcefKm[1] - satEcefKm[1],
                       obsEcefKm[2] - satEcefKm[2] };
    double nSun = std::sqrt(vSun[0]*vSun[0] + vSun[1]*vSun[1] + vSun[2]*vSun[2]);
    double nObs = std::sqrt(vObs[0]*vObs[0] + vObs[1]*vObs[1] + vObs[2]*vObs[2]);
    if (!std::isfinite(nSun) || !std::isfinite(nObs) || nSun <= 0.0 || nObs <= 0.0)
        return M_PI;   // 退化/NaN 几何: 按最暗处理
    double dot = (vSun[0]*vObs[0] + vSun[1]*vObs[1] + vSun[2]*vObs[2]) / (nSun * nObs);
    if (dot >  1.0) dot =  1.0;
    if (dot < -1.0) dot = -1.0;
    return std::acos(dot);
}

double VisualMagnitudeCalc::visualMagnitude(double stdMag, double rangeKm, double phaseRad)
{
    // Vallerie 1963: m = m₀ − 15 + 5·log10(R) − 2.5·log10(f(B))
    // f(B) = (sin B + (π − B)·cos B)/π: B=0 → 1（满相位），B=π → 0（背日）
    double B = phaseRad;
    if (B < 0.0) B = 0.0;
    if (B > M_PI) B = M_PI;
    double f = (std::sin(B) + (M_PI - B) * std::cos(B)) / M_PI;
    if (f < 1e-6) f = 1e-6;   // 防 log10(0) → −∞（星等 +∞）
    return stdMag - 15.0 + 5.0 * std::log10(rangeKm) - 2.5 * std::log10(f);
}
```

---

## core/VisualMagnitude.h

```cpp
#ifndef CORE_VISUALMAGNITUDE_H
#define CORE_VISUALMAGNITUDE_H
// =============================================================================
// VisualMagnitude.h — 卫星视星等估算（Vallerie 1963 相位模型）
// 公式: m = m₀ − 15 + 5·log10(R) − 2.5·log10(f(B))
//   m₀ = 固有星等（1000 km、满相位）; R = 观测者-卫星斜距(km)
//   f(B) = (sin B + (π − B)·cos B)/π  = 朝向观测者的受照圆面比例
//   B    = 相位角（卫星处"指向太阳"与"指向观测者"的夹角）
// m₀ 不在 TLE 中，需外部数据：内置常见亮星目录（近似值，参考 qsmag/McCants
// 星等数据库；Quicksat 假设 100% 照明、Molczan 假设 50%，差异约 0.8 星等），
// 未知卫星使用调用方提供的回退值（UI 可配置）。
// 说明: 估算精度 ±1~2 星等（受卫星几何姿态/表面反射率影响），仅作观测辅助。
// =============================================================================
#include <array>
#include <string>

class VisualMagnitudeCalc {
public:
    // 按名称子串匹配内置星等目录（不区分大小写思路: 目录项直接用大写）；
    // 未命中返回 fallback。卫星名来自 TLE 名称行或 NORAD 编号。
    static double standardMagnitude(const std::string &satName, double fallback = 4.5);

    // 相位角（弧度，[0, π]）: 卫星处"指向太阳"与"指向观测者"的单位矢量夹角。
    // 全部输入为 ECEF 地固系 km 坐标（太阳位置 = 地心方向 × 日地距离）。
    static double phaseAngleRad(const std::array<double, 3> &satEcefKm,
                                const std::array<double, 3> &sunEcefKm,
                                const std::array<double, 3> &obsEcefKm);

    // 视星等（越小越亮）。相位因子下限钳制 1e-6，防 log(0) → ±∞ 发散。
    static double visualMagnitude(double stdMag, double rangeKm, double phaseRad);
};

#endif // CORE_VISUALMAGNITUDE_H
```

---

## business/PassPredictorService.cpp

```cpp
// =============================================================================
// PassPredictorService.cpp — 预报调度实现（修复版：正确的线程生命周期管理）
// =============================================================================
#include "PassPredictorService.h"
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <limits>

PassPredictorService::PassPredictorService(QObject *parent)
    : QObject(parent), m_batchCompleted(0), m_batchSeq(0), m_batchFinished(false) {}

PassPredictorService::~PassPredictorService()
{
    // 析构路径: 带超时等待，防止窗口关闭时被长时间批量任务卡死。
    // 协作取消使任务通常毫秒级退出；超时(极端情况)后线程在后台自然后退，
    // 进程退出时由系统回收，绝不在主线程无限阻塞。
    cleanupAll(2000);

    // 孤儿线程兜底: cleanupAll 已连接 finished→deleteLater 且调用过 cancel，
    // 此处不再冗余 quit/wait/deleteLater —— 仅解除父子关系避免析构 UB
    m_reapTimer.reset();
    for (auto &aj : m_orphans) {
        QThread *t = aj.thread;
        if (!t) continue;
        t->setParent(nullptr);
        aj.worker.reset();
        aj.thread = nullptr;
    }
    m_orphans.clear();
}

void PassPredictorService::startJob(const PassPredictWorker::Job &job)
{
    cleanupAll(2000);

    // 单星任务不走批量汇总; 递增序号使在途批量任务的迟到信号全部失效
    // （cleanupAll→cancelAll 已递增一次，此处唯一确定本任务取到的序号）
    const int seq = ++m_batchSeq;
    auto *thread = new QThread(this);
    auto worker = std::make_shared<PassPredictWorker>();
    worker->moveToThread(thread);

    // resultReady → quit thread
    connect(worker.get(), &PassPredictWorker::resultReady, thread, &QThread::quit);
    // errorOccurred → also quit thread（修复：之前不 quit 导致线程泄漏）
    connect(worker.get(), &PassPredictWorker::errorOccurred, thread, &QThread::quit);
    // jobSkipped → also quit thread（修复：恢复跳过路径此前无信号触发 quit,
    // 线程永久驻留 exec() 空转, 2s 后被孤儿回收、10s 后泄漏——单星模式下
    // 每跳过一次即泄漏一个线程）
    connect(worker.get(), &PassPredictWorker::jobSkipped, thread, &QThread::quit);
    // 注意: 不得用 finished→deleteLater 删 worker——线程结束时即释放对象,
    // 但 m_activeJobs/转发 lambda 的 shared_ptr 仍引用它, 下次 cleanupAll→
    // cancelAll 会对已释放对象调用 cancel()（悬垂指针 UAF, 析构即崩溃）。
    // worker 生命周期由 shared_ptr 全权管理（cleanupJob 中 reset 释放）
    // 转发信号（metaInfo 同样带序号守卫: 取消/换任务后迟到元信息一律丢弃,
    // 防止状态栏显示已淘汰任务的模型与历元）
    connect(worker.get(), &PassPredictWorker::resultReady, this,
            [this, seq](QVector<PassEvent> passes) {
                if (seq != m_batchSeq) return;
                emit resultReady(passes);
            });
    connect(worker.get(), &PassPredictWorker::errorOccurred, this,
            [this, seq](QString msg) {
                if (seq != m_batchSeq) return;
                emit errorOccurred(msg);
            });
    // 被自动恢复策略跳过（如 WARNING 级校验和错误）: 单星模式必须告知用户,
    // 否则"计算中"状态永不结束
    connect(worker.get(), &PassPredictWorker::jobSkipped, this,
            [this, seq](QString satelliteId, QString reason) {
                if (seq != m_batchSeq) return;
                emit errorOccurred(QString("卫星 %1 已跳过: %2")
                                       .arg(satelliteId, reason));
            });
    // 校验和警告: 非致命继续, 不计数不跳过——独立信号直通 UI 状态栏
    //（不得复用 errorOccurred: 批量会误入失败汇总, 单星会错误弹窗）
    connect(worker.get(), &PassPredictWorker::checksumWarning, this,
            [this, seq](QString satelliteId) {
                if (seq != m_batchSeq) return;
                emit checksumWarning(satelliteId);
            });
    connect(worker.get(), &PassPredictWorker::metaInfo, this,
            [this, seq](QString model, QString epochUtc, double ageDays) {
                if (seq != m_batchSeq) return;
                emit metaInfo(model, epochUtc, ageDays);
            });

    m_activeJobs.append({thread, worker, QDateTime::currentMSecsSinceEpoch()});

    // 启动线程（在线程 started 信号中调用 worker->run）
    connect(thread, &QThread::started, worker.get(), [worker, job]() {
        worker->run(job);
    });
    // QThread::start() 返回 void, 但**并非**不会失败: 底层线程创建失败时 Qt 只
    // 置 running=false 并发一次 finished()（进程不受影响），此后本任务不再产生
    // 任何 worker 信号 → UI 永久停在"计算中"。这里显式检测并复位。
    thread->start();
    if (!thread->isRunning()) {
        // 已 finished 的线程对象留在 m_activeJobs 中由下次 cleanupJob 的
        // isFinished 分支 deleteLater 回收, 此处不提前释放（避免与队列中的
        // finished→deleteLater 竞争）
        emit errorOccurred("无法创建计算线程，任务未启动");
    }
}

void PassPredictorService::startBatch(const QVector<PassPredictWorker::Job> &jobs)
{
    cleanupAll(2000);

    if (jobs.isEmpty()) {
        emit errorOccurred("无有效 TLE");
        return;
    }

    // 本批次序号: 迟到的旧批次信号在此校验丢弃（worker 池各 lambda 共用）
    const int seq = ++m_batchSeq;

    {
        QMutexLocker locker(&m_batchMutex);
        m_batchPasses.clear();
        m_batchCompleted = 0;
        m_batchTotal = jobs.size();
        m_batchFinished = false;   // 新批次开始, 允许转发单星元信息
        // 预分配: PassEvent 含 std::vector<PassSample>, QVector 扩容重分配会对
        // 存量元素整体深拷贝(非共享), 千颗级批量下是 O(n²) 开销
        // 16 倍: 低轨 168h 窗口单星可达 100+ 过境, 4 倍预估偏小
        m_batchPasses.reserve(qMin<qsizetype>(static_cast<qint64>(m_batchTotal) * 16, 400000));
    }

    // 固定大小 worker 池（H2 修复）: 逐颗建线程在 starlink/active 等大组
    // （数千颗）下会耗尽线程资源并静默挂死。每个 worker 顺序消费一个切片。
    const int maxWorkers   = qBound(1, QThread::idealThreadCount() * 2, 16);
    const int workerCount  = qMin(maxWorkers, m_batchTotal);

    for (int w = 0; w < workerCount; ++w) {
        // 任务轮转均分: worker w 处理下标 w, w+workerCount, ... 的任务
        QVector<PassPredictWorker::Job> slice;
        for (int i = w; i < m_batchTotal; i += workerCount)
            slice.append(jobs[i]);

        // 堆分配 QThread: 生命周期归 cleanupJob/孤儿回收管理（deleteLater）,
        // 绝不可用栈/unique_ptr——作用域结束即销毁仍运行的 QThread 是未定义行为
        auto *thread = new QThread(this);
        auto worker = std::make_shared<PassPredictWorker>();
        worker->moveToThread(thread);

        // 成功: 汇总结果并计数
        connect(worker.get(), &PassPredictWorker::resultReady, this,
                [this, seq](QVector<PassEvent> passes) {
                    if (seq != m_batchSeq) return;   // 旧批次迟到信号: 丢弃
                    
                    bool batchDone = false;
                    {
                        QMutexLocker locker(&m_batchMutex);
                        m_batchPasses.append(std::move(passes));
                        int completed = m_batchCompleted.fetchAndAddRelaxed(1) + 1;
                        // 用 >= 而非 ==: 一旦出现重复计数（如同一任务在
                        // emit 分配阶段抛 bad_alloc 后由 catch 再补发一次），
                        // == 会永久错失相等点 → resultReady 永不发出（批量悬挂）
                        batchDone = (completed >= m_batchTotal);
                    }
                    emit batchProgress(m_batchCompleted.loadAcquire(), m_batchTotal);
                    if (batchDone) finishBatch();
                });

        // 失败: 转发错误并计数（否则批量永远等不到完成 → 悬挂）
        connect(worker.get(), &PassPredictWorker::errorOccurred, this,
                [this, seq](QString msg) {
                    if (seq != m_batchSeq) return;   // 旧批次迟到信号: 丢弃
                    
                    bool batchDone = false;
                    {
                        QMutexLocker locker(&m_batchMutex);
                        int completed = m_batchCompleted.fetchAndAddRelaxed(1) + 1;
                        // 用 >= 而非 ==: 一旦出现重复计数（如同一任务在
                        // emit 分配阶段抛 bad_alloc 后由 catch 再补发一次），
                        // == 会永久错失相等点 → resultReady 永不发出（批量悬挂）
                        batchDone = (completed >= m_batchTotal);
                    }
                    emit batchProgress(m_batchCompleted.loadAcquire(), m_batchTotal);
                    emit errorOccurred(msg);
                    if (batchDone) finishBatch();
                });

        // 被自动恢复策略跳过（如 WARNING 级校验和错误）: 不报错但必须计数,
        // 否则 completed 永远 < total → resultReady 永不发出（批量悬挂）
        connect(worker.get(), &PassPredictWorker::jobSkipped, this,
                [this, seq](QString satelliteId, QString reason) {
                    if (seq != m_batchSeq) return;   // 旧批次迟到信号: 丢弃
                    
                    bool batchDone = false;
                    {
                        QMutexLocker locker(&m_batchMutex);
                        int completed = m_batchCompleted.fetchAndAddRelaxed(1) + 1;
                        // 用 >= 而非 ==: 一旦出现重复计数（如同一任务在
                        // emit 分配阶段抛 bad_alloc 后由 catch 再补发一次），
                        // == 会永久错失相等点 → resultReady 永不发出（批量悬挂）
                        batchDone = (completed >= m_batchTotal);
                    }
                    emit batchProgress(m_batchCompleted.loadAcquire(), m_batchTotal);
                    emit errorOccurred(QString("卫星 %1 已跳过: %2")
                                           .arg(satelliteId, reason));
                    if (batchDone) finishBatch();
                });

        // 校验和警告: 非致命继续, 不计数不跳过（completed 由 resultReady
        // 正常结算）——独立信号直通 UI 状态栏, 不得混入失败汇总
        connect(worker.get(), &PassPredictWorker::checksumWarning, this,
                [this, seq](QString satelliteId) {
                    if (seq != m_batchSeq) return;   // 旧批次迟到信号: 丢弃
                    emit checksumWarning(satelliteId);
                });

        connect(worker.get(), &PassPredictWorker::resultReady, thread, &QThread::quit);
        connect(worker.get(), &PassPredictWorker::errorOccurred, thread, &QThread::quit);
        // 同 startJob: 恢复跳过路径必须 quit, 否则切片全部被跳过的 worker
        // 线程驻留空转（finishBatch 后 cleanupAll join 阻塞 2s, 10s 后泄漏）
        connect(worker.get(), &PassPredictWorker::jobSkipped, thread, &QThread::quit);
        // 同 startJob: worker 生命周期归 shared_ptr, 不得 finished→deleteLater
        // （否则线程结束时对象被释放, m_activeJobs 引用悬垂 → cancelAll UAF）
        connect(worker.get(), &PassPredictWorker::metaInfo, this,
                [this, seq](QString model, QString epochUtc, double ageDays) {
                    // 批次收尾(resultReady 已发出)后, 同批次内迟到 worker 的
                    // 单星元信息不再转发——否则会覆盖"批量 N 颗"汇总并随机
                    // 改写历元栏（队列投递顺序无保证）
                    if (seq != m_batchSeq || m_batchFinished.load()) return;
                    emit metaInfo(model, epochUtc, ageDays);
                });

        m_activeJobs.append({thread, worker, QDateTime::currentMSecsSinceEpoch()});

        connect(thread, &QThread::started, worker.get(), [worker, slice]() {
            worker->runJobs(slice);
        });
        // 同上: start() 失败时本切片既不会 resultReady 也不会 errorOccurred/
        // jobSkipped, completed 永不达 total → 批量永久悬挂（进度条卡住）。
        // 按"整片失败"一次性补计数并在最后一片时正常收尾。
        thread->start();
        if (!thread->isRunning() && seq == m_batchSeq) {   // 已取消则不补计数
            const int n = slice.size();
            int completed = 0;
            {
                QMutexLocker locker(&m_batchMutex);
                for (int k = 0; k < n; ++k)
                    completed = m_batchCompleted.fetchAndAddRelaxed(1) + 1;
            }
            emit batchProgress(m_batchCompleted.loadAcquire(), m_batchTotal);
            emit errorOccurred(QString("无法创建计算线程，%1 颗卫星未执行").arg(n));
            if (completed >= m_batchTotal) finishBatch();
        }
    }
}

void PassPredictorService::finishBatch()
{
    // 创建临时副本以减少锁的持有时间
    QVector<PassEvent> batchCopy;
    {
        QMutexLocker locker(&m_batchMutex);
        // 按可见性优先 + 最大仰角降序排列
        std::sort(m_batchPasses.begin(), m_batchPasses.end(),
            [](const PassEvent &a, const PassEvent &b) {
                if (a.visibility != b.visibility)
                    return static_cast<int>(a.visibility) < static_cast<int>(b.visibility);
                return a.maxElevDeg > b.maxElevDeg;
            });
        // 移动到临时量: 信号参数按值接收, 直接 emit 成员会深拷贝
        // 整批采样向量; 移动后主线程交付只剩队列转发那一份必要拷贝
        batchCopy = std::move(m_batchPasses);
        m_batchFinished = true;   // 收尾: 迟到的单星 metaInfo 不再转发
    }
    
    // emit 按值接收: 移动后主线程交付只剩队列转发那一份必要拷贝
    emit resultReady(std::move(batchCopy));
}

void PassPredictorService::cancelAll()
{
    // 递增批次序号: 取消后本批次迟到的结果/错误信号一律丢弃，
    // 杜绝"取消后结果又冒出来"的竞态（M5）
    m_batchSeq.fetchAndAddRelaxed(1);
    for (auto &aj : m_activeJobs) {
        if (aj.worker) {
            aj.worker->cancel();   // 协作式取消，线程会快速自然退出
        }
    }
    // 立即释放已累积的批量结果: 取消后这些过境不会再被交付（seq 守卫全部丢弃）,
    // 若留到下次 startBatch 才 clear, 期间最多 400000 条 PassEvent（长窗口大组
    // 可达数百 MB）会一直驻留。finishBatch 已 move 走内容时此处为空操作。
    {
        QMutexLocker locker(&m_batchMutex);
        m_batchPasses.clear();
        m_batchPasses.squeeze();
    }
}

bool PassPredictorService::cleanupJob(ActiveJob &aj, int waitTimeoutMs)
{
    if (!aj.thread) {
        aj.worker = nullptr;
        return true;
    }
    
    // 先尝试取消worker，确保线程能快速响应退出
    if (aj.worker) {
        aj.worker->cancel();
    }
    
    if (aj.thread->isRunning()) {
        // 请求事件循环退出并等待。协作式取消保证线程快速退出，
        // 不再使用已弃用的 QThread::terminate()
        aj.thread->quit();
        bool exited = (waitTimeoutMs > 0)
            ? aj.thread->wait(waitTimeoutMs)
            : aj.thread->wait();   // ≤0: 无限等待（与头文件契约一致）
            
        if (!exited) {
            // 超时: 线程仍在运行（极端情况）。不等了——
            // 移入孤儿列表交给后台定时器二次回收（进程存活期间最终
            // 全部回收成功），而不是永久泄漏或即使执行 delete 正在运行
            // 的 QThread（未定义行为）。
            // 连接 finished→deleteLater: 线程自然退出后自动回收，防止
            // reapCount 超限后 removeAt 移除条目但 QThread 对象泄漏
            QObject::connect(aj.thread, &QThread::finished,
                             aj.thread, &QObject::deleteLater);
            m_orphans.append({aj.thread, aj.worker, QDateTime::currentMSecsSinceEpoch()});
            startReapTimer();
            return false;
        }
    } else {
        // 线程未被观察为运行状态: 若已自然结束（isFinished），finished 信号
        // 在连接建立前就已发射，finished→deleteLater 永不触发——必须直接
        // deleteLater，否则 QThread 对象随会话累积（每完成一次单星任务
        // 就残留一个子对象直到 service 析构）
        if (aj.thread->isFinished()) {
            aj.thread->deleteLater();
            aj.worker.reset();  // worker 由 shared_ptr 自动管理
            return true;
        }
        // 尚未启动（理论上不可达: start() 紧跟构造）: 防御性 quit()。
        // 删除改由 finished→deleteLater 兜底——若 OS 线程尚未进入 run(),
        // 主循环当轮处理 DeferredDelete 时线程可能仍在启动, 直接
        // deleteLater 会触发 "QThread: Destroyed while still running"
        aj.thread->quit();
        QObject::connect(aj.thread, &QThread::finished,
                         aj.thread, &QObject::deleteLater);
        return true;
    }
    
    aj.thread->deleteLater();
    aj.thread = nullptr;
    aj.worker.reset();  // worker 生命周期归 shared_ptr（cleanupJob 中 reset 释放）
    return true;
}

void PassPredictorService::cleanupAll(int waitTimeoutMs)
{
    // 先取消，给线程时间自然退出
    cancelAll();
    // 全局截止时间（M7）: 避免逐线程串行等待在任务多时总时长 N×timeout。
    // 所有线程共享同一剩余时间预算，超时后后续线程移入孤儿列表由定时器回收。
    // waitTimeoutMs<=0 表示无限等待: 不设全局截止时间, 逐线程 wait(-1)
    const qint64 deadline = (waitTimeoutMs > 0)
        ? QDateTime::currentMSecsSinceEpoch() + waitTimeoutMs
        : std::numeric_limits<qint64>::max();
    for (auto &aj : m_activeJobs) {
        int remaining = waitTimeoutMs;
        if (waitTimeoutMs > 0) {
            remaining = static_cast<int>(
                deadline - QDateTime::currentMSecsSinceEpoch());
        } else {
            remaining = std::numeric_limits<int>::max();  // 无限等待: cleanupJob 内部 wait(INT_MAX)
        }
        if (remaining > 0) {
            cleanupJob(aj, remaining);
        } else if (aj.thread && aj.thread->isRunning()) {
            // 预算耗尽: 移入孤儿列表由 reapOrphans 回收，避免 wait(0) 无限阻塞
            QObject::connect(aj.thread, &QThread::finished,
                             aj.thread, &QObject::deleteLater);
            m_orphans.append({aj.thread, aj.worker,
                              QDateTime::currentMSecsSinceEpoch()});
            aj.thread = nullptr;
            aj.worker.reset();
        } else if (aj.thread && !aj.thread->isRunning()) {
            // 预算耗尽但线程已自然结束: 直接清理，避免资源泄漏
            aj.thread->deleteLater();
            aj.thread = nullptr;
            aj.worker.reset();
        }
    }
    // 仅在确有孤儿时启动回收定时器: reapOrphans() 在空表时早退（早退分支在
    // 停表逻辑之前），无条件启动会让 500 ms 周期定时器一直空跑到进程退出
    if (!m_orphans.isEmpty()) startReapTimer();
    m_activeJobs.clear();
}

void PassPredictorService::startReapTimer()
{
    if (m_reapTimer) return;   // 已在运行
    m_reapTimer = std::make_unique<QTimer>(nullptr);  // 不设 parent，避免与 unique_ptr 双重所有
    m_reapTimer->setInterval(500);  // 缩短间隔，提高响应速度
    connect(m_reapTimer.get(), &QTimer::timeout, this, &PassPredictorService::reapOrphans);
    m_reapTimer->start();
}

void PassPredictorService::reapOrphans()
{
    if (m_orphans.isEmpty()) return;
    
    // 每次 tick 只处理一个孤儿，避免多个孤儿累积 wait() 阻塞主线程
    for (int i = 0; i < m_orphans.size(); ++i) {
        ActiveJob &aj = m_orphans[i];
        QThread *t = aj.thread;
        
        if (!t) {
            aj.worker.reset();
            m_orphans.removeAt(i);
            break;
        }
        
        if (!t->isRunning()) {
            t->deleteLater();
            aj.thread = nullptr;
            aj.worker.reset();
            m_orphans.removeAt(i);
            break;
        }
        
        t->quit();
        
        qint64 age = QDateTime::currentMSecsSinceEpoch() - aj.createTime;
        const int waitTime = 50;
        
        if (t->wait(waitTime)) {
            t->deleteLater();
            aj.thread = nullptr;
            aj.worker.reset();
            m_orphans.removeAt(i);
        } else if (age > 10000) {
            t->setParent(nullptr);
            aj.worker.reset();
            aj.createTime = QDateTime::currentMSecsSinceEpoch();
            ++aj.reapCount;
            if (aj.reapCount >= 5) {
                qWarning() << "reapOrphans: orphan" << aj.thread
                           << "exceeded max retries, removing";
                m_orphans.removeAt(i);
            }
        }
        break;  // 每次 tick 仅处理一个
    }
    
    // 如果所有孤儿都已处理，停止定时器
    if (m_orphans.isEmpty() && m_reapTimer) {
        // 槽内不销毁发送者 QTimer（Qt6 COW 通常安全非保证）:
        // 延迟到下一轮事件循环收尾
        QTimer::singleShot(0, this, [this]() {
            if (m_orphans.isEmpty() && m_reapTimer) {
                m_reapTimer->stop();   // 全部回收完毕 → 停表省电
                m_reapTimer.reset();
            }
        });
    }
}
```

---

## business/PassPredictorService.h

```cpp
#ifndef BUSINESS_PASSPREDICTORSERVICE_H
#define BUSINESS_PASSPREDICTORSERVICE_H
// =============================================================================
// PassPredictorService.h — 预报调度中心（单星和批量模式）
// 管理 PassPredictWorker 的生命周期和线程调度
// =============================================================================
#include <QObject>
#include <QPointer>
#include <QVector>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QAtomicInt>
#include <atomic>
#include <memory>
#include "core/DataTypes.h"
#include "PassPredictWorker.h"

class QTimer;

class PassPredictorService : public QObject {
    Q_OBJECT
public:
    explicit PassPredictorService(QObject *parent = nullptr);
    ~PassPredictorService() override;

    // 单星预报
    void startJob(const PassPredictWorker::Job &job);

    // 批量预报（多颗卫星）
    void startBatch(const QVector<PassPredictWorker::Job> &jobs);

    // 取消当前所有计算
    void cancelAll();

    // 清理所有任务线程。
    // waitTimeoutMs: 单个线程的最长等待时间（毫秒）；>0 时超时后放弃等待
    //（让线程在后台自然后退，避免析构路径卡死主线程），≤0 表示无限等待
    void cleanupAll(int waitTimeoutMs = -1);

signals:
    void resultReady(QVector<PassEvent> passes);
    void errorOccurred(QString message);
    // TLE 校验和警告: 非致命（解析成功, 继续计算）。独立信号通道——
    // 若复用 errorOccurred 会被 UI 当作失败计数入汇总列表（批量把成功
    // 卫星误列"预报失败"）、单星路径弹出时机错误的模态框
    void checksumWarning(QString satelliteId);
    void batchProgress(int completed, int total);
    void metaInfo(QString model, QString epochUtc, double ageDays);

private:
    struct ActiveJob {
        QThread *thread = nullptr;
        std::shared_ptr<PassPredictWorker> worker;   // 使用 shared_ptr 管理worker生命周期
        qint64 createTime = 0;                         // 创建时间，用于孤儿线程回收策略
        int reapCount = 0;                             // 孤儿重试次数（超限时强制移除）
    };
    QVector<ActiveJob> m_activeJobs;

    // 批量模式汇总状态（成员变量，避免 lambda 捕获悬垂引用）
    QVector<PassEvent> m_batchPasses;   // 已完成的各卫星过境
    QMutex m_batchMutex;                // 保护批量状态的互斥锁
    QAtomicInt m_batchCompleted;        // 已完成（成功+失败）任务数（原子操作）
    int  m_batchTotal     = 0;          // 批量任务总数
    QAtomicInt m_batchSeq;              // 批次序号: 防旧批次迟到信号污染新批次
    std::atomic<bool> m_batchFinished;  // 本批已收尾（resultReady 发出前已置位）,
                                        // 迟到的单星 metaInfo 不再转发

    void finishBatch();                 // 全部完成后排序并发出 resultReady
    bool cleanupJob(ActiveJob &job, int waitTimeoutMs);

    // 孤儿线程回收: cleanupAll 超时(极端情况)放弃的线程不永久泄漏——
    // 移入 m_orphans 后由后台定时器周期性再试 wait, 退出后 deleteLater。
    // 进程存活期间最终全部回收; 析构时再兜底一次(超时仍不退才解除父子关系)
    QVector<ActiveJob> m_orphans;
    std::unique_ptr<QTimer> m_reapTimer;
    void startReapTimer();
    void reapOrphans();
};

#endif // BUSINESS_PASSPREDICTORSERVICE_H
```

---

## business/PassPredictWorker.cpp

```cpp
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

// 毫秒/天换算常量
static constexpr double kMsPerDay = 86400000.0;

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
            job.tleLine2.toStdString(), &parseErr, job.strictChecksum);
    } else {
        tleOpt = TleParser::parse(
            job.tleLine1.toStdString(),
            job.tleLine2.toStdString(), &parseErr, job.strictChecksum);
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
            // 仅在"强制校验和"模式下可达（宽松模式解析成功, 走下方
            // checksumWarning 提示路径）
            bool line1Valid = TleParser::verifyChecksum(job.tleLine1.toStdString());
            bool line2Valid = TleParser::verifyChecksum(job.tleLine2.toStdString());
            details = QString("校验和错误（强制模式已阻断）: 行1 %1, 行2 %2")
                .arg(line1Valid ? "通过" : "失败").arg(line2Valid ? "通过" : "失败");
            suggestion = "请使用校验和正确的TLE数据；如需容忍损坏数据，"
                         "可关闭\"强制校验和\"后重试";
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
        // 注: 此处曾有 case DECAYED 分支——但 TleParser::parse 从不产生该
        // 错误码（再入由 perturb 在 Propagator::initialize 阶段以
        // Sgp4Error::DECAYED 报出, 见下方 Step 2 的 errMsg 文案处理）,
        // 属不可达死代码, 已删除
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

    // 校验和未通过: 解析成功（非致命继续计算）, 但必须让用户感知坏数据
    //（否则 WARNING 告警路径 CHECKSUM_FAILED 从未执行——静默无感）。
    // 独立信号: 不计数、不跳过, 由 service 转发为 errorOccurred
    if (!tle.checksumValid) {
        emit checksumWarning(QString::fromStdString(tle.name));
    }
    
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

    // 批量取消检测: TLE 解析耗时可观, 中途取消时跳过后续传播初始化
    if (m_cancelFlag->isCancelled()) return;

    // ---- Step 2: 初始化 Propagator（封装 perturb::Satellite） ----
    Propagator prop;
    std::string errMsg;
    
    // 记录初始化开始时间
    auto initStartTime = std::chrono::steady_clock::now();
    
    bool ok = prop.initialize(
        job.tleLine1.toStdString(),
        job.tleLine2.toStdString(), errMsg,
        job.calcCfg.gravityModel);

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
        
        // 恢复成功: 自动跳过——通知调度层计数防悬挂
        //（R17 契约: 默认策略仅 WARNING 级返回 true; createPropagatorError
        // 固定 ERROR 级 → 实际恒走下方 errorOccurred 分支）
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
        .toMSecsSinceEpoch() + static_cast<qint64>((tle.epochDay - 1.0) * kMsPerDay + 0.5);
    double ageDays = (job.startUtc.toMSecsSinceEpoch() - epochMs) / kMsPerDay;
    if (ageDays < 0.0) ageDays = 0.0;   // 未来历元（时钟/数据异常）按 0 处理
    emit metaInfo(modelStr,
                  QDateTime::fromMSecsSinceEpoch(epochMs, QTimeZone::UTC)
                      .toString("yyyy-MM-dd HH:mm:ss 'UTC'"),
                  ageDays);

    // 设置卫星名称
    prop.setSatelliteName(tle.name);

    // ---- Step 3: 构建传播 Lambda ----
    auto cancelFlag = m_cancelFlag;  // 捕获 shared_ptr, 解耦 worker 生命周期
    auto propagateFn = [&prop, cancelFlag](double jd) -> std::optional<EciState> {
        if (cancelFlag->isCancelled()) return std::nullopt;
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
    double jdStart = (job.startUtc.toMSecsSinceEpoch() / kMsPerDay) + 2440587.5;
    
    // 记录计算开始时间
    auto calcStartTime = std::chrono::steady_clock::now();
    
    std::vector<PassEvent> passes;
    try {
        passes = calc.predict(propagateFn, obs, jdStart, job.hoursAhead, m_cancelFlag);

        if (m_cancelFlag->isCancelled()) {
            emit jobSkipped(QString::fromStdString(tle.name), "计算被取消");
            return;
        }
        
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

        // 优先用异常类型分类（不依赖消息文本语言/措辞）
        if (dynamic_cast<const std::bad_alloc *>(&e)) {
            suggestion = "系统内存不足；减少同时计算的卫星数量或缩短预测时间窗口";
        } else if (dynamic_cast<const std::overflow_error *>(&e)) {
            suggestion = "数值溢出；检查观测站坐标和仰角阈值是否合理";
        } else if (dynamic_cast<const std::underflow_error *>(&e)) {
            suggestion = "数值下溢；检查时间步长设置";
        } else if (dynamic_cast<const std::out_of_range *>(&e)) {
            suggestion = "参数越界；检查观测站坐标和仰角阈值是否合理";
        } else if (dynamic_cast<const std::invalid_argument *>(&e)) {
            suggestion = "参数无效；检查TLE数据格式和观测站配置";
        } else if (dynamic_cast<const std::runtime_error *>(&e)) {
            // runtime_error 细分: 用消息文本做补充分类
            const QString msg = QString::fromUtf8(e.what());
            if (msg.contains("传播") || msg.contains("propag")) {
                suggestion = "检查TLE数据时效性，或使用更新的TLE";
            } else if (msg.contains("数值") || msg.contains("范围")
                       || msg.contains("numer") || msg.contains("rang")) {
                suggestion = "检查观测站坐标和仰角阈值是否合理";
            } else if (msg.contains("时间") || msg.contains("time")) {
                suggestion = "缩短预测时间窗口，或检查起始时间设置";
            } else {
                suggestion = "尝试缩短预测时间窗口，或检查输入参数";
            }
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
        
        // 恢复成功: 自动跳过
        if (recoverySuccess) {
            emit jobSkipped(satelliteId, errorInfo.message);
            return;
        }
        // 恢复失败，发送错误信号
        emit errorOccurred(errorInfo.message + "\n" + errorInfo.detailedContext + "\n" + errorInfo.suggestedAction);
        
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

    // 写入 TLE（供详情视图按需重新传播）
    const std::string tl1 = job.tleLine1.toStdString();
    const std::string tl2 = job.tleLine2.toStdString();
    for (auto &p : passes) {
        p.tleLine1 = tl1;
        p.tleLine2 = tl2;
    }

    emit resultReady(QVector<PassEvent>(passes.begin(), passes.end()));
}

void PassPredictWorker::cancel()
{
    m_cancelFlag->set(true);
}
```

---

## business/PassPredictWorker.h

```cpp
#ifndef BUSINESS_PASSPREDICTWORKER_H
#define BUSINESS_PASSPREDICTWORKER_H
// =============================================================================
// PassPredictWorker.h — 工作在独立 QThread 中的过境预报执行器
// =============================================================================
#include <QObject>
#include <QVector>
#include <QDateTime>
#include <atomic>
#include "core/DataTypes.h"
#include "core/OrbitCalculator.h"
#include "core/AtomicCancelFlag.h"

// 注册跨线程信号槽所需的自定义类型
Q_DECLARE_METATYPE(QVector<PassEvent>)
// PassEvent 自身也注册: 供 QVariant 直接存取（回环/调试/序列化扩展）
Q_DECLARE_METATYPE(PassEvent)

class PassPredictWorker : public QObject {
    Q_OBJECT
public:
    struct Job {
        QString tleLine0;          // 卫星名称行（可选）
        QString tleLine1;          // TLE 第一行
        QString tleLine2;          // TLE 第二行
        double  observerLatDeg  = 33.0;
        double  observerLonDeg  = 103.0;
        double  observerAltKm   = 0.05;
        double  minElevDeg      = 10.0;
        QDateTime startUtc;        // 预报起始 UTC 时间
        double  hoursAhead       = 24.0;
        // TLE 校验和策略: false=宽松（校验失败仅告警继续计算）
        //                  true =强制（校验失败即判该星解析失败并跳过）
        bool    strictChecksum   = false;
        OrbitCalculator::Config calcCfg;
    };

    explicit PassPredictWorker(QObject *parent = nullptr);
    ~PassPredictWorker() override = default;

public slots:
    void run(Job job);
    // 批量切片: 依次执行多个任务（线程池模式，固定 worker 数消费全部任务）
    void runJobs(const QVector<Job> &jobs);
    void cancel();

signals:
    void resultReady(QVector<PassEvent> passes);
    void errorOccurred(QString message);
    // TLE 元信息: 传播模型 / 历元(UTC 字符串) / 数据龄期(天)
    void metaInfo(QString model, QString epochUtc, double ageDays);
    // 任务被自动恢复策略跳过（如 WARNING 级校验和错误）:
    // 不 emit errorOccurred, 但必须通知调度层——否则单星 UI 永久"计算中"、
    // 批量 completed 计数缺失导致 resultReady 永不发出（悬挂）
    void jobSkipped(QString satelliteId, QString reason);
    // TLE 校验和未通过但解析成功（非致命继续计算）: 用户须感知坏数据存在,
    // 否则误用精度受损的根数而不知。走独立信号不计数、不跳过, 由 service
    // 转发为 errorOccurred（UI 状态栏去重后单条提示）
    void checksumWarning(QString satelliteId);

private:
    AtomicCancelFlagPtr m_cancelFlag;
    void runOneJob(const Job &job);   // 单任务执行体（run/runJobs 共用）
};

#endif // BUSINESS_PASSPREDICTWORKER_H
```

---

## business/TleBatchParser.cpp

```cpp
// =============================================================================
// TleBatchParser.cpp — 多颗 TLE 文本块解析实现
// =============================================================================
#include "TleBatchParser.h"

QVector<TleBlock> parseTleBlocks(const QString &text, int *incompleteCount)
{
    QVector<TleBlock> blocks;
    QString pendingName;
    QString pendingL1;
    int incomplete = 0;

    // TLE 行严格判据: 前缀 "1 "/"2 " 且长度 ≥ 69。
    // 避免把 "1U CUBESAT"、"1999-xxx 论文标题" 之类名称行误判为 TLE 行
    const auto isTleLine1 = [](const QString &l) {
        return l.startsWith("1 ") && l.length() >= 69;
    };
    const auto isTleLine2 = [](const QString &l) {
        return l.startsWith("2 ") && l.length() >= 69;
    };

    const QStringList lines = text.split('\n');
    for (const QString &raw : lines) {
        const QString l = raw.trimmed();
        if (l.isEmpty()) continue;
        // 注释行跳过（批量导入对话框承诺「含注释头时自动忽略」）:
        // 若当作名称行, 「名称→注释→行1」序列会把卫星名替换为注释文本,
        // 污染表格显示/身份键/导出
        if (l.startsWith('#')) continue;
        if (isTleLine1(l)) {
            if (!pendingL1.isEmpty()) incomplete++;  // 上一颗只有行1 → 半截块
            pendingL1 = l;
        } else if (isTleLine2(l)) {
            if (!pendingL1.isEmpty()) {
                blocks.append({ pendingName, pendingL1, l });
                pendingName.clear();
                pendingL1.clear();
            } else {
                incomplete++;   // 只有行2 → 半截块
            }
        } else {
            pendingName = l;   // 非 TLE 行视为下一颗卫星的名称
        }
    }
    if (!pendingL1.isEmpty()) incomplete++;   // 结尾残留单行1
    if (incompleteCount) *incompleteCount = incomplete;
    return blocks;
}
```

---

## business/TleBatchParser.h

```cpp
#ifndef BUSINESS_TLEBATCHPARSER_H
#define BUSINESS_TLEBATCHPARSER_H
// =============================================================================
// TleBatchParser.h — 多颗 TLE 文本块解析（Qt 层）
// 支持 CelesTrak / NORAD 下载的 3LE 格式: [名称行] + 行1 + 行2
// 严格判据: 行1 以 "1 " 开头且长度 ≥ 69, 行2 以 "2 " 开头且长度 ≥ 69
// 名称行可选; 注释/说明行会被当作名称并被下一对 TLE 覆盖（无害）
// =============================================================================
#include <QString>
#include <QVector>

struct TleBlock {
    QString name;    // 名称行（可能为空）
    QString line1;
    QString line2;
};

// 解析多颗 TLE 文本块
// incompleteCount(可选出参): 半截块数量（只有行1/行2 而无配对者，静默丢失的源头）
QVector<TleBlock> parseTleBlocks(const QString &text, int *incompleteCount = nullptr);

#endif // BUSINESS_TLEBATCHPARSER_H
```

---

## business/TleCache.cpp

```cpp
// =============================================================================
// TleCache.cpp — TLE 本地磁盘缓存实现
// =============================================================================
#include "TleCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

// tag → 安全文件名: 仅保留 [A-Za-z0-9._-]，其余替换为 '_'
// （tag 来自组标识 "space-station"/"CATNR-25544" 等，仍需防御性净化，
//   避免路径分隔符/相对路径逃逸出缓存目录）
QString sanitizeTag(const QString &tag)
{
    QString out;
    out.reserve(tag.size());
    for (const QChar c : tag) {
        const ushort u = c.unicode();
        const bool ok = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')
                     || (u >= '0' && u <= '9') || u == '.' || u == '_' || u == '-';
        out.append(ok ? c : QChar('_'));
    }
    if (out.isEmpty()) out = QStringLiteral("_");
    return out;
}

} // namespace

QString TleCache::cacheDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/tle_cache");
}

QString TleCache::cachePath(const QString &tag)
{
    return cacheDir() + QStringLiteral("/") + sanitizeTag(tag) + QStringLiteral(".3le");
}

bool TleCache::read(const QString &tag, QString &text, qint64 &ageSec)
{
    text.clear();
    ageSec = -1;
    if (tag.isEmpty()) return false;

    const QString path = cachePath(tag);
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile() || fi.size() <= 0) return false;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QByteArray raw = f.readAll();
    f.close();

    const QString cached = QString::fromUtf8(raw);
    if (cached.trimmed().isEmpty()) return false;

    // 时间戳异常（未来时间/时钟回拨）→ ageSec < 0, 调用方按 "不新鲜" 处理
    const QDateTime mtime = fi.lastModified();
    if (mtime.isValid())
        ageSec = mtime.secsTo(QDateTime::currentDateTimeUtc());
    else
        ageSec = -1;

    text = cached;
    return true;
}

void TleCache::write(const QString &tag, const QString &text)
{
    if (tag.isEmpty() || text.trimmed().isEmpty()) return;

    QDir dir(cacheDir());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return;

    // QSaveFile: 先写临时文件再原子替换 —— 直接 Truncate 写会在
    // 进程被杀/磁盘满时留下**半截缓存**, 下次启动会被当成有效数据解析
    // （表现为"下载成功却少了若干颗卫星"）。commit() 失败则丢弃临时文件,
    // 旧缓存保持不变。
    QSaveFile f(cachePath(tag));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    if (f.write(text.toUtf8()) < 0) { f.cancelWriting(); return; }
    f.commit();
}

void TleCache::remove(const QString &tag)
{
    if (tag.isEmpty()) return;
    QFile::remove(cachePath(tag));
}

int TleCache::clearAll()
{
    QDir dir(cacheDir());
    if (!dir.exists()) return 0;
    int n = 0;
    const QStringList files = dir.entryList({QStringLiteral("*.3le")}, QDir::Files);
    for (const QString &name : files)
        if (dir.remove(name)) ++n;
    return n;
}
```

---

## business/TleCache.h

```cpp
#ifndef BUSINESS_TLECACHE_H
#define BUSINESS_TLECACHE_H
// =============================================================================
// TleCache.h — TLE 下载结果的本地磁盘缓存
//
// 目的:
//   1) 避免重复下载。CelesTrak 的 GP 数据每 ~2 小时才更新一次，且自 2026-03
//      起对 Active/Starlink 等大组强制"每次更新只允许下载一次"，重复请求会
//      返回 HTTP 403；单日超量请求会被加入防火墙（官方使用政策）。
//   2) 限流/离线时仍可使用最近一次成功下载的数据。
//
// 设计:
//   - 缓存位置: QStandardPaths::AppDataLocation/tle_cache/<tag>.3le
//   - 以文件 mtime 作为"下载时间"，无需额外索引文件
//   - TTL 默认 2 小时（kTtlSec），与 CelesTrak 更新周期一致
//   - 纯 QtCore 实现（可被单元测试直接链接，无需 network 模块）
// =============================================================================
#include <QString>
#include <QtGlobal>

class TleCache {
public:
    // 缓存有效期（秒）: CelesTrak GP 数据更新周期约 2 小时
    static constexpr qint64 kTtlSec = 2 * 3600;

    // 缓存目录（不存在时按需创建）
    static QString cacheDir();

    // 某 tag 对应的缓存文件路径
    static QString cachePath(const QString &tag);

    // 读取缓存。命中（文件存在且非空）返回 true，并给出文本与
    // "距上次下载的秒数"（ageSec，<0 表示时间戳异常）
    static bool read(const QString &tag, QString &text, qint64 &ageSec);

    // 写入缓存（覆盖）。失败静默——缓存不是关键路径，绝不因此中断下载流程
    static void write(const QString &tag, const QString &text);

    // 删除某 tag 的缓存
    static void remove(const QString &tag);

    // 缓存是否新鲜（ageSec ≤ TTL）
    static bool isFresh(qint64 ageSec) { return ageSec >= 0 && ageSec <= kTtlSec; }

    // 清空整个缓存目录，返回删除的文件数
    static int clearAll();
};

#endif // BUSINESS_TLECACHE_H
```

---

## business/TleFetcher.cpp

```cpp
// =============================================================================
// TleFetcher.cpp — CelesTrak 在线 TLE 下载实现
// 本地缓存: 2 小时内的成功下载直接复用（见 business/TleCache），
// 既省流量也规避 CelesTrak 的"每次更新只允许下载一次"限流（HTTP 403）
// =============================================================================
#include "TleFetcher.h"
#include "TleCache.h"
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QTimer>

// CelesTrak 网络请求超时（毫秒）及看门狗间隔
static constexpr int kNetworkTimeoutMs = 30000;

const QVector<TleFetcher::Group> &TleFetcher::groups()
{
    static const QVector<Group> g = {
        { "space-station", "空间站" },
        { "visual",        "最亮 100 颗" },
        { "stations",      "常用业余卫星" },
        { "active",        "全部活动卫星" },
        { "starlink",      "星链 Starlink" },
        { "oneweb",        "一网 OneWeb" },
        { "gps-ops",       "GPS 星座" },
        { "glo-ops",       "GLONASS 星座" },
        { "gal-ops",       "Galileo 星座" },
        { "beidou",        "北斗星座" },
        { "geo",           "地球同步轨道" },
        { "weather",       "气象卫星" },
        { "science",       "科学卫星" },
        { "cubesat",       "立方星" },
        { "last-30-days",  "近 30 天发射" },
    };
    return g;
}

TleFetcher::TleFetcher(QObject *parent)
    : QObject(parent)
{
}

void TleFetcher::fetchGroup(const QString &groupId, bool forceRefresh)
{
    if (tryServeFromCache(groupId, forceRefresh)) return;

    QUrlQuery q;
    q.addQueryItem("GROUP", groupId);
    q.addQueryItem("FORMAT", "tle");
    QUrl url("https://celestrak.org/NORAD/elements/gp.php");
    url.setQuery(q);
    startRequest(url, groupId);
}

void TleFetcher::fetchSatellite(int catnorID, bool forceRefresh)
{
    const QString tag = QString("CATNR-%1").arg(catnorID);
    if (tryServeFromCache(tag, forceRefresh)) return;

    QUrlQuery q;
    q.addQueryItem("CATNR", QString::number(catnorID));
    q.addQueryItem("FORMAT", "tle");
    QUrl url("https://celestrak.org/NORAD/elements/gp.php");
    url.setQuery(q);
    startRequest(url, tag);
}

// 缓存优先: 新鲜（≤2 小时）缓存直接复用, 不发网络请求。
// 延迟到下一轮事件循环投递, 保持 downloaded/finished 的异步时序
//（否则会在按钮点击栈内同步弹出模态批量对话框, 且 finished 的
// 入口复位逻辑与调用栈交错）
bool TleFetcher::tryServeFromCache(const QString &tag, bool forceRefresh)
{
    if (forceRefresh || tag.isEmpty()) return false;
    // 在途保护:
    //  - 同一 tag 的投递已排队 → 直接复用（不再排队, 也不发网络请求）
    //  - 已有网络请求在途 → 返回 false, 交调用方按"新请求替换旧请求"处理
    //    （startRequest 会中止旧回复并作废排队投递, 语义与改动前一致）
    if (m_cachePending && m_cacheTag == tag) return true;
    if (m_reply != nullptr) return false;

    QString cached;
    qint64 ageSec = 0;
    if (!TleCache::read(tag, cached, ageSec) || !TleCache::isFresh(ageSec))
        return false;

    m_cachePending = true;
    m_cacheTag = tag;   // 记住本次排队投递的 tag: 后续请求会覆盖它并作废本投递
    QTimer::singleShot(0, this, [this, tag, cached, ageSec]() {
        // 期间被 cancel() 撤销、被新一轮请求取代（tag 不同）→ 静默丢弃,
        // 保证"投递的数据"与"最后一次请求的 tag"始终一致
        if (!m_cachePending || m_cacheTag != tag) return;
        m_cachePending = false;
        m_cacheTag.clear();
        emit cacheServed(tag, ageSec);
        emit downloaded(cached, tag);
        emit finished();
    });
    return true;
}

void TleFetcher::cancel()
{
    // 仅在存在活动请求（在途回复或已排队的缓存投递）时置取消标志:
    // 无请求时置位会让下一次正常下载被 onReplyFinished 的静默分支误吞
    const bool hadPendingCache = m_cachePending;
    m_cachePending = false;   // 撤销排队中的缓存投递（其回调自查该标志）
    m_cacheTag.clear();
    bool hadReply = (m_reply != nullptr);
    if (m_reply) {
        m_cancelled = true;
        m_reply->abort();           // abort 可能同步触发 onReplyFinished → m_cancelled=false
        m_reply = nullptr;          // 防止 abort 未同步触发 finished 时后续 cancel() 重入
    }
    m_timeoutTimer.reset();         // 请求已中止, 看门狗失效
    // 无条件通知: 保证调用者收到 finished 信号（在途回复或被撤销的缓存投递都算活动请求）
    if (hadReply || hadPendingCache) emit finished();
}

void TleFetcher::startRequest(const QUrl &url, const QString &tag)
{
    // 新请求开始即解除历史取消状态（防残留标志吞掉本次正常结果）
    m_cancelled = false;
    // 作废排队中的缓存投递: 同一时刻只允许一次投递在途（否则"缓存命中"
    // 后立刻强制刷新会导致 downloaded 被投递两次）
    m_cachePending = false;
    m_cacheTag.clear();
    if (m_reply) {
        // Qt6 的 QNetworkReply::abort() 会同步发射 finished() 重入
        // onReplyFinished（QCoro/Qt bug 报告已证实, 非异步）——必须先把
        // m_reply 摘为 nullptr 再 abort, 否则 onReplyFinished 内 m_reply=nullptr
        // 返回后此处继续解引用即空指针崩溃; 同时置取消标志让旧请求的收尾
        // 走静默清理分支（否则 aborted 回复会走正常错误路径弹虚假"网络错误"）
        // 注意: 不调用 old->deleteLater() —— onReplyFinished 的取消分支
        // (line 151) 已负责 reply->deleteLater(), 双重 deleteLater 会导致堆损坏
        QNetworkReply *old = m_reply;
        m_reply = nullptr;
        m_cancelled = true;
        old->abort();
    }
    m_cancelled = false;   // 旧请求已中止/标记丢弃, 新请求恢复正常语义

    // 销毁旧看门狗: 延迟单发回调 (onReplyFinished cancelled 分支) 可能在
    // 事件循环中排队等待重置旧 timer; 若此处复用旧 timer 对象, 延迟回调
    // 的指针相等守卫会误杀新请求的看门狗 → 请求永久挂起。先 reset 使延迟
    // 回调找到 nullptr != old_timer → no-op, 再创建新 timer
    m_timeoutTimer.reset();

    QNetworkRequest req(url);
    req.setTransferTimeout(kNetworkTimeoutMs);
    // CelesTrak 若返回 3xx（域名/路径调整），自动跟随，避免"未解析到有效
    // TLE"的误导性错误（仅允许降级到同样安全或更安全的策略）
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("XGZ-PassPredictor/1.0 (Qt %1)").arg(QT_VERSION_STR));

    m_pendingTag = tag;
    QNetworkReply *reply = m_nam.get(req);
    m_reply = reply;
    // 捕获 reply 指针而非依赖 m_reply: 防止旧请求的 finished 迟到时
    // 误处理已被新请求替换的 m_reply（并发竞态）
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply);
    });

    // 启动超时看门狗（单发 30s, 与 setTransferTimeout 同量级）:
    // DNS 解析/建连挂起时 transferTimeout 在部分平台不触发, 看门狗兜底
    if (!m_timeoutTimer) {
        m_timeoutTimer = std::make_unique<QTimer>(this);
        m_timeoutTimer->setSingleShot(true);
        m_timeoutTimer->setInterval(kNetworkTimeoutMs);
        connect(m_timeoutTimer.get(), &QTimer::timeout, this, [this]() {
            if (!m_reply || m_cancelled) return;   // 已收尾/已在取消路径
            if (m_reply->isFinished()) return;     // 数据已到, finished 已排队, 看门狗退场
            m_cancelled = true;                    // aborted 回复走静默清理分支
            m_reply->abort();
            emit fetchError("网络请求超时（30 秒未完成，请检查网络后重试）");
            emit finished();
        });
    }
    m_timeoutTimer->start();
    ++m_timerGeneration;   // 新请求看门狗已就绪，递增世代使旧延迟回调失效
}

void TleFetcher::onReplyFinished(QNetworkReply *reply)
{
    // 取消请求或已被新请求取代 → 静默忽略（不弹"网络错误"误报）
    if (m_cancelled) {
        // 双取消竞态: 本分支只清自身状态。旧取消请求的迟到 finished
        // 若在此被处理（m_reply 可能已指向新一轮请求）就无条件清除
        // m_cancelled/m_pendingTag, 会让紧随其后的第二轮取消 aborted
        // 回复误走"正常错误路径"弹出虚假的"网络错误"对话框。
        // → 仅当这是当前活动回复时才复位全局状态
        if (m_reply == reply) {
            m_cancelled = false;
            m_reply = nullptr;
            m_pendingTag.clear();
            // 本函数可能在 timeout→abort→finished 的同步重入链内被调用:
            // 此时销毁发送者 QTimer 属"槽内删除信号发送者"高风险模式
            //（Qt6 连接数据 COW 通常兜住, 非保证）——延迟到下一轮事件循环。
            // 指针+世代双守卫: 若延迟期间 startRequest 已重建看门狗,
            // 即使分配器复用同一地址（指针相等），世代号不等也会跳过误杀
            QTimer *timer = m_timeoutTimer.get();
            std::size_t gen = m_timerGeneration;
            QTimer::singleShot(0, this, [this, timer, gen]() {
                // 注意: 本回调可能被"随后的模态弹窗"(onTleError 的 QMessageBox)
                // 打开的**嵌套事件循环**提前执行, 而那正是 QTimer::timeout 的
                // 发射栈——此时 delete 发送者是危险模式。改用 deleteLater():
                // DeferredDelete 事件不会被内层事件循环提前投递
                if (m_timeoutTimer.get() == timer && m_timerGeneration == gen)
                    m_timeoutTimer.release()->deleteLater();
            });
        }
        reply->deleteLater();   // 修复: 取消分支此前漏删 reply，反复取消会累积泄漏
        return;
    }
    if (m_reply != reply) {
        reply->deleteLater();   // 迟到的旧请求，直接丢弃
        return;
    }
    m_reply = nullptr;
    m_timeoutTimer.reset();   // 当前请求已收尾, 看门狗失效

    // 先读取全部数据再调度 deleteLater，避免嵌套事件循环下悬垂指针
    const QString tag = m_pendingTag;
    m_pendingTag.clear();

    const QNetworkReply::NetworkError err = reply->error();
    if (err != QNetworkReply::NoError) {
        const int httpCode = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString errorMsg = reply->errorString();
        reply->deleteLater();
        // 限流/无数据给出可操作的专门提示（CelesTrak 使用政策 + 格式限制）
        if (httpCode == 403) {
            emit fetchError(
                "CelesTrak 暂时拒绝下载（HTTP 403）\n\n"
                "官方政策：GP 数据每 ~2 小时才更新一次，且同一组数据在两次更新之间\n"
                "只允许下载一次；重复请求会被临时封禁。\n"
                "建议：2 小时内直接使用本地缓存（取消勾选\"强制刷新\"），稍后再试。");
        } else if (httpCode == 404) {
            emit fetchError(
                "CelesTrak 未返回数据（HTTP 404）\n\n"
                "该编目号/组当前没有可用的 GP 数据。注意：TLE 文本格式不支持\n"
                "6 位编目号（≥100000）的目标，此类对象只能通过 OMM（CSV/JSON/XML）获取。");
        } else {
            emit fetchError(httpCode > 0
                ? QString("网络错误(HTTP %1): %2").arg(httpCode).arg(errorMsg)
                : QString("网络错误: %1").arg(errorMsg));
        }
        emit finished();
        return;
    }

    const QByteArray data = reply->readAll();
    const QString text = QString::fromUtf8(data).trimmed();
    reply->deleteLater();
    if (text.isEmpty()) {
        emit fetchError("服务器未返回数据（请检查组名或稍后重试）");
        emit finished();
        return;
    }
    // 成功数据落盘缓存: 供 2 小时内的重复请求复用（也便于限流期间继续工作）
    TleCache::write(tag, text);
    emit downloaded(text, tag);
    emit finished();
}
```

---

## business/TleFetcher.h

```cpp
#ifndef BUSINESS_TLEFETCHER_H
#define BUSINESS_TLEFETCHER_H
// =============================================================================
// TleFetcher.h — CelesTrak 在线 TLE 下载器
// 使用公开 API: https://celestrak.org/NORAD/elements/gp.php
//   组下载: ?GROUP=<id>&FORMAT=tle    单星: ?CATNR=<编号>&FORMAT=tle
// 返回 3LE 文本（名称行 + 两行根数），与批量导入解析器直接兼容
// 注意: 请遵守 CelesTrak 数据使用政策，避免高频轮询
// =============================================================================
#include <QObject>
#include <QVector>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QTimer>
#include <memory>

class QTimer;

class TleFetcher : public QObject {
    Q_OBJECT
public:
    struct Group {
        QString id;      // API 组标识
        QString label;   // 中文显示名
    };

    // CelesTrak 常用 GP 组
    static const QVector<Group> &groups();

    explicit TleFetcher(QObject *parent = nullptr);

    // 下载一组卫星的 3LE 文本（异步）
    // forceRefresh=false（默认）: 命中 2 小时内的本地缓存则直接复用，不发网络请求
    //   （CelesTrak 每 ~2 小时才更新，重复请求会 403 甚至被封 IP）
    // forceRefresh=true: 忽略缓存，强制联网下载
    void fetchGroup(const QString &groupId, bool forceRefresh = false);

    // 下载单个编目号卫星的 3LE 文本（异步；缓存语义同 fetchGroup）
    void fetchSatellite(int catnorID, bool forceRefresh = false);

    void cancel();

signals:
    // 下载成功: 原始 3LE 文本（UTF-8）
    void downloaded(QString rawText, QString groupId);
    // 命中本地缓存（未发网络请求）: tag 与缓存年龄（秒），供 UI 提示数据新鲜度
    void cacheServed(QString tag, qint64 ageSec);
    // 下载失败（网络错误/空数据）
    void fetchError(QString message);
    // 请求结束（无论成败，供 UI 复位状态）
    void finished();

private:
    void startRequest(const QUrl &url, const QString &tag);
    void onReplyFinished(QNetworkReply *reply);

    // 缓存命中时的延迟投递（保持 downloaded/finished 的异步时序，
    // 避免在按钮点击处理栈内同步弹出模态对话框）
    bool tryServeFromCache(const QString &tag, bool forceRefresh);

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    QString m_pendingTag;
    bool m_cancelled = false;   // 取消请求后静默忽略迟到回复
    bool m_cachePending = false;   // 已排队的缓存投递（cancel 需能撤销）
    QString m_cacheTag;            // 排队投递对应的 tag（被新请求取代时作废）
    // 超时看门狗: setTransferTimeout 在部分平台对 DNS 解析/建连挂起不生效,
    // 用单发定时器兜底——到期仍未完成即中止并以超时错误上报
    std::unique_ptr<QTimer> m_timeoutTimer;
    std::size_t m_timerGeneration{0};   // 延迟重置回调的世代守卫（防地址复用误杀新 timer）
};

#endif // BUSINESS_TLEFETCHER_H
```

---

## ui/CatmullRomSpline.h

```cpp
#ifndef UI_CATMULLROMSPLINE_H
#define UI_CATMULLROMSPLINE_H
// =============================================================================
// CatmullRom.h — 采样点平滑曲线工具（Catmull-Rom → 三次贝塞尔）
// 均匀参数 Catmull-Rom 经过所有控制点，用相邻四点的中点切线构造贝塞尔段:
//   段 i: P0=pi, C1=pi+(p(i+1)−p(i−1))/6, C2=p(i+1)−(p(i+2)−pi)/6, P3=p(i+1)
// 端点处边界点复制（p−1=p0, pn=p(n−1)），曲线保持插值且 C1 连续
// =============================================================================
#include <QPainterPath>
#include <QPointF>
#include <QVector>

namespace CatmullRomSpline {

// 将点列转成经过每个点的平滑 QPainterPath（不闭合成环）
// pts 少于 2 个时: 空集返回空路径; 单点段补一条 0.02px 微线段——
// 方位环绕/反经线断段后的孤立采样若生成纯 moveTo 空路径,
// 该点会在图上完全消失
inline QPainterPath path(const QVector<QPointF> &pts)
{
    QPainterPath p;
    const int n = pts.size();
    if (n == 0) return p;
    p.moveTo(pts[0]);
    if (n < 2) {
        p.lineTo(pts[0] + QPointF(0.02, 0.0));   // 微线段 → 画笔下呈圆点
        return p;
    }

    auto at = [&pts, n](int i) -> QPointF {
        if (i < 0)   return pts[0];
        if (i >= n)  return pts[n - 1];
        return pts[i];
    };

    for (int i = 0; i < n - 1; ++i) {
        const QPointF p0 = at(i - 1);
        const QPointF p1 = pts[i];
        const QPointF p2 = pts[i + 1];
        const QPointF p3 = at(i + 2);
        p.cubicTo(p1 + (p2 - p0) / 6.0,
                  p2 - (p3 - p1) / 6.0,
                  p2);
    }
    return p;
}

} // namespace CatmullRomSpline

#endif // UI_CATMULLROMSPLINE_H
```

---

## ui/DopplerPlotWidget.cpp

```cpp
// =============================================================================
// DopplerPlotWidget.cpp — 多普勒全程曲线实现
// 每条过境一条折线: 相对 AOS 的时间(秒) × 多普勒频移(kHz)
// 选中过境高亮（白线加粗），其余曲线半透明便于区分
// =============================================================================
#include "DopplerPlotWidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <cmath>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
// 曲线配色（与极坐标图风格一致，深色底）
const QColor kPalette[] = {
    QColor(79, 195, 247), QColor(255, 183, 77), QColor(129, 199, 132),
    QColor(229, 115, 115), QColor(186, 104, 200), QColor(77, 182, 172),
    QColor(255, 241, 118), QColor(161, 136, 127),
};

// 判断一次过境是否含多普勒曲线数据
bool hasDopplerData(const PassEvent &p)
{
    for (const auto &s : p.samples)
        if (s.dopplerKHz != 0.0) return true;
    return false;
}

// 某条曲线的 (X=相对AOS秒, Y=kHz) 点集
QVector<QPointF> curvePoints(const PassEvent &p)
{
    QVector<QPointF> pts;
    for (const auto &s : p.samples)
        pts.append(QPointF((s.jdMs - p.aosJdMs) / 1000.0, s.dopplerKHz));
    return pts;
}
} // namespace

DopplerPlotWidget::DopplerPlotWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(400, 260);
}

void DopplerPlotWidget::setPasses(const QVector<const PassEvent *> &passes)
{
    m_passes = passes;
    // 保留原选中行: 过滤切换后若选中越界则钳到末尾, 而非强制回到第 0 行
    if (passes.isEmpty())
        m_selectedIndex = -1;
    else if (m_selectedIndex >= passes.size())
        m_selectedIndex = passes.size() - 1;
    update();
}

void DopplerPlotWidget::setSelectedPass(int index)
{
    if (index >= 0 && index < m_passes.size()) {
        m_selectedIndex = index;
        update();
    }
}

void DopplerPlotWidget::clear()
{
    m_passes.clear();
    m_selectedIndex = -1;
    update();
}

// ============================================================================
// 绘制
// ============================================================================
void DopplerPlotWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(38, 42, 54));

    // 收集含数据曲线的过境
    QVector<int> curveIdx;   // m_passes 中的下标
    for (int i = 0; i < m_passes.size(); ++i)
        if (hasDopplerData(*m_passes[i])) curveIdx.append(i);
    if (curveIdx.isEmpty()) {
        painter.setPen(QColor(175, 182, 195));
        painter.drawText(rect(), Qt::AlignCenter, "无多普勒数据\n（载频为零或采样未启用）");
        return;
    }

    // 点集只计算一次（范围扫描与绘制共用），避免每条曲线重复遍历/分配
    // 缓存按「指针集合」失效: 点集只依赖过境数据, 与选中无关;
    // 数据未变的重绘（resize/遮挡/选中切换）直接复用
    // 数据未变的重绘（resize/遮挡/选中切换）直接复用
    if (m_cacheKeyPasses != m_passes) {
        m_curvePtsCache.resize(curveIdx.size());
        for (int i = 0; i < curveIdx.size(); ++i)
            m_curvePtsCache[i] = curvePoints(*m_passes[curveIdx[i]]);
        m_cacheKeyPasses = m_passes;
    }
    const auto &curvePts = m_curvePtsCache;

    // 数据范围: X = 0..maxDuration, Y = [min, max]
    double xMax = 0.0;
    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();
    for (const auto &pts : curvePts) {
        for (const auto &pt : pts) {
            xMax = std::max(xMax, pt.x());
            yMin = std::min(yMin, pt.y());
            yMax = std::max(yMax, pt.y());
        }
    }
    if (yMax - yMin < 1e-9) { yMin -= 0.5; yMax += 0.5; }
    const double yPad = (yMax - yMin) * 0.08;
    yMin -= yPad; yMax += yPad;
    if (xMax <= 0.0) xMax = 1.0;   // 除零防护: 全部数据点 x=0 的退化曲线
    const double xPad = xMax * 0.02;
    xMax += xPad;

    const QRectF plotArea(55, 20, width() - 70, height() - 70);
    auto mapX = [&](double x) {
        return plotArea.left() + (x / xMax) * plotArea.width();
    };
    auto mapY = [&](double y) {
        return plotArea.bottom() - ((y - yMin) / (yMax - yMin)) * plotArea.height();
    };

    // 网格与坐标轴
    painter.setPen(QPen(QColor(105, 113, 130), 1));
    const int yTicks = 5;
    for (int t = 0; t <= yTicks; ++t) {
        double y = yMin + (yMax - yMin) * t / yTicks;
        double sy = mapY(y);
        painter.drawLine(QPointF(plotArea.left(), sy), QPointF(plotArea.right(), sy));
        painter.setPen(QColor(195, 202, 215));
        painter.drawText(QRectF(0, sy - 8, plotArea.left() - 5, 16),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(y, 'f', 1));
        painter.setPen(QPen(QColor(105, 113, 130), 1));
    }
    const int xTicks = 5;
    for (int t = 0; t <= xTicks; ++t) {
        double x = xMax * t / xTicks;
        double sx = mapX(x);
        painter.drawLine(QPointF(sx, plotArea.top()), QPointF(sx, plotArea.bottom()));
        painter.setPen(QColor(195, 202, 215));
        painter.drawText(QRectF(sx - 25, plotArea.bottom() + 6, 50, 16),
                         Qt::AlignCenter, QString::number(x, 'f', 0));
        painter.setPen(QPen(QColor(105, 113, 130), 1));
    }
    painter.setPen(QColor(215, 222, 235));
    painter.drawText(QRectF(plotArea.left(), 2, plotArea.width(), 14),
                     Qt::AlignHCenter, "多普勒频移（kHz，相对过境入境时刻）");

    // 零频参考线（中心频率 → 多普勒偏移 0）
    if (yMin < 0.0 && yMax > 0.0) {
        painter.setPen(QPen(QColor(255, 255, 255, 90), 1, Qt::DashLine));
        painter.drawLine(QPointF(plotArea.left(), mapY(0.0)),
                         QPointF(plotArea.right(), mapY(0.0)));
    }

    // 曲线
    const bool hasSel = (m_selectedIndex >= 0 && m_selectedIndex < m_passes.size()
                         && hasDopplerData(*m_passes[m_selectedIndex]));
    for (int i = 0; i < curveIdx.size(); ++i) {
        const int idx = curveIdx[i];
        const bool isSel = (idx == m_selectedIndex);
        QColor col = kPalette[i % (sizeof(kPalette) / sizeof(kPalette[0]))];
        if (hasSel && !isSel) {
            col.setAlpha(150);
            painter.setPen(QPen(col, 1.5));
        } else if (isSel) {
            painter.setPen(QPen(QColor(255, 255, 255), 2.6));
        } else {
            painter.setPen(QPen(col, 1.8));
        }

        const QVector<QPointF> &pts = curvePts[i];
        QPolygonF poly;
        for (const auto &pt : pts)
            poly.append(QPointF(mapX(pt.x()), mapY(pt.y())));
        if (!poly.isEmpty()) painter.drawPolyline(poly);

        // 选中过境: 图例 + 极值标注
        if (isSel) {
            double yPeak = pts.isEmpty() ? 0.0 : pts[0].y();
            for (const auto &pt : pts) {
                if (std::abs(pt.y()) > std::abs(yPeak)) yPeak = pt.y();
            }
            painter.setPen(QColor(220, 220, 220));
            painter.drawText(QRectF(plotArea.left(), plotArea.bottom() + 22,
                                    plotArea.width(), 16),
                             Qt::AlignHCenter,
                             QString("%1   峰值频移: %2 kHz")
                                 .arg(QString::fromStdString(m_passes[idx]->satelliteName))
                                 .arg(yPeak, 0, 'f', 2));
        }
    }
}
```

---

## ui/DopplerPlotWidget.h

```cpp
#ifndef UI_DOPPLERPLOTWIDGET_H
#define UI_DOPPLERPLOTWIDGET_H
// =============================================================================
// DopplerPlotWidget.h — 多普勒全程曲线图
// 数据: PassEvent::samples[].dopplerKHz（过境弧段等步长采样）
// X 轴: 相对各过境入境时刻的时间（秒）; Y 轴: 多普勒频移（kHz）
// =============================================================================
#include <QWidget>
#include <QVector>
#include <QPointF>
#include "core/DataTypes.h"

class DopplerPlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit DopplerPlotWidget(QWidget *parent = nullptr);

    // 共享数据指针（指向 MainWindow 持有的过境集合, 不深拷贝采样向量）
    void setPasses(const QVector<const PassEvent *> &passes);
    void setSelectedPass(int index);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<const PassEvent *> m_passes;
    int m_selectedIndex = -1;

    // 数据点集缓存: paintEvent 每帧重建（每条曲线几百点 × 批量数百曲线 =
    // 每帧数千次分配）; 以「指针集合」为版本键（点集只依赖过境数据）,
    // 数据未变时复用
    QVector<QVector<QPointF>> m_curvePtsCache;
    QVector<const PassEvent *> m_cacheKeyPasses;
};

#endif // UI_DOPPLERPLOTWIDGET_H
```

---

## ui/GroundTrackWidget.cpp

```cpp
// =============================================================================
// GroundTrackWidget.cpp — 地面轨迹图实现
// =============================================================================
#include "GroundTrackWidget.h"
#include "CatmullRomSpline.h"
#include <QPainter>
#include <QPainterPath>
#include <QFile>
#include <QTextStream>
#include <cmath>

GroundTrackWidget::GroundTrackWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(400, 250);
    // 陆地轮廓推迟到首次 paint 时再解析（72KB/5.3k 行, 毫秒级）:
    // 不占用窗口构造路径, 主窗口 show() 更早返回
}

// ============================================================================
// loadLandData — 从 qrc 资源加载 Natural Earth 110m 陆地多边形
// 格式: "P 名称" 开始一个多边形, 随后每行 "lat lon"（度，一位小数即 ~11km
// 精度，适合 110m 比例尺），"E" 结束。注: 南极洲多边形在 180°/−180° 处
// 存在经度跳变, 但其接缝恰好沿纬度 -90°（地图底边）投影后不可见——
// 若引入其他跨经线数据集须另行分段处理
// ============================================================================
bool GroundTrackWidget::loadLandData()
{
    QFile f(":/land110m.txt");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    QPolygonF cur;
    QString line;
    while (!(line = in.readLine()).isNull()) {
        line = line.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith('P')) {
            if (!cur.isEmpty()) m_landPolygons.append(cur);
            cur.clear();
        } else if (line.startsWith('E')) {
            if (!cur.isEmpty()) m_landPolygons.append(cur);
            cur.clear();
        } else {
            const QStringList parts = line.split(' ');
            if (parts.size() >= 2) {
                bool ok1 = false, ok2 = false;
                const double lat = parts[0].toDouble(&ok1);
                const double lon = parts[1].toDouble(&ok2);
                if (ok1 && ok2) cur.append(QPointF(lat, lon));
            }
        }
    }
    if (!cur.isEmpty()) m_landPolygons.append(cur);
    return !m_landPolygons.isEmpty();
}

void GroundTrackWidget::setPasses(const QVector<const PassEvent *> &passes,
                                   double observerLatDeg, double observerLonDeg)
{
    m_passes = passes;
    m_obsLat = observerLatDeg;
    m_obsLon = observerLonDeg;
    // 保留原选中行: 过滤切换后若选中越界则钳到末尾, 而非强制回到第 0 行
    if (passes.isEmpty())
        m_selectedIndex = -1;
    else if (m_selectedIndex >= passes.size())
        m_selectedIndex = passes.size() - 1;
    update();
}

void GroundTrackWidget::setSelectedPass(int index)
{
    if (index >= 0 && index < m_passes.size()) {
        m_selectedIndex = index;
        update();
    }
}

void GroundTrackWidget::clear()
{
    m_passes.clear();
    m_selectedIndex = -1;
    update();
}

// 单条过境的地面轨迹路径（供缓存复用; 静态: 不依赖成员状态）
QPainterPath GroundTrackWidget::buildTrackPath(const PassEvent &p,
                                               double x0, double y0, double sx, double sy) const
{
    QPainterPath trackPath;
    if (p.samples.size() >= 2) {
        // 真实地面轨迹: Catmull-Rom 平滑曲线（经过所有采样点）
        // 跨越 ±180° 经线处断开分段平滑，避免整幅横穿伪影
        auto screenPt = [&](const PassSample &s) {
            return geoToScreen(s.geo.latDeg, s.geo.lonDeg, x0, y0, sx, sy);
        };
        QVector<QPointF> seg;
        for (size_t k = 0; k < p.samples.size(); ++k) {
            if (!seg.isEmpty()) {
                const double dlon = std::abs(p.samples[k].geo.lonDeg
                                             - p.samples[k - 1].geo.lonDeg);
                if (dlon > 180.0) {       // 跨经线: 断开
                    trackPath.addPath(CatmullRomSpline::path(seg));
                    seg.clear();
                }
            }
            seg.append(screenPt(p.samples[k]));
        }
        trackPath.addPath(CatmullRomSpline::path(seg));
    } else {
        // 回退: AOS → TCA → LOS 二次贝塞尔近似（无采样数据时）
        trackPath.moveTo(geoToScreen(p.aosSatGeo.latDeg, p.aosSatGeo.lonDeg, x0, y0, sx, sy));
        trackPath.quadTo(geoToScreen(p.tcaSatGeo.latDeg, p.tcaSatGeo.lonDeg, x0, y0, sx, sy),
                         geoToScreen(p.losSatGeo.latDeg, p.losSatGeo.lonDeg, x0, y0, sx, sy));
    }
    return trackPath;
}

QPointF GroundTrackWidget::geoToScreen(double latDeg, double lonDeg,
                                        double x0, double y0, double sx, double sy) const
{
    // 等矩形投影: lat → y (北=上=小), lon → x (东=右=大)
    double x = x0 + (lonDeg + 180.0) * sx;
    double y = y0 + (90.0 - latDeg) * sy;
    return QPointF(x, y);
}

void GroundTrackWidget::paintEvent(QPaintEvent *)
{
    if (!m_landLoaded) {
        m_landLoaded = true;          // 只尝试一次（失败不反复重试）
        loadLandData();               // 首帧解析陆地轮廓, 之后不再触发
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    double w = width(), h = height();
    double margin = 40;
    double x0 = margin, y0 = margin;
    double plotW = w - 2 * margin, plotH = h - 2 * margin;
    double sx = plotW / 360.0;   // 经度 → 像素
    double sy = plotH / 180.0;   // 纬度 → 像素

    // 背景（亮色系, 保证深色轨迹清晰可见）
    painter.fillRect(rect(), QColor(30, 36, 50));

    // 网格线（每 30°）
    painter.setPen(QPen(QColor(95, 105, 125), 1));
    for (int lat = -90; lat <= 90; lat += 30) {
        double y = y0 + (90.0 - lat) * sy;
        painter.drawLine(QPointF(x0, y), QPointF(x0 + plotW, y));
    }
    for (int lon = -180; lon <= 180; lon += 30) {
        double x = x0 + (lon + 180.0) * sx;
        painter.drawLine(QPointF(x, y0), QPointF(x, y0 + plotH));
    }

    // 赤道加粗
    painter.setPen(QPen(QColor(140, 150, 170), 2));
    {
        double yEq = y0 + 90.0 * sy;
        painter.drawLine(QPointF(x0, yEq), QPointF(x0 + plotW, yEq));
    }

    // 经纬度标签
    painter.setPen(QColor(190, 198, 212));
    QFont labelFont = painter.font();
    labelFont.setPointSize(8);
    painter.setFont(labelFont);
    for (int lat = -60; lat <= 60; lat += 30) {
        double y = y0 + (90.0 - lat) * sy;
        painter.drawText(QPointF(x0 - 35, y + 4), QString::number(lat) + "°");
    }
    for (int lon = -180; lon <= 180; lon += 60) {
        double x = x0 + (lon + 180.0) * sx;
        painter.drawText(QPointF(x - 15, y0 + plotH + 15), QString::number(lon) + "°");
    }

    // 真实大陆轮廓（Natural Earth 110m，qrc 资源加载）
    // 先于过境轨迹绘制，保证轨迹线不被大陆遮挡
    painter.setPen(QPen(QColor(120, 130, 148), 1));
    painter.setBrush(QColor(58, 68, 86));
    const qreal dpr = devicePixelRatioF();
    if (m_projectedSize != QSize(w, h) || m_projectedDpr != dpr) {
        // 尺寸/DPR 变化（首帧/resize/跨不同缩放显示器拖动）: 重建投影缓存
        //（DPR 不入键则逻辑尺寸不变而物理尺寸变, 大陆轮廓错位拉伸）
        m_projectedLand.clear();
        m_projectedLand.reserve(m_landPolygons.size());
        for (const auto &poly : m_landPolygons) {
            if (poly.size() < 3) continue;
            QPolygonF screen;
            screen.reserve(poly.size());
            for (const QPointF &pt : poly)
                screen.append(geoToScreen(pt.x(), pt.y(), x0, y0, sx, sy));
            m_projectedLand.append(screen);
        }
        m_projectedSize = QSize(w, h);
        m_projectedDpr = dpr;
    }
    for (const auto &screen : m_projectedLand)
        painter.drawPolygon(screen);

    // 地面站标注（无数据时不画——空任务时坐标保持默认 (0,0) 会在大西洋
    // 画一个虚假站点, 容易被误认为真实观测站）
    if (!m_passes.isEmpty()) {
        QPointF obsP = geoToScreen(m_obsLat, m_obsLon, x0, y0, sx, sy);
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(Qt::black);
        painter.drawRect(QRectF(obsP.x() - 5, obsP.y() - 5, 10, 10));
        painter.drawText(obsP + QPointF(8, -8), "站");
    }

    // 绘制过境地面轨迹
    QVector<QColor> trackColors = {
        QColor(95, 200, 255), QColor(255, 180, 75),
        QColor(115, 255, 125), QColor(255, 115, 115)
    };

    // 轨迹路径缓存: 数据或尺寸未变时复用（每帧重建数百条 Catmull-Rom
    // 轨迹在选中切换/resize 重绘风暴下开销明显, 与陆地投影缓存同理）
    if (m_pathKeyPasses != m_passes || m_pathKeySize != QSize(w, h)) {
        m_trackPathCache.resize(m_passes.size());
        for (int pi = 0; pi < m_passes.size(); ++pi)
            m_trackPathCache[pi] = buildTrackPath(*m_passes[pi], x0, y0, sx, sy);
        m_pathKeyPasses = m_passes;
        m_pathKeySize = QSize(w, h);
    }

    for (int pi = 0; pi < m_passes.size(); ++pi) {
        const PassEvent *p = m_passes[pi];
        bool isSelected = (pi == m_selectedIndex);
        QColor col = trackColors[pi % trackColors.size()];
        if (!isSelected) col.setAlpha(160);

        const QPainterPath &trackPath = m_trackPathCache[pi];

        if (isSelected) {
            // 选中路径光晕（先宽后窄，突出主轨迹）
            QColor glow = col;
            glow.setAlpha(70);
            painter.setPen(QPen(glow, 9));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(trackPath);
        }
        painter.setPen(QPen(col, isSelected ? 3.5 : 1.8));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(trackPath);

        if (isSelected) {
            // 特征点标记（仅选中过境, 三点投影开销可忽略）
            const QPointF aosP = geoToScreen(p->aosSatGeo.latDeg, p->aosSatGeo.lonDeg, x0, y0, sx, sy);
            const QPointF tcaP = geoToScreen(p->tcaSatGeo.latDeg, p->tcaSatGeo.lonDeg, x0, y0, sx, sy);
            const QPointF losP = geoToScreen(p->losSatGeo.latDeg, p->losSatGeo.lonDeg, x0, y0, sx, sy);
            painter.setPen(QPen(QColor(15, 20, 28), 1));
            painter.setBrush(QColor(0, 255, 200));
            painter.drawEllipse(aosP, 5, 5);
            painter.setBrush(QColor(255, 255, 0));
            painter.drawEllipse(tcaP, 5, 5);
            painter.setBrush(QColor(255, 90, 90));
            painter.drawEllipse(losP, 5, 5);

            painter.setPen(Qt::white);
            painter.drawText(aosP + QPointF(8, -6), "AOS");
            painter.drawText(tcaP + QPointF(8, -6), "TCA");
            painter.drawText(losP + QPointF(8, -6), "LOS");
        }
    }

    // 如果没有过境，显示提示
    if (m_passes.isEmpty()) {
        painter.setPen(QColor(175, 182, 195));
        // 基于应用字体只调字号（硬编码字体会绕过 main.cpp 的全局
        // 字体设定, 中文文案经 Arial 回退渲染风格漂移）
        QFont hintFont = painter.font();
        hintFont.setPointSize(12);
        painter.setFont(hintFont);
        painter.drawText(QRectF(x0, y0, plotW, plotH),
                         Qt::AlignCenter, "暂无过境数据");
    }
}
```

---

## ui/GroundTrackWidget.h

```cpp
#ifndef UI_GROUNDTRACKWIDGET_H
#define UI_GROUNDTRACKWIDGET_H
// =============================================================================
// GroundTrackWidget.h — 二维地面轨迹图
// 等矩形投影（横轴=经度，纵轴=纬度），QPainter 纯绘制
// =============================================================================
#include <QWidget>
#include <QVector>
#include <QPolygonF>
#include <QPainterPath>
#include "core/DataTypes.h"

class GroundTrackWidget : public QWidget {
    Q_OBJECT
public:
    explicit GroundTrackWidget(QWidget *parent = nullptr);

    // 共享数据指针（指向 MainWindow 持有的过境集合, 不深拷贝采样向量）
    void setPasses(const QVector<const PassEvent *> &passes,
                   double observerLatDeg, double observerLonDeg);
    void setSelectedPass(int index);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<const PassEvent *> m_passes;
    int m_selectedIndex = -1;
    double m_obsLat = 0.0, m_obsLon = 0.0;

    // 真实大陆轮廓（Natural Earth 110m，经度在 [-180,180] 内，无需断片）
    QVector<QPolygonF> m_landPolygons;   // 点 = (latDeg, lonDeg)
    bool m_landLoaded = false;           // 首次 paint 惰性加载标记
    // 投影缓存: 每帧重投影数百个多边形在重绘风暴（resize/遮挡）下开销明显,
    // 尺寸不变时复用缓存结果
    QVector<QPolygonF> m_projectedLand;
    QSize m_projectedSize;               // 投影时的 widget 尺寸（变化即失效）
    qreal m_projectedDpr = 0.0;          // 投影时的设备像素比（跨不同缩放显示器拖动时失效重建）

    // 轨迹路径缓存: 与陆地投影缓存同理, 以「指针集合 + 尺寸」为版本键
    QVector<QPainterPath> m_trackPathCache;
    QVector<const PassEvent *> m_pathKeyPasses;
    QSize m_pathKeySize;

    bool loadLandData();                 // 从 :/land110m.txt 加载一次
    QPointF geoToScreen(double latDeg, double lonDeg,
                        double x0, double y0, double sx, double sy) const;

    // 单条过境的轨迹路径构建（供缓存复用）
    QPainterPath buildTrackPath(const PassEvent &p,
                                double x0, double y0, double sx, double sy) const;
};

#endif // UI_GROUNDTRACKWIDGET_H
```

---

## ui/MainWindow.cpp

```cpp
// =============================================================================
// MainWindow.cpp — 主窗口完整实现
// =============================================================================
#include "MainWindow.h"
#include "PolarPlotWidget.h"
#include "GroundTrackWidget.h"
#include "DopplerPlotWidget.h"
#include "business/TleBatchParser.h"
#include "core/TleParser.h"
#include "core/CoordTransform.h"
#include "core/ErrorHandler.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QScrollBar>
#include <QSplitter>
#include <QTimeZone>
#include <QCloseEvent>
#include <QtConcurrent>   // 导出文件写入异步化（Qt6 中 QtConcurrent 是独立模块, XGZ.pro 已 QT += concurrent）
#include <QFutureWatcher>
#include <QApplication>     // processEvents
#include <QMenu>            // 过境列表右键菜单（选中某颗卫星的全部过境）
#include <QItemSelectionModel>   // 多选: 导出范围取表格选中行
#include <cmath>          // std::abs（KML 反经线跨越断线）
#include <algorithm>      // std::sort/std::unique（选中行去重排序）
#include <vector>
#include <iterator>       // std::size（列宽数组 static_assert）

// ============================================================================
// TLE 文本入口共用工具（文件局部）
// ============================================================================
namespace {
// TLE 龄期告警阈值（天）: 行业惯例 >14 天预报误差可达数十秒，需醒目提示。
// 单星模式在历元标签标红；批量模式累积超期颗数并汇总（见 onMetaInfo）
constexpr double kStaleAgeDays = 14.0;

// 剥离 UTF-8 BOM（U+FEFF, Windows 记事本"UTF-8 (BOM)"保存常见）:
// U+FEFF 不属于空白字符, trimmed() 不剥; 残留时首行变 "\uFEFF1 ..."
// 无法匹配行号前缀 "1 ", 首颗卫星被误判缺行而丢弃
QString stripUtf8Bom(QString s)
{
    if (s.startsWith(QChar(0xFEFF)))
        s.remove(0, 1);
    return s;
}
} // namespace

// ============================================================================
// 批量 TLE 导入辅助（文件局部）
// ============================================================================
namespace {

// 批量导入对话框：多行 TLE 文本 + 从文件加载
class BatchTleDialog : public QDialog {
    Q_OBJECT
public:
    explicit BatchTleDialog(QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("批量导入 TLE");
        resize(760, 520);

        auto *lay = new QVBoxLayout(this);
        auto *hint = new QLabel(
            "每颗卫星：可选名称行 + TLE 两行根数（也可直接两行），可粘贴多颗。\n"
            "支持从 Celestrak / NORAD 下载的文本块（含注释头时自动忽略）。");
        hint->setWordWrap(true);
        lay->addWidget(hint);

        m_edit = new QPlainTextEdit;
        m_edit->setPlaceholderText(
            "ISS (ZARYA)\n"
            "1 25544U 98067A   24083.56167013  .00012345  00000-0  24655-3 0  9994\n"
            "2 25544  51.6423  23.4567 0004567 123.4567 236.7890 15.50012345678901\n"
            "\n"
            "STARLINK-1001\n"
            "1 44713U 19074A   24083.91234567  .00000000  00000-0  00000+0 0  1234\n"
            "2 44713  53.0541  88.1234 0001234  45.6789 314.5678 15.06323456789012");
        lay->addWidget(m_edit, 1);

        auto *btnRow = new QHBoxLayout;
        auto *loadBtn = new QPushButton("从文件加载");
        auto *okBtn = new QPushButton("开始批量预报");
        okBtn->setDefault(true);
        auto *cancelBtn = new QPushButton("取消");
        btnRow->addWidget(loadBtn);
        btnRow->addStretch();
        btnRow->addWidget(okBtn);
        btnRow->addWidget(cancelBtn);
        lay->addLayout(btnRow);

        connect(loadBtn, &QPushButton::clicked, this, [this]() {
            const QString path = QFileDialog::getOpenFileName(
                this, "选择 TLE 文件", QString(),
                "Text Files (*.txt);;All Files (*)");
            if (path.isEmpty()) return;
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QMessageBox::warning(this, "批量导入", "无法读取文件: " + path);
                return;
            }
            m_edit->setPlainText(stripUtf8Bom(QString::fromUtf8(f.readAll())));
        });
        connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    }

    QString text() const { return m_edit->toPlainText(); }
    void setText(const QString &t) { m_edit->setPlainText(t); }

private:
    QPlainTextEdit *m_edit;
};

// ============================================================================
// 导出文件写入器（QtConcurrent 线程池执行; 输入为共享过境指针集,
// 只读解引用——导出期间所有数据变更入口被 startExport 锁定, 指针必然有效;
// 返回错误串, 空串=成功; 不触碰任何 UI 成员, 无跨线程数据竞争）
// ============================================================================
QString writePassesCsv(const QVector<const PassEvent *> &passes, const QString &path,
                       const QAtomicInt *cancelFlag = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("无法写入文件: %1").arg(path);
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);
    out.setLocale(QLocale::c());   // 修复: 默认跟随系统 locale, 逗号小数点的
                                   // 区域设置下 CSV 数字会被破坏为 "40,5"

    // 表头
    out << "#,卫星名,入境(UTC),出境(UTC),持续(s),最大仰角(°),方位角(°),"
           "可见性,太阳高度(°),卫星光照,可见弧段起点(UTC),可见弧段终点(UTC),"
           "入境纬度(°),入境经度(°),入境高度(km),"
           "斜距(km),多普勒(kHz),星等,质量,AOS光照,TCA光照,LOS光照\n";

    // 导出文件里的 UTC 时间串 + RFC4180 引号，格式 "yyyy-MM-dd_HH:mm:ss"。
    // 为什么用下划线而不是空格: Excel/WPS 会把 "2026-09-19 07:24:29" 这种写法解析成
    // **日期值**，并按默认格式 yyyy/m/d h:mm 显示——秒被隐藏，同一分钟的多行看起来时间
    // 完全相同，从 Excel 另存还会真正丢秒（实测 A2.NumberFormat='yyyy/m/d h:mm'、
    // A2.Value2=46284.3…日期序列号；引号挡不住这种类型转换）。
    // 实测在 Excel 中保持纯文本的写法有三种: "2026-09-19_07:24:29"、"… 07:24:29 UTC"、
    // ISO-8601 "2026-09-19T07:24:29Z"；取**不带字母**的下划线写法，既原样显示又好读。
    // pandas / MATLAB: 直接按 "%Y-%m-%d_%H:%M:%S" 解析，或把下划线替换成空格。
    auto jdToStr = [](int64_t jdMs) -> QString {
        return "\"" + QDateTime::fromMSecsSinceEpoch(
                   jdMs - kJdEpochOffsetMs, QTimeZone::UTC)
                   .toString("yyyy-MM-dd_HH:mm:ss") + "\"";
    };
    auto illumStr = [](double f) -> QString {
        if (f <= 0.0)      return "0%";
        if (f >= 1.0)      return "100%";
        return QString("%1%").arg(static_cast<int>(f * 100.0));
    };
    // CSV 字段转义: 卫星名可能含逗号/引号/换行，按 RFC 4180 包裹引号
    auto csvField = [](const QString &s) -> QString {
        return "\"" + QString(s).replace('"', "\"\"") + "\"";
    };

    for (int i = 0; i < passes.size(); ++i) {
        if (cancelFlag && cancelFlag->loadAcquire() != 0)
            return QStringLiteral("导出已取消");
        const PassEvent *p = passes[i];
        // 可见性文字
        QString visStr = "白天";
        if (p->visibility == PassVisibility::Visible) visStr = "可见";
        else if (p->visibility == PassVisibility::RadioOnly) visStr = "无线电";
        out << (i + 1) << ","
            << csvField(QString::fromStdString(p->satelliteName)) << ","
            << jdToStr(p->aosJdMs) << ","
            << jdToStr(p->losJdMs) << ","
            << p->durationSec << ","
            << p->maxElevDeg << ","
            << p->tcaLook.azDeg << ","
            << visStr << ","
            << p->tcaSunAlt << ","
            << ((p->tcaSatShadow == ShadowStatus::FullSunlight) ? "日照" :
                (p->tcaSatShadow == ShadowStatus::InPenumbra) ? "半影" : "本影") << ","
            << (p->hasVisibleArc ? jdToStr(p->visAosJdMs) : "") << ","
            << (p->hasVisibleArc ? jdToStr(p->visLosJdMs) : "") << ","
            << p->aosSatGeo.latDeg << ","
            << p->aosSatGeo.lonDeg << ","
            << p->aosSatGeo.altKm << ","
            << p->tcaLook.rangeKm << ","
            << p->dopplerAtTcaKHz << ","
            << (p->magValid ? QString::number(p->magAtTca, 'f', 1) : "") << ","
            << ((p->quality == PassQuality::Excellent) ? "优秀" :
                (p->quality == PassQuality::Good) ? "良好" : "一般") << ","
            << illumStr(p->aosIllum) << ","
            << illumStr(p->tcaIllum) << ","
            << illumStr(p->losIllum) << "\n";
    }
    // 写盘结果校验: 磁盘满/IO 错误时缓冲可能未落盘, 静默返回成功会生成
    // 空或半截文件且 UI 报"导出成功"
    out.flush();
    if (out.status() != QTextStream::Ok || file.error() != QFileDevice::NoError)
        return QStringLiteral("CSV 写入失败（磁盘空间不足或 IO 错误）: %1").arg(path);
    return QString();
}

QString writePassesKml(const QVector<const PassEvent *> &passes, const QString &path,
                       double taskLatDeg, double taskLonDeg,
                       const QAtomicInt *cancelFlag = nullptr)
{
    // KML 是 XML：卫星名可能含 & < > 等特殊字符，必须转义
    //（名称来自 TLE 名称行或用户粘贴文本，不可信）
    auto xmlEsc = [](const QString &s) { return s.toHtmlEscaped(); };

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("无法写入文件: %1").arg(path);
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    // 坐标数值恒用 '.' 小数点（KML 规范要求）——QTextStream 浮点输出
    // 默认跟随 locale, 逗号小数区域会产出 "116,4074" 非法坐标。
    // 与 CSV 写入器的 setLocale 同源修复
    out.setLocale(QLocale::c());

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n"
        << "<Document>\n"
        << "<name>XGZ Satellite Passes</name>\n";

    // 标注地面站（使用任务锁定坐标, 与图表/轨迹标记一致, 而非当前输入框值）
    out << "<Placemark>\n<name>Ground Station</name>\n"
        << "<Point><coordinates>"
        << taskLonDeg << "," << taskLatDeg << ",0"
        << "</coordinates></Point>\n</Placemark>\n";

    for (int i = 0; i < passes.size(); ++i) {
        if (cancelFlag && cancelFlag->loadAcquire() != 0)
            return QStringLiteral("导出已取消");
        const PassEvent *p = passes[i];
        QString desc = QString("Duration: %1 s; Max elev: %2°")
            .arg(p->durationSec).arg(p->maxElevDeg, 0, 'f', 3);
        if (p->hasVisibleArc) {
            desc += QString("; Visible: %1 – %2")
                .arg(QDateTime::fromMSecsSinceEpoch(p->visAosJdMs - kJdEpochOffsetMs,
                                                    QTimeZone::UTC)
                         .toString("HH:mm:ss"))
                .arg(QDateTime::fromMSecsSinceEpoch(p->visLosJdMs - kJdEpochOffsetMs,
                                                    QTimeZone::UTC)
                         .toString("HH:mm:ss"));
        }
        if (p->magValid)
            desc += QString("; Mag: %1").arg(p->magAtTca, 0, 'f', 1);
        out << "<Placemark>\n"
            << "<name>Pass " << (i + 1) << ": "
            << xmlEsc(QString::fromStdString(p->satelliteName)) << " "
            << p->maxElevDeg << "°</name>\n"
            << "<description>" << xmlEsc(desc) << "</description>\n";
        // 优先绘制真实采样弧段；无采样时回退 AOS-TCA-LOS 三点。
        // ±180° 反经线跨越断线: 相邻点经度差 >180° 时 KML 会画成横穿
        // 整幅地图的长直线伪影——按跨越点切分为多个 LineString
        //（与 GroundTrackWidget/PolarPlotWidget 的断线策略一致）
        QVector<QVector<QPair<double, double>>> segs;
        segs.append(QVector<QPair<double, double>>());
        auto appendPt = [&segs](double lon, double lat) {
            // 注意: 绝不能在 segs.append() 之前取 segs.last() 的引用——
            // append 可能触发 QVector 扩容重分配, 旧引用随即悬垂,
            // 之后再 cur.append() 就是往已释放内存里写（堆破坏）。
            // 每一次访问都重新取 last(), 且跨越反经线的点作为**新段首点**
            if (!segs.last().isEmpty()
                && std::abs(lon - segs.last().last().first) > 180.0)
                segs.append(QVector<QPair<double, double>>());
            segs.last().append(qMakePair(lon, lat));
        };
        if (p->samples.size() >= 2) {
            for (const auto &s : p->samples)
                appendPt(s.geo.lonDeg, s.geo.latDeg);
        } else {
            appendPt(p->aosSatGeo.lonDeg, p->aosSatGeo.latDeg);
            appendPt(p->tcaSatGeo.lonDeg, p->tcaSatGeo.latDeg);
            appendPt(p->losSatGeo.lonDeg, p->losSatGeo.latDeg);
        }
        for (const auto &seg : segs) {
            if (seg.size() < 2) continue;   // 单点无法成线
            out << "<LineString><coordinates>";
            for (const auto &pt : seg)
                out << pt.first << "," << pt.second << ",0 ";
            out << "</coordinates></LineString>\n";
        }
        out << "</Placemark>\n";
    }
    out << "</Document>\n</kml>\n";
    // 写盘结果校验（同 CSV）: 磁盘满/IO 错误不得静默报成功
    out.flush();
    if (out.status() != QTextStream::Ok || file.error() != QFileDevice::NoError)
        return QStringLiteral("KML 写入失败（磁盘空间不足或 IO 错误）: %1").arg(path);
    return QString();
}

QString writePassesJson(const QVector<const PassEvent *> &passes, const QString &path,
                        const QAtomicInt *cancelFlag = nullptr)
{
    QJsonArray arr;
    for (const PassEvent *p : passes) {
        if (cancelFlag && cancelFlag->loadAcquire() != 0)
            return QStringLiteral("导出已取消");
        QJsonObject obj;
        obj["satellite"]   = QString::fromStdString(p->satelliteName);
        obj["aosJdMs"]     = static_cast<qint64>(p->aosJdMs);
        obj["losJdMs"]     = static_cast<qint64>(p->losJdMs);
        obj["tcaJdMs"]     = static_cast<qint64>(p->tcaJdMs);
        obj["maxElevDeg"]  = p->maxElevDeg;
        obj["durationSec"] = p->durationSec;
        obj["visibility"]  = static_cast<int>(p->visibility);
        obj["quality"]     = static_cast<int>(p->quality);
        obj["dopplerKHz"]  = p->dopplerAtTcaKHz;
        obj["aosSunAlt"]   = p->aosSunAlt;
        obj["aosSatLit"]   = static_cast<int>(p->aosSatShadow);
        obj["aosIllum"]    = p->aosIllum;
        obj["tcaIllum"]    = p->tcaIllum;
        obj["losIllum"]    = p->losIllum;
        obj["aosLat"]      = p->aosSatGeo.latDeg;
        obj["aosLon"]      = p->aosSatGeo.lonDeg;
        obj["aosAltKm"]    = p->aosSatGeo.altKm;
        obj["aosAzDeg"]    = p->aosLook.azDeg;
        obj["aosRangeKm"]  = p->aosLook.rangeKm;
        obj["hasVisibleArc"] = p->hasVisibleArc;
        if (p->hasVisibleArc) {
            obj["visAosJdMs"] = static_cast<qint64>(p->visAosJdMs);
            obj["visLosJdMs"] = static_cast<qint64>(p->visLosJdMs);
        }
        obj["magValid"] = p->magValid;
        if (p->magValid) obj["magAtTca"] = p->magAtTca;

        // 逐秒采样数据（供外部工具分析）
        if (!p->samples.empty()) {
            QJsonArray sampleArr;
            for (const auto &s : p->samples) {
                QJsonObject so;
                so["jdMs"]       = static_cast<qint64>(s.jdMs);
                so["elevDeg"]    = s.look.elevDeg;
                so["azDeg"]      = s.look.azDeg;
                so["rangeKm"]    = s.look.rangeKm;
                so["dopplerKHz"] = s.dopplerKHz;
                sampleArr.append(so);
            }
            obj["samples"] = sampleArr;
        }

        arr.append(obj);
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("无法写入文件: %1").arg(path);
    if (file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented)) < 0)
        return QStringLiteral("JSON 写入失败（磁盘空间不足或 IO 错误）: %1").arg(path);
    file.flush();
    if (file.error() != QFileDevice::NoError)
        return QStringLiteral("JSON 写入失败（磁盘空间不足或 IO 错误）: %1").arg(path);
    return QString();
}

// ---------------------------------------------------------------------------
// 逐秒（1 s）详情：详情页签与"含逐秒详情"CSV 导出共用同一套计算，
// 保证页签里看到的数据与导出文件逐点一致
// ---------------------------------------------------------------------------

// 逐秒重传播参数（页面参数快照）: 工作线程只读按值捕获的副本，
// 不跨线程访问 MainWindow 成员
struct DetailPropagationParams {
    std::string tleLine1;
    std::string tleLine2;
    double latDeg   = 0.0;   // 测站纬度/经度/高程
    double lonDeg   = 0.0;
    double altKm    = 0.0;
    double maskDeg  = 0.0;   // 掩星角
    double freqMHz  = 0.0;   // 多普勒频率
    GravityModel gravity = GravityModel::WGS72;
    int tzOffsetSec = 0;     // 0 = UTC
};

// 逐秒详情行数上限（172800 = 48 h @1 s）: 只对超长弧段生效
// （GEO 单圈可见 24 h ≈ 8.6 万点仍在上限内），防止极端窗口把内存/文件写爆
constexpr int kMaxDetailSteps = 172800;

// 计算某个整秒时刻的 5 列数据（时间/仰角/方位角/距离/多普勒）
// 返回 false 表示该点无效（传播失败或出现非有限值），由调用方跳过。
// utcTextOut 非空时输出**UTC** 的 "yyyy-MM-dd_HH:mm:ss"（例 2026-09-19_07:24:29），
// 供 CSV 导出使用（Excel 不会把该写法转成日期值，见 jdToStr 处说明）；
// 表内显示仍用带时区的 "yyyy-MM-dd HH:mm:ss"。
inline bool computeDetailRow(Propagator &prop, const Observer &obs, double jd,
                             double freqMHz, int tzOffsetSec, QStringList &row,
                             QString *utcTextOut = nullptr)
{
    auto eciOpt = prop.propagate(jd);
    if (!eciOpt) return false;

    const auto ecef = CoordTransform::eciToEcef(eciOpt->posKm, jd);
    const auto look = obs.lookAngle(ecef);
    if (!std::isfinite(look.elevDeg) || !std::isfinite(look.azDeg)
        || !std::isfinite(look.rangeKm))
        return false;

    const double doppler = DopplerCalc::compute(
        eciOpt->posKm, eciOpt->velKms, jd, obs.ecef(), freqMHz);
    const int64_t jdMs = jdToMs(jd);
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(
        jdMs - kJdEpochOffsetMs, QTimeZone::UTC);
    if (utcTextOut)
        *utcTextOut = dt.toString("yyyy-MM-dd_HH:mm:ss");   // 恒为 UTC，与显示时区无关
    if (tzOffsetSec != 0)
        dt = dt.toTimeZone(QTimeZone(tzOffsetSec));

    row = QStringList{
        dt.toString("yyyy-MM-dd HH:mm:ss"),
        QString::number(look.elevDeg, 'f', 3),
        QString::number(look.azDeg, 'f', 3),
        QString::number(look.rangeKm, 'f', 3),
        QString::number(doppler, 'f', 3)
    };
    return true;
}

// 逐秒详情 CSV 导出（单个过境）: 按 1 s 重新传播，口径与"逐秒数据"页签一致。
// 注意: 早前版本直接写 pass.samples（弧段采样点, sampleStepSec = 15 s），
// 文件名却叫 detail/"逐秒详情"——导出文件实际是 15 s 一个点（每分钟 4 行同标签，
// 看起来像"同一时刻有 4 组不同数据"）。现改为与页签同为 1 s。
// 时间列: UTC + "yyyy-MM-dd_HH:mm:ss"（Excel 保持纯文本原样显示，见 jdToStr 处说明）。
QString writePassDetailCsv(const PassEvent &pass, const DetailPropagationParams &dp,
                           const QAtomicInt *cancelFlag, const QString &path)
{
    if (dp.tleLine1.empty() || dp.tleLine2.empty())
        return QStringLiteral("TLE 为空，无法重传播: %1").arg(path);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("无法写入文件: %1").arg(path);
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);
    out.setLocale(QLocale::c());

    out << QString::fromUtf8("时间(UTC),仰角(°),方位角(°),距离(km),多普勒(kHz)\n");

    Propagator prop;
    std::string errMsg;
    if (!prop.initialize(dp.tleLine1, dp.tleLine2, errMsg, dp.gravity))
        return QStringLiteral("TLE 初始化失败: %1")
            .arg(QString::fromStdString(errMsg));
    Observer obs(dp.latDeg, dp.lonDeg, dp.altKm, dp.maskDeg);

    const double jdAOS = msToJd(pass.aosJdMs);
    const double jdLOS = msToJd(pass.losJdMs);
    const double step  = 1.0 / 86400.0;   // 1 s

    int written = 0;
    for (int sec = 0; sec < kMaxDetailSteps; ++sec) {
        const double jd = jdAOS + sec * step;   // 整数索引: 避免 jd += step 的累积漂移
        // 容差 1e-9 天 ≈ 86 µs: 吸收 jdLOS 在 JD≈2.46e6 处的表示舍入, 末行不被漏掉
        if (jd > jdLOS + 1e-9) break;
        if (cancelFlag && cancelFlag->loadAcquire()) break;   // 协作取消: 保留已写内容

        QStringList row;
        QString utcText;
        if (!computeDetailRow(prop, obs, jd, dp.freqMHz, dp.tzOffsetSec, row, &utcText))
            continue;

        // 时间列用 UTC 的 "yyyy-MM-dd_HH:mm:ss": 与表头"时间(UTC)"一致，Excel 不转日期
        out << "\"" << utcText << "\"," << row[1] << ',' << row[2] << ','
            << row[3] << ',' << row[4] << '\n';
        ++written;
    }

    out.flush();
    if (out.status() != QTextStream::Ok || file.error() != QFileDevice::NoError)
        return QStringLiteral("CSV 写入失败: %1").arg(path);
    if (cancelFlag && cancelFlag->loadAcquire())
        return QStringLiteral("导出已取消: %1").arg(path);
    if (written == 0)
        return QStringLiteral("无有效逐秒数据: %1").arg(path);
    return QString();
}

} // namespace

// ============================================================================
// 构建 UI
// ============================================================================
void MainWindow::setupUI()
{
    setWindowTitle("XGZ — 卫星 TLE 过境预报系统");
    resize(1280, 800);
    // 保证顶部状态栏三项文字完整展示（不被挤占截断）所需的最小宽度
    setMinimumWidth(1080);

    auto *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    auto *mainLayout = new QVBoxLayout(centralWidget);

    // ---- 输入区 ----
    auto *inputGroup = new QGroupBox("输入参数");
    auto *inputLayout = new QGridLayout(inputGroup);

    // TLE 输入
    inputLayout->addWidget(new QLabel("TLE 行 1:"), 0, 0);
    m_tleInput1 = new QPlainTextEdit;
    m_tleInput1->setFixedHeight(36);
    m_tleInput1->setPlaceholderText(
        "1 25544U 98067A   24083.56167013  .00012345  00000-0  24655-3 0  9994");
    inputLayout->addWidget(m_tleInput1, 0, 1, 1, 5);

    inputLayout->addWidget(new QLabel("TLE 行 2:"), 1, 0);
    m_tleInput2 = new QPlainTextEdit;
    m_tleInput2->setFixedHeight(36);
    m_tleInput2->setPlaceholderText(
        "2 25544  51.6423  23.4567 0004567 123.4567 236.7890 15.50012345678901");
    inputLayout->addWidget(m_tleInput2, 1, 1, 1, 5);

    // 地面站行
    inputLayout->addWidget(new QLabel("站纬度(°):"), 2, 0);
    m_latSpin = new QDoubleSpinBox;
    m_latSpin->setRange(-90.0, 90.0);
    m_latSpin->setValue(33.0000);
    m_latSpin->setDecimals(4);
    m_latSpin->setSuffix("°");   // 不用 °N: 负值=南纬, 后缀 N 会显示成「-30°N」歧义
    inputLayout->addWidget(m_latSpin, 2, 1);

    inputLayout->addWidget(new QLabel("站经度(°):"), 2, 2);
    m_lonSpin = new QDoubleSpinBox;
    m_lonSpin->setRange(-180.0, 180.0);
    m_lonSpin->setValue(103.0000);
    m_lonSpin->setDecimals(4);
    m_lonSpin->setSuffix("°");   // 同上: 负值=西经
    inputLayout->addWidget(m_lonSpin, 2, 3);

    inputLayout->addWidget(new QLabel("海拔(km):"), 2, 4);
    m_altSpin = new QDoubleSpinBox;
    m_altSpin->setRange(-1.0, 100.0);
    m_altSpin->setValue(0.05);
    m_altSpin->setDecimals(3);
    inputLayout->addWidget(m_altSpin, 2, 5);

    // 参数行
    inputLayout->addWidget(new QLabel("掩蔽角(°):"), 3, 0);
    m_maskSpin = new QDoubleSpinBox;
    m_maskSpin->setRange(0.0, 90.0);
    m_maskSpin->setValue(10.0);
    inputLayout->addWidget(m_maskSpin, 3, 1);

    inputLayout->addWidget(new QLabel("时区:"), 3, 4);
    m_timezoneCombo = new QComboBox;
    m_timezoneCombo->addItem("UTC+0", 0);
    m_timezoneCombo->addItem("UTC+8 (北京)", 8 * 3600);
    inputLayout->addWidget(m_timezoneCombo, 3, 5);

    // 高级参数行
    inputLayout->addWidget(new QLabel("暗天阈值:"), 4, 0);
    m_darkSkyCombo = new QComboBox;
    m_darkSkyCombo->addItem("民用 −6°", -6.0);
    m_darkSkyCombo->addItem("航海 −12°", -12.0);
    m_darkSkyCombo->addItem("天文 −18°", -18.0);
    inputLayout->addWidget(m_darkSkyCombo, 4, 1);

    inputLayout->addWidget(new QLabel("载频(MHz):"), 4, 2);
    m_freqSpin = new QDoubleSpinBox;
    m_freqSpin->setRange(1.0, 100000.0);
    m_freqSpin->setValue(437.800);
    m_freqSpin->setDecimals(3);
    inputLayout->addWidget(m_freqSpin, 4, 3);

    m_visibleOnlyCheck = new QCheckBox("仅显示可见过境");
    inputLayout->addWidget(m_visibleOnlyCheck, 4, 4, 1, 2);

    inputLayout->addWidget(new QLabel("开始时间(UTC):"), 5, 0);
    m_startTimeEdit = new QDateTimeEdit(QDateTime::currentDateTimeUtc());
    m_startTimeEdit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    m_startTimeEdit->setTimeZone(QTimeZone::UTC);
    inputLayout->addWidget(m_startTimeEdit, 5, 1, 1, 2);

    inputLayout->addWidget(new QLabel("结束时间(UTC):"), 5, 3);
    m_endTimeEdit = new QDateTimeEdit(QDateTime::currentDateTimeUtc().addSecs(24 * 3600));
    m_endTimeEdit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    m_endTimeEdit->setTimeZone(QTimeZone::UTC);
    inputLayout->addWidget(m_endTimeEdit, 5, 4, 1, 2);

    inputLayout->addWidget(new QLabel("固有星等:"), 3, 2);
    m_stdMagSpin = new QDoubleSpinBox;
    m_stdMagSpin->setRange(-5.0, 20.0);   // 允许负值: 内置目录里 ISS=-1.8 / 天宫=-0.9

    m_stdMagSpin->setValue(4.5);
    m_stdMagSpin->setDecimals(1);
    m_stdMagSpin->setToolTip(
        "卫星固有星等（1000 km、满相位，越小越亮）\n"
        "内置星等目录含 ISS/天宫/哈勃等常见亮星；\n"
        "未匹配目录的卫星使用此值估算视星等");
    inputLayout->addWidget(m_stdMagSpin, 3, 3);

    // 在线更新行
    inputLayout->addWidget(new QLabel("在线更新:"), 6, 0);
    m_tleGroupCombo = new QComboBox;
    for (const auto &g : TleFetcher::groups())
        m_tleGroupCombo->addItem(g.label, g.id);
    m_tleGroupCombo->setToolTip("从 CelesTrak 下载该组卫星的最新 TLE");
    inputLayout->addWidget(m_tleGroupCombo, 6, 1, 1, 3);
    m_onlineUpdateBtn = new QPushButton("下载 TLE");
    inputLayout->addWidget(m_onlineUpdateBtn, 6, 4, 1, 2);

    // 重力模型选择（行7）
    inputLayout->addWidget(new QLabel("重力模型:"), 7, 0);
    m_gravModelCombo = new QComboBox;
    m_gravModelCombo->addItem("WGS-72 (标准)", static_cast<int>(GravityModel::WGS72));
    m_gravModelCombo->addItem("WGS-84", static_cast<int>(GravityModel::WGS84));
    m_gravModelCombo->setToolTip("SGP4/SDP4 重力常数模型\n"
        "WGS-72 标准: NORAD TLE 默认，推荐使用\n"
        "WGS-84: 部分特殊 TLE 使用");
    inputLayout->addWidget(m_gravModelCombo, 7, 1);

    // 数据策略行（行8）: 两项都直接对应"数据可信度/合规"取舍
    m_forceRefreshCheck = new QCheckBox("强制刷新（忽略本地缓存）");
    m_forceRefreshCheck->setToolTip(
        "CelesTrak 的 GP 数据每 ~2 小时才更新一次，且官方对同一组数据\n"
        "在两次更新之间只允许下载一次（重复请求返回 HTTP 403，超量会被封 IP）。\n"
        "默认（不勾选）: 2 小时内的本地缓存直接复用，不发网络请求；\n"
        "勾选后忽略缓存强制联网下载。");
    inputLayout->addWidget(m_forceRefreshCheck, 8, 0, 1, 3);

    m_strictChecksumCheck = new QCheckBox("强制校验和（失败即拒绝）");
    m_strictChecksumCheck->setToolTip(
        "TLE 每行第 69 位是模 10 校验和。\n"
        "默认（不勾选，宽松模式）: 校验和不符仅告警，仍继续计算；\n"
        "勾选后（强制模式）: 校验和不符的 TLE 直接判定为解析失败并跳过，\n"
        "适用于对数据完整性要求高的场景。");
    inputLayout->addWidget(m_strictChecksumCheck, 8, 3, 1, 3);

    mainLayout->addWidget(inputGroup);

    // ---- 控制按钮行 ----
    auto *btnLayout = new QHBoxLayout;
    m_predictBtn = new QPushButton("▶ 开始预报");
    m_predictBtn->setStyleSheet("font-weight: bold; min-height: 32px;");
    m_batchImportBtn = new QPushButton("批量导入");
    m_cancelBtn  = new QPushButton("✕ 取消");
    m_cancelBtn->setEnabled(false);
    m_exportCsvBtn = new QPushButton("导出 CSV");
    m_exportKmlBtn = new QPushButton("导出 KML");
    m_exportJsonBtn = new QPushButton("导出 JSON");
    // 导出范围 = 表格中选中的过境行（未选择任何行时导出全部）
    const QString exportScopeTip =
        "只导出过境列表中选中的行（该行所属卫星及其逐秒详情）；\n"
        "未选择任何行时导出全部过境。\n"
        "提示: 在表格中右键可一键“选中某颗卫星的全部过境”。";
    m_exportCsvBtn->setToolTip(exportScopeTip);
    m_exportKmlBtn->setToolTip(exportScopeTip);
    m_exportJsonBtn->setToolTip(exportScopeTip);
    m_exportDetailCheck = new QCheckBox("含逐秒详情");
    m_exportDetailCheck->setToolTip("CSV 导出时额外为每个过境生成 xxx_detail_N.csv："
                                    "按 1 秒重新传播的逐秒数据（与\"逐秒数据\"页签口径一致）；"
                                    "超长过境按 172800 行（48 h）上限截断");
    m_clearBtn   = new QPushButton("清空");

    btnLayout->addWidget(m_predictBtn);
    btnLayout->addWidget(m_batchImportBtn);
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_exportCsvBtn);
    btnLayout->addWidget(m_exportKmlBtn);
    btnLayout->addWidget(m_exportJsonBtn);
    btnLayout->addWidget(m_exportDetailCheck);
    btnLayout->addWidget(m_clearBtn);
    btnLayout->addStretch();

    m_progressBar = new QProgressBar;
    m_progressBar->setVisible(false);
    m_progressBar->setFixedWidth(200);
    btnLayout->addWidget(m_progressBar);

    mainLayout->addLayout(btnLayout);

// ---- 状态栏 ----
    auto *statusLayout = new QHBoxLayout;
    statusLayout->setSpacing(28);
    m_statusModel = new QLabel("模型: —");
    m_statusEpoch = new QLabel("历元: —");
    m_statusCount = new QLabel("过境: 0");
    // 状态标签永不压缩（内容完整展示, 不互相挤占截断）;
    // 超长时可悬停查看全文（setStatusText 同步 tooltip）
    for (QLabel *lbl : {static_cast<QLabel *>(m_statusModel),
                        static_cast<QLabel *>(m_statusEpoch),
                        static_cast<QLabel *>(m_statusCount)}) {
        lbl->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    statusLayout->addWidget(m_statusModel);
    statusLayout->addWidget(m_statusEpoch);
    statusLayout->addWidget(m_statusCount);
    statusLayout->addStretch();
    mainLayout->addLayout(statusLayout);

    // ---- 结果区（三页签） ----
    m_tabWidget = new QTabWidget;

    // 页签 1: 过境列表（QTableView + 懒加载 model: 仅可见单元格被请求,
    // 批量过境不再整表构建 QTableWidgetItem——M 修复的刷新卡顿根源）
    m_tableModel = new PassTableModel(this);
    m_passTable = new QTableView;
    m_passTable->setModel(m_tableModel);
    m_passTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    // "卫星名" 与 "可见弧段" 拉伸填充剩余空间, 其余列固定宽度, 避免最右列被拉空
    m_passTable->horizontalHeader()->setSectionResizeMode(1,  QHeaderView::Stretch);
    m_passTable->horizontalHeader()->setSectionResizeMode(10, QHeaderView::Stretch);
    const int colWidths[16] = {
        /* # */ 40, /* 卫星名 */ 120, /* 入境 */ 150, /* 出境 */ 150, /* 持续 */ 60,
        /* 最大仰角 */ 75, /* 方位角 */ 60, /* 可见性 */ 50, /* 太阳高度 */ 70, /* 卫星光照 */ 55,
        /* 可见弧段 */ 170, /* 入境位置 */ 165, /* 斜距 */ 85, /* 多普勒 */ 90, /* 星等 */ 55, /* 质量 */ 60
    };
    static_assert(std::size(colWidths) == size_t(PassTableModel::ColCount),
                  "列宽数组必须与 PassTableModel::ColCount 同步增删");
    for (int c = 0; c < 16; ++c)
        m_passTable->setColumnWidth(c, colWidths[c]);
    m_passTable->horizontalHeader()->setMinimumSectionSize(45);
    m_passTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    // 多选: 导出只导出选中行（单颗卫星的多个过境可 Ctrl/Shift 多选，
    // 或用右键菜单一键选中该卫星的全部过境）
    m_passTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_passTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_passTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_passTable->setAlternatingRowColors(false);
    m_passTable->setStyleSheet(
        "QTableView { background-color: white; color: black; }"
        "QTableView::item:selected { background-color: #3399ff; color: white; }"
    );
    m_passTable->verticalHeader()->setVisible(false);
    m_tabWidget->addTab(m_passTable, "过境列表");

    // 页签 2: 极坐标图
    m_polarPlot = new PolarPlotWidget;
    m_tabWidget->addTab(m_polarPlot, "极坐标图");

    // 页签 3: 多普勒曲线
    m_dopplerPlot = new DopplerPlotWidget;
    m_tabWidget->addTab(m_dopplerPlot, "多普勒曲线");

    // 页签 4: 地面轨迹图
    m_groundTrack = new GroundTrackWidget;
    m_tabWidget->addTab(m_groundTrack, "地面轨迹");

    // 页签 5: 逐秒数据
    m_detailTable = new QTableView;
    m_detailTable->setStyleSheet(
        "QTableView { background-color: white; color: black; }"
        "QTableView::item:selected { background-color: #3399ff; color: white; }");
    m_detailTable->verticalHeader()->setVisible(false);
    m_detailModel = new QStandardItemModel(0, 5, this);
    m_detailModel->setHorizontalHeaderLabels(
        {QString::fromUtf8("时间(UTC)"),
         QString::fromUtf8("仰角(°)"),
         QString::fromUtf8("方位角(°)"),
         QString::fromUtf8("距离(km)"),
         QString::fromUtf8("多普勒(kHz)")});
    m_detailTable->setModel(m_detailModel);
    m_detailTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_detailTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tabWidget->addTab(m_detailTable, "逐秒数据");

    mainLayout->addWidget(m_tabWidget, 1);
}

// ============================================================================
// 断开所有信号槽连接
// ============================================================================
void MainWindow::disconnectAllConnections()
{
    // 断开所有按钮的信号槽连接
    disconnect(m_predictBtn, &QPushButton::clicked, this, &MainWindow::onPredict);
    disconnect(m_batchImportBtn, &QPushButton::clicked, this, &MainWindow::onBatchImport);
    disconnect(m_onlineUpdateBtn, &QPushButton::clicked, this, &MainWindow::onOnlineUpdate);
    disconnect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancel);
    disconnect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::onClear);
    disconnect(m_exportCsvBtn, &QPushButton::clicked, this, &MainWindow::onExportCSV);
    disconnect(m_exportKmlBtn, &QPushButton::clicked, this, &MainWindow::onExportKML);
    disconnect(m_exportJsonBtn, &QPushButton::clicked, this, &MainWindow::onExportJSON);
    
    // 断开TleFetcher的信号槽连接
    if (m_tleFetcher) {
        disconnect(m_tleFetcher.get(), &TleFetcher::downloaded, this, &MainWindow::onTleDownloaded);
        disconnect(m_tleFetcher.get(), &TleFetcher::fetchError, this, &MainWindow::onTleError);
        disconnect(m_tleFetcher.get(), &TleFetcher::finished, this, nullptr);
    }
    
    // 断开PassPredictorService的信号槽连接
    if (m_service) {
        disconnect(m_service.get(), &PassPredictorService::resultReady, this, &MainWindow::onResultsReady);
        disconnect(m_service.get(), &PassPredictorService::errorOccurred, this, &MainWindow::onError);
        disconnect(m_service.get(), &PassPredictorService::checksumWarning, this, &MainWindow::onChecksumWarning);
        disconnect(m_service.get(), &PassPredictorService::batchProgress, this, nullptr);
        disconnect(m_service.get(), &PassPredictorService::metaInfo, this, &MainWindow::onMetaInfo);
    }
    
    // 断开表格选择模型的信号槽连接
    if (m_passTable && m_passTable->selectionModel()) {
        disconnect(m_passTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this, nullptr);
    }
    
    // 断开其他UI控件的信号槽连接
    disconnect(m_visibleOnlyCheck, &QCheckBox::toggled, this, nullptr);
    disconnect(m_timezoneCombo, &QComboBox::currentIndexChanged, this, nullptr);
    
    // 断开导出watcher的信号槽连接
    if (m_exportWatcher) {
        disconnect(m_exportWatcher.get(), &QFutureWatcher<QString>::finished, this, &MainWindow::onExportFinished);
    }

    // 取消并断开逐秒详情计算 watcher
    // 判据用 !isFinished(): QtConcurrent 任务"已入队未开跑"时 isRunning() 仍是
    // false, 用 isRunning() 会漏等——worker lambda 捕获了 MainWindow 成员, 不等就 UAF
    if (m_detailWatcher && !m_detailWatcher->isFinished()) {
        m_detailCalcCancel.storeRelease(1);
        m_detailWatcher->waitForFinished();
    }
    if (m_detailWatcher) {
        m_detailWatcher.reset();
    }

    // 断开 ErrorHandler 单例的信号槽连接（单例生命周期长于 MainWindow, 防析构后 UAF）
    disconnect(&ErrorHandler::instance(), &ErrorHandler::errorOccurred, this, nullptr);
    disconnect(&ErrorHandler::instance(), &ErrorHandler::errorRecoveryAttempt, this, nullptr);
    disconnect(&ErrorHandler::instance(), &ErrorHandler::errorRecoveryResult, this, nullptr);
}

// ============================================================================
// 槽函数连接
// ============================================================================
void MainWindow::setupConnections()
{
    m_service = std::make_unique<PassPredictorService>(this);
    m_tleFetcher = std::make_unique<TleFetcher>(this);

    connect(m_predictBtn, &QPushButton::clicked, this, &MainWindow::onPredict);
    connect(m_batchImportBtn, &QPushButton::clicked, this, &MainWindow::onBatchImport);
    connect(m_onlineUpdateBtn, &QPushButton::clicked, this, &MainWindow::onOnlineUpdate);
    connect(m_tleFetcher.get(), &TleFetcher::downloaded, this, &MainWindow::onTleDownloaded);
    connect(m_tleFetcher.get(), &TleFetcher::fetchError, this, &MainWindow::onTleError);
    // 缓存命中提示: 让用户知道本次未联网（数据新鲜度 + 限流保护已生效）
    connect(m_tleFetcher.get(), &TleFetcher::cacheServed, this,
            [this](QString, qint64 ageSec) {
                if (m_isClosing || m_batchMode) return;
                const qint64 mins = ageSec / 60;
                setStatusText(m_statusModel,
                    QString("模型: 使用本地缓存（%1 分钟前下载，CelesTrak 每 ~2 小时更新）")
                        .arg(mins));
            });
    connect(m_tleFetcher.get(), &TleFetcher::finished, this, [this]() {
        // 检查对象是否仍然有效
        if (m_isClosing) return;

        // 批量态（含后台解析窗口期——m_batchMode 已由 startBatchFromBlocks
        // 提前置位）: 入口保持禁用, 收尾由批量完成/失败路径统一负责。
        // 此前无条件恢复会在解析窗口内越权启用预报/导出, 用户点击的
        // 任务随后被 startBatch→cleanupAll 静默替换。
        // 但**按钮文案必须在这里复位**: 仅恢复使能会让按钮永久停在"下载中…"
        if (m_batchMode) {
            m_onlineUpdateBtn->setText("下载 TLE");
            return;
        }

        m_onlineUpdateBtn->setEnabled(true);
        m_onlineUpdateBtn->setText("下载 TLE");
        setExportEnabled(true);   // 下载结束, 导出入口恢复
        m_predictBtn->setEnabled(true);
        m_batchImportBtn->setEnabled(true);
        m_cancelBtn->setEnabled(false);
    });
    connect(m_cancelBtn,  &QPushButton::clicked, this, &MainWindow::onCancel);
    connect(m_clearBtn,   &QPushButton::clicked, this, &MainWindow::onClear);
    connect(m_exportCsvBtn, &QPushButton::clicked, this, &MainWindow::onExportCSV);
    connect(m_exportKmlBtn, &QPushButton::clicked, this, &MainWindow::onExportKML);
    connect(m_exportJsonBtn, &QPushButton::clicked, this, &MainWindow::onExportJSON);

    connect(m_service.get(), &PassPredictorService::resultReady,
            this, &MainWindow::onResultsReady);
    connect(m_service.get(), &PassPredictorService::errorOccurred,
            this, &MainWindow::onError);
    // 校验和警告独立通道: 仅状态栏去重提示——不入失败汇总、不弹窗、
    // 不复位控件（计算仍在进行, resultReady 随后正常到达）
    connect(m_service.get(), &PassPredictorService::checksumWarning,
            this, &MainWindow::onChecksumWarning);
    connect(m_service.get(), &PassPredictorService::batchProgress,
            this, [this](int completed, int total) {
                // 检查对象是否仍然有效
                if (m_isClosing) return;
                
                m_progressBar->setRange(0, total);
                m_progressBar->setValue(completed);
            });
    connect(m_service.get(), &PassPredictorService::metaInfo,
            this, &MainWindow::onMetaInfo);
    // 点击/键盘方向键/程序化移动统一走 currentRowChanged（model 视图方案）
    connect(m_passTable->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &cur, const QModelIndex &) {
                // 检查对象是否仍然有效
                if (m_isClosing) return;
                
                if (cur.isValid()) onTableRowSelected(cur.row(), 0);
            });
    // 过境列表右键菜单: 一键选中该卫星的全部过境（"只导出选中的卫星"用得上）
    connect(m_passTable, &QTableView::customContextMenuRequested, this,
            [this](const QPoint &pos) {
        if (m_isClosing || !m_passTable->selectionModel() || !m_tableModel) return;
        const QModelIndex idx = m_passTable->indexAt(pos);

        QString satName;
        if (idx.isValid()) {
            QMutexLocker locker(&m_dataMutex);
            const int r = idx.row();
            if (r >= 0 && r < m_filteredPasses.size())
                satName = QString::fromStdString(m_filteredPasses[r]->satelliteName);
        }

        QMenu menu(this);
        QAction *actSat = satName.isEmpty() ? nullptr
            : menu.addAction(QString("选中卫星「%1」的全部过境").arg(satName));
        QAction *actOne = idx.isValid() ? menu.addAction("仅选中此过境") : nullptr;
        menu.addSeparator();
        QAction *actClear = menu.addAction("清除选择（导出全部）");
        QAction *chosen = menu.exec(m_passTable->viewport()->mapToGlobal(pos));
        if (!chosen) return;

        QItemSelection sel;
        auto selectRows = [&](const QVector<int> &rows) {
            const int lastCol = m_tableModel->columnCount() - 1;
            for (int r : rows) {
                if (r < 0 || r >= m_tableModel->rowCount()) continue;
                sel.select(m_tableModel->index(r, 0), m_tableModel->index(r, lastCol));
            }
            m_passTable->selectionModel()->select(
                sel, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        };

        if (chosen == actClear) {
            m_passTable->clearSelection();
            showStatusDedup(QStringLiteral("已清除选择：导出将包含全部过境"));
            return;
        }
        if (chosen == actOne && idx.isValid()) {
            selectRows({idx.row()});
            showStatusDedup(QStringLiteral("已选中 1 个过境（导出仅包含该过境）"));
            return;
        }
        if (chosen == actSat && !satName.isEmpty()) {
            QVector<int> rows;
            {
                QMutexLocker locker(&m_dataMutex);
                for (int i = 0; i < m_filteredPasses.size(); ++i)
                    if (QString::fromStdString(m_filteredPasses[i]->satelliteName) == satName)
                        rows.append(i);
            }
            selectRows(rows);
            showStatusDedup(QString("已选中卫星「%1」的 %2 个过境（导出仅包含这些过境）")
                                .arg(satName).arg(rows.size()));
        }
    });
    // 切换"仅显示可见过境"时立即重新过滤并刷新表格/图表
    connect(m_visibleOnlyCheck, &QCheckBox::toggled, this, [this](bool) {
        // 检查对象是否仍然有效
        if (m_isClosing) return;
        
        if (m_currentPasses.isEmpty()) return;
        applyFilterAndRefresh();
    });
    // 时区切换: 无需重新计算, 重建表格时间列与表头即可（H3 修复）
    connect(m_timezoneCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        // 检查对象是否仍然有效
        if (m_isClosing) return;
        
        if (m_currentPasses.isEmpty()) return;
        applyFilterAndRefresh();
    });
}

// ============================================================================
// 按可见性开关过滤并刷新表格/图表/统计
// ============================================================================
void MainWindow::applyFilterAndRefresh(const QString &preserveKey,
                                       int preserveScroll)
{
    if (m_isClosing) return;

    // 过滤条件变更前终止在途逐秒计算: m_filteredPasses 即将重建,
    // 旧计算结果引用的过境指针将失效
    if (m_detailWatcher && !m_detailWatcher->isFinished()) {
        m_detailCalcCancel.storeRelease(1);
        m_detailWatcher->waitForFinished();
    }
    m_detailCalcCancel.storeRelease(0);
    // 世代号递增 + 显式收起进度条: 被取消任务的 finished 回调会在
    // "gen != m_detailGeneration" 处提前 return, 其 setVisible(false) 永不执行;
    // 若此处不收起, 进度条常驻会让 startExport 的闸门永久拦截导出
    ++m_detailGeneration;
    m_progressBar->setVisible(false);

    // 使用互斥锁保护共享数据
    QMutexLocker dataLocker(&m_dataMutex);
    
    // 创建过滤后的数据副本
    QVector<const PassEvent *> filtered;
    if (m_visibleOnlyCheck->isChecked()) {
        for (const auto &p : m_currentPasses)
            if (p.visibility == PassVisibility::Visible)
                filtered.append(&p);
    } else {
        filtered.reserve(m_currentPasses.size());
        for (const auto &p : m_currentPasses)
            filtered.append(&p);
    }
    
    // 保存当前选中卫星身份与滚动位置: 过滤/时区切换后行序收缩,
    // 按行号恢复会漂移到另一颗卫星（越界钳到第 0 行同样漂移）,
    // 必须以「卫星 + 过境时刻」身份键找回原选择。
    // 身份键优先取调用方预快照（onResultsReady 路径: 共享数据已被覆盖,
    // m_filteredPasses 的旧指针已悬垂, 在此解引用即 use-after-free）;
    // 其余调用方共享数据未变, 现场快照安全
    QString prevKey = preserveKey;
    if (prevKey.isEmpty()) {
        const int prevRow = (m_tableModel->rowCount() > 0)
            ? m_passTable->currentIndex().row() : -1;
        if (prevRow >= 0 && prevRow < m_filteredPasses.size()) {
            const auto *prev = m_filteredPasses[prevRow];
            prevKey = QString::number(prev->aosJdMs) + "|" +
                      QString::fromStdString(prev->satelliteName);
        }
    }
    // 滚动位置读取不依赖共享数据; 调用方显式传入时以其为准
    const int prevScroll = (preserveScroll >= 0)
        ? preserveScroll : m_passTable->verticalScrollBar()->value();
    
    // 更新过滤后的数据
    m_filteredPasses = filtered;
    
    // 释放数据锁，允许UI更新
    dataLocker.unlock();
    
    // 刷新UI（在主线程中执行）
    populateTable(m_filteredPasses);
    m_polarPlot->setPasses(m_filteredPasses);
    m_dopplerPlot->setPasses(m_filteredPasses);
    // 地面标记使用任务发起时的观测站坐标（非当前输入框值, 防止结果与标记漂移）
    m_groundTrack->setPasses(m_filteredPasses, m_taskLatDeg, m_taskLonDeg);

    int visCount = 0;
    for (const auto &p : m_currentPasses)
        if (p.visibility == PassVisibility::Visible) ++visCount;
    setStatusText(m_statusCount, QString("过境: %1 (可见: %2)")
        .arg(m_currentPasses.size()).arg(visCount));

    // 恢复选中: 按身份键找回原选择（行序可能已变）, 找不到/无选中时钳到第 0 行
    if (m_tableModel->rowCount() > 0) {
        int row = 0;
        if (!prevKey.isEmpty()) {
            for (int i = 0; i < m_filteredPasses.size(); ++i) {
                const auto *p = m_filteredPasses[i];
                const QString key = QString::number(p->aosJdMs) + "|" +
                                    QString::fromStdString(p->satelliteName);
                if (key == prevKey) { row = i; break; }
            }
        }
        m_passTable->selectRow(row);
        // selectRow 触发 currentRowChanged → onTableRowSelected → setSelectedPass,
        // 但 setPasses 中的 clamp 可能让 plot 控件指向错误行; 此处强制同步
        m_polarPlot->setSelectedPass(row);
        m_dopplerPlot->setSelectedPass(row);
        m_groundTrack->setSelectedPass(row);
    }
    // 恢复滚动位置（模型重置后滚动条归零; 超界值由视图自动钳制）
    m_passTable->verticalScrollBar()->setValue(prevScroll);
}

void MainWindow::setStatusText(QLabel *label, const QString &text)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    label->setText(text);
    label->setToolTip(text);   // 文本超长时悬停查看完整内容
}

void MainWindow::showStatusDedup(const QString &text)
{
    // 连续相同文本只刷一次: 批量失败风暴（千颗同因失败）下错误/恢复信号
    // 逐条到达, 逐条重绘占满 UI 线程; 首条已表达全部信息, 后续相同条目丢弃
    if (m_lastErrorStatusText == text) return;
    m_lastErrorStatusText = text;
    setStatusText(m_statusModel, text);
}

// ============================================================================
// 预报触发
// ============================================================================
// 时间窗校验: 起止时间改为 QDateTimeEdit 后, 用户可以设出"结束 ≤ 开始"。
// 此时 hoursAhead 会被 qMax(0.0, …) 钳成 0, 而 OrbitCalculator 对 0 是合法输入,
// 结果是**静默**算完并显示"过境: 0"——用户会误以为该星无过境。
// 单星与批量两条入口都先过这里。
bool MainWindow::ensureValidTimeWindow()
{
    if (m_endTimeEdit->dateTime() > m_startTimeEdit->dateTime()) return true;
    QMessageBox::warning(this, "时间范围错误",
        QString("结束时间必须晚于开始时间。\n\n当前: 开始 %1 / 结束 %2（UTC）")
            .arg(m_startTimeEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss"))
            .arg(m_endTimeEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss")));
    return false;
}

void MainWindow::onPredict()
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    // 单星粘贴路径同样剥 BOM: 从带 BOM 文件复制首行时 U+FEFF 会随剪贴板带入
    QString tle1 = stripUtf8Bom(m_tleInput1->toPlainText().trimmed());
    QString tle2 = stripUtf8Bom(m_tleInput2->toPlainText().trimmed());

    if (tle1.isEmpty() || tle2.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "请输入完整的 TLE 两行根数");
        return;
    }
    if (!ensureValidTimeWindow()) return;

    PassPredictWorker::Job job;
    fillJobFromUi(job);
    job.tleLine1 = tle1;
    job.tleLine2 = tle2;

    // 锁定本次任务的观测站坐标（绘图地面标记使用）
    m_taskLatDeg = m_latSpin->value();
    m_taskLonDeg = m_lonSpin->value();
    m_taskAltKm = m_altSpin->value();
    m_taskMaskDeg = m_maskSpin->value();
    m_taskFreqMHz = m_freqSpin->value();
    m_taskGravityModel = static_cast<GravityModel>(m_gravModelCombo->currentData().toInt());

    m_batchMode.store(false);
    m_batchErrors.clear();
    m_lastErrorStatusText.clear();   // 新任务开始: 错误状态栏去重复位
    m_predictBtn->setEnabled(false);
    m_batchImportBtn->setEnabled(false);
    m_onlineUpdateBtn->setEnabled(false);
    setExportEnabled(false);   // 计算期间导出旧数据无意义, 一并禁用
    m_cancelBtn->setEnabled(true);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 0);  // 不确定进度
    setStatusText(m_statusEpoch, "历元: —");
    m_statusEpoch->setStyleSheet(QString());

    m_service->startJob(job);
}

// ============================================================================
// 批量导入
// ============================================================================
void MainWindow::onBatchImport()
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    BatchTleDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    int incomplete = 0;
    const auto blocks = parseTleBlocks(dlg.text(), &incomplete);
    if (blocks.isEmpty()) {
        QMessageBox::warning(this, "批量导入", "未识别到有效的 TLE 两行根数");
        return;
    }
    if (incomplete > 0) {
        QMessageBox::warning(this, "批量导入",
            QString("已解析 %1 颗卫星；另有 %2 条 TLE 不完整（缺少行1或行2），已跳过。")
                .arg(blocks.size()).arg(incomplete));
    }
    startBatchFromBlocks(blocks, "批量导入");
}

// ============================================================================
// 在线更新 TLE（CelesTrak）
// ============================================================================
void MainWindow::onOnlineUpdate()
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    const QString groupId = m_tleGroupCombo->currentData().toString();
    if (groupId.isEmpty()) return;
    m_onlineUpdateBtn->setEnabled(false);
    m_onlineUpdateBtn->setText("下载中…");
    // 下载期间禁止发起预报/批量/导出（防止任务与下载交错, 导出需稳定数据）
    m_predictBtn->setEnabled(false);
    m_batchImportBtn->setEnabled(false);
    setExportEnabled(false);
    // 取消按钮在下载期间可用: onCancel 会中止在途下载
    m_cancelBtn->setEnabled(true);
    const bool forceRefresh = m_forceRefreshCheck && m_forceRefreshCheck->isChecked();
    setStatusText(m_statusModel, QString("模型: %1 %2 的 TLE…")
                  .arg(forceRefresh ? "正在下载" : "正在获取")
                  .arg(m_tleGroupCombo->currentText()));
    m_tleFetcher->fetchGroup(groupId, forceRefresh);
}

// ============================================================================
// TLE 下载完成 → 解析并弹出批量导入对话框（预填文本）
// ============================================================================
void MainWindow::onTleDownloaded(QString rawText, QString groupId)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    int incomplete = 0;
    const auto blocks = parseTleBlocks(rawText, &incomplete);
    if (blocks.isEmpty()) {
        QMessageBox::warning(this, "在线更新",
            QString("组 [%1] 未解析到有效 TLE（可能是空数据或格式变化）")
                .arg(groupId));
        return;
    }
    if (incomplete > 0) {
        QMessageBox::warning(this, "在线更新",
            QString("组 [%1] 中有 %2 条 TLE 不完整，已跳过（其余 %3 颗正常）。")
                .arg(groupId).arg(incomplete).arg(blocks.size()));
    }
    setStatusText(m_statusModel, QString("模型: 已下载 %1 颗卫星的 TLE").arg(blocks.size()));

    BatchTleDialog dlg(this);
    dlg.setWindowTitle(QString("批量导入 — 在线下载 (%1 颗)").arg(blocks.size()));
    dlg.setText(rawText);
    if (dlg.exec() != QDialog::Accepted) return;

    // 与"批量导入"入口语义统一: 以对话框编辑后的文本为唯一数据源。
    // 用户可能删除/增补/修改卫星, 预解析的 blocks 在此作废重解析
    int editedIncomplete = 0;
    const auto editedBlocks = parseTleBlocks(dlg.text(), &editedIncomplete);
    if (editedBlocks.isEmpty()) {
        QMessageBox::warning(this, "在线更新",
            "编辑后的文本中未识别到有效的 TLE 两行根数");
        return;
    }
    if (editedIncomplete > 0) {
        QMessageBox::warning(this, "在线更新",
            QString("编辑后的文本中有 %2 条 TLE 不完整，已跳过（其余 %1 颗正常）。")
                .arg(editedBlocks.size()).arg(editedIncomplete));
    }
    startBatchFromBlocks(editedBlocks, "在线更新");
}

// ============================================================================
// 公共批量启动: 解析校验 TLE 块 → 构建任务 → 启动批量预报
// 供"批量导入"与"在线更新下载"两条入口复用
// ============================================================================
void MainWindow::startBatchFromBlocks(const QVector<TleBlock> &blocks,
                                      const QString &origin)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;

    // 时间窗先校验: 批量同样会因 hoursAhead=0 静默算出 0 条过境
    //（在线更新路径会从对话框返回后再进来, 故此处校验对所有入口生效）
    if (!ensureValidTimeWindow()) return;

    // 解析期即禁用入口: 千颗级后台解析有数百毫秒窗口, 期间重复触发
    // 会并发两个批次（seq 守卫保证结果不串, 但浪费计算且 UX 混乱）。
    // 全部失败时由 startBatchParsed 失败分支恢复
    m_predictBtn->setEnabled(false);
    m_batchImportBtn->setEnabled(false);
    m_onlineUpdateBtn->setEnabled(false);
    setExportEnabled(false);

    // 提前置位批量态: TleFetcher 的 finished 在 downloaded 同步链返回后才
    // 发射——若等异步解析回调才置位, finished 的 !m_batchMode 守卫会在
    // 解析窗口期越权恢复预报/导出入口, 用户随后点击的任务会被
    // startBatch→cleanupAll 静默替换（凭空消失）
    m_batchMode.store(true);
    m_batchParsePending = true;

    // 批量解析在 QtConcurrent 线程池执行（TleParser 为纯函数, 无 UI 访问）:
    // 千颗级粘贴/下载在 GUI 线程串行解析会冻结界面数百毫秒。
    // UI 参数先快照为模板 job（后台线程禁止读取 UI 控件）
    PassPredictWorker::Job templateJob;
    fillJobFromUi(templateJob);

    struct ParseResult {
        QVector<PassPredictWorker::Job> jobs;
        int failed = 0;
        int lenIssues = 0;   // TLE 行长度不足计数（错误诊断用）
        int fmtIssues = 0;   // 行号前缀错误计数（错误诊断用）
    };

    // watcher 父子 this: 窗口关闭时随对象树销毁;
    // lambda 捕获 this 用于调用 startBatchParsed, 但 closeEvent 已先置 m_isClosing
    // 并断开连接, 析构时 Qt 自动断开剩余信号, 不会触发陈旧回调
    auto *watcher = new QFutureWatcher<ParseResult>(this);
    connect(watcher, &QFutureWatcher<ParseResult>::finished, this,
            [this, watcher, origin]() {
                if (m_isClosing) { watcher->deleteLater(); return; }
                // result() 会重抛工作线程里被 QtConcurrent 捕获的异常
                //（如 bad_alloc）——槽内不捕获会让异常穿出事件循环 → terminate
                ParseResult r;
                try {
                    r = watcher->result();
                } catch (const std::exception &e) {
                    watcher->deleteLater();
                    m_batchParsePending = false;
                    m_batchMode.store(false);
                    m_predictBtn->setEnabled(true);
                    m_batchImportBtn->setEnabled(true);
                    m_onlineUpdateBtn->setEnabled(true);
                    setExportEnabled(true);
                    QMessageBox::critical(this, origin,
                        QString("批量解析异常（已中止本批次）: %1").arg(QString::fromUtf8(e.what())));
                    return;
                } catch (...) {
                    watcher->deleteLater();
                    m_batchParsePending = false;
                    m_batchMode.store(false);
                    m_predictBtn->setEnabled(true);
                    m_batchImportBtn->setEnabled(true);
                    m_onlineUpdateBtn->setEnabled(true);
                    setExportEnabled(true);
                    QMessageBox::critical(this, origin, "批量解析异常（未知类型，已中止本批次）");
                    return;
                }
                watcher->deleteLater();
                startBatchParsed(r.jobs, r.failed, r.lenIssues, r.fmtIssues, origin);
            });
    watcher->setFuture(QtConcurrent::run([templateJob, blocks]() -> ParseResult {
        ParseResult r;
        for (const auto &b : blocks) {
            if (b.line1.length() < 69 || b.line2.length() < 69) {
                ++r.failed;
                ++r.lenIssues;
                continue;
            }
            if (b.line1[0] != '1' || b.line2[0] != '2') {
                ++r.failed;
                ++r.fmtIssues;
                continue;
            }
            TleParser::ParseError parseErr;
            auto tleOpt = TleParser::parse(b.line1.toStdString(), b.line2.toStdString(),
                                           &parseErr, templateJob.strictChecksum);
            if (!tleOpt) { ++r.failed; continue; }
            PassPredictWorker::Job job = templateJob;
            job.tleLine0 = b.name;
            job.tleLine1 = b.line1;
            job.tleLine2 = b.line2;
            r.jobs.append(job);
        }
        return r;
    }));
}

void MainWindow::startBatchParsed(const QVector<PassPredictWorker::Job> &jobs,
                                  int failed, int lenIssues, int fmtIssues,
                                  const QString &origin)
{
    // 解析在途期间可能已发生取消/清除（onCancel/onClear 复位 m_batchMode
    // 并恢复按钮）: 迟到的解析结果作废, 不再启动批次
    if (!m_batchParsePending || !m_batchMode) {
        m_batchParsePending = false;
        return;
    }
    m_batchParsePending = false;

    if (jobs.isEmpty()) {
        QString errorMsg = QString("批量导入失败: 共 %1 颗卫星，全部解析失败\n\n")
            .arg(failed);
        
        // 分析失败原因并提供详细建议（计数由后台解析任务统计）
        errorMsg += "可能的原因:\n";
        
        if (lenIssues > 0) {
            errorMsg += "• TLE行长度不足（每行至少需要69字符）\n";
        }
        
        if (fmtIssues > 0) {
            errorMsg += "• TLE行号错误（行1应以'1'开头，行2应以'2'开头）\n";
        }
        
        errorMsg += "\n建议解决方案:\n";
        errorMsg += "1. 检查TLE数据格式是否符合NORAD标准\n";
        errorMsg += "2. 确保每颗卫星包含完整的两行根数数据\n";
        errorMsg += "3. 尝试从Celestrak或其他可靠来源重新下载TLE\n";
        errorMsg += "4. 使用单颗卫星导入测试具体的错误原因\n";
        
        // 解析期已禁用入口且 m_batchMode 已提前置位（异步化）: 全部失败时复位
        m_batchMode.store(false);
        m_cancelBtn->setEnabled(false);
        m_predictBtn->setEnabled(true);
        m_batchImportBtn->setEnabled(true);
        m_onlineUpdateBtn->setEnabled(true);
        setExportEnabled(true);
        
        QMessageBox::critical(this, origin, errorMsg);
        return;
    }

    // 批量任务共用 UI 观测站参数, 锁定本次任务坐标（绘图地面标记使用）
    m_taskLatDeg = m_latSpin->value();
    m_taskLonDeg = m_lonSpin->value();
    m_taskAltKm = m_altSpin->value();
    m_taskMaskDeg = m_maskSpin->value();
    m_taskFreqMHz = m_freqSpin->value();
    m_taskGravityModel = static_cast<GravityModel>(m_gravModelCombo->currentData().toInt());

    m_batchMode.store(true);
    m_batchErrors.clear();
    m_lastErrorStatusText.clear();   // 新批量开始: 错误状态栏去重复位
    // 重置批量时效汇总（onMetaInfo 逐星累积）
    m_batchMaxAgeDays = 0.0;
    m_batchStaleCount = 0;
    m_batchMetaCount  = 0;
    m_predictBtn->setEnabled(false);
    m_batchImportBtn->setEnabled(false);
    m_onlineUpdateBtn->setEnabled(false);
    setExportEnabled(false);   // 批量计算期间导出旧数据无意义, 一并禁用
    m_cancelBtn->setEnabled(true);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, jobs.size());
    m_progressBar->setValue(0);
    // 批量模式不逐星显示历元，改为在 onMetaInfo 里累积批量级汇总
    //（最老龄期 + 超期颗数, 见该函数）: 此处须清掉上次单星任务残留的
    // 模型/历元显示（可能带红色"数据过期"样式）, 否则批量期间状态栏悬挂
    // 一颗无关卫星的过期信息, 与"模型: 批量 N 颗"并存产生歧义
    setStatusText(m_statusModel, "模型: —");
    setStatusText(m_statusEpoch, "历元: —");
    m_statusEpoch->setStyleSheet(QString());
    setStatusText(m_statusModel, QString("模型: 批量 %1 颗").arg(jobs.size()));
    if (failed > 0)
        setStatusText(m_statusModel,
            QString("模型: 批量 %1 颗（%2 颗解析失败跳过）")
                .arg(jobs.size()).arg(failed));

    m_service->startBatch(jobs);
}

void MainWindow::onTleError(QString message)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    setStatusText(m_statusModel, "模型: TLE 下载失败");
    
    // 添加错误恢复建议
    QString detailedMessage = message + "\n\n错误恢复建议:\n";
    detailedMessage += "1. 检查网络连接是否正常\n";
    detailedMessage += "2. 确认Celestrak等数据源服务可用\n";
    detailedMessage += "3. 尝试手动导入TLE文件作为替代方案\n";
    detailedMessage += "4. 稍后重试在线更新功能\n";
    detailedMessage += "5. 联系系统管理员检查网络配置\n";
    
    // 显示错误对话框，同时提供恢复选项（合并为单个模态框，避免双弹窗）
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "在线更新失败", detailedMessage + "\n\n是否尝试手动导入TLE文件作为替代？",
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    
    if (reply == QMessageBox::Yes) {
        onBatchImport();  // 触发手动导入
    }
}

// 从当前 UI 控件填充观测站参数与计算配置
void MainWindow::fillJobFromUi(PassPredictWorker::Job &job) const
{
    job.observerLatDeg = m_latSpin->value();
    job.observerLonDeg = m_lonSpin->value();
    job.observerAltKm  = m_altSpin->value();
    job.minElevDeg     = m_maskSpin->value();
    job.startUtc       = m_startTimeEdit->dateTime();
    job.hoursAhead     = qMax(0.0, job.startUtc.secsTo(m_endTimeEdit->dateTime()) / 3600.0);

    // 配置 OrbitCalculator 参数
    // 粗搜步长 15s: 高掩蔽角 + 低轨的短过境（窗口 1~2 分钟）也能可靠检出
    //（60s 步长会概率性漏检；每颗卫星全窗口约 0.2s，批量并行可接受）
    job.calcCfg.coarseStepSec      = 15.0;
    job.calcCfg.bisectionIters     = 18;
    job.calcCfg.enableSunAnalysis  = true;
    job.calcCfg.enableShadowCheck  = true;
    // 大气折射已移除: STK 11 AER 报告为几何仰角
    job.calcCfg.enableDoppler      = true;
    job.calcCfg.carrierFreqMHz     = m_freqSpin->value();
    job.calcCfg.enableMagnitude    = true;
    job.calcCfg.stdMagFallback     = m_stdMagSpin->value();
    job.calcCfg.darkSkyThresholdDeg = m_darkSkyCombo->currentData().toDouble();
    job.calcCfg.gravityModel = static_cast<GravityModel>(m_gravModelCombo->currentData().toInt());
    // TLE 校验和策略: 强制模式下校验失败即拒绝该星（见 TleParser::parse）
    job.strictChecksum = m_strictChecksumCheck && m_strictChecksumCheck->isChecked();
}

void MainWindow::onCancel()
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    // 清除全部计算线程（内部先协作式取消，毫秒级退出；
    // 2000ms 全局截止防极端情况下无限阻塞 UI）
    if (m_service) {
        m_service->cleanupAll(2000);
    }
    // 中止在途的在线 TLE 下载（无活动请求时静默无效）
    if (m_tleFetcher) {
        m_tleFetcher->cancel();
    }
    m_batchMode.store(false);
    m_batchParsePending = false;   // 在途批量解析结果作废（startBatchParsed 守卫）
    m_batchErrors.clear();
    // 复位状态栏: 取消后不再显示已淘汰任务的模型/历元信息
    setStatusText(m_statusModel, "模型: —");
    setStatusText(m_statusEpoch, "历元: —");
    m_statusEpoch->setStyleSheet(QString());
    m_predictBtn->setEnabled(true);
    m_batchImportBtn->setEnabled(true);
    m_onlineUpdateBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(false);
    // 导出进行中不恢复导出按钮: 防止 UI 状态瞬间不一致,
    // onExportFinished 会在导出结束后统一恢复。
    // 判据用 !isFinished(): QtConcurrent 任务"已入队未开跑"时 isRunning() 仍为
    // false, 用 isRunning() 会误判"无导出任务"而提前放开导出按钮（同一 future
    // 句柄被下一次导出覆盖 → 旧任务成为无人等待的孤儿）
    if (!m_exportWatcher || m_exportWatcher->isFinished())
        setExportEnabled(true);
    // 下载流程收尾（cancel() 内已 emit finished，此处为空操作保留语义清晰）
}

void MainWindow::onClear()
{
    if (m_isClosing) return;

    // 清空视图的同时终止在途计算, 防止迟到的结果覆盖刚清空的界面。
    // 用 cleanupAll(2000) 而非 cancelAll: 在途线程被协作式取消后回收
    // （quit+wait), 反复"开始→清除"不残留空转线程/worker 对象
    if (m_service) {
        m_service->cleanupAll(2000);
    }
    if (m_tleFetcher) {
        m_tleFetcher->cancel();
    }
    
    // 使用互斥锁保护共享数据
    QMutexLocker dataLocker(&m_dataMutex);
    m_batchMode.store(false);
    m_batchParsePending = false;   // 在途批量解析结果作废
    m_batchErrors.clear();

    m_tleInput1->clear();
    m_tleInput2->clear();
    m_tableModel->setPasses({});   // 清空懒加载 model
    m_currentPasses.clear();
    m_filteredPasses.clear();   // 过滤结果同步清空（M4: 否则内存滞留过境数据）
    
    // 释放数据锁
    dataLocker.unlock();
    
    if (m_visibleOnlyCheck->isChecked()) {
        // 复位"仅显示可见过境"勾选（阻塞信号避免空数据下的多余刷新）
        m_visibleOnlyCheck->blockSignals(true);
        m_visibleOnlyCheck->setChecked(false);
        m_visibleOnlyCheck->blockSignals(false);
    }
    m_polarPlot->clear();
    m_dopplerPlot->clear();
    m_groundTrack->clear();
    m_detailModel->removeRows(0, m_detailModel->rowCount());
    // 清空后不再有"当前过境": 逐秒页签标题复位（去掉历元标注）
    if (const int tabIdx = m_tabWidget->indexOf(m_detailTable); tabIdx >= 0) {
        m_tabWidget->setTabText(tabIdx, "逐秒数据");
        m_tabWidget->setTabToolTip(tabIdx, QString());
    }
    // 取消逐秒详情后台计算。
    // 判据 !isFinished(): "已入队未开跑"时 isRunning() 为 false, 漏等会让该任务
    // 在下面 storeRelease(0) 之后带着"未取消"状态起跑——它捕获了本对象成员,
    // 并可能把陈旧行回填进刚清空的逐秒表（世代号只能拦住交付, 拦不住执行）
    if (m_detailWatcher && !m_detailWatcher->isFinished()) {
        m_detailCalcCancel.storeRelease(1);
        m_detailWatcher->waitForFinished();
    }
    m_detailCalcCancel.storeRelease(0);
    // 关键: waitForFinished() 只阻塞、不处理事件——被取消任务的 finished 回调
    // 必然在 onClear 返回后才投递。此处必须递增世代号, 否则回调里
    // "gen != m_detailGeneration" 守卫失效（取消标志也已被上面清 0）,
    // 陈旧的部分结果会被回填进刚清空的逐秒表, 并误隐藏进度条
    ++m_detailGeneration;
    // 重置任务快照（防御性: 防止旧参数残留影响后续逻辑）
    m_taskLatDeg = m_latSpin->value();
    m_taskLonDeg = m_lonSpin->value();
    m_taskAltKm = m_altSpin->value();
    m_taskMaskDeg = m_maskSpin->value();
    m_taskFreqMHz = m_freqSpin->value();
    m_taskGravityModel = static_cast<GravityModel>(m_gravModelCombo->currentData().toInt());
    setStatusText(m_statusModel, "模型: —");
    setStatusText(m_statusEpoch, "历元: —");
    m_statusEpoch->setStyleSheet(QString());
    setStatusText(m_statusCount, "过境: 0");
    m_predictBtn->setEnabled(true);
    m_batchImportBtn->setEnabled(true);
    m_onlineUpdateBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(false);
    setExportEnabled(true);
}

// ============================================================================
// 关闭窗口: 先断开信号槽连接，再协作式取消任务，消除析构链中 cleanupAll 的等待冻结感
// ============================================================================
void MainWindow::closeEvent(QCloseEvent *event)
{
    // 设置关闭标志，通知所有线程正在关闭
    m_isClosing.store(true);

    // 首先断开所有信号槽连接，防止在取消过程中有信号触发导致访问已销毁的对象
    disconnectAllConnections();
    
    // 然后取消所有任务
    if (m_service) {
        m_service->cancelAll();      // 立即置协作取消标志, 线程毫秒级退出
    }
    // 线程退出等待由 ~PassPredictorService 的 cleanupAll(2000) 兜底;
    // 此处不阻塞主线程，避免关闭时界面冻结（disconnectAllConnections 已阻断旧信号）
    if (m_tleFetcher) {
        m_tleFetcher->cancel();      // 中止在途下载
    }
    
    // 等待导出线程完成（协作式取消已置位, 导出任务毫秒级退出）
    // 注: 导出函数为同步写入，取消标志仅用于快速退出判断
    // 判据 !isFinished(): 见 disconnectAllConnections 处说明（入队未开跑时 isRunning() 为 false）
    if (m_exportWatcher && !m_exportWatcher->isFinished()) {
        m_exportCancel.storeRelease(1);
        m_exportWatcher->waitForFinished();
    }
    // detail CSV 后台导出: lambda 捕获 QPointer<MainWindow>, 析构后自动跳过,
    // 但仍须等待 future 完成, 防止工作线程在 ~MainWindow 成员销毁后仍访问
    if (!m_detailCsvFuture.isFinished()) {
        m_detailCsvCancel.storeRelease(1);  // 协作取消: 置位后 lambda 在下一过境边界退出
        m_detailCsvFuture.waitForFinished();
    }
    // 批量后台解析任务（startBatchFromBlocks 的 QtConcurrent）无需等待:
    // watcher 为 this 子对象, closeEvent 已先断开连接并置 m_isClosing,
    // 回调入口有 m_isClosing 守卫; 进程退出时 QThreadPool 全局池析构会
    // 兜底等待（千颗级解析延迟数百 ms, 属可接受的设计取舍）

    event->accept();
}

// ============================================================================
// 结果处理
// ============================================================================
void MainWindow::onResultsReady(QVector<PassEvent> passes)
{
    if (m_isClosing) return;

    // 世代守卫: waitForFinished() 处理事件期间可能触发新的 onResultsReady,
    // 通过世代号检测丢弃过期调用，避免重入导致 stale preserveKey
    const int expected = m_resultsGeneration.fetchAndAddRelease(1) + 1;

    // 使用互斥锁保护共享数据
    QMutexLocker dataLocker(&m_dataMutex);

    // 预快照选中身份键与滚动位置: 必须在覆盖 m_currentPasses【之前】——
    // 赋值后 QVector COW 缓冲更换, 旧 PassEvent 全部析构, m_filteredPasses
    // 内的裸指针立即悬垂; 若让 applyFilterAndRefresh 自行快照即触发
    // use-after-free（第二次预报起每次必现）
    QString preserveKey;
    {
        const int prevRow = (m_tableModel->rowCount() > 0)
            ? m_passTable->currentIndex().row() : -1;
        if (prevRow >= 0 && prevRow < m_filteredPasses.size()) {
            const auto *prev = m_filteredPasses[prevRow];
            preserveKey = QString::number(prev->aosJdMs) + "|" +
                          QString::fromStdString(prev->satelliteName);
        }
    }
    const int preserveScroll = m_passTable->verticalScrollBar()->value();

    // 批量模式收尾状态
    bool wasBatchMode = m_batchMode.load();
    m_batchMode.store(false);
    
    // 保存批量错误信息
    QStringList batchErrorsCopy;
    if (wasBatchMode) {
        batchErrorsCopy = m_batchErrors;
        m_batchErrors.clear();
    }

    // 在同一临界区内完成: 更新 m_currentPasses → 重建 m_filteredPasses
    // 确保 m_filteredPasses 的裸指针永远指向 m_currentPasses 中存活的对象
    m_currentPasses = passes;
    QVector<const PassEvent *> filtered;
    if (m_visibleOnlyCheck->isChecked()) {
        for (const auto &p : m_currentPasses)
            if (p.visibility == PassVisibility::Visible)
                filtered.append(&p);
    } else {
        filtered.reserve(m_currentPasses.size());
        for (const auto &p : m_currentPasses)
            filtered.append(&p);
    }
    m_filteredPasses = filtered;

    // 在锁内取消逐秒 watcher: 防止 m_filteredPasses 更新后 worker 仍通过
    // 捕获的 this 指针访问已析构成员（UAF 崩溃）。必须在 m_filteredPasses
    // 赋值后、dataLocker.unlock() 前完成，否则竞态窗口内旧指针悬垂
    // 判据 !isFinished(): 同 onClear——入队未开跑的 watcher 会让 isRunning()
    // 漏判, 该任务随后仍会执行并读取本函数正在替换的过境数据（UAF/越界）
    if (m_detailWatcher && !m_detailWatcher->isFinished()) {
        m_detailCalcCancel.storeRelease(1);
        // 释放锁后再 waitForFinished: 持锁等待可能死锁（worker 完成后
        // 需要主线程事件循环处理 finished 信号，但锁阻止了新事件处理）
        dataLocker.unlock();
        m_detailWatcher->waitForFinished();
        // 重入检测: waitForFinished() 处理事件期间新 resultReady 可能已到达
        if (m_resultsGeneration.loadAcquire() != expected) {
            m_detailCalcCancel.storeRelease(0);
            ++m_detailGeneration;
            return;
        }
        // 已在上方 unlock; 跳过后续 unlock
    } else {
        dataLocker.unlock();
    }
    m_detailCalcCancel.storeRelease(0);
    ++m_detailGeneration;

    // 清空详情页签旧数据（新结果到达时，旧选中的过境已失效）
    m_detailModel->removeRows(0, m_detailModel->rowCount());
    // 页签标题同步复位（去掉上一轮过境的"历元 …"标注，与 onClear 行为一致）
    if (const int tabIdx = m_tabWidget->indexOf(m_detailTable); tabIdx >= 0) {
        m_tabWidget->setTabText(tabIdx, "逐秒数据");
        m_tabWidget->setTabToolTip(tabIdx, QString());
    }

    // 应用过滤和刷新（传入预快照, 避免内部读取已悬垂的旧指针）
    applyFilterAndRefresh(preserveKey, preserveScroll);

    // 批量模式收尾: 汇总失败信息（若有）
    if (wasBatchMode && !batchErrorsCopy.isEmpty()) {
        // 大批量下失败条目可达数千, 弹窗只列前 20 条防止对话框失控
        QStringList shown = batchErrorsCopy.mid(0, 20);
        if (batchErrorsCopy.size() > shown.size())
            shown << QString("…等 %1 项（共 %2 项）")
                       .arg(batchErrorsCopy.size() - shown.size())
                       .arg(batchErrorsCopy.size());
        QMessageBox::warning(this, "批量预报",
            QString("%1 颗卫星预报失败:\n%2")
                .arg(batchErrorsCopy.size())
                .arg(shown.join("\n")));
        // 收尾: 状态栏落最终失败数（节流期间的模数可能未覆盖末条）
        setStatusText(m_statusModel,
                      QString("模型: 批量错误 %1 项").arg(batchErrorsCopy.size()));
    }

    // 恢复控件状态
    m_predictBtn->setEnabled(true);
    m_batchImportBtn->setEnabled(true);
    m_onlineUpdateBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(false);
    setExportEnabled(true);
}

void MainWindow::onChecksumWarning(QString satelliteId)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    // 仅状态栏去重提示: 校验和失败的 TLE 已正常参与计算（非致命）,
    // 不入批量失败汇总、不弹模态框、不复位控件
    showStatusDedup(QString("警告: 卫星 %1 的 TLE 校验和未通过, 计算结果仅供参考")
                        .arg(satelliteId));
}

void MainWindow::onError(QString msg)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }

    // 使用互斥锁保护共享数据
    QMutexLocker dataLocker(&m_dataMutex);
    
    // 批量模式处理
    if (m_batchMode) {
        // 批量模式: 不弹阻塞对话框，累计到结果时统一汇总
        m_batchErrors << msg;
        // 状态栏节流: 每 100 项刷一次进度, 防千颗级失败风暴逐条重绘
        // （实时总量由 batchProgress 进度条呈现, 最终失败数由汇总对话框给出）
        const bool throttleTick = (m_batchErrors.size() % 100 == 0);

        // 解锁后再碰 UI（项目约定: 锁内不做 setText 等界面操作）
        dataLocker.unlock();
        if (throttleTick) {
            setStatusText(m_statusModel,
                          QString("模型: 批量错误 %1 项").arg(m_batchErrors.size()));
        }
        return;
    }
    
    // 释放数据锁
    dataLocker.unlock();
    
    // 单星任务错误处理
    QMessageBox::warning(this, "错误", msg);
    // 单星任务失败: 复位上次任务残留在状态栏的模型/历元, 与新错误提示并存
    // 会造成"明明失败了却显示模型/历元正常"的歧义
    setStatusText(m_statusModel, "模型: —");
    setStatusText(m_statusEpoch, "历元: —");
    m_statusEpoch->setStyleSheet(QString());
    m_predictBtn->setEnabled(true);
    m_batchImportBtn->setEnabled(true);
    m_onlineUpdateBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(false);
    setExportEnabled(true);
}

void MainWindow::onMetaInfo(QString model, QString epochUtc, double ageDays)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }

    // 使用互斥锁保护共享数据
    QMutexLocker dataLocker(&m_dataMutex);
    
    // 批量模式: 不逐星展示（否则状态栏被上千颗卫星的历元刷屏），
    // 但**必须**保留时效风险提示——逐星累积"最老龄期 + 超期颗数"，
    // 在状态栏给出批量级汇总（此前直接 return, 批量用户完全看不到
    // "数据过期"警告, 是最严重的告警通道缺口）
    if (m_batchMode) {
        if (ageDays > m_batchMaxAgeDays) m_batchMaxAgeDays = ageDays;
        if (ageDays > kStaleAgeDays)     ++m_batchStaleCount;
        ++m_batchMetaCount;
        const double maxAge = m_batchMaxAgeDays;
        const int staleCount = m_batchStaleCount;
        const int metaCount  = m_batchMetaCount;
        dataLocker.unlock();

        QString text = QString("历元: 批量 %1 颗（最旧 %2 天）")
                           .arg(metaCount).arg(maxAge, 0, 'f', 1);
        if (staleCount > 0)
            text += QString(" [数据过期 %1 颗]").arg(staleCount);
        setStatusText(m_statusEpoch, text);
        m_statusEpoch->setStyleSheet(staleCount > 0 ? "color:#d04040;" : QString());
        return;
    }
    
    // 释放数据锁
    dataLocker.unlock();
    
    setStatusText(m_statusModel, QString("模型: %1").arg(model));
    // 行业惯例: TLE 龄期 > 14 天预报误差可达数 10 秒，需醒目提示
    const bool stale = ageDays > kStaleAgeDays;
    setStatusText(m_statusEpoch, QString("历元: %1（%2 天）%3")
        .arg(epochUtc).arg(ageDays, 0, 'f', 1)
        .arg(stale ? " [数据过期]" : ""));
    m_statusEpoch->setStyleSheet(stale ? "color:#d04040;" : QString());
}

void MainWindow::onTableRowSelected(int row, int col)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }
    
    Q_UNUSED(col);
    if (row >= 0 && row < m_tableModel->rowCount()) {
        m_polarPlot->setSelectedPass(row);
        m_dopplerPlot->setSelectedPass(row);
        m_groundTrack->setSelectedPass(row);
        showDetailForPass(row);
    }
}

// ============================================================================
// 逐秒详情视图：点击过境行后按需重新传播，显示每秒的观测数据
// ============================================================================
void MainWindow::showDetailForPass(int row)
{
    // 取消上一次未完成的逐秒计算。
    // 判据 !isFinished(): 入队未开跑的旧任务若不等待, 会在下面清掉取消标志后
    // 与新任务并发执行同一个 future 句柄（旧任务成为孤儿, 且两路同时写进度/表）
    if (m_detailWatcher && !m_detailWatcher->isFinished()) {
        m_detailCalcCancel.storeRelease(1);
        m_detailWatcher->waitForFinished();
    }
    m_detailCalcCancel.storeRelease(0);
    ++m_detailGeneration;   // 作废旧回调

    // 清空旧数据
    m_detailModel->removeRows(0, m_detailModel->rowCount());

    // 在锁内拷贝所需字段，防止 m_filteredPasses 被并发覆盖后 UAF
    std::string tleLine1, tleLine2;
    int64_t aosJdMs = 0, losJdMs = 0;
    double taskLat = 0, taskLon = 0, taskAlt = 0, taskMask = 0, taskFreq = 0;
    GravityModel taskGrav = GravityModel::WGS72;
    int tzOffsetSec = 0;
    {
        QMutexLocker locker(&m_dataMutex);
        if (row < 0 || row >= m_filteredPasses.size()) return;
        const PassEvent *ev = m_filteredPasses[row];
        tleLine1 = ev->tleLine1;
        tleLine2 = ev->tleLine2;
        aosJdMs  = ev->aosJdMs;
        losJdMs  = ev->losJdMs;
        taskLat  = m_taskLatDeg;
        taskLon  = m_taskLonDeg;
        taskAlt  = m_taskAltKm;
        taskMask = m_taskMaskDeg;
        taskFreq = m_taskFreqMHz;
        taskGrav = m_taskGravityModel;   // 详情重传播必须复用主结果所用模型
        if (m_timezoneCombo->currentIndex() > 0)
            tzOffsetSec = m_timezoneCombo->currentData().toInt();
    }

    // TLE 为空则跳过（防御性检查）
    if (tleLine1.empty() || tleLine2.empty()) return;

    // 逐秒数据页签标注**本次实际使用的 TLE 历元**:
    // 跨工具（STK/Gpredict…）逐秒比对前必须先确认两边用的是同一份 TLE——
    // 历元差几小时/几天即可造成 km 级沿迹位置差（远超实现差异）。
    // 显示为 "逐秒数据 [历元 yyyy-MM-dd HH:mm, N.N 天]"，便于直接对照
    {
        TleParser::ParseError pe;
        const auto tleMeta = TleParser::parse(tleLine1, tleLine2, &pe);
        if (tleMeta) {
            const QDateTime yearStart(QDate(tleMeta->epochYear, 1, 1), QTime(0, 0), QTimeZone::UTC);
            const QDateTime epochUtc = yearStart.addMSecs(
                static_cast<qint64>((tleMeta->epochDay - 1.0) * 86400000.0 + 0.5));
            const double ageDays = epochUtc.secsTo(QDateTime::currentDateTimeUtc()) / 86400.0;
            const int tabIdx = m_tabWidget->indexOf(m_detailTable);
            if (tabIdx >= 0) {
                m_tabWidget->setTabText(tabIdx,
                    QString("逐秒数据 [历元 %1, %2 天]")
                        .arg(epochUtc.toString("yyyy-MM-dd HH:mm"))
                        .arg(ageDays, 0, 'f', 1));
                m_tabWidget->setTabToolTip(tabIdx,
                    QString("TLE 历元(UTC): %1\n数据龄期: %2 天\n"
                            "跨工具比对请确认两侧 TLE 历元/元素集完全一致")
                        .arg(epochUtc.toString("yyyy-MM-dd HH:mm:ss 'UTC'"))
                        .arg(ageDays, 0, 'f', 2));
            }
        }
    }

    // 计算总步数用于进度条
    const double jdAOS = msToJd(aosJdMs);
    const double jdLOS = msToJd(losJdMs);
    const int totalSteps = std::min(static_cast<int>((jdLOS - jdAOS) * 86400.0) + 1,
                                    kMaxDetailSteps);

    // 显示进度条（确定进度模式）
    m_progressBar->setRange(0, totalSteps);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);

    // 创建/复用 watcher
    if (!m_detailWatcher)
        m_detailWatcher = std::make_unique<QFutureWatcher<void>>(this);
    else
        m_detailWatcher->disconnect();

    auto result = std::make_shared<QVector<QStringList>>();
    const int gen = m_detailGeneration.loadAcquire();

    connect(m_detailWatcher.get(), &QFutureWatcher<void>::finished, this,
        [this, result, gen]() {
            if (m_isClosing) return;
            if (gen != m_detailGeneration.loadAcquire()) return;   // 已被新请求作废
            m_progressBar->setVisible(false);
            if (m_detailCalcCancel.loadAcquire()) return;
            if (result->isEmpty()) {
                m_detailModel->setRowCount(1);
                m_detailModel->setItem(0, 0, new QStandardItem(
                    QStringLiteral("计算完成，无有效数据")));
                return;
            }
            // 填充模型（固定 5 列: 时间/仰角/方位角/距离/多普勒）
            constexpr int cols = 5;
            m_detailModel->setRowCount(result->size());
            for (int i = 0; i < result->size(); ++i) {
                for (int c = 0; c < cols; ++c)
                    m_detailModel->setItem(i, c, new QStandardItem((*result)[i][c]));
            }
        });

    // 后台线程: 逐秒传播 + 坐标转换 + 多普勒 + 格式化
    // （行上限 kMaxDetailSteps 与详情 CSV 导出共用, 定义在文件局部）
    m_detailWatcher->setFuture(QtConcurrent::run(
        [this, result, tleLine1, tleLine2, jdAOS, jdLOS,
         taskLat, taskLon, taskAlt, taskMask, taskFreq, taskGrav,
         tzOffsetSec]() {
        QPointer<MainWindow> safeThis(this);

        try {
            Propagator prop;
            std::string errMsg;
            // 复用主结果所用重力模型: 否则详情表与表格/极坐标口径不一致
            //（WGS72 μ=398600.8/Re=6378.135 vs WGS84 μ=398600.5/Re=6378.137）
            if (!prop.initialize(tleLine1, tleLine2, errMsg, taskGrav)) return;

            Observer obs(taskLat, taskLon, taskAlt, taskMask);
            const double step = 1.0 / 86400.0;
            const double freqMHz = taskFreq;

            int iterCount = 0;
            // 逐秒时刻用"起点 + 整数秒 × 步长": jd += step 会累积浮点漂移
            // （数百步即达亚毫秒级, 与 STK 逐秒数据比对时可见），此处必须精确
            for (int sec = 0; sec < kMaxDetailSteps; ++sec) {
                const double jd = jdAOS + sec * step;
                // 容差 1e-9 天 ≈ 86 µs: 仅用于吸收 jdLOS 在 JD≈2.46e6 处的
                // 表示舍入(ulp≈40 µs), 保证整秒窗口的末行不被漏掉
                if (jd > jdLOS + 1e-9) break;
                if (m_detailCalcCancel.loadAcquire()) return;

                // 与"含逐秒详情"CSV 导出共用同一函数: 页签与导出文件逐点一致
                QStringList row;
                if (computeDetailRow(prop, obs, jd, freqMHz, tzOffsetSec, row))
                    result->append(row);

                ++iterCount;
                if (iterCount % 500 == 0) {
                    QMetaObject::invokeMethod(m_progressBar,
                        [safeThis, iterCount]() {
                            if (safeThis) safeThis->m_progressBar->setValue(iterCount);
                        },
                        Qt::QueuedConnection);
                }
            }
        } catch (...) {
        }
    }));
}

// ============================================================================
// 表格填充（懒加载: 数据入 model, 视图按需请求单元格）
// ============================================================================
void MainWindow::populateTable(const QVector<const PassEvent *> &passes)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }
    
    // 时区表头/时间列由 model 响应 setTimeZoneOffset 自动刷新
    m_tableModel->setTimeZoneOffset(m_timezoneCombo->currentData().toInt());
    m_tableModel->setPasses(passes);
}

// ============================================================================
// 导出
// ============================================================================
void MainWindow::setExportEnabled(bool on)
{
    // 检查对象是否仍然有效
    if (m_isClosing) return;
    
    m_exportCsvBtn->setEnabled(on);
    m_exportKmlBtn->setEnabled(on);
    m_exportJsonBtn->setEnabled(on);
}

void MainWindow::onExportCSV()
{
    if (m_filteredPasses.isEmpty()) return;
    QString filePath = QFileDialog::getSaveFileName(this, "导出 CSV",
        "passes_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd") + ".csv",
        "CSV Files (*.csv)");
    if (filePath.isEmpty()) return;

    // 导出范围与状态栏提示由 startExport 决定（有选中行 → 仅选中过境）;
    // 逐秒详情用同一份范围，保证"主表里有哪些过境，就有哪些 detail 文件"
    QVector<int> detailRows;
    QString scopeNote;
    startExport(ExportKind::Csv, filePath, &detailRows, &scopeNote);

    // 勾选"含逐秒详情"时，为**本次导出的每个过境**生成独立 detail CSV
    //（后台线程按 1 s 重传播写盘）。
    // 仅在主 CSV 导出成功启动后才派生（m_exportLock 仍为 0 表示 startExport 拒绝了）
    if (m_exportDetailCheck->isChecked() && m_exportLock.loadAcquire() != 0) {
        // 深拷贝 PassEvent 值（非指针），防止新预测覆盖 m_currentPasses 后 UAF；
        // 同时快照逐秒重传播参数（测站/掩星角/频率/重力模型/时区）——
        // 工作线程只读按值捕获的副本, 不跨线程访问 MainWindow 成员
        QVector<PassEvent> snapshot;
        QVector<int> snapshotRows;      // 与 snapshot 一一对应: 过境在过滤列表中的下标
        DetailPropagationParams dp;
        QFileInfo fi(filePath);
        QString baseName = fi.completeBaseName();  // 不含扩展名
        QString dir = fi.absolutePath();
        {
            QMutexLocker locker(&m_dataMutex);
            snapshot.reserve(detailRows.size());
            snapshotRows.reserve(detailRows.size());
            for (int r : detailRows) {
                if (r < 0 || r >= m_filteredPasses.size()) continue;
                snapshot.append(*m_filteredPasses[r]);
                snapshotRows.append(r);
            }
            dp.latDeg  = m_taskLatDeg;
            dp.lonDeg  = m_taskLonDeg;
            dp.altKm   = m_taskAltKm;
            dp.maskDeg = m_taskMaskDeg;
            dp.freqMHz = m_taskFreqMHz;
            dp.gravity = m_taskGravityModel;   // 详情重传播必须复用主结果所用模型
            if (m_timezoneCombo->currentIndex() > 0)
                dp.tzOffsetSec = m_timezoneCombo->currentData().toInt();
        }
        // 在主线程构造 QPointer: 安全检查入口, 避免 lambda 内从悬垂指针构造
        QPointer<MainWindow> safeThis(this);
        // 上一轮详单导出仍在进行时, 必须先取消并等待完成:
        // 否则"清取消标志 + 覆盖 future 句柄"会让旧任务变成无人等待的孤儿,
        // 继续解引用本对象成员 detailCancelPtr（窗口销毁后即 UAF）
        if (!m_detailCsvFuture.isFinished()) {
            m_detailCsvCancel.storeRelease(1);
            m_detailCsvFuture.waitForFinished();
        }
        m_detailCsvCancel.storeRelease(0);
        auto *detailCancelPtr = &m_detailCsvCancel;
        m_detailCsvFuture = QtConcurrent::run(
            [safeThis, snapshot, snapshotRows, dp, dir, baseName, detailCancelPtr]() {
            if (!safeThis) return;
            QStringList errors;
            const int total = snapshot.size();
            for (int i = 0; i < total; ++i) {
                if (detailCancelPtr->loadAcquire()) break;  // 协作取消: closeEvent 置位后提前退出
                const PassEvent &p = snapshot[i];
                if (p.tleLine1.empty() || p.tleLine2.empty()) continue;
                // 文件编号 = 表格行号（下标+1）: 用户可在界面/主表里一一对应
                const int passNo = snapshotRows.value(i, i) + 1;
                // 每颗卫星用各自过境携带的 TLE；其余参数与页签同源
                DetailPropagationParams passParams = dp;
                passParams.tleLine1 = p.tleLine1;
                passParams.tleLine2 = p.tleLine2;
                QString detailPath = QString("%1/%2_detail_%3.csv")
                    .arg(dir).arg(baseName).arg(passNo);
                QString err = writePassDetailCsv(p, passParams, detailCancelPtr, detailPath);
                if (!err.isEmpty() && !detailCancelPtr->loadAcquire())
                    errors.append(QString("过境%1: %2").arg(passNo).arg(err));
                // 逐秒重传播比原实现（15 s 采样直写）耗时更长, 状态栏给出进度
                if (!detailCancelPtr->loadAcquire()) {
                    QMetaObject::invokeMethod(safeThis, [safeThis, i, total, passNo]() {
                        if (!safeThis || safeThis->m_isClosing) return;
                        safeThis->showStatusDedup(
                            QString("逐秒详情导出: %1/%2（过境 #%3）")
                                .arg(i + 1).arg(total).arg(passNo));
                    }, Qt::QueuedConnection);
                }
            }
            if (!errors.isEmpty() && !detailCancelPtr->loadAcquire()) {
                QString msg = QString("详情CSV导出失败 (%1 个):\n%2")
                    .arg(errors.size()).arg(errors.join("\n"));
                QMetaObject::invokeMethod(safeThis, [safeThis, msg]() {
                    if (!safeThis || safeThis->m_isClosing) return;
                    safeThis->showStatusDedup(msg);
                }, Qt::QueuedConnection);
            }
        });
    }
}

void MainWindow::onExportKML()
{
    if (m_filteredPasses.isEmpty()) return;
    QString filePath = QFileDialog::getSaveFileName(this, "导出 KML",
        "passes_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd") + ".kml",
        "KML Files (*.kml)");
    if (filePath.isEmpty()) return;
    startExport(ExportKind::Kml, filePath);
}

void MainWindow::onExportJSON()
{
    if (m_filteredPasses.isEmpty()) return;

    // 按**实际导出范围**估算 JSON 大小（有选中行时只算选中的过境，含 samples 数组时可能很大）
    const QVector<int> selRows = selectedPassRows();
    qint64 estimatedBytes = 0;
    {
        QMutexLocker locker(&m_dataMutex);
        auto accum = [&estimatedBytes](const PassEvent *p) {
            estimatedBytes += 500;  // 过境摘要 ~500 字节
            if (!p->samples.empty())
                estimatedBytes += p->samples.size() * 120;  // 每采样 ~120 字节 JSON
        };
        if (selRows.isEmpty()) {
            for (const PassEvent *p : m_filteredPasses) accum(p);
        } else {
            for (int r : selRows)
                if (r >= 0 && r < m_filteredPasses.size()) accum(m_filteredPasses[r]);
        }
    }
    if (estimatedBytes > 50 * 1024 * 1024) {  // > 50 MB
        auto ret = QMessageBox::warning(this, "导出确认",
            QStringLiteral("JSON 文件预计 %.1f MB（%1），可能较大。是否继续？")
                .arg(estimatedBytes / (1024.0 * 1024.0))
                .arg(selRows.isEmpty() ? QStringLiteral("全部过境")
                                       : QStringLiteral("仅选中的 %1 个过境").arg(selRows.size())),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
    }

    QString filePath = QFileDialog::getSaveFileName(this, "导出 JSON",
        "passes_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd") + ".json",
        "JSON Files (*.json)");
    if (filePath.isEmpty()) return;
    startExport(ExportKind::Json, filePath);
}

QVector<int> MainWindow::selectedPassRows() const
{
    QVector<int> rows;
    if (m_isClosing || !m_passTable || !m_passTable->selectionModel()) return rows;
    const QModelIndexList sel = m_passTable->selectionModel()->selectedRows();
    rows.reserve(sel.size());
    for (const QModelIndex &ix : sel)
        if (ix.isValid()) rows.append(ix.row());
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;   // 空 = 无选中 → 调用方按"全部"处理
}

void MainWindow::startExport(ExportKind kind, const QString &filePath,
                             QVector<int> *rowsOut, QString *scopeOut)
{
    // 检查是否正在关闭
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_isClosing) return;
    }
    
    // 使用原子操作检查导出锁
    if (m_exportLock.fetchAndAddAcquire(1) != 0) {
        // 如果已经有导出在进行，提示用户并返回
        m_exportLock.fetchAndAddRelease(-1);
        showStatusDedup(QStringLiteral("导出正在进行中，请稍候"));
        return;
    }
    
    // 计算/下载在途时禁止导出（入口按钮已同步禁用, 此处双保险）:
    // 导出线程直接读共享过境指针, 要求数据在导出期间不可变。
    // 不能静默 return: 一旦进度条因取消路径残留（见 applyFilterAndRefresh 的
    // 显式复位）, 用户会看到"按钮能点但完全没反应"
    if (m_progressBar->isVisible()) {
        m_exportLock.fetchAndAddRelease(-1);
        showStatusDedup("导出未执行: 计算/下载进行中，请稍候或先取消");
        return;
    }

    // 导出范围: 过境列表中有选中行 → **只导出选中的过境**（及其逐秒详情）;
    // 无选中行 → 全部（过滤后的）过境，保持旧行为。
    // rowsOut 回传下标供详情文件命名（下标+1 = 表格行号，用户可一一对应）
    const QVector<int> selRows = selectedPassRows();

    // 锁定一切数据变更入口（清除/计算/下载/取消/导出）:
    // 此后 onResultsReady / onClear 均不可达, m_currentPasses 不可变,
    // 写盘线程解引用共享指针无悬垂风险, 也无需深拷贝快照（零主线程开销）
    setExportEnabled(false);
    m_clearBtn->setEnabled(false);
    m_predictBtn->setEnabled(false);
    m_batchImportBtn->setEnabled(false);
    m_onlineUpdateBtn->setEnabled(false);
    m_cancelBtn->setEnabled(false);

    // 入口恢复（范围为空等早退路径必须成对调用，否则导出入口永久禁用）
    auto restoreEntryUi = [this]() {
        setExportEnabled(true);
        m_clearBtn->setEnabled(true);
        m_predictBtn->setEnabled(true);
        m_batchImportBtn->setEnabled(true);
        m_onlineUpdateBtn->setEnabled(true);
        m_cancelBtn->setEnabled(false);
    };

    if (!m_exportWatcher) {
        m_exportWatcher = std::make_unique<QFutureWatcher<QString>>(this);
        connect(m_exportWatcher.get(), &QFutureWatcher<QString>::finished,
                this, &MainWindow::onExportFinished);
    }
    
    // 使用互斥锁保护共享数据
    QMutexLocker dataLocker(&m_dataMutex);
    
    // 指针向量 COW 浅拷贝（8B/项, 微秒级）: 即使主线程重建 m_filteredPasses
    // 也不影响本线程的只读视图; 过境对象本身因入口锁定不可变
    QVector<const PassEvent *> src;
    QVector<int> usedRows;
    const int total = m_filteredPasses.size();
    if (selRows.isEmpty()) {
        src = m_filteredPasses;
        usedRows.reserve(total);
        for (int i = 0; i < total; ++i) usedRows.append(i);
    } else {
        src.reserve(selRows.size());
        usedRows.reserve(selRows.size());
        for (int r : selRows) {
            if (r < 0 || r >= total) continue;   // 选中行已失效（结果被替换）
            src.append(m_filteredPasses[r]);
            usedRows.append(r);
        }
    }
    const double taskLat = m_taskLatDeg;
    const double taskLon = m_taskLonDeg;
    
    // 释放数据锁
    dataLocker.unlock();

    if (src.isEmpty()) {
        m_exportLock.fetchAndAddRelease(-1);
        restoreEntryUi();
        showStatusDedup(QStringLiteral("导出未执行: 没有可导出的过境"));
        return;
    }

    if (rowsOut)  *rowsOut  = usedRows;
    if (scopeOut) {
        *scopeOut = selRows.isEmpty()
            ? QString("未选择行 → 导出全部 %1 个过境").arg(src.size())
            : QString("仅导出表格中选中的 %1 个过境").arg(src.size());
        showStatusDedup(*scopeOut);
    }
    
    // 使用弱指针模式，确保在导出过程中对象被销毁时能够安全处理
    // 导出函数支持协作式取消: closeEvent 置位 m_exportCancel 后写操作
    // 在下一过境边界处提前退出，避免阻塞关闭流程
    m_exportCancel.storeRelease(0);
    auto *cancelPtr = &m_exportCancel;
    m_exportWatcher->setFuture(
        QtConcurrent::run([kind, filePath, taskLat, taskLon, src, cancelPtr]() -> QString {
            switch (kind) {
            case ExportKind::Csv:  return writePassesCsv(src, filePath, cancelPtr);
            case ExportKind::Kml:  return writePassesKml(src, filePath, taskLat, taskLon, cancelPtr);
            case ExportKind::Json: return writePassesJson(src, filePath, cancelPtr);
            }
            return QString();
        }));
}

void MainWindow::onExportFinished()
{
    // 检查对象是否仍然有效
    if (m_isClosing) {
        m_exportLock.fetchAndAddRelease(-1);   // 早退也必须释放（配对缺口会永久禁用导出）
        return;
    }
    
    // 释放导出锁
    m_exportLock.fetchAndAddRelease(-1);
    
    // 无论成败都恢复全部入口（仅在 watcher 回调, 无泄漏）;
    // 共享数据指针的生命周期由 startExport 的入口锁定保证
    setExportEnabled(true);
    m_clearBtn->setEnabled(true);
    m_predictBtn->setEnabled(true);
    m_batchImportBtn->setEnabled(true);
    m_onlineUpdateBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    // result() 会重抛工作线程里被 QtConcurrent 捕获的异常（磁盘/内存类），
    // 槽内必须捕获, 否则异常穿出事件循环直接 terminate
    QString err;
    try {
        err = m_exportWatcher->result();
    } catch (const std::exception &e) {
        err = QString("导出线程异常: %1").arg(QString::fromUtf8(e.what()));
    } catch (...) {
        err = QStringLiteral("导出线程异常（未知类型）");
    }
    if (!err.isEmpty())
        QMessageBox::warning(this, "导出失败", err);
}

// ============================================================================
// 构造/析构
// ============================================================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_exportLock(0)
{
    // 初始化错误处理系统
    setupErrorHandling();
    
    setupUI();
    // 确保UI完全设置完成后再建立信号槽连接
    setupConnections();
    
    // 初始化完成，显示欢迎信息
    #ifdef QT_DEBUG
    qDebug() << "MainWindow initialized successfully";
    #endif
}

MainWindow::~MainWindow()
{
    // 设置关闭标志，通知所有线程正在关闭
    m_isClosing.store(true);

    // 在析构前断开所有信号槽连接，防止在对象销毁后有信号触发
    disconnectAllConnections();

    // 析构路径兜底: closeEvent 并非必经（父对象销毁 / 会话结束 / 直接 delete）。
    // disconnectAllConnections 只等了逐秒详情 watcher, 这里补齐导出与
    // detail CSV 两个 QtConcurrent 任务的等待——它们捕获了 MainWindow 的成员
    // (m_exportCancel / m_detailCsvCancel), 不等就可能在工作线程里访问已析构成员。
    // 判据用 !isFinished(): 任务"已入队未开跑"时 isRunning() 为 false（会漏等）
    if (m_exportWatcher && !m_exportWatcher->isFinished()) {
        m_exportCancel.storeRelease(1);
        m_exportWatcher->waitForFinished();
    }
    if (!m_detailCsvFuture.isFinished()) {
        m_detailCsvCancel.storeRelease(1);
        m_detailCsvFuture.waitForFinished();
    }

    // 清理错误处理相关资源
    ErrorHandler::instance().clearErrorHistory();
    
    // 服务对象由 Qt 父子关系自动管理，无需显式调用
}

void MainWindow::setupErrorHandling()
{
    // 连接错误处理系统的信号
    connect(&ErrorHandler::instance(), &ErrorHandler::errorOccurred,
            this, &MainWindow::onErrorOccurred);
    
    connect(&ErrorHandler::instance(), &ErrorHandler::errorRecoveryAttempt,
            this, &MainWindow::onErrorRecoveryAttempt);
    
    connect(&ErrorHandler::instance(), &ErrorHandler::errorRecoveryResult,
            this, &MainWindow::onErrorRecoveryResult);
    
    // 设置是否启用自动恢复
    ErrorHandler::instance().setAutoRecoveryEnabled(true);
    
    #ifdef QT_DEBUG
    qDebug() << "Error handling system initialized";
    #endif
}

void MainWindow::onErrorOccurred(const ErrorInfo& error)
{
    if (m_isClosing) return;

    // 状态栏文本去重（连续相同消息只刷一次）: 批量失败风暴（千颗同因失败）
    // 下错误信号逐条到达, 逐条重绘会占满 UI 线程; 首条已表达全部信息
    // （实现见 showStatusDedup）

    // 根据错误严重程度决定如何处理
    switch (error.severity) {
    case ErrorSeverity::INFO:
        // 信息性错误，只在状态栏显示
        showStatusDedup(QString("信息: %1").arg(error.message));
        break;
        
    case ErrorSeverity::WARNING:
        // 警告错误，显示在状态栏并可能影响UI
        showStatusDedup(QString("警告: %1").arg(error.message));
        
        // 如果是TLE解析警告，可以继续处理
        if (error.type == ErrorType::TLE_PARSE_ERROR) {
            Q_UNUSED(error);
        }
        break;
        
    case ErrorSeverity::ERROR:
        // 错误，状态栏显示（阻塞对话框由 worker→service→onError 路径统一处理:
        // 单星弹窗/批量汇总, 避免此处无条件弹窗导致重复弹窗或批量刷屏）
        showStatusDedup(QString("错误: %1").arg(error.message));
        
        // 如果是网络错误，可能需要重试
        if (error.type == ErrorType::NETWORK_ERROR) {
            handleNetworkError(error);
        }
        break;
        
    case ErrorSeverity::CRITICAL:
        // 严重错误，显示严重错误对话框
        showStatusDedup(QString("严重错误: %1").arg(error.message));
        
        // 批量模式下不弹模态（千颗失败风暴 = 千次弹窗刷屏, 状态栏已提示;
        // 当前 CRITICAL 制造路径在生产代码中无调用者, 门控为未来复用防御）
        if (!m_batchMode) {
            QMessageBox::critical(this, "严重错误", 
                                error.message + "\n\n详细信息:\n" + error.detailedContext + 
                                "\n\n建议操作:\n" + error.suggestedAction);
        }
        
        // 对于严重错误，可能需要禁用某些功能
        handleCriticalError(error);
        break;
    }
}

void MainWindow::onErrorRecoveryAttempt(const ErrorInfo& error, RecoveryAction action)
{
    if (m_isClosing) return;

    // 显示恢复尝试信息
    QString actionText;
    switch (action) {
    case RecoveryAction::RETRY:
        actionText = "重试";
        break;
    case RecoveryAction::SKIP:
        actionText = "跳过";
        break;
    case RecoveryAction::FALLBACK:
        actionText = "使用备用方案";
        break;
    case RecoveryAction::ABORT:
        actionText = "中止";
        break;
    case RecoveryAction::MANUAL_INTERVENTION:
        actionText = "需要人工干预";
        break;
    }
    
    showStatusDedup(QString("恢复尝试: %1 (%2)").arg(error.message).arg(actionText));
    
    #ifdef QT_DEBUG
    qDebug() << "Recovery attempt for error:" << error.message << "Action:" << actionText;
    #endif
}

void MainWindow::onErrorRecoveryResult(const ErrorInfo& error, bool success)
{
    if (m_isClosing) return;

    if (success) {
        showStatusDedup(QString("恢复成功: %1").arg(error.message));
        
        // 如果是TLE解析错误恢复成功，可能需要更新UI
        if (error.type == ErrorType::TLE_PARSE_ERROR) {
            // 可以在这里更新TLE列表或重新计算
        }
    } else {
        // 恢复失败: 仅状态栏提示。错误详情已由 onErrorOccurred 状态栏/
        // 弹窗路径呈现（单星弹窗、批量汇总、CRITICAL 弹窗）——此处若弹
        // 模态对话框, 批量千颗失败会逐条刷屏（用户需点击上千次）
        showStatusDedup(QString("恢复失败: %1").arg(error.message));
    }
}

void MainWindow::handleNetworkError(const ErrorInfo& error)
{
    // 网络错误的特殊处理
    if (error.type == ErrorType::NETWORK_ERROR) {
        // 批量模式下不弹模态对话框（千颗级批量会逐条刷屏），仅状态栏提示
        if (m_batchMode) {
            showStatusDedup(QString("网络错误: %1").arg(error.message));
            return;
        }
        // 检查是否应该重试
        bool shouldRetry = ErrorHandler::instance().shouldRetry(error);
        
        if (shouldRetry) {
            // 显示重试对话框
            QMessageBox::StandardButton reply = QMessageBox::question(
                this, "网络错误", 
                error.message + "\n\n是否重试？",
                QMessageBox::Yes | QMessageBox::No);
            
            if (reply == QMessageBox::Yes) {
                // 执行重试逻辑
                retryNetworkOperation(error);
            }
        }
    }
}

void MainWindow::handleCriticalError(const ErrorInfo& error)
{
    // 严重错误的特殊处理
    if (error.type == ErrorType::CONFIG_ERROR) {
        // 批量模式下不弹模态询问框（与上方 CRITICAL 弹窗同一防刷屏门控:
        // 千颗级批量会连弹等量模态框卡死界面）, 仅状态栏提示
        if (m_batchMode) {
            showStatusDedup(QString("配置错误: %1").arg(error.message));
            return;
        }
        // 配置错误，可能需要重置配置
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "配置错误",
            error.message + "\n\n是否重置为默认配置？",
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            resetToDefaultConfig();
        }
    }
    
    // 对于其他严重错误，可能需要禁用某些功能
    if (error.type == ErrorType::FILE_IO_ERROR) {
        // 文件IO错误: 临时禁用导出（10秒后自动恢复），避免永久锁定
        setExportEnabled(false);
        QTimer::singleShot(10000, this, [this]() {
            if (!m_isClosing) setExportEnabled(true);
        });
    }
}

void MainWindow::retryNetworkOperation(const ErrorInfo &info)
{
    Q_UNUSED(info);   // Release 下仅 QT_DEBUG 分支使用
    // 重试网络操作: 重新触发当前 TLE 组的在线下载
    //（NETWORK_ERROR 当前生产无触发路径——createNetworkError 无调用者,
    // 本函数为未来网络链路复用预留, 重试语义与 onOnlineUpdate 一致）
    
    #ifdef QT_DEBUG
    qDebug() << "Retrying network operation for error:" << info.message;
    #endif
    
    // 显示重试进度
    setStatusText(m_statusModel, "正在重试网络操作...");
    
    onOnlineUpdate();
}

void MainWindow::resetToDefaultConfig()
{
    // 重置为默认配置的逻辑
    // 这里可以重置所有的UI控件到默认值
    
    #ifdef QT_DEBUG
    qDebug() << "Resetting to default configuration";
    #endif
    
    // 重置观测站位置（与 setupUI 默认值一致）
    m_latSpin->setValue(33.0);
    m_lonSpin->setValue(103.0);
    m_altSpin->setValue(0.05);     // 50米海拔
    
    // 重置其他配置
    m_maskSpin->setValue(10.0);    // 10度掩星角
    m_startTimeEdit->setDateTime(QDateTime::currentDateTimeUtc());
    m_endTimeEdit->setDateTime(QDateTime::currentDateTimeUtc().addSecs(24 * 3600));
    m_freqSpin->setValue(437.800);  // 437.800 MHz（与 setupUI 默认一致）
    m_stdMagSpin->setValue(4.5);    // 标准星等（与 setupUI 默认一致）
    
    // 重置暗天阈值
    m_darkSkyCombo->setCurrentIndex(0);

    // 重置重力模型
    m_gravModelCombo->setCurrentIndex(0);  // WGS-72 标准

    // 重置数据策略开关（默认为宽松校验 + 使用本地缓存）
    if (m_strictChecksumCheck) m_strictChecksumCheck->setChecked(false);
    if (m_forceRefreshCheck)   m_forceRefreshCheck->setChecked(false);

    setStatusText(m_statusModel, "配置已重置为默认值");
}

#include "MainWindow.moc"
```

---

## ui/MainWindow.h

```cpp
#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H
// =============================================================================
// MainWindow.h — 主窗口
// 三页签: 过境列表 / 极坐标图 / 地面轨迹图
// 支持单星和批量模式，CSV/KML/JSON 导出
// =============================================================================
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QTableView>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QTabWidget>
#include <QDateTimeEdit>
#include <QFutureWatcher>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QAtomicInt>
#include <memory>
#include "core/DataTypes.h"
#include "core/ErrorHandler.h"
#include "business/PassPredictorService.h"
#include "business/TleFetcher.h"
#include "business/TleBatchParser.h"
#include "PassTableModel.h"
#include "core/Propagator.h"
#include "core/OrbitCalculator.h"
#include "core/CoordTransform.h"
#include "core/Observer.h"
#include "core/DopplerCalc.h"
#include <QStandardItemModel>

QT_BEGIN_NAMESPACE
class PolarPlotWidget;
class GroundTrackWidget;
class DopplerPlotWidget;
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    // 关窗前先协作式取消在途计算/下载, 避免析构链中等待线程导致的冻结感
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onPredict();
    void onBatchImport();
    void onOnlineUpdate();
    void onTleDownloaded(QString rawText, QString groupId);
    void onTleError(QString message);
    void onCancel();
    void onClear();
    void onExportCSV();
    void onExportKML();
    void onExportJSON();
    void onResultsReady(QVector<PassEvent> passes);
    void onError(QString msg);
    // TLE 校验和警告: 非致命, 仅状态栏去重提示
    void onChecksumWarning(QString satelliteId);
    void onTableRowSelected(int row, int col);
    void showDetailForPass(int row);
    void onMetaInfo(QString model, QString epochUtc, double ageDays);

    // 统一错误处理: ErrorHandler 信号接入（错误必然上报, 批量模式只入汇总）
    void onErrorOccurred(const ErrorInfo &error);
    void onErrorRecoveryAttempt(const ErrorInfo &error, RecoveryAction action);
    void onErrorRecoveryResult(const ErrorInfo &error, bool success);

private:
    void setupUI();
    void setupConnections();
    void disconnectAllConnections();
    void populateTable(const QVector<const PassEvent *> &passes);
    // 从当前 UI 控件填充 Job 的观测站参数与计算配置
    void fillJobFromUi(PassPredictWorker::Job &job) const;
    // 起止时间合法性校验（结束必须晚于开始；否则 hoursAhead=0 会静默算出 0 条过境）
    bool ensureValidTimeWindow();
    // 公共批量启动: 解析校验 TLE 块 → 构建任务 → 启动批量预报。
    // 解析在 QtConcurrent 线程池执行（startBatchFromBlocks 异步化）,
    // 完成后经 startBatchParsed 在主线程继续
    void startBatchFromBlocks(const QVector<TleBlock> &blocks, const QString &origin);
    void startBatchParsed(const QVector<PassPredictWorker::Job> &jobs, int failed,
                          int lenIssues, int fmtIssues, const QString &origin);
    // 按"仅显示可见过境"开关过滤 m_currentPasses → m_filteredPasses 并刷新全部视图。
    // preserveKey/preserveScroll: 调用方在共享数据被覆盖【之前】预快照的
    // 选中身份键与滚动位置（如 onResultsReady —— 覆盖后 m_filteredPasses
    // 旧指针即悬垂, 函数内部无法再安全读取）; 缺省时由函数内部从仍有效的
    // m_filteredPasses 快照（过滤开关/时区切换路径）
    void applyFilterAndRefresh(const QString &preserveKey = QString(),
                               int preserveScroll = -1);
    // 统一错误处理接线（ctor 末尾调用）
    void setupErrorHandling();
    // 网络/严重错误专项处理与恢复动作
    void handleNetworkError(const ErrorInfo &error);
    void handleCriticalError(const ErrorInfo &error);
    void retryNetworkOperation(const ErrorInfo &error);
    void resetToDefaultConfig();
    // 状态栏标签: 同步文本与悬浮提示（文本过长时完整内容可悬停查看）
    void setStatusText(QLabel *label, const QString &text);
    // 状态栏去重刷新: 连续相同文本只刷一次（错误风暴节流, 供错误/恢复信号共用）
    void showStatusDedup(const QString &text);
    QVector<PassEvent> m_currentPasses;         // 原始全部过境（唯一所有权）
    QVector<const PassEvent *> m_filteredPasses; // 过滤后（共享指针, 零深拷贝）

    // 导出异步化: 写盘在 QtConcurrent 线程池直接读共享过境指针执行
    //（导出期间一切数据变更入口被锁定, 数据不可变, 无需深拷贝快照）
    enum class ExportKind { Csv, Kml, Json };
    // 导出范围: 表格中有选中行 → 只导出这些过境; 无选中行 → 全部（过滤后的）过境。
    // rowsOut 回传被导出过境在 m_filteredPasses 中的下标（详情文件名沿用 下标+1，
    // 与表格行号一致）；scopeOut 回传范围说明文本（状态栏提示）。
    void startExport(ExportKind kind, const QString &filePath,
                     QVector<int> *rowsOut = nullptr, QString *scopeOut = nullptr);
    // 表格中选中的过境行（升序去重）；返回空表示"无选中 → 全部"
    QVector<int> selectedPassRows() const;
    void onExportFinished();
    // 计算/下载在途或导出进行中统一禁用导出入口
    void setExportEnabled(bool on);
    std::unique_ptr<QFutureWatcher<QString>> m_exportWatcher;
    QFuture<void> m_detailCsvFuture;   // detail CSV 后台导出（fire-and-forget → 跟踪等待）
    
    // 线程同步机制
    // 注: 曾有 QWaitCondition/QAtomicInt 计数器脚手架, 但全部增减均发生在
    // GUI 线程槽内, 不存在并发竞争, 属误导性死重量, 已移除（第9轮审查 A6）
    mutable QMutex m_dataMutex;           // 保护共享数据的互斥锁
    std::atomic<bool> m_isClosing{false}; // 窗口关闭标志（atomic: 跨线程无锁读）
    QAtomicInt m_exportLock;             // 导出锁，防止并发导出
    std::unique_ptr<QFutureWatcher<void>> m_detailWatcher;  // 逐秒详情计算 watcher
    QAtomicInt m_detailCalcCancel;       // 逐秒计算取消标志（1=取消）
    QAtomicInt m_detailGeneration;       // 逐秒计算世代号（每次新计算 +1，finished 回调校验）
    QAtomicInt m_resultsGeneration;      // 结果处理世代号（onResultsReady 重入守卫）
    QAtomicInt m_exportCancel;           // 导出取消标志（1=取消, closeEvent 置位）
    QAtomicInt m_detailCsvCancel;        // 详情 CSV 导出取消标志（1=取消, closeEvent 置位）

    // 输入控件
    QPlainTextEdit  *m_tleInput1;
    QPlainTextEdit  *m_tleInput2;
    QDoubleSpinBox  *m_latSpin;
    QDoubleSpinBox  *m_lonSpin;
    QDoubleSpinBox  *m_altSpin;
    QDoubleSpinBox  *m_maskSpin;
    QComboBox       *m_timezoneCombo;
    QComboBox       *m_darkSkyCombo;
    QComboBox       *m_gravModelCombo;
    QComboBox       *m_tleGroupCombo;
    QDoubleSpinBox  *m_freqSpin;
    QDoubleSpinBox  *m_stdMagSpin;   // 固有星等（1000 km）: 无目录匹配时回退值
    QCheckBox       *m_forceRefreshCheck;   // 在线更新: 忽略本地缓存强制联网
    QCheckBox       *m_strictChecksumCheck; // TLE 校验和强制模式
    QCheckBox       *m_visibleOnlyCheck;
    QDateTimeEdit   *m_startTimeEdit;
    QDateTimeEdit   *m_endTimeEdit;

    // 控制按钮
    QPushButton *m_predictBtn;
    QPushButton *m_batchImportBtn;
    QPushButton *m_onlineUpdateBtn;
    QPushButton *m_cancelBtn;
    QPushButton *m_exportCsvBtn;
    QPushButton *m_exportKmlBtn;
    QPushButton *m_exportJsonBtn;
    QPushButton *m_clearBtn;
    QCheckBox   *m_exportDetailCheck;  // "包含逐秒详情" 导出选项

    // 批量模式状态
    std::atomic<bool> m_batchMode{false};  // 当前是否为批量预报（atomic: 跨线程无锁读）
    bool m_batchParsePending = false; // 批量后台解析在途（TleFetcher::finished 收尾守卫用）
    QStringList m_batchErrors;        // 批量中各任务错误信息（汇总显示）
    // onErrorOccurred 状态栏去重: 连续相同消息只刷一次, 防批量失败风暴逐条重绘
    QString m_lastErrorStatusText;
    // 本次任务实际使用的观测站坐标（预报发起时锁定, 绘图地面标记用;
    // 防止预报后用户改动输入框导致标记漂移与结果不一致）
    double m_taskLatDeg = 0.0;
    double m_taskLonDeg = 0.0;
    double m_taskAltKm = 0.05;
    double m_taskMaskDeg = 10.0;
    double m_taskFreqMHz = 437.800;
    // 本次任务使用的重力模型（详情视图逐秒重传播必须复用同一模型,
    // 否则表格/极坐标与详情表口径不一致）
    GravityModel m_taskGravityModel = GravityModel::WGS72;

    // 批量模式的 TLE 时效汇总（onMetaInfo 逐星累积, 状态栏给出批量级提示）:
    // 批量下不逐星显示历元, 但"数据过期"这类风险必须让用户看到
    double m_batchMaxAgeDays = 0.0;   // 本批最旧 TLE 的龄期（天）
    int    m_batchStaleCount = 0;     // 超过 kStaleAgeDays 的颗数
    int    m_batchMetaCount  = 0;     // 已回报元信息的颗数

    // 状态栏
    QLabel       *m_statusModel;
    QLabel       *m_statusEpoch;
    QLabel       *m_statusCount;
    QProgressBar *m_progressBar;

    // 结果区
    QTabWidget   *m_tabWidget;
    QTableView   *m_passTable;      // 懒加载视图: 仅请求可见单元格
    PassTableModel *m_tableModel;
    PolarPlotWidget  *m_polarPlot;
    GroundTrackWidget *m_groundTrack;
    DopplerPlotWidget *m_dopplerPlot;
    QTableView   *m_detailTable;    // 逐秒数据页签
    QStandardItemModel *m_detailModel;

    // 业务层 - 使用智能指针管理生命周期
    std::unique_ptr<PassPredictorService> m_service;
    std::unique_ptr<TleFetcher> m_tleFetcher;
};

#endif // UI_MAINWINDOW_H
```

---

## ui/PassTableModel.cpp

```cpp
// =============================================================================
// PassTableModel.cpp — 过境列表懒加载模型实现
// 单元格文本/颜色/对齐/提示全部按角色惰性计算, 仅可见单元格被求值
// =============================================================================
#include "PassTableModel.h"
#include <QTimeZone>
#include <QDateTime>
#include <QBrush>

PassTableModel::PassTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void PassTableModel::setPasses(const QVector<const PassEvent *> &passes)
{
    {
        QMutexLocker locker(&m_dataMutex);

        // 指针集合未变（时区切换、同参数重算等）: 跳过全量 reset,
        // 保留视图滚动位置与当前选中（setTimeZoneOffset 已另行发通知）
        if (m_passes == passes) return;
        m_passes = passes;
    }

    // 必须在锁外发射 reset 信号: 视图会同步回调 data()/rowCount()
    // 再抢同一把非递归锁, 持锁 reset 会与 data() 死锁
    beginResetModel();
    endResetModel();
}

void PassTableModel::setTimeZoneOffset(int tzOffsetSec)
{
    QMutexLocker locker(&m_dataMutex);
    
    if (m_tzOffsetSec == tzOffsetSec) return;
    m_tzOffsetSec = tzOffsetSec;

    // 快照行数后再释放锁，避免解锁后读 m_passes 的数据竞争
    const int rows = m_passes.size();
    locker.unlock();

    // 表头与时间列随之变化（时区敏感列含可见弧段 ColArc:
    // 其表头打 tz 标签、单元格经 jdMsToStr 格式化, 漏通知会与
    // 入境/出境列显示不一致）
    emit headerDataChanged(Qt::Horizontal, ColAos, ColArc);
    if (rows > 0)
        emit dataChanged(index(0, ColAos), index(rows - 1, ColArc));
}

QString PassTableModel::tzLabel() const
{
    if (m_tzOffsetSec == 0) return QStringLiteral("UTC");
    return QStringLiteral("UTC%1%2")
        .arg(m_tzOffsetSec > 0 ? QStringLiteral("+") : QStringLiteral("-"))
        .arg(qAbs(m_tzOffsetSec) / 3600);
}

QString PassTableModel::jdMsToStr(int64_t jdMs) const
{
    // JD×86400000 毫秒 → 1970 epoch 毫秒（纯整数运算，无舍入/精度损失）
    qint64 msecs = jdMs - kJdEpochOffsetMs;  // 2440587.5 × 86400000
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(msecs, QTimeZone::UTC);
    if (m_tzOffsetSec != 0)
        dt = dt.toTimeZone(QTimeZone(m_tzOffsetSec));
    return dt.toString("HH:mm:ss");
}

int PassTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_passes.size();
}

int PassTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant PassTableModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const
{
    if (orientation != Qt::Horizontal || section < 0 || section >= ColCount)
        return QVariant();

    if (role == Qt::DisplayRole) {
        static const char *labels[ColCount] = {
            "#", "卫星名", "入境", "出境", "持续",
            "最大仰角", "方位角", "可见性", "太阳高度", "卫星光照",
            "可见弧段", "入境位置", "斜距", "多普勒", "星等", "质量"
        };
        static_assert(sizeof(labels) / sizeof(labels[0]) == ColCount,
                      "labels 数组大小必须与 ColCount 列数匹配");
        // 时间列（入境/出境/可见弧段）内容均经 jdMsToStr 按时区偏移,
        // 表头须统一标注时区, 否则弧段列易被误读为 UTC
        if (section == ColAos || section == ColLos
            || section == ColArc)
            return QStringLiteral("%1(%2)").arg(labels[section]).arg(tzLabel());
        return QString::fromUtf8(labels[section]);
    }
    if (role == Qt::ToolTipRole) {
        static const char *tips[ColCount] = {
            nullptr, nullptr, nullptr, nullptr,
            "过境持续时长", "最大仰角（TCA 时刻）",
            "TCA 时刻方位角", "光学可见 / 仅无线电 / 白天",
            "TCA 时刻太阳高度角", "TCA 时刻卫星日照状态（半影带为太阳圆面可见比例）",
            "几何过境内「暗天 + 被照亮」的连续可见时段",
            "入境时刻卫星星下点经纬度与高度",
            "TCA 时刻观测者至卫星斜距", "TCA 时刻多普勒频移",
            "视星等估算（Vallerie 相位模型），越小越亮",
            "综合质量评级"
        };
        static_assert(sizeof(tips) / sizeof(tips[0]) == ColCount,
                      "tips 数组大小必须与 ColCount 列数匹配");
        return tips[section] ? QString::fromUtf8(tips[section]) : QVariant();
    }
    return QVariant();
}

QVariant PassTableModel::data(const QModelIndex &index, int role) const
{
    QMutexLocker locker(&m_dataMutex);
    
    if (!index.isValid() || index.row() < 0 || index.row() >= m_passes.size())
        return QVariant();
    const PassEvent &p = *m_passes[index.row()];
    const int c = index.column();
    // 列号防御: 与行号同级校验（视图协议保证只请求合法列, 防御越界数组访问）
    if (c < 0 || c >= ColCount)
        return QVariant();

    // 列对齐: 数值右对齐、状态居中、文本左对齐
    if (role == Qt::TextAlignmentRole) {
        static const Qt::Alignment aligns[ColCount] = {
            Qt::AlignCenter,                   /* # */
            Qt::AlignLeft  | Qt::AlignVCenter, /* 卫星名 */
            Qt::AlignLeft  | Qt::AlignVCenter, /* 入境 */
            Qt::AlignLeft  | Qt::AlignVCenter, /* 出境 */
            Qt::AlignCenter,                   /* 持续 */
            Qt::AlignRight | Qt::AlignVCenter, /* 最大仰角 */
            Qt::AlignRight | Qt::AlignVCenter, /* 方位角 */
            Qt::AlignCenter,                   /* 可见性 */
            Qt::AlignRight | Qt::AlignVCenter, /* 太阳高度 */
            Qt::AlignCenter,                   /* 卫星光照 */
            Qt::AlignCenter,                   /* 可见弧段 */
            Qt::AlignLeft  | Qt::AlignVCenter, /* 入境位置 */
            Qt::AlignRight | Qt::AlignVCenter, /* 斜距 */
            Qt::AlignRight | Qt::AlignVCenter, /* 多普勒 */
            Qt::AlignRight | Qt::AlignVCenter, /* 星等 */
            Qt::AlignCenter                    /* 质量 */
        };
        return QVariant::fromValue(aligns[c]);
    }

    if (role == Qt::ToolTipRole) {
        switch (c) {
        case ColDur:
            // 截断过境: 过境延续至预测窗口外, 出境时刻并非真实值
            if (p.truncated)
                return QStringLiteral(
                    "过境延续至预测窗口结束时刻（真实出境在窗口外, 持续时长被截断）");
            return QVariant();
        case ColArc:
            if (p.hasVisibleArc)
                return QStringLiteral(
                    "几何过境内「暗天 + 被照亮」的连续时段，观测者实际可见范围");
            return QVariant();
        case ColMag:
            if (p.magValid)
                return QStringLiteral(
                    "视星等估算（Vallerie 相位模型），越小越亮\n"
                    "固有星等来自内置目录或输入框回退值，误差 ±1~2 星等");
            return QVariant();
        default:
            return QVariant();
        }
    }

    if (role == Qt::ForegroundRole) {
        switch (c) {
        case ColVis: {
            QColor col;
            switch (p.visibility) {
            case PassVisibility::Visible:   col = QColor(0, 150, 0); break;
            case PassVisibility::RadioOnly: col = QColor(0, 100, 180); break;
            case PassVisibility::Daylight:  col = QColor(100, 100, 100); break;
            }
            return QBrush(col);
        }
        case ColMaxEl: {
            QColor col;
            if (p.maxElevDeg >= 50.0)      col = QColor(0, 160, 0);
            else if (p.maxElevDeg >= 20.0) col = QColor(200, 150, 0);
            else                           col = QColor(200, 80, 0);
            return QBrush(col);
        }
        default:
            return QVariant();
        }
    }

    if (role != Qt::DisplayRole) return QVariant();

    // 全程持锁返回: 值均为标量/短字符串格式化（微秒级）, 无需中途解锁。
    // 此前曾在此处 unlock——但 p 是受锁保护的引用, 解锁后继续解引用
    // 属锁纪律错误（当前 GUI-only 线程模型下无实际竞争, 形式上仍应一致）

    switch (c) {
    case ColIndex:  return QString::number(index.row() + 1);
    case ColName:   return QString::fromStdString(p.satelliteName);
    case ColAos:    return jdMsToStr(p.aosJdMs);
    case ColLos:    return jdMsToStr(p.losJdMs);
    case ColDur:    return QStringLiteral("%1s%2")
                            .arg(static_cast<int>(p.durationSec))
                            .arg(p.truncated ? " *" : "");
    case ColMaxEl:  return QString::number(p.maxElevDeg, 'f', 3) + "°";
    case ColAz:     return QString::number(p.tcaLook.azDeg, 'f', 3) + "°";
    case ColVis: {
        switch (p.visibility) {
        case PassVisibility::Visible:   return QStringLiteral("可见");
        case PassVisibility::RadioOnly: return QStringLiteral("无线电");
        case PassVisibility::Daylight:  return QStringLiteral("白天");
        }
        return QVariant();
    }
    case ColSunAlt: return QString::number(p.tcaSunAlt, 'f', 1) + "°";
    case ColIllum: {
        switch (p.tcaSatShadow) {
        case ShadowStatus::FullSunlight: return QStringLiteral("日照");
        case ShadowStatus::InPenumbra:
            // 边界值 tcaIllum==1.0（理论刚出半影）显示 100% 而非钳制的 99%,
            // 与"日照"分类在界线上衔接一致
            return QStringLiteral("半影 %1%")
                .arg(qBound(1, static_cast<int>(p.tcaIllum * 100.0), 100));
        case ShadowStatus::InUmbra:      return QStringLiteral("本影");
        }
        return QVariant();
    }
    case ColArc:
        if (p.hasVisibleArc)
            return jdMsToStr(p.visAosJdMs) + " – " + jdMsToStr(p.visLosJdMs);
        return QStringLiteral("—");
    case ColAosGeo:
        return QStringLiteral("%1°, %2°, %3km")
            .arg(p.aosSatGeo.latDeg, 0, 'f', 1)
            .arg(p.aosSatGeo.lonDeg, 0, 'f', 1)
            .arg(p.aosSatGeo.altKm, 0, 'f', 0);
    case ColRange:  return QString::number(p.tcaLook.rangeKm, 'f', 0) + " km";
    case ColDoppler: return QString::number(p.dopplerAtTcaKHz, 'f', 2) + " kHz";
    case ColMag:
        return p.magValid ? QString::number(p.magAtTca, 'f', 1) : QStringLiteral("—");
    case ColQuality:
        switch (p.quality) {
        case PassQuality::Excellent: return QStringLiteral("★★★");
        case PassQuality::Good:      return QStringLiteral("★★");
        case PassQuality::Poor:      return QStringLiteral("★");
        }
        return QVariant();
    default:
        return QVariant();
    }
}
```

---

## ui/PassTableModel.h

```cpp
#ifndef UI_PASSTABLEMODEL_H
#define UI_PASSTABLEMODEL_H
// =============================================================================
// PassTableModel.h — 过境列表懒加载模型（QTableView + model 方案, M 修复）
// 数据为共享指针集（指向 MainWindow 持有的过境集合, 零深拷贝）;
// QTableView 仅请求可见单元格的 data(), 千颗卫星级批量不再整表逐格
// 创建 QTableWidgetItem（此前每行 16 个 item 全量构建是刷新卡顿根源）
// =============================================================================
#include <QAbstractTableModel>
#include <QVector>
#include <QMutex>
#include <QMutexLocker>
#include "core/DataTypes.h"

class PassTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColIndex = 0, ColName, ColAos, ColLos, ColDur, ColMaxEl,
        ColAz, ColVis, ColSunAlt, ColIllum, ColArc, ColAosGeo,
        ColRange, ColDoppler, ColMag, ColQuality,
        ColCount
    };

    explicit PassTableModel(QObject *parent = nullptr);

    // 共享数据指针集（指向调用方持有的 PassEvent, 模型不持有所有权）
    void setPasses(const QVector<const PassEvent *> &passes);
    const QVector<const PassEvent *> &passes() const { return m_passes; }

    // 时区（秒偏移）: 更新入境/出境表头与时间列显示
    void setTimeZoneOffset(int tzOffsetSec);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

private:
    QVector<const PassEvent *> m_passes;
    int m_tzOffsetSec = 0;   // 0 = UTC
    mutable QMutex m_dataMutex;  // 保护数据的互斥锁

    QString tzLabel() const;
    QString jdMsToStr(int64_t jdMs) const;
};

#endif // UI_PASSTABLEMODEL_H
```

---

## ui/PolarPlotWidget.cpp

```cpp
// =============================================================================
// PolarPlotWidget.cpp — 极坐标图实现
// =============================================================================
#include "PolarPlotWidget.h"
#include "CatmullRomSpline.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PolarPlotWidget::PolarPlotWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(300, 300);
}

void PolarPlotWidget::setPasses(const QVector<const PassEvent *> &passes)
{
    m_passes = passes;
    // 保留原选中行: 过滤切换后若选中越界则钳到末尾, 而非强制回到第 0 行
    if (passes.isEmpty())
        m_selectedIndex = -1;
    else if (m_selectedIndex >= passes.size())
        m_selectedIndex = passes.size() - 1;
    update();
}

void PolarPlotWidget::setSelectedPass(int index)
{
    if (index >= 0 && index < m_passes.size()) {
        m_selectedIndex = index;
        update();
    }
}

void PolarPlotWidget::clear()
{
    m_passes.clear();
    m_selectedIndex = -1;
    update();
}

// ============================================================================
// 坐标映射: 仰角/方位角 → 屏幕笛卡尔坐标
// azDeg: 0=N, 90=E, 180=S, 270=W
// elDeg: 0=地平线, 90=天顶
// 圆心 = 天顶(90°), 最外圈 = 地平(0°)
// ============================================================================
QPointF PolarPlotWidget::polarToScreen(double azDeg, double elDeg,
                                        double cx, double cy, double radius) const
{
    // 将仰角映射为距圆心的距离：elDeg=90 → r=0, elDeg=0 → r=radius
    // 钳制上限: 数据异常 elDeg>90 会得到负半径, 点被镜像到圆心另一侧
    // （伪影弧线穿过图心）; 负仰角保留——画在外圈之外属预期行为
    double r = radius * (1.0 - std::min(elDeg, 90.0) / 90.0);
    // 方位角: 北=0°，但 Qt 坐标系 Y 轴向下，所以需要转换
    // 地图: 北=上(270° in math), 东=右(0° in math)
    // 转换为标准数学坐标系: 0°=右, 90°=上
    double azRad = (90.0 - azDeg) * M_PI / 180.0;
    double x = cx + r * std::cos(azRad);
    double y = cy - r * std::sin(azRad);
    return QPointF(x, y);
}

// 单条过境的仰角-方位角曲线路径（供缓存复用）
QPainterPath PolarPlotWidget::buildTrackPath(const PassEvent &p,
                                    double cx, double cy, double radius) const
{
    QPainterPath path;
    if (p.samples.size() >= 2) {
        // 真实弧段: Catmull-Rom 平滑曲线（经过所有采样点）
        // 方位角跨 0°/360° 处断开分段平滑，避免整圈环绕伪影
        QVector<QPointF> seg;
        for (size_t k = 0; k < p.samples.size(); ++k) {
            const auto &s = p.samples[k];
            if (!seg.isEmpty()) {
                const double daz = std::abs(s.look.azDeg - p.samples[k - 1].look.azDeg);
                if (daz > 180.0) {    // 跨 0°/360°: 断开
                    path.addPath(CatmullRomSpline::path(seg));
                    seg.clear();
                }
            }
            seg.append(polarToScreen(s.look.azDeg, s.look.elevDeg, cx, cy, radius));
        }
        path.addPath(CatmullRomSpline::path(seg));
    } else {
        // 回退: AOS → TCA → LOS 二次贝塞尔近似（无采样数据时）
        path.moveTo(polarToScreen(p.aosLook.azDeg, p.aosLook.elevDeg, cx, cy, radius));
        path.quadTo(polarToScreen(p.tcaLook.azDeg, p.maxElevDeg, cx, cy, radius),
                    polarToScreen(p.losLook.azDeg, p.losLook.elevDeg, cx, cy, radius));
    }
    return path;
}

// ============================================================================
// 绘制
// ============================================================================
void PolarPlotWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    double w = width(), h = height();
    double cx = w / 2.0, cy = h / 2.0;
    double radius = std::min(w, h) / 2.0 - 40;

    // 背景（亮色系, 保证深色曲线清晰可见）
    painter.fillRect(rect(), QColor(38, 42, 54));
    painter.setPen(QPen(QColor(105, 113, 130), 1));

    // 同心圆（仰角环）
    for (int elRing : {90, 70, 50, 30, 10, 0}) {
        double r = radius * (1.0 - elRing / 90.0);
        if (elRing == 0)
            painter.setPen(QPen(QColor(135, 145, 165), 2));   // 地平圈加粗
        else
            painter.setPen(QPen(QColor(105, 113, 130), 1));
        painter.drawEllipse(QPointF(cx, cy), r, r);
    }

    // 径向线（方位角刻度）
    for (int az = 0; az < 360; az += 30) {
        double azRad = (90.0 - az) * M_PI / 180.0;
        double x = cx + radius * std::cos(azRad);
        double y = cy - radius * std::sin(azRad);
        bool cardinal = (az % 90 == 0);
        painter.setPen(QPen(cardinal ? QColor(135, 145, 165)
                                     : QColor(88, 95, 112), cardinal ? 2 : 1));
        painter.drawLine(QPointF(cx, cy), QPointF(x, y));
    }

    // 绘制各过境的仰角-方位角曲线（全部以半透明绘制）
    QVector<QColor> passColors = {
        QColor(110, 215, 255), QColor(255, 195, 90),
        QColor(150, 255, 150), QColor(255, 145, 135),
        QColor(215, 175, 255), QColor(255, 255, 135)
    };

    // 曲线路径缓存: 数据或尺寸未变时复用（每帧重建数百条 Catmull-Rom
    // 路径在选中切换/resize 重绘风暴下开销明显）
    if (m_pathKeyPasses != m_passes || m_pathKeySize != QSize(w, h)) {
        m_pathCache.resize(m_passes.size());
        for (int pi = 0; pi < m_passes.size(); ++pi)
            m_pathCache[pi] = buildTrackPath(*m_passes[pi], cx, cy, radius);
        m_pathKeyPasses = m_passes;
        m_pathKeySize = QSize(w, h);
    }

    for (int pi = 0; pi < m_passes.size(); ++pi) {
        const PassEvent *p = m_passes[pi];
        bool isSelected = (pi == m_selectedIndex);
        QColor col = passColors[pi % passColors.size()];
        if (!isSelected) col.setAlpha(170);

        const QPainterPath &path = m_pathCache[pi];

        if (isSelected) {
            // 选中路径光晕（先宽后窄，突出主曲线）
            QColor glow = col;
            glow.setAlpha(70);
            painter.setPen(QPen(glow, 9));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        }
        painter.setPen(QPen(col, isSelected ? 3.5 : 1.8));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        // 标注点（仅选中过境, 三点投影开销可忽略）
        if (isSelected) {
            const QPointF aosP = polarToScreen(p->aosLook.azDeg, p->aosLook.elevDeg, cx, cy, radius);
            const QPointF tcaP = polarToScreen(p->tcaLook.azDeg, p->maxElevDeg, cx, cy, radius);
            const QPointF losP = polarToScreen(p->losLook.azDeg, p->losLook.elevDeg, cx, cy, radius);
            painter.setPen(QPen(QColor(20, 24, 32), 1));
            painter.setBrush(QColor(0, 255, 200));
            painter.drawEllipse(aosP, 5, 5);
            painter.setBrush(QColor(255, 255, 0));
            painter.drawEllipse(tcaP, 5, 5);
            painter.setBrush(QColor(255, 90, 90));
            painter.drawEllipse(losP, 5, 5);

            // 标签
            painter.setPen(Qt::white);
            painter.drawText(aosP + QPointF(9, -9), "AOS");
            painter.drawText(tcaP + QPointF(9, -9), "TCA");
            painter.drawText(losP + QPointF(9, -9), "LOS");
        }
    }

    // 刻度标签在曲线之后绘制, 保证永不被过境曲线遮挡
    for (int elRing : {90, 70, 50, 30, 10, 0}) {
        double r = radius * (1.0 - elRing / 90.0);
        painter.setPen(QColor(195, 202, 215));
        QString txt = QString::number(elRing) + "°";
        painter.drawText(QRectF(cx + 6, cy - r - 8, 40, 16),
                         Qt::AlignLeft | Qt::AlignVCenter, txt);
    }
    for (int az = 0; az < 360; az += 30) {
        double azRad = (90.0 - az) * M_PI / 180.0;
        QPointF labelPos(cx + (radius + 18) * std::cos(azRad),
                         cy - (radius + 18) * std::sin(azRad));
        painter.setPen(QColor(205, 212, 225));
        QString label;
        if (az == 0) label = "N";
        else if (az == 90) label = "E";
        else if (az == 180) label = "S";
        else if (az == 270) label = "W";
        else label = QString::number(az) + "°";
        painter.drawText(QRectF(labelPos.x() - 20, labelPos.y() - 10, 40, 20),
                         Qt::AlignCenter, label);
    }

    // 如果没有过境，显示提示
    if (m_passes.isEmpty()) {
        painter.setPen(QColor(175, 182, 195));
        painter.drawText(QRectF(cx - 100, cy - 15, 200, 30),
                         Qt::AlignCenter, "暂无过境数据");
    }
}
```

---

## ui/PolarPlotWidget.h

```cpp
#ifndef UI_POLARPLOTWIDGET_H
#define UI_POLARPLOTWIDGET_H
// =============================================================================
// PolarPlotWidget.h — 极坐标仰角-方位角图（雷达图）
// QPainter 纯绘制: 同心圆=仰角, 径向线=方位角
// =============================================================================
#include <QWidget>
#include <QVector>
#include <QPainterPath>
#include "core/DataTypes.h"

class PolarPlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit PolarPlotWidget(QWidget *parent = nullptr);

    // 共享数据指针（指向 MainWindow 持有的过境集合, 不深拷贝采样向量）
    void setPasses(const QVector<const PassEvent *> &passes);
    void setSelectedPass(int index);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<const PassEvent *> m_passes;
    int m_selectedIndex = -1;

    // 将仰角/方位角映射到绘图区域的笛卡尔坐标
    QPointF polarToScreen(double azDeg, double elDeg, double cx, double cy, double radius) const;

    // 单条过境的曲线路径构建（供缓存复用）
    QPainterPath buildTrackPath(const PassEvent &p,
                                double cx, double cy, double radius) const;

    // 曲线路径缓存: 每帧为全部过境重建 Catmull-Rom 路径（批量数百条曲线 ×
    // 数十采样点）在选中切换/resize 重绘风暴下开销明显——以「指针集合 + 尺寸」
    // 为版本键（路径只依赖数据与屏幕几何）, 未变时复用
    QVector<QPainterPath> m_pathCache;
    QVector<const PassEvent *> m_pathKeyPasses;
    QSize m_pathKeySize;
};

#endif // UI_POLARPLOTWIDGET_H
```

---

## resources/resources.qrc

```xml
<!DOCTYPE RCC>
<!-- 应用资源：Natural Earth 110m 陆地多边形（公共领域），
     由 ne_110m_land.json 转换生成（127 多边形 / 5091 顶点） -->
<RCC>
    <qresource prefix="/">
        <file alias="land110m.txt">land110m.txt</file>
        <file alias="app.ico">app.ico</file>
    </qresource>
</RCC>
```

---

## resources/app.rc

```text
#include <windows.h>

IDI_ICON1 ICON "app.ico"

VS_VERSION_INFO VERSIONINFO
FILEVERSION     1,0,0,0
PRODUCTVERSION  1,0,0,0
FILEFLAGSMASK   0x3fL
FILEFLAGS       0x0L
FILEOS          VOS_NT_WINDOWS32
FILETYPE        VFT_APP
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904B0"
        BEGIN
            VALUE "CompanyName",      "XGZ"
            VALUE "FileDescription",  "Satellite TLE Pass Predictor"
            VALUE "FileVersion",      "1.0.0.0"
            VALUE "ProductName",      "XGZ"
            VALUE "ProductVersion",   "1.0.0.0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0409, 1200
    END
END
```


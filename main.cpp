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
#include "core/models.h"

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

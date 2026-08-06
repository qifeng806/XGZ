// =============================================================================
// main.cpp — 卫星 TLE 过境预报系统 (XGZ) 入口
// Qt 6.11.0, C++17, qmake
// =============================================================================
#include <QApplication>
#include <QSurfaceFormat>
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

    QApplication app(argc, argv);
    app.setApplicationName("XGZ");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("XGZ");

    // 设置默认字体（兼容中文显示）
    QFont defaultFont = app.font();
    defaultFont.setPointSize(10);
    app.setFont(defaultFont);

    // 深色主题风格
    app.setStyleSheet(R"(
        QMainWindow { background-color: #2b2b2b; }
        QGroupBox {
            color: #e0e0e0;
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
    )");

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}

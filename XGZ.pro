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

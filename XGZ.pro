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
    core/Magnitude.cpp \
    core/OrbitCalculator.cpp \
    business/PassPredictWorker.cpp \
    business/PassPredictorService.cpp \
    business/TleFetcher.cpp \
    business/TleBlocks.cpp \
    ui/MainWindow.cpp \
    ui/PolarPlotWidget.cpp \
    ui/GroundTrackWidget.cpp \
    ui/DopplerPlotWidget.cpp \
    ui/PassTableModel.cpp \
    perturb-master/src/perturb.cpp \
    perturb-master/src/sgp4.cpp \
    perturb-master/src/tle.cpp

HEADERS += \
    core/models.h \
    core/Propagator.h \
    core/TleParser.h \
    core/CoordTransform.h \
    core/Observer.h \
    core/SunPosition.h \
    core/Nutation.h \
    core/EarthShadow.h \
    core/DopplerCalc.h \
    core/Magnitude.h \
    core/OrbitCalculator.h \
    business/PassPredictWorker.h \
    business/PassPredictorService.h \
    business/TleFetcher.h \
    business/TleBlocks.h \
    ui/MainWindow.h \
    ui/PolarPlotWidget.h \
    ui/GroundTrackWidget.h \
    ui/DopplerPlotWidget.h \
    ui/CatmullRom.h \
    ui/PassTableModel.h \
    perturb-master/include/perturb/perturb.hpp \
    perturb-master/include/perturb/sgp4.hpp \
    perturb-master/include/perturb/tle.hpp

# ---- Include Paths ----
INCLUDEPATH += . core business ui perturb-master/include

# ---- 输出目录固定为基于 $$PWD 的绝对路径 ----
# 保证无论从哪个目录执行 qmake/make（常见误操作: 在 release/ 子目录里构建），
# moc 中间文件、目标文件与可执行文件都落到 D:\AAAAAAAAA\XGZ\release\ 下，
# 且 `#include "MainWindow.moc"` 通过绝对 -I 路径解析，不再依赖相对目录
DESTDIR     = $$PWD/release
OBJECTS_DIR = $$PWD/release/obj
MOC_DIR     = $$PWD/release/moc
INCLUDEPATH += $$PWD/release/moc

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

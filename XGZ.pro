# =============================================================================
# XGZ.pro — Satellite TLE Pass Predictor
# Qt 6.11.0 + qmake + C++17 + MinGW-w64
# 集成 perturb 库 (gunvirranu/perturb, MIT) 作为 SGP4/SDP4 引擎
# =============================================================================
QT       += core gui widgets network
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
    perturb-master/include/perturb/perturb.hpp \
    perturb-master/include/perturb/sgp4.hpp \
    perturb-master/include/perturb/tle.hpp

# ---- Include Paths ----
INCLUDEPATH += . core business ui perturb-master/include

# ---- Compiler Flags ----
win32-g++ {
    QMAKE_CXXFLAGS += -Wall -Wextra -std=c++17
    # 抑制 perturb 内部的 magic-number 警告
    QMAKE_CXXFLAGS += -Wno-missing-braces
}
win32-msvc* {
    QMAKE_CXXFLAGS += /utf-8 /MP /W3 /std:c++17
}

# ---- Debug/Release ----
CONFIG(debug, debug|release) {
    TARGET = $${TARGET}_d
} else {
    DEFINES += QT_NO_DEBUG_OUTPUT
}

# ---- Windows RC ----
RC_FILE = resources/app.rc

# =============================================================================
# tests.pro — XGZ 核心算法单元测试（Qt Test）
# 覆盖: TLE 解析 | 坐标转换 | 太阳位置 | 地影 | SGP4 传播(Vallado 官方数据) | 过境引擎
# 构建:   cd tests && qmake tests.pro && mingw32-make -j8
# 运行:   ./release/tst_xgz.exe
# =============================================================================
QT += core testlib
QT -= gui
CONFIG += console c++17 testcase
TARGET = tst_xgz
TEMPLATE = app

INCLUDEPATH += .. ../perturb-master/include

SOURCES += \
    main.cpp \
    tst_tleparser.cpp \
    tst_coordtransform.cpp \
    tst_sunposition.cpp \
    tst_nutation.cpp \
    tst_tleblocks.cpp \
    tst_earthshadow.cpp \
    tst_propagator.cpp \
    tst_orbitcalc.cpp \
    tst_magnitude.cpp \
    ../core/TleParser.cpp \
    ../core/Propagator.cpp \
    ../core/CoordTransform.cpp \
    ../core/Observer.cpp \
    ../core/SunPosition.cpp \
    ../core/Nutation.cpp \
    ../core/EarthShadow.cpp \
    ../core/DopplerCalc.cpp \
    ../core/Magnitude.cpp \
    ../core/OrbitCalculator.cpp \
    ../business/TleBlocks.cpp \
    ../perturb-master/src/perturb.cpp \
    ../perturb-master/src/sgp4.cpp \
    ../perturb-master/src/tle.cpp

HEADERS += \
    testutils.h \
    tst_runners.h

# 测试数据（随源码分发）
DISTFILES += \
    data/SGP4-VER.TLE

win32-g++ {
    QMAKE_CXXFLAGS += -Wall -Wextra -std=c++17 -Wno-missing-braces
}
win32-msvc* {
    QMAKE_CXXFLAGS += /utf-8 /MP /W3 /std:c++17
}
# perturb 历史遗留未使用变量/参数已用 [[maybe_unused]] 在源码标注
#（qmake 的 Makefile 生成器不支持按源文件作用域设置编译选项）

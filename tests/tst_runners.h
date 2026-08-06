// =============================================================================
// tst_runners.h — 各测试类的运行入口声明（由 main.cpp 统一调度）
// =============================================================================
#ifndef TESTS_TST_RUNNERS_H
#define TESTS_TST_RUNNERS_H

int runTstTleParser(int argc, char *argv[]);
int runTstCoordTransform(int argc, char *argv[]);
int runTstSunPosition(int argc, char *argv[]);
int runTstEarthShadow(int argc, char *argv[]);
int runTstPropagator(int argc, char *argv[]);
int runTstOrbitCalc(int argc, char *argv[]);

#endif // TESTS_TST_RUNNERS_H

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

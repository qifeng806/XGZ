# tools/stk_compare — XGZ 与 STK 11 结果一致性验证

## 目的

回答一个具体问题：**同样的 TLE、同样的测站，XGZ 算出来的位置/角度和 STK 11 是否一致？**
本目录提供可重复执行的验证工具：直接从**本机正在运行的 STK 11** 取基准数据（COM 附加，不新建场景、不改动用户工程），
再让 **XGZ 自己的引擎**（`core/*` + 内置 perturb，与主程序完全同一份代码）算同一批时刻，逐点比对。

## 结论（2026-09-19 实测，BEIDOU-3M/43107，站 33°N/103°E/0.05 km）

| 指标 | 实测最大偏差 | 说明 |
|---|---|---|
| ΔTEME（|Δr|，SGP4 原始输出） | **0.14 m** | 与 STK 的 `TEMEOfDate` 帧逐点对比 |
| ΔECEF（|Δr|，含地球自转/时标） | **0.09 m** | 与 STK 的 `Fixed` 帧逐点对比 |
| Δ斜距 | **0.02 m** | 站心几何 |
| Δ仰角 / Δ方位角 | **≈0.000000°** | 远小于显示精度（0.001°） |

即：**同一输入下两者数值等价**（0.14 m / 26000 km ≈ 5×10⁻⁹）。
如果对比时看到 km 级差异，几乎一定是**输入不同**——最常见的是两侧 TLE 历元不同（见下）。

## 用法

前置条件：
- 本机已装 STK 11，且 STK 已启动并打开含目标卫星与站点的场景（工具通过 COM 附加到运行实例）；
- 有 MinGW g++（默认路径 `D:\QT\Tools\mingw1310_64\bin\g++.exe`，可用 `-Gxx` 改）；
- 卫星与站点的名字与场景中一致（默认 `BEIDOU_3M3_43107` / `Facility2`）。

```powershell
cd <仓库根>
.\tools\stk_compare\compare.ps1 `
    -Tle "C:\Users\杨洋\Documents\BEIDOU 3M3.txt" `   # 与 STK 场景里**同一份** TLE 文件
    -Lat 33.0 -Lon 103.0 -AltKm 0.05 `                 # 与 STK 站点一致（含站高！）
    -Start "2026-09-19 22:57:00" -Stop "2026-09-19 23:00:00" -Step 1 `
    -StkSatellite "BEIDOU_3M3_43107" -StkFacility "Facility2" `
    -WorkDir "D:\XGZ_bench\cmp"
```

可选参数：

| 参数 | 说明 |
|---|---|
| `-PropStep 1` | 临时把 STK 传播器星历步长设为 1 s（导出后还原）。STK 的数据/报告按传播器星历**插值**，默认 300 s 会残留约 0.1 m 插值差；要"逐位对齐"时使用 |
| `-Step 0.1` | 比对步长（秒），默认 1 s |
| `-Gxx <path>` | 指定 g++ |

脚本会依次：编译 `xgz_dump` → 从 STK 导出 `stk_teme.csv` / `stk_ecef.csv` / `stk_facility_ecef.csv` →
跑 XGZ 引擎生成 `xgz.csv` → 逐点比对并打印判定结果（阈值：位置差 ≤100 m 视为通过）。

单独使用某一步：

```powershell
# 只从 STK 导出基准（也可用 -PropStep 1）
.\tools\stk_compare\stk_export.ps1 -OutDir D:\tmp -Satellite BEIDOU_3M3_43107 -Facility Facility2

# 只用 XGZ 引擎导出参考序列（不依赖 STK）
D:\tmp\xgz_dump.exe "C:\...\BEIDOU 3M3.txt" 33.0 103.0 0.05 "2026-09-19 22:57:00" "2026-09-19 23:00:00" 1 xgz.csv
```

## xgz_dump 输出列

`Time, TemeX/Y/Z, EcefX/Y/Z, Az, El, Range`
- `TEME` = SGP4 直接输出（TEME of Date），对应 STK `Cartesian Position / TEMEOfDate`
- `ECEF` = TEME→ECEF（GMST，UT1≈UTC），对应 STK `Cartesian Position / Fixed`
- `Az/El/Range` = 站心 AER（方位角自北顺时针、几何仰角、斜距），对应 STK 的 `AER` 报告

## 排查：出现偏差时的检查顺序

1. **TLE 是否同一份**（最常见，km 级）——`xgz_dump` 头部会打印所用 TLE 的历元（年 + 年内日）；
   与 STK 的 TLE 文件（`*.txt` 第 2 行第 19–32 列）逐字对比。
   XGZ 界面"逐秒数据"页签也会显示本次使用的 TLE 历元，便于直接对照。
2. **测站是否同一位置**（站高 50 m ≈ 9 m 斜距）——工具会自动反解 STK 站点的大地高并提示
   （例：`STK 站点大地高 = 0.0500 km（XGZ 传入 0.0500 km）`），两边不一致时用 `-AltKm` 对齐。
3. **STK 的星历步长**（约 0.1 m）——加 `-PropStep 1` 排除插值影响。
4. **地球定向参数（EOP）**——STK 场景若配置了存在的 EOP 文件（`UT1−UTC`、极移），
   与本工程"UT1≈UTC"的近似会有差异：`UT1−UTC = 0.9 s` 时约 0.25 m 斜距 / 0.0024° 仰角。
   本机 STK 当前**没有**可用的 EOP 文件，因此该差异为 0。
5. 以上都一致却仍有 ≥1 m 差：把两侧结果文件（`stk_teme.csv` / `xgz.csv`）保留下来做定位。

## 已知实现细节（为什么会有 0.1 m 级残差）

- XGZ 使用 Vallado 参考实现同源的 SGP4（内置 perturb），已实测在**同一 TLE** 下与
  `perturb-master/original/SGP4.cpp` 逐点一致到 <0.000001 km；
- 与 STK 的残差约 0.1–0.2 m 来自两者深空（12 h 共振 MEO）实现的细微差别，量级 5×10⁻⁹ 相对，
  对过境时刻（<1 ms）、仰角（<10⁻⁶°）、多普勒（<0.001 Hz）均无可见影响；
- 采样时刻一律用"起点 + 整数索引 × 步长"生成（`xgz_dump.cpp` 与 `core/OrbitCalculator.cpp`
  均已如此），避免 `jd += step` 的浮点累积漂移（24 h/15 s 窗口可达 5 ms ≈ 40 m）。

## 文件

| 文件 | 作用 |
|---|---|
| `compare.ps1` | 一键流程：编译 → 取 STK 基准 → 跑 XGZ → 逐点比对 → 判定 |
| `stk_export.ps1` | 从运行中的 STK 11 导出 TEME/Fixed/站点 ECEF（COM，`-PropStep` 可控星历步长） |
| `xgz_dump.cpp` | 用 XGZ 引擎输出参考序列（g++ 直接编译，无需 Qt） |

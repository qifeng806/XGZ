# XGZ × STK 11 一致性审查报告

审查对象：当前工作树（`core/`、`business/`、`ui/`、`tools/stk_compare/`）
基准：本机 **STK 11.2** 自带帮助（`D:\AGI Systems Tool Kit(STK)\Help\Content`，逐条给出主题文件与行号）
+ 本机实测（复用工程自身的 `release/obj/*.o` 链接一个临时审计程序，见 §5）

---

## 0. 结论摘要

| # | 项目 | XGZ 现状 | STK 11 | 判定 | 量化影响 |
|---|------|----------|--------|------|----------|
| **A1** | Access 是否用折射 | **默认启用** Saastamoinen，UI 三处声称"与 STK 11 一致" | **默认不启用**；模型只有 Effective Radius / ITU-R P.834-4 / SCF 三种 | ❌ **不一致（含错误声明）** | 掩星角 10° → AOS/LOS 差 **1.14 s**；5° → **3.85 s**；2° → **过境数 6→7**（凭空多出擦边过境） |
| **A2** | 地影/光照模型 | 圆柱模型，**二态**（本影/日照），无半影 | Lighting 为**三态**：Direct Sun / Penumbra / Umbra | ❌ **不一致** | LEO 半影带 **≈8.5 s**；圆柱边界落在 f=0.577；每跨越点分类偏差最多 **4.75 s**（日照↔半影）/ **3.75 s**（半影↔本影） |
| **A3** | SGP4 重力常数模型 | 下拉框**默认 WGS-84** | CSSI SGP4（默认 WGS-72 常数；本机实测亦为 WGS-72） | ❌ **默认不一致** | ISS 24 h 内最大 **|Δr| = 372 m**、|Δv| = 0.39 m/s、ΔAOS ≤ 0.05 s；而 WGS-72 下与 STK 差 **0.14 m** |
| **B1** | 过境合并/最短时长 | `mergeGapSec=60 s` 合并、`minDurationSec=10 s` 丢弃 | Access **逐一列出**，无合并、无最短时长概念 | ⚠️ 口径不同 | 过境条数与时长与 STK Access 报告不可直接对照 |
| **B2** | 光行时/光行差 | 纯几何 | Access **默认考虑光行时**（报告/AER 默认仍是几何） | ⚠️ 轻微 | 光行时 ~7 ms（LEO）→ 等效位置 ~53 m → AOS/LOS 数十 ms |
| **B3** | 暗天判据的地平俯角修正 | 阈值按 `dip=1.0152·√h` 下移 | Sun Elevation 相对站点本地切平面，**无俯角修正** | ⚠️ 不一致（自定义项） | 默认站 50 m：0.227° ≈ **57 s** 暮光时刻差；且 UI 显示的"太阳高度"与判据不同源 |
| **B4** | 太阳星历 | Meeus 低精度（~0.01°）+ ΔT 分段式 | STK 自身高精度太阳星历（可含光行时/光行差） | ⚠️ 可接受 | 暮光边界 **~2–3 s** 量级 |
| **C1** | AOS/LOS 精度 | 毫秒量化（±0.5 ms） | 亚毫秒 | ⚠️ 已记录 | LEO 距离 ±3.5 m（"逐秒零量化对齐"时可见） |
| **C2** | 掩星角范围 | 构造时钳到 `[0°, 90°]` | 允许负仰角约束（如 −0.72°） | ⚠️ 功能缺口 | 无法复现 STK 的负仰角 Access |
| **C3** | TLE 宽容度 | 自动修字母 NORAD 前缀/缺小数点/多余空格 | 相对严格 | ⚠️ 更宽松 | 可能接受 STK 会拒绝的 TLE |
| **C4** | 多普勒 | 一阶距离变化率 `Δf=−ṙ/c·f` | Doppler 在 Comm/Radar 模块，含收发两端与光行时 | ⚠️ 未对齐验证 | 一阶项一致；高阶项/符号约定需与 STK 报告核对 |
| **D** | **已实测一致的部分** | 见 §1 | — | ✅ | TEME 0.14 m / ECEF 0.09 m / 斜距 0.02 m / 角 0.000000° |

**一句话**：**几何链（SGP4→TEME→ECEF→站心 AER）与 STK 11 数值等价**，这一点有实测支撑；
但**折射、光照分类、重力常数默认值**这三条"物理/配置口径"与 STK 11 不一致，其中 A1 还伴随三处**错误的"与 STK 11 一致"声明**，
A2 使 `ShadowStatus::InPenumbra` 与表格"半影 N%"成为**不可达死代码**。

> **本报告 A1 / A2 / A3 已按"以 STK 11 为基准"实施整改**（删除折射与校验和/强制刷新功能、默认重力模型改 WGS-72、
> 地影恢复圆锥三态、TLE 解析改为 STK 定列 + 列格式校验），实施记录与验证数据见 **§7**。

---

## 1. 已与 STK 11 一致的部分（含证据）

| 项目 | 证据 |
|------|------|
| SGP4 版本谱系 | STK 帮助：*"By default, STK utilizes the CSSI SGP4 routine, Version 2008-11-03"*（`stk/vehSat_orbitProp_msgp4.htm:40`）；XGZ 用 perturb（Vallado 谱系），实测与 `perturb-master/original/SGP4.cpp` 逐点 < 1e-6 km |
| 坐标系 | 同帮助：*"generates ephemeris in the True Equator Mean Equinox coordinate system"*（`vehSat_orbitProp_msgp4.htm:41`）→ XGZ 输出 TEME of Date ✅ |
| TEME→Fixed | `CoordTransform::gmstRad` 用 IAU-1982 GMST + UT1≈UTC（`core/CoordTransform.cpp:59-123`），与 Vallado TOD↔PEF 口径一致；注释已说明为何**不能**换 IAU-2006（`:51-57`）。实测 ΔECEF 0.09 m |
| 站心 AER 定义 | 方位角自北顺时针、**detic**（椭球法线）仰角、几何斜距（`CoordTransform.cpp:694-709`）。STK 帮助亦明确折射模型要"adapted … by using detic elevation rather than centric"（`stk/sn-14.htm:53`）→ 实测 ΔAz/ΔEl = 0.000000°、ΔRange 0.02 m |
| 大地基准 | WGS-84 椭球（`CoordTransform.h:15-18`）；STK 帮助：站点高度"references WGS84"（`stk/toolsdb-02.htm:78`）、椭球为 "WGS-84 ellipsoid for the Earth"（`stk/tools-02a.htm:197`）✅ |
| 窗口末端截断 | `OrbitCalculator.cpp:868` 按 STK Access 语义在窗口终点截断并标 `truncated` ✅ |
| 采样时刻无漂移 | "起点 + 整数索引 × 步长"（`OrbitCalculator.cpp:326-331`、`xgz_dump.cpp:159-166`）✅ |

⚠️ **一个必须点明的验证盲区**：`tools/stk_compare` 里 `xgz_dump` 输出的是 `look.elevDeg`（**几何**仰角，`tools/stk_compare/xgz_dump.cpp:174`），
所以那套"181/181 点、ΔEl 0.000000°"的比对**完全没有覆盖折射链路**——即使折射实现全错，该比对仍然全绿。
同理它也不覆盖光照分类、暮光判据、Access 时刻（AOS/LOS）。

---

## 2. 不一致项明细

### A1（P0）折射：默认值与模型都与 STK 11 不同，且代码/UI 有三处错误声明

**STK 11 事实**（`stk/sn-14.htm`）：
- `:161` *"By default, refraction is not considered when performing the relative apparent position computation in Access."*
  → **STK 的默认是"Access 不算折射"**，需要用户勾选 *Use Refraction in Access Computations*。
- `:58-63` 模型清单只有三种：**Effective Radius（4/3 地球半径）**、**ITU-R P.834-4**、**SCF（'RC, Smith 1978）**。
  **没有 Saastamoinen 模型**；且 STK 的折射模型是"天气无关"的简化模型（ITU 用地面折射率、SCF 用折射率多项式）。

**XGZ 现状**：
- `core/CoordTransform.cpp:705-707` 在 `computeLookAngle` 内无条件算折射，固定标准大气 T=10 ℃、P=1013.25 hPa、H=40%；
- `core/CoordTransform.h:88` 注释："标准大气(与 STK 11 一致)"；
- `ui/MainWindow.cpp:657` "与 STK 11 Access 默认行为对齐"、`:659` "默认启用，与 STK 11 一致"、`:1523` 同；
- `business/PassPredictWorker.h:35` `applyRefraction = true`。

**量化影响**（本次实测，ISS，站 33°N/103°E/0.05 km）：

| 掩星角 | 最大 \|ΔAOS\| | 最大 \|ΔLOS\| | 备注 |
|--------|--------------|--------------|------|
| 10°（应用默认） | **1.135 s** | **1.126 s** | 每颗卫星每天每次都偏 1 s 量级 |
| 5° | **3.850 s** | **3.857 s** | |
| 2° | — | — | **过境数 6 → 7**：折射把擦边过境抬升为"可见过境"，STK（默认无折射）不会列出 |

即：**用户若把 STK 与 XGZ 都按各自默认设置跑，同一过境的 AOS/LOS 会差 1 s 量级，低仰角场景还会多出过境**。

附带问题：`ui/MainWindow.cpp:1514` 仍写着 `// 大气折射已移除: STK 11 AER 报告为几何仰角`，
而紧接着 `:1523-1524` 又把折射接回 Access 判据——注释与代码自相矛盾（属上一轮"移除折射"重构的残留）。

> 说明：XGZ 的 Saastamoinen 实现本身是**物理正确**的（本次实测折射量：0° 29.02′、5° 9.69′、10° 5.41′、45° 1.01′，
> 与通行标准折射表 28.9′/9.9′/5.3′/1.0′ 相差 <2%）。问题不在精度，而在**默认开/关**与**模型不可选**。

### A2（P0）地影模型：工作树改成了"圆柱二态"，与 STK 的三态 Lighting 不符

**STK 11 事实**：
- `stk/constraints-02.htm:165-196`：Lighting 约束可选 **Direct Sun / Penumbra or Direct Sun / Penumbra / Penumbra or Umbra / Umbra / Umbra or Direct Sun**，
  即 **Penumbra（半影）是 STK 的一等状态**；
- `stk/map_lighting.htm:57-62`：明确给出 *Sunlight/Penumbra transition* 与 *Penumbra/Umbra transition* 两条界线（可按卫星高度绘制）；
- `stk/timeTool.htm:285`：`LightingIntervals` 组件包含 **Sunlight / Umbra / Penumbra** 三个子区间列表。
→ STK 的光照判定是**圆锥（含太阳视半径）三态**模型，不存在"无半影过渡的默认行为"。

**XGZ 现状**：`core/EarthShadow.cpp:37` 只返回 `0.0/1.0`（`d < Re → 本影`），`checkShadow` 只产生 `InUmbra/FullSunlight`；
头文件宣称 *"无半影过渡——这是 STK 11 的默认行为"*、*"参考 STK 11 Help > EarthShadow > Cylindrical"*（`core/EarthShadow.h:4-7`）——
**该帮助主题在本机 STK 11.2 全量帮助（20 078 个文件）中不存在**（按 `Cylindrical|Conical|shadow model|Eclipse Model` 全文检索，仅命中地图投影/柱坐标等无关主题）。

**量化影响**（ISS，本次实测，用标准面积法圆锥阴影函数 f∈[0,1] 对照）：

```
以圆柱边界为 0 点：
  t=-100.00 s  f=0.000  本影      ← XGZ 说"本影" ✅ 一致
  t=  -4.75 s  f=0.001  半影      ← STK 进入半影；XGZ 仍说"本影" ❌
  t=   0.00 s  f=0.577  半影      ← XGZ 在此切到"全日照" ❌（半影已过半）
  t=  +3.75 s  f=0.999  全日照    ← 此后一致
```
- LEO 半影带总宽 **≈8.5 s**；每天 31 次跨越 → 每次分类偏差最多 **4.75 s**（日照↔半影）、**3.75 s**（半影↔本影）；
- XGZ 的 `umbraFactor>0.5` 判据等价于"f>0.577"，与 STK 的 "Penumbra or Direct Sun"（f>0）差 3.75 s、与 "Direct Sun"（f=1）差 4.75 s；
- 过境"可见性"分类（TCA 落在半影带内时）会与 STK 判定翻转。

**连带死代码/过时注释**（改回圆锥前必须一并清理）：
- `core/DataTypes.h:122-126` 的 `ShadowStatus::InPenumbra`、`core/DataTypes.h:219-223` 的 *"半影带内按几何比例 f 取 f^0.7，非线性——详见 EarthShadow::umbraFactor"* → **`f^0.7` 在现实现中已不存在**；
- `ui/PassTableModel.cpp:218-229` 的"半影 %1%"单元格分支不可达；
- `Git`: **HEAD（f427d24）里的 `EarthShadow` 原本就是圆锥三态实现**（`git show HEAD:core/EarthShadow.cpp`），
  当前的圆柱实现是**未提交改动**（`git diff --numstat HEAD` = EarthShadow.cpp 20+/39−、EarthShadow.h 13+/14−）。

### A3（P0）重力常数模型：应用默认 WGS-84，STK 侧是 WGS-72

**事实**：
- STK 帮助：SGP4 默认用 **CSSI 例程 v2008-11-03**（`vehSat_orbitProp_msgp4.htm:40`），其参考实现的默认重力常数是 **WGS-72**；帮助本身未逐字文档化该常数（已检索确认）；
- 本机实测证据（工程自带验证链）：`tools/stk_compare/xgz_dump.cpp:133` 显式传 `GravityModel::WGS72` 才与 STK 逐点吻合到 **0.14 m**；
  上一轮审查记录亦注明"把 STK 侧换成 WGS-84 会带来 4.9 m 差"（`任务表1_代码核查与修改建议.md:440`）→ **STK 侧实际使用 WGS-72**；
- HEAD 版本**硬编码 WGS72**（`git show HEAD:core/Propagator.cpp`：`perturb::GravModel::WGS72`），工作树新增了下拉框并**把默认设为 WGS-84**：
  `ui/MainWindow.cpp:633-637`（列表首项 WGS-84、tooltip "默认，精度更高"）、`:2764`（重置默认 index 0）、
  `core/Propagator.h:35`（默认实参 `GravityModel::WGS84`）。

**量化影响**（本次实测，同一 TLE、同一测站、24 h 逐 15 s）：

| 对象 | 最大 \|Δr\|(WGS72−WGS84) | 最大 \|Δv\| | 最大 ΔAOS | 最大 ΔmaxEl |
|------|--------------------------|-------------|-----------|-------------|
| ISS（LEO，24 h 内） | **372.25 m**（t=+22.6 h） | **0.3946 m/s** | **0.050 s** | **0.004094°** |

即：**"XGZ 与 STK 差 0.14 m"这一结论只在 WGS-72 下成立**；按应用**默认设置**跑，与 STK 的偏差是 **数百米级**（沿迹分量，随离 TLE 历元时间增长）。
（ΔAOS 0.05 s × 7.6 km/s ≈ 380 m，与 Δr 372 m 相互印证。）

### B1（P1）过境合并与最短时长：STK Access 没有这两个概念

`core/OrbitCalculator.h:25-26`：`minDurationSec = 10.0`、`mergeGapSec = 60.0`。
- STK 的 Access 会把每一次"仰角约束满足"的区间**逐一列出**；两次间隔 < 60 s 的短弧在 STK 里是**两条** Access，XGZ 合成一条（`OrbitCalculator.cpp:874-961` 的合并逻辑还会取并集的最大仰角/可见弧段）；
- < 10 s 的弧在 XGZ 被**静默丢弃**，STK 仍会列出。
→ 与 STK Access 报告对账时，**条数/时长/最大仰角都可能不同**。建议至少提供开关（默认关闭）并在 UI/导出中标注"已合并"。

### B2（P1）光行时（light-time）

STK 帮助：*"Usually, light time delay between the objects will be considered"*（`stk/tools-01.htm:54`），
Access 高级选项有 *Use Light Time Delay* 与 *Aberration Type: None/Annual/Total*（`stk/tools-01a.htm:134-146`）；
而 AER 报告/数据提供器默认是**几何**量（这也是本次比对能吻合到 0.000000° 的原因）。
XGZ 的 Access 判据完全不含光行时：LEO 斜距 2000 km → τ ≈ 6.7 ms → 等效位置 ~53 m → AOS/LOS 偏差几十毫秒（远小于 A1/A3）。

### B3（P1）暗天判据的地平俯角修正：STK 无对应项

`core/OrbitCalculator.cpp:393`（`dark = darkSkyThresholdDeg − horizonDipDeg(altKm)`，`dip=1.0152√h`，`:29-33`；`fillPassDetails` 内同式见 `:141`）。
STK 的 Sun Elevation 约束是**相对站点本地切平面**的太阳高度，不含"站点抬高导致视地平线下沉"的修正。
默认站 50 m 时 dip≈0.227° ≈ **57 s** 的暮光时刻差；海拔 1 km 站可达 ~1° ≈ 4 min。
另外 UI 表格里"太阳高度"列显示的是**未经 dip 修正**的原始太阳高度，而分类用的是修正后的阈值 → 用户拿界面上的角度去比对自己设的 −6° 会对不上（显示与判据不同源）。

### B4（P2）太阳星历精度

`core/SunPosition.cpp:1-2`：Meeus 第 25 章低精度式（~0.01°）+ 63 项章动 + ΔT 分段式（`:43`）。
STK 用自身高精度太阳星历（并可含光行时/光行差）。暮光边界换算：太阳高度变化率 ~0.004°/s（中纬度日落）→ **2–3 s** 量级偏差。
代码本身自洽（用 GAST 配视赤经、光行差与章动各只算一次，`:91-101` 有详细勘误说明）✅

### C 档（轻微/功能缺口）

- **C1** AOS/LOS 毫秒量化（`jdToMs`）：±0.5 ms → LEO ±3.5 m。要做"逐秒 CSV 与 STK 零量化对齐"需在 `PassEvent` 存双精度 JD（前一轮已记为可选项）。
- **C2** `Observer` 构造把掩星角钳到 `[0°, 90°]`（`core/Observer.cpp:33`）→ 无法复现 STK 的负仰角约束（SCF 模型可算到 −0.72°）。
- **C3** TLE 宽容度**高于** STK：`TleParser::normalizeTleLines` 会自动剥离字母 NORAD 前缀、补隐含小数点、压缩多余空格（`Propagator.cpp:49-51`、`TleParser.cpp:382`）。
  注释写"与 STK 宽容策略对齐"，实际是"比 STK 更宽容"——建议改写注释，避免误导。
- **C4** 多普勒只有一阶距离变化率（`core/DopplerCalc.cpp:54-59`），ECEF 速度含地球自转项 ✅；STK 的 Doppler 属 Comm/Radar 模块（含收发两端/光行时/可选相对论项），
  若要"与 STK 报告一致"须按 STK 定义逐项核对（本工程未声称，但表格里叫"多普勒频移"易被直接对账）。
- **C5** "可见性"是 XGZ 复合判据（暗天 + 被照亮，`OrbitCalculator.cpp:130-141`），STK 中需用 **Sun Elevation 约束 + Lighting 约束**两条组合复现 → 建议在 UI/文档给出"如何在 STK 中复现"的对照说明。

---

## 3. 建议的修复（分两档）

### Tier A —— 无争议（只影响声明与死代码，不改结果）
1. **删除/更正错误声明**：`MainWindow.cpp:657,659,663,1523`、`CoordTransform.h:88`、`EarthShadow.h:4-7,37`、
   `PassPredictWorker.h:35` → 改为准确表述（"STK 11 默认不计折射；本工程默认启用 Saastamoinen，与 STK 需显式对齐"、"圆柱二态模型，与 STK 的 Lighting 三态不等价"）。
2. **清理矛盾注释**：`MainWindow.cpp:1514`"大气折射已移除"。
3. **地影死代码**：`DataTypes.h:219-223` 的 `f^0.7` 注释、`PassTableModel.cpp:218-229` 的"半影"分支、`ShadowStatus::InPenumbra` 的可达性说明——或在 §A2 修复后恢复其语义。
4. **默认重力模型改回 WGS-72**（与 HEAD、与 STK、与唯一被验证过的配置一致），或把 WGS-84 明确标注为"非 STK 默认"。

### Tier B —— 需产品决策（会改变输出）
1. **折射默认关闭**（完全对齐 STK 默认），或保留默认开启但在 UI/导出中标注"STK 默认不含折射，对账时请两侧一致"；
2. **地影恢复圆锥三态**（HEAD 已有实现；建议直接用标准面积法阴影函数而不是 `(θ−(ρe−ρs))/(2ρs)` 线性斜坡），
   并把分类阈值对齐 STK 的 Direct Sun(f≈1) / Penumbra(0<f<1) / Umbra(f=0)；
3. `mergeGapSec`/`minDurationSec` 可配置或默认关闭；
4. Access 增加"考虑光行时"选项；暗天阈值增加"地平俯角修正"开关；
5. `PassEvent` 保存双精度 AOS/LOS（消除毫秒量化）。

---

## 4. 建议的验证矩阵（现有工具覆盖不到的部分）

| 待验证项 | STK 侧设置 | 比对量 | 预期容差 |
|---|---|---|---|
| 几何链（已有） | 任意；**关闭**折射 | TEME / Fixed / AER | 0.14 m / 0.09 m / 0.02 m（已达标，注意 `xgz_dump` 用的是 WGS-72） |
| 折射 | Facility/Sensor → Refraction = **ITU-R P.834-4**（或 SCF / Effective 4/3）；勾选 *Use Refraction in Access Computations* | 折射仰角、AOS/LOS | 模型不同 → 低仰角 0.01–0.05°（≈0.1–0.5 s），**需逐模型给出差异表** |
| 光照/地影 | Satellite → Lighting 数据提供器导出 Direct Sun/Penumbra/Umbra 区间；约束 Lighting | 进/出本影与半影时刻 | 圆柱 vs 圆锥差 ~4–5 s/端（见 §A2） |
| Access 时刻 | Access 报告（Start/Stop/Duration/Max El）；若要"几何口径"需关闭 light-time | AOS/LOS/TCA | 当前 ≤0.035 s / ≤0.001 s（WGS-72+双侧同折射设置下） |
| 暮光判据 | Facility → Sun Elevation 约束（−6°/−12°/−18°） | 暗天起止时刻 | dip 修正差异 → 50 m 站 ~57 s |
| 重力模型 | 卫星传播器常数（STK 侧 WGS-72） | 位置 | WGS-84 默认下 → 数百米（见 §A3） |

---

## 5. 本次审查的实测方法（可复现）

1. 复用工程 release 目标产物：`release/obj/{TleParser,Propagator,CoordTransform,Observer,SunPosition,Nutation,EarthShadow,DopplerCalc,VisualMagnitude,OrbitCalculator,perturb,sgp4,tle}.o` + `-lQt6Core`；
2. 临时程序做四组测量（用完已删除，需要时可落成 `tools/stk_audit/`）：
   - **A** `predict` 分别用 WGS-84 / WGS-72 → 配对过境的 ΔAOS/ΔLOS/ΔmaxEl；
   - **B** 折射开/关 × 掩星角 {10°,5°,2°} → ΔAOS/ΔLOS 与过境条数；
   - **C** `applyRefraction` 在 0°…90° 的折射量，与通行标准折射表对照；
   - **D** 1 s 扫描 24 h：`EarthShadow::umbraFactor`（圆柱）边界 vs 标准面积法圆锥阴影函数 f 的跨越时刻与 f 值；
   - **E** WGS-72/WGS-84 双传播 24 h 逐 15 s 的 |Δr|、|Δv| 最大值。
3. STK 侧事实取自本机 `Help\Content` 全文检索（`.htm` 主题，含行号引用见上）。

---

## 6. 附：与 STK 11 对账时的操作建议（给用户）

- **想要与 STK 对得上**：STK 侧保持 SGP4 默认（WGS-72）、Facility 折射**不勾选** *Use Refraction in Access Computations*、Access 关闭 light-time。
  XGZ 侧（本轮整改后已默认如此）：**重力模型 = WGS-72**、**无折射功能**。此时 AOS/LOS 差 ≤0.05 s 量级、位置差 0.14 m 级。
- **光照/可见性**：XGZ 已恢复**圆锥三态**（本影/半影/全日照），与 STK Lighting 的 Direct Sun / Penumbra / Umbra 一一对应；
  但 XGZ 的"可见性"仍是"暗天阈值 + 被照亮"的复合判据，STK 中需用 **Sun Elevation 约束 + Lighting 约束**两条组合复现。

---

## 7. 本轮整改实施记录（按"以 STK 11 为基准 + 删除无关功能"的指令）

### 7.1 已删除的功能

| 功能 | 删除范围 |
|------|----------|
| **大气折射** | `CoordTransform::applyRefraction`（含 `CoordTransform.h` 声明）、`computeLookAngle` 内的调用、`Observer` 的 `applyRefraction` 构造参数与访问器、`DataTypes::LookAngle::elevRefracDeg` 字段与 4 参构造、`OrbitCalculator::elevationAt` 改用 `elevDeg`、`PolarPlotWidget` 绘图改用 `elevDeg`、`MainWindow` 的"大气折射修正（Saastamoinen）"复选框与其全部链路（`DetailPropagationParams`、逐秒详情重传播、`tools/stk_compare/xgz_dump.cpp`） |
| **强制刷新（忽略本地缓存）** | `MainWindow` 复选框与 `onOnlineUpdate` 分支、`TleFetcher::fetchGroup/fetchSatellite/tryServeFromCache` 的 `forceRefresh` 参数（缓存语义保留：2 h 内直接复用） |
| **强制校验和 / 校验和机制** | `TleParser::verifyChecksum`、`TleData::checksumValid`、`ParseError::CHECKSUM_FAILED`、`TleParser::parse` 的 `strictChecksum` 参数、`Job::strictChecksum`、`PassPredictWorker::checksumWarning` 信号、`PassPredictorService::checksumWarning` 信号、`MainWindow::onChecksumWarning` 槽与状态栏提示、`MainWindow` 的复选框。<br>依据：STK 帮助 TLE 表格中第 69 列明写 **"No validation performed"**（行1/行2 均是），即 STK 根本不校验校验和 |
| **星等估算（Vallerie 1963）** | 整条纵向功能：源文件 `core/VisualMagnitude.{h,cpp}`、`PassEvent::magAtTca/magValid`、`Config::enableMagnitude/stdMagFallback`、`fillPassDetails` 的星等分支、合并路径的星等字段、UI"固有星等"输入框、表格"星等"列（含 tooltip/对齐/列宽）、CSV/KML 的星等字段。<br>依据：STK 无此功能（属 XGZ 自定义估算模型） |
| **多普勒** | 整条纵向功能：源文件 `core/DopplerCalc.{h,cpp}`、`ui/DopplerPlotWidget.{h,cpp}`（含"多普勒曲线"页签）、`PassEvent::dopplerAtTcaKHz`、`PassSample::dopplerKHz`、`Config::enableDoppler/carrierFreqMHz`、采样与 TCA 的多普勒计算、UI"载频(MHz)"输入框、表格"多普勒"列、逐秒详情第 5 列、CSV/KML 的多普勒字段、`CoordTransform::eciStateToEcef`（仅多普勒使用，随之删除）。<br>依据：STK 的 Doppler 属 Comm/Radar 模块，基础 AER/Access 无此报告 |
| **JSON 导出** | `writePassesJson`、`onExportJSON` 槽、"导出 JSON"按钮、`ExportKind::Json`、文件大小预估对话框、`QJson*` 头文件。<br>依据：STK 无 JSON 导出（报表/CSV 与 KML 才是 STK 对应能力） |
| **死代码（随上述删除暴露）** | `CoordTransform::clearCache/getCacheSize`（仅测试调用）、`CoordTransform::eciStateToEcef`、`TleData::isDecayed`、`TleData::periodMinutes()`、`PassEvent::passType`（只写不读，表格/导出均未使用） |

**删除后仍保留的功能**（均可在 STK 中复现，不属"无关"）：
过境预报与 AOS/TCA/LOS、掩星角约束、窗口末端截断语义、**可见性分类与可见弧段**（STK 中 = Sun Elevation 约束 + Lighting 约束组合）、太阳高度/光照三态、斜距/方位角、弧段采样、极坐标图、地面轨迹图、逐秒数据页签与逐秒详情 CSV、CSV/KML 导出、TLE 在线更新与 2 小时本地缓存、TLE 批量导入。

### 7.2 以 STK 11 为基准的口径修正

| 项目 | 修正前 | 修正后（STK 口径） |
|------|--------|-------------------|
| **SGP4 重力常数默认值** | WGS-84 | **WGS-72**（STK 的 SGP4 默认 = CSSI 例程 WGS-72 常数；也是唯一与 STK 实测 0.14 m 吻合的配置）。24 h 位置差：WGS-84 vs STK 最大 **372 m**（ISS）→ WGS-72 恢复 0.14 m 级 |
| **地影/光照模型** | 圆柱二态（本影/日照），`InPenumbra` 不可达 | **圆锥三态**：`f = 太阳圆面未被遮挡面积比`；θ≥ρe+ρs → 全日照、θ≤ρe−ρs → 本影、其间 → 半影。与 STK Lighting 的 Direct Sun / Penumbra / Umbra 对应（LEO 半影带约 8.5 s，实测量级见 §2/A2） |
| **TLE 解析** | 按空格分词再"重排列对齐"（`normalizeTleLines`），可自动修列错位/缺小数点/字母前缀 | **STK 定列读取**（fixed-read）：只按 STK 帮助列号取值；新增 STK 的列格式校验（空列必须空格、小数点必须在指定列、数字列必须数字、倾角 0–180、RAAN/近地点辐角/平近点角 0–360、平运动 ≤17.0、历元日整数部分 ≤366）。列错位等输入现在**被拒绝**而不是被"修好" |
| **解析字段覆盖面** | 仅 7 个根数 + 历元 + B* | 补齐 STK `IAgVeSGP4Segment` 暴露的全部字段：n/2、n/6、密级、国际编号、星历类型、元素集号、编目号原始串（`TleData` 新增 6 个字段） |
| **历元 2 位年→4 位年** | `≤56 → 20xx` | 保持 **`<57 → 20xx`**（SGP4/CSSI 参考实现与 perturb 内部一致；STK 内部即用 CSSI 例程）。注：STK 帮助"报告风格"一节写的是 00-59→20xx（分界 60），仅在 1957–1959 三年份上有差异，属文档与实现的口径差 |

### 7.3 TLE 解析一致性验证（要求：与 STK 一致，误差 < 0.001）

在 STK 11 侧的"解析值"无法通过跨进程 COM 直接导出（见 §7.5）的情况下，采用三重独立验证：

| 验证 | 方法 | 结果 |
|------|------|------|
| ① 与**真正参与传播**的解析一致 | `TleParser` ↔ perturb `TwoLineElement`（`Satellite::sat_rec`，统一换算到度 / rev·day⁻¹ / rad·min⁻ⁿ） | 6 个用例 × 11 字段，最大偏差 **1.4e-14** |
| ② 与**按 STK 列定义独立重算**一致 | PowerShell `[decimal]`（28 位十进制）按列号取值精确重算，与 XGZ 解析值逐字段比较 | 最大偏差 **3e-14** ≪ 0.001 → **PASS** |
| ③ 与 STK 的**外部一致性**（已有工具） | `tools/stk_compare`（同 TLE、同测站，181 点） | ΔTEME 0.14 m / ΔECEF 0.09 m / Δ斜距 0.02 m / Δ角 0.000000°（WGS-72 配置） |

用例：ISS、STK 帮助自带示例（06909 / 13083 / 10637，含 12 h 共振与高偏心）、负 B*、70+ 列扩展格式。
校验行为验证：列错位、"平运动 > 17"、"倾角 > 180"、"历元日 367"、小数点错位、偏心率列含字母 —— **全部按 STK Validator 规则拒绝**；第 69 列校验和故意写错 —— **仍正常解析**（与 STK 一致）。

### 7.4 仍存在的差异（已知、未改）

1. **校验和**：STK 完全不校验；XGZ 的 `TleParser` 也已不校验，但**传播内核 perturb 会校验并在不符时拒绝**（`TLEParseError::CHECKSUM_MISMATCH` → `INVALID_TLE`）。
   即：校验和错误的 TLE 在 XGZ 仍会在传播初始化阶段失败、而 STK 会照算。要完全等同需改第三方内核，未做。
2. **星历类型（列63）**：STK 接受 0 与 2（=2 表示平运动已是 Brouwer 形式、不做 Kozai 转换）；perturb 要求 `ephemeris_type == 0`。
   即：类型 2 的 TLE（极罕见）XGZ 拒绝、STK 可用。
3. **行长**：STK 帮助自带的示例 TLE 只有 **68 列**（缺第 69 列校验和），而 XGZ 与 perturb 都要求 ≥69 列（CSSI 例程同样要求）。使用该示例需自行补第 69 列。
4. **光行时/光行差**：STK Access 默认考虑光行时（AER 报告默认仍是几何量）；XGZ 全链路为几何，未做。
5. **过境合并/最短时长**：`mergeGapSec=60 s`、`minDurationSec=10 s` 与 STK Access"逐条列出"不同。
6. **暗天阈值的地平俯角修正**、**毫秒量化**、**负掩星角** —— 见 §2/§3，尚未处理（属"是否对齐"的口径决策）。

### 7.4.1 删除无关功能后的回归验证

- `mingw32-make -j8 release`：**exit 0，零警告**（`-Wall -Wextra`）；`release/XGZ.exe` 启动冒烟通过（存活且 `Responding`）。
- 引擎回归（ISS，24 h，WGS-72）：过境 3 条，`AOS≤TCA≤LOS`、时长 >0、可见弧段落在过境内、弧段采样点全部落在过境区间内且无 NaN —— **全部通过**；
  光照三态（10 s 采样）全日照 5227 / 半影 26 / 本影 3387 —— **三态均出现**。
- `tools/stk_compare/xgz_dump.cpp` 独立编译通过（删除折射参数后仍可用）。
- 全树检索确认 `doppler/Doppler/magAtTca/magValid/stdMag/VisualMagnitude/QJson/writePassesJson/onExportJSON/exportJson` **零残留引用**。

### 7.5 环境与过程备注

- 本机 STK 11.2 配置（`Documents\STK 11 (x64)\Config\Defaults\_Default.ap`）：
  `BEGIN TLEValidation → Validator On`（列格式校验开启，与 §7.2 的实现口径一致）；
  `BEGIN SGP4Defaults → Use1PtSGP4Prop Yes / Validate1PtSGP4Prop No / Suppress1PtSGP4InterpWarning Yes / DefaultSGP4SwitchMethod Epoch`。
- 曾尝试用 COM 自动化直接读取 STK 解析后的根数（`IAgVePropagatorSGP4.Segments.Item(i)` 的 Inclination/RAAN/… 属性）：
  已成功启动 STK 11、建立临时场景并插入卫星，但跨进程 COM 无法把返回对象 QI 到 `AGI.STKObjects.IAgSatellite`
  （`SetPropagatorType` 在 IDispatch 上亦不可见），故改为上表 ①②③ 三重验证。临时场景未保存、未落盘，已确认无残留。
- **过程事故（已修复）**：`ui/PolarPlotWidget.cpp` 曾被一次批量文本替换（PowerShell 5.1 的 `Get-Content -Raw | Set-Content`
  默认按 ANSI 编解码，而源文件是 UTF-8 无 BOM）破坏编码与换行；已从 `XGZ_源码_最终版.zip`（2026-09-15 快照）恢复该文件，
  并重新应用工作树中唯一早于快照的实质改动（`setSelectedPass` 越界时置 `-1`；经 ASCII 结构逐行比对确认无其它差异）。
- 删除星等/多普勒/JSON 时，`ui/MainWindow.cpp` 的其中一批改动同样用 PowerShell 按行号删除，导致 3 处无关行被误删
  （`Propagator prop;`、`for (sec…)` 循环头、`QFileDialog::getSaveFileName` 及若干闭合花括号）。已由编译器定位并逐处修复，
  最终构建零警告、冒烟与引擎回归通过。**教训**：批量删除一律改用编辑工具按上下文锚点操作，不再按行号做文本手术。

---

## 8. 第三轮：按"影响评估报告"逐项核对与修复

针对外部提交的《各项问题对功能的实际影响评估》逐条复核（含"是否真的能触发"的判断）：

| 编号 | 报告结论 | 复核判定 | 处置 |
|------|----------|----------|------|
| **P0-1** | KML 按钮被 connect 两次 → 每次导出弹两个保存框 | ✅ **属实**（是本轮删 JSON 时把 `connect(m_exportJsonBtn…)` 误改成 `connect(m_exportKmlBtn…)` 造成的重复连接） | **已修**：删除重复行；已核验 CSV/KML 各只有一条 `connect` |
| **P0-2** | `findTCA` 兜底路径字段不一致（极坐标/TCA 标记画到屏幕外） | ⚠️ **已不成立**（第二轮 M-9 已修：兜底时把 `bestJD` 一并回退到粗扫峰值时刻 `coarseBestJD`，`computeMoment` 与 `outElDeg` 同刻） | 无需改动；残余路径分析见下方注① |
| **M-1** | 编目号列验证逻辑不直白 | ✅ 成立（`!allOf(isAlnum) && !allDigitOrBlank(...)` 冗余） | **已修**：改为 STK 原文判据 `isAlnumOrBlank`（列3-6 字母数字或空格、列7 必须字母数字），并新增"字母编目号 `A0370`"用例（`noradID=0`、`noradStr` 保留原文） |
| **M-2** | 403 提示仍让用户"取消勾选强制刷新"（该复选框已删） | ✅ **属实** | **已修**：文案改为"本程序对 2 小时内的成功下载自动复用本地缓存（状态栏会提示"使用本地缓存"）" |
| **M-3** | 删除强制刷新后 2 小时内无手动刷新入口 | ✅ **属实（功能回退）** | **已修（按你的选择）**：不加常驻控件，改为 **Ctrl+点击"下载 TLE"** 或 **该按钮右键菜单 → "忽略本地缓存强制刷新"**；`TleFetcher` 新增 `refetchGroup()`（`fetchGroupImpl(groupId, bypassCache)`），缓存命中提示与按钮 tooltip 都写明了该入口。数值验证见下 |
| **M-4** | `parseScientific` 的 catch 过宽 | ✅ 成立（会把 `bad_alloc` 改写成"格式错误"） | **已修**：只捕获 `std::invalid_argument` 并补上下文，其它异常向上传播 |
| **M-5** | 地影用日地距离代替卫星-太阳距离 | ✅ 成立但量级 5e-5（≈0.05 ms） | **已修**：改用 `|sunDir·AU − r_sat|` 精确距离（无额外代价） |
| **L-1** | `numericField` 空格处理 | 对标准 TLE 结果一致 | 不改（与 STK `sscanf("%lf")` 忽略空格的行为一致） |
| **L-2** | `assert(false)` 在 Release 静默 | 成立（且本工程 release **未定义 NDEBUG**，断言为启用态，风险是 abort） | **已修**：移除该 `assert`，改为注释说明 `-Wswitch` 会在新增枚举值时报警 |
| **L-3** | `DataTypes.h` 注释过时 | ✅ 成立（字段数、校验和措辞） | **已修**：`DataTypes.h`（字段计数/校验和）、`CoordTransform.h`（工具列表）、`MainWindow.h`（页签数/导出格式）、`ErrorHandler.cpp`（校验和分支与契约注释）、`PassPredictWorker/Service`、`TleFetcher` 的相关注释 |
| **L-4** | `#include <QSplitter>` 未用 | ✅ 成立 | **已修**：删除 |
| **L-5** | 缓存命中时状态栏先闪"正在获取" | 无实际影响（`cacheServed` 信号已在同一轮事件循环内改为"使用本地缓存（N 分钟前下载…）"） | 不改 |
| **L-6** | 模型后缀拼接判据 | 可接受降级 | 不改 |
| **M-6** | `scopeNote` 冗余 | ✅ 成立 | **已修**：改为传 `nullptr`（范围说明由 `startExport` 直接进状态栏） |

**注①（P0-2 残余路径分析）**：兜底分支只在 `outElDeg==SENTINEL && bestEl> SENTINEL` 时触发，而 `bestEl` 必然来自一次成功的 `elevationAt(coarseBestJD)`
→ 该时刻 `propagate` 成功、`eciToEcef`/`lookAngle` 均产生有限值 → `computeMoment(coarseBestJD)` 不会再失败，仰角与 LookAngle/GeoPos 同刻一致。
若粗扫阶段就全失败（`bestEl==SENTINEL`，典型为使用已再入卫星的过期 TLE），`maxElevDeg` 保持 −1000 → `assemblePass` 的 `maxElevDeg < maskEl` 直接丢弃该段，
不会产生"表格有仰角、图上画到屏幕外"的组合。故该问题在当前代码中**无用户可见路径**。

**本轮验证**：`mingw32-make -j8 release` → exit 0、零警告；启动冒烟通过；
临时校验程序（TLE 解析 ↔ perturb 逐字段 + 引擎回归 + 光照三态）→ **ALL CHECKS PASSED**
（6 个 TLE 用例含字母编目号；过境 3 条不变量全过；三态齐全）；
**强制刷新入口行为验证**（临时 Qt 程序，隔离缓存目录）：写入新鲜缓存后
`fetchGroup()` → `cacheServed` 命中 1 次（走缓存）；`refetchGroup()` → `cacheServed` 0 次（确实绕过缓存）→ **PASS**。

顺带删除两处死 API：`TleCache::remove()`、`TleCache::clearAll()`（强制刷新入口改为修饰键方案后已无调用者）。

### 8.1 强制刷新入口的使用方式（无需常驻控件）

| 操作 | 行为 |
|------|------|
| 左键"下载 TLE" | 常规下载：2 小时内的成功下载直接复用本地缓存；命中时状态栏提示"使用本地缓存（N 分钟前下载…）" |
| **Ctrl + 左键** | 忽略本地缓存强制联网（状态栏显示"强制刷新…（忽略本地缓存）"） |
| **右键"下载 TLE"** | 弹出单项菜单"忽略本地缓存强制刷新" |

限制：CelesTrak 对同一组数据 2 小时内只允许拉取一次，强制刷新可能直接返回 HTTP 403——
此时会弹出专门的限流说明（文案已更新为指向"自动复用本地缓存 + 稍后再试"）。

---

## 9. 第四轮：按第二次影响评估报告逐项核对与修复

| 编号 | 报告结论 | 复核判定（含证据） | 处置 |
|------|----------|-------------------|------|
| **P0-1** | `ev.passType` 赋值但字段已删 → **编译失败** | ❌ **不成立（报告基于旧快照）**。证据：① 全树检索 `passType` **零命中**（`fillPassDetails` 与合并重算两处赋值在删字段的同一轮即已删除）；② 删除 `release/obj/OrbitCalculator.o` 后强制重编译并链接 → **exit 0** | 无需改动；本轮再次以强制重编译验证 |
| **P0-2** | `applyFilterAndRefresh` 缺 `m_groundTrack->setSelectedPass(row)` | ✅ **属实**（该处历史上就只同步了极坐标，`onTableRowSelected` 则同步两个） | **已修**：抽出 `syncSelectedPass(row)`（集中同步极坐标 + 地面轨迹），两处入口统一调用；并补上"行号未变但该行已换成别的过境"时重算逐秒详情（原选中被"仅显示可见"过滤掉的场景，否则逐秒表停留在已被过滤掉的过境上） |
| **M-1** | `onOnlineUpdate` 已成死代码 | ✅ 属实（连线改为 lambda → `forceFetchTle`） | **已修**：删除该槽与声明；`retryNetworkOperation` 直接调 `forceFetchTle(false)` |
| **M-2** | `m_sharedDataBusy.store(false)` 早于数据交换 → "清空"可能被静默撤销 | ✅ 属实 | **已修**：守卫改为**深度计数** `m_sharedDataWaitDepth`，从"进入等待"一直保持到"数据交换完成并解锁"；`onClear`/`showDetailForPass` 按 `>0` 短路 |
| **M-3** | `onResultsReady` 入口按 busy 短路会**永久丢弃新结果** | ✅ 属实（且与下一轮的 newest-wins 语义冲突） | **已修**：删除该入口检查，交由 `m_resultsGeneration`（后到者接管，前一轮在重入检测处自行作废） |
| **M-4** | busy 令牌与世代号职责重叠 | ✅ 属实 | **已修**：明确分工并在头文件注释写明——世代号 = 结果之间 newest-wins；深度计数 = 只保护其它改写槽（清空/详情重算），二者不再互相干扰 |
| **M-5** | `TleFetcher::cancel` 重入路径复杂 | 复核：逻辑正确（取消分支只 `deleteLater`，`cancel()` 自身清 `m_pendingTag`、按 `hadReply/hadPendingCache` 决定是否 emit `finished`） | 不改（注释已在上一轮写明；本轮未发现功能缺陷） |
| **M-6** | `umbraFactor` 用方向矢量×距离转位置有精度损失 | 复核：相对误差 ~7e-17（双精度绝对误差 1e-8 km / 1.5e8 km） | 不改（报告自身结论亦为"可忽略"） |
| **L-1** | `data()` 中 `index.row()` 先于 `isValid()` | ✅ 成立（安全但不合惯例） | **已修**：先判 `isValid()` 再取行号 |
| **L-2** | `BAD_FORMAT` 文案含开发者术语 | ✅ 成立 | **已修**：改为"TLE 列格式不符: <列号说明>（本程序按 STK 11 的定列读取规则解析…）" |
| **L-3** | `resetToDefaultConfig` 两次取时导致窗口略短于 24 h | ✅ 成立（微秒级） | **已修**：改用同一个 `now` 计算起止时刻 |
| **L-4** | `assemblePass` 以 `[&]` 捕获 `cancel` | 复核：生命周期覆盖，正确 | 不改 |
| **L-5** | 批量下 `m_statusEpoch` 可能停留在"历元: —" | 复核：合理默认值 | 不改 |
| **L-6** | `/*bypassCache=*/false` 注释式命名参数 | ✅ 成立（MSVC 视作普通注释） | **已修**：改为 `false /* bypassCache */` 写法 |

**本轮验证**：`mingw32-make -j8 release` → **exit 0、零警告**；`release/XGZ.exe` 启动冒烟通过（存活且响应）；
`setSelectedPass` 调用点全树仅剩 `syncSelectedPass` 一处（杜绝再次漏同步）；`m_sharedDataBusy`、`onOnlineUpdate` 零残留。

> **关于"提交前必须编译"**：本轮报告把 P0-1 记为编译失败，但该结论对应的代码状态在本仓库已不存在——
> 上一轮删除 `passType` 时已同步删除赋值，且当轮即构建通过。为保证结论可核验，本文档中每条
> "已修"都附带可复现的验证方式（检索命令 / 强制重编译 / 数值程序）。


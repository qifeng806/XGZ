# XGZ — 卫星 TLE 过境预报系统

基于 **Qt 6 / C++17** 的桌面端卫星过境预报软件。输入 TLE 根数或从 CelesTrak 在线下载，计算卫星相对地面观测站的过境事件（AOS / TCA / LOS）、仰角与方位轨迹、光照可见性，并以表格、极坐标图和地面轨迹图展示，支持批量预报与 CSV / KML 导出。

## 功能特性

- **TLE 输入与下载**
  - 手动粘贴两行 / 三行 TLE
  - 从 CelesTrak 按组下载（空间站、最亮 100、活跃卫星、Starlink、OneWeb、GPS / GLONASS / Galileo / 北斗、气象、科学卫星等）
  - 本地磁盘缓存（默认 2 小时），避免重复请求触发 CelesTrak 限流；支持强制刷新
- **过境预报**
  - 15 s 粗搜 + 二分法精化，输出 AOS / TCA / LOS、最大仰角、方位角、斜距、过境时长
  - 可配置最小仰角、预报时间窗、时区
  - 支持 WGS-72 / WGS-84 重力模型
  - 过境合并与最短时长过滤
- **可见性分析**
  - 太阳位置与章动计算、圆柱地影模型
  - 暗天阈值判定（默认太阳高度 −6°）与可见弧段子区间
  - “仅显示可见过境”过滤
- **批量模式**
  - 从文件导入多颗卫星 TLE，后台并行计算
  - 进度显示与错误汇总，结果统一排序
  - TLE 历元龄期提示，过期数据告警
- **结果展示**
  - **过境列表**：可排序、可多选、懒加载表格
  - **极坐标图**：以观测站为中心的方位—仰角轨迹
  - **地面轨迹图**：内置 110m 陆地轮廓底图，显示星下点轨迹与观测站
  - **逐秒数据**：选中过境的逐秒位置 / 角度 / 斜距明细
- **导出**
  - CSV、KML（Google Earth）
  - 可选附带逐秒详情；有选中行时只导出选中过境，否则导出全部
- **工程性**
  - 计算与下载全部在后台线程执行，可协作式取消
  - 单实例保护、深色主题、统一错误处理

## 界面

主窗口包含四个页签：

| 页签 | 内容 |
|---|---|
| 过境列表 | 全部过境事件及关键参数，支持排序与过滤 |
| 极坐标图 | 选中过境的方位—仰角轨迹与可见弧段 |
| 地面轨迹 | 星下点地面轨迹、观测站位置与可见范围 |
| 逐秒数据 | 过境期间的逐秒计算结果 |

## 环境要求

- **Qt 6.11+**，需要 `core`、`gui`、`widgets`、`network`、`concurrent` 模块
- **C++17** 编译器
  - MinGW-w64（推荐 Qt 自带的 13.1 工具链）
  - 或 MSVC 2022
- **qmake**（随 Qt 安装）

## 构建

### 命令行（MinGW）

```bash
git clone https://github.com/qifeng806/XGZ.git
cd XGZ
qmake XGZ.pro
mingw32-make -j8
```

生成的程序位于 `release/XGZ.exe`（Debug 构建目标名为 `XGZ_d`）。

### Qt Creator

1. 打开 `XGZ.pro`
2. 选择已配置的 Qt 6 Kit（MinGW 或 MSVC）
3. 构建并运行

> 支持影子构建（shadow build）。工程通过相对路径设置 `DESTDIR` / `OBJECTS_DIR` / `MOC_DIR`，请勿在 `release/` 子目录内直接运行 `make`。

## 使用流程

1. 在左侧输入 TLE，或选择卫星组后点击“在线更新”下载
2. 设置观测站纬度、经度、海拔，最小仰角、时间窗、时区与暗天阈值
3. 点击“开始预报”
4. 在四个页签中查看结果；选中过境后可导出 CSV / KML

> 在线数据来自 [CelesTrak](https://celestrak.org/)。GP 数据约每 2 小时更新一次，请遵守其数据使用政策，避免高频请求。

## 目录结构

```
XGZ/
├── main.cpp                  # 程序入口、主题与单实例保护
├── XGZ.pro                   # qmake 工程文件
├── core/                     # 计算核心
│   ├── TleParser.*           # TLE 解析与校验
│   ├── Propagator.*          # SGP4/SDP4 传播器封装
│   ├── OrbitCalculator.*     # 过境搜索与事件组装
│   ├── CoordTransform.*      # TEME / ECEF / 站心坐标变换
│   ├── Observer.*            # 观测站模型
│   ├── SunPosition.*         # 太阳位置
│   ├── Nutation.*            # 章动修正
│   ├── EarthShadow.*         # 地影与光照判定
│   └── DataTypes.h           # 公共数据结构
├── business/                 # 业务调度
│   ├── PassPredictorService.*# 单星 / 批量任务调度
│   ├── PassPredictWorker.*   # 单星预报工作线程
│   ├── TleFetcher.*          # CelesTrak 下载与超时控制
│   ├── TleCache.*            # TLE 本地缓存（2 小时 TTL）
│   └── TleBatchParser.*      # 批量 TLE 解析
├── ui/                       # 界面
│   ├── MainWindow.*          # 主窗口与四页签
│   ├── PolarPlotWidget.*     # 极坐标轨迹图
│   ├── GroundTrackWidget.*   # 地面轨迹图
│   ├── PassTableModel.*      # 过境表格模型
│   └── CatmullRomSpline.h    # 轨迹平滑
├── resources/                # 图标、qrc、110m 陆地轮廓
├── perturb-master/           # 内置 SGP4/SDP4 引擎（第三方，MIT）
└── tools/stk_compare/        # 与 STK 11 的一致性验证工具
```

## 精度验证

`tools/stk_compare` 提供与 **STK 11** 的可重复对比流程：使用同一份 TLE、同一测站，分别导出 STK 与 XGZ 引擎的 TEME / ECEF / 站心 AER 序列并逐点比较。

本机实测（BEIDOU-3M / 43107）：

| 指标 | 最大偏差 |
|---|---|
| TEME 位置 | 0.14 m |
| ECEF 位置 | 0.09 m |
| 斜距 | 0.02 m |
| 仰角 / 方位角 | ≈ 0.000000° |

即同一输入下两者数值等价（相对误差约 5×10⁻⁹）。详细用法与排查步骤见 [`tools/stk_compare/README.md`](tools/stk_compare/README.md)。

## 已知限制

- 预报精度依赖 TLE 时效；数据过期时界面会给出历元龄期提示，建议及时更新。
- 坐标时标采用 `UT1 ≈ UTC` 近似，未引入 EOP 文件；与配置了 EOP 的 STK 场景可能存在米级/毫角秒级差异。
- 与 STK 的深空共振实现存在约 0.1 m 级残差，对过境时刻、仰角和多普勒结果无可见影响。
- 当前发布版未附带自动化测试工程。

## 第三方组件

- [perturb](https://github.com/gunvirranu/perturb) — SGP4/SDP4 传播库（MIT License），已内置在 `perturb-master/`
- [Qt](https://www.qt.io/) — 应用框架（LGPLv3 / GPLv3 / 商业许可）
- [CelesTrak](https://celestrak.org/) — TLE/GP 数据源

## 许可证

本仓库暂未附带独立的开源许可证文件。第三方组件遵循其各自许可证；如需复用本工程代码，请先联系仓库作者。

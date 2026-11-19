# compare.ps1 — XGZ 引擎 vs STK 11 逐点一致性验证
#
# 流程:
#   1) 编译 tools/stk_compare/xgz_dump.cpp（用 XGZ 自己的 core/* + 内置 perturb）
#   2) 从正在运行的 STK 11 导出同窗口的 TEME/Fixed 位置序列（stk_export.ps1）
#   3) 用同一 TLE、同一站点跑 XGZ 引擎
#   4) 逐点比对 ΔTEME / ΔECEF / ΔAER，并与阈值判定
#
# 用法:
#   .\compare.ps1 -Tle "C:\Users\杨洋\Documents\BEIDOU 3M3.txt" -Lat 33.0 -Lon 103.0 -AltKm 0.0 `
#                 -Start "2026-09-19 22:57:00" -Stop "2026-09-19 23:00:00" -Step 1
param(
    [Parameter(Mandatory = $true)][string]$Tle,
    [double]$Lat = 33.0,
    [double]$Lon = 103.0,
    [double]$AltKm = 0.0,
    [string]$Start = "2026-09-19 22:57:00",
    [string]$Stop  = "2026-09-19 23:00:00",
    [double]$Step  = 1.0,
    [string]$StkSatellite = "BEIDOU_3M3_43107",
    [string]$StkFacility  = "Facility2",
    [string]$WorkDir = "$env:TEMP\xgz_stk_compare",
    [string]$Gxx = "D:\QT\Tools\mingw1310_64\bin\g++.exe",
    # STK 侧传播器星历步长: >0 时临时设为该值（导出后还原）。
    # STK 的数据/报告按此星历插值 —— 要逐位对齐请传 1；否则残留 ~0.1 m 插值差
    [double]$PropStep = 0.0
)

$ErrorActionPreference = "Stop"
# 出错时给出脚本行号与代码（PowerShell 默认只报“方法调用失败”这类模糊信息）
trap {
    Write-Output "[ERROR] $($_.Exception.Message)"
    Write-Output "        位置: $($_.InvocationInfo.ScriptName):$($_.InvocationInfo.ScriptLineNumber)"
    Write-Output "        代码: $($_.InvocationInfo.Line.Trim())"
    exit 1
}
Write-Output "[i] TLE=[$Tle] 范围=[$Start → $Stop] 步长=${Step}s 站点=[$Lat, $Lon, $AltKm km] STK=[$StkSatellite / $StkFacility]"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # tools/stk_compare → 仓库根
if (-not (Test-Path (Join-Path $repo "core\TleParser.cpp"))) { $repo = (Get-Location).Path }
if (-not (Test-Path $WorkDir)) { New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null }

# ---------- 1) 编译 xgz_dump ----------
$dump = Join-Path $WorkDir "xgz_dump.exe"
Write-Output "=== [1/4] 编译 XGZ 引擎导出工具 ==="
$src = Join-Path $PSScriptRoot "xgz_dump.cpp"
$sources = @(
    "core\TleParser.cpp", "core\Propagator.cpp", "core\CoordTransform.cpp", "core\Observer.cpp",
    "perturb-master\src\perturb.cpp", "perturb-master\src\sgp4.cpp", "perturb-master\src\tle.cpp"
) | ForEach-Object { Join-Path $repo $_ }
# 先删旧产物: g++ 失败时不会删除已存在的 exe, 否则会拿**上一次**的二进制当本次结果
Remove-Item -Force $dump -ErrorAction SilentlyContinue
& $Gxx -O2 -std=gnu++17 -I"$repo" -I"$repo\core" -I"$repo\perturb-master\include" `
       $src @sources -o $dump
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $dump)) { throw "xgz_dump 编译失败（见上方 g++ 输出）" }
Write-Output "  → $dump"

# ---------- 2) 从 STK 导出基准 ----------
Write-Output "=== [2/4] 从 STK 11 导出基准数据 ==="
# STK 时间格式
$stkStart = ([datetime]::ParseExact($Start, "yyyy-MM-dd HH:mm:ss", [Globalization.CultureInfo]::InvariantCulture)).ToString("dd MMM yyyy HH:mm:ss.fff", [Globalization.CultureInfo]::InvariantCulture)
$stkStop  = ([datetime]::ParseExact($Stop,  "yyyy-MM-dd HH:mm:ss", [Globalization.CultureInfo]::InvariantCulture)).ToString("dd MMM yyyy HH:mm:ss.fff", [Globalization.CultureInfo]::InvariantCulture)
& (Join-Path $PSScriptRoot "stk_export.ps1") -Satellite $StkSatellite -Facility $StkFacility `
    -Start $stkStart -Stop $stkStop -Step $Step -OutDir $WorkDir -PropStep $PropStep

# ---------- 3) 跑 XGZ 引擎 ----------
Write-Output "=== [3/4] 运行 XGZ 引擎 ==="
$xgzCsv = Join-Path $WorkDir "xgz.csv"
& $dump $Tle $Lat $Lon $AltKm $Start $Stop $Step $xgzCsv
if (-not (Test-Path $xgzCsv)) { throw "xgz_dump 未产生输出" }

# ---------- 4) 比对 ----------
Write-Output "=== [4/4] 逐点比对 ==="
# 读回 CSV: 返回 时间 → 数值列数组（列序与写出一致）
function Read-Csv([string]$path) {
    $rows = @{}
    Get-Content $path | Select-Object -Skip 1 | Where-Object { $_ -match '\S' } | ForEach-Object {
        $p = @($_ -split ',')
        if ($p.Count -ge 2) {
            $vals = @()
            for ($i = 1; $i -lt $p.Count; $i++) { $vals += [double]$p[$i] }
            $rows[$p[0]] = $vals
        }
    }
    return $rows
}
$x  = Read-Csv $xgzCsv                                   # [0..2]=TEME [3..5]=ECEF [6]=Az [7]=El [8]=Range
$t  = Read-Csv (Join-Path $WorkDir "stk_teme.csv")       # [0..2]=TEME
$e  = Read-Csv (Join-Path $WorkDir "stk_ecef.csv")       # [0..2]=ECEF(Fixed)

# 站点: 默认用命令行参数；若 STK 导出了站点 ECEF，则反解其大地高以便自动对齐
$gcp = @(0.0, 0.0, 0.0)
$facFile = Join-Path $WorkDir "stk_facility_ecef.csv"
$stkFacAltKm = $null
if (Test-Path $facFile) {
    # 注意: 单行文件用 (Get-Content)[0] 会取到"首字符"（字符串索引），必须用 @() 强制成数组
    $facLine = @(Get-Content $facFile | Where-Object { $_ -match '^\s*-?[\d.]' })[0]
    if ($facLine) {
        $parts = @($facLine.Trim() -split '\s*,\s*')
        if ($parts.Count -ge 3) { $gcp = @([double]$parts[0], [double]$parts[1], [double]$parts[2]) }
    }
}
$flat = 1.0 / 298.257223563; $e2 = 2 * $flat - $flat * $flat; $a = 6378.137
if ($gcp[0] -ne 0.0 -or $gcp[1] -ne 0.0 -or $gcp[2] -ne 0.0) {
    # 由 STK 站点 ECEF 反解大地高（迭代 3 次足够；用于提示"站高是否一致"）
    $lonS = [Math]::Atan2($gcp[1], $gcp[0])
    $p = [Math]::Sqrt($gcp[0] * $gcp[0] + $gcp[1] * $gcp[1])
    $latS = [Math]::Atan2($gcp[2], $p * (1 - $e2))
    for ($i = 0; $i -lt 5; $i++) {
        $Ns = $a / [Math]::Sqrt(1 - $e2 * [Math]::Sin($latS) * [Math]::Sin($latS))
        $altS = $p / [Math]::Cos($latS) - $Ns
        $latS = [Math]::Atan2($gcp[2], $p * (1 - $e2 * $Ns / ($Ns + $altS)))
    }
    $stkFacAltKm = $altS
}
$latR = $Lat * [Math]::PI / 180; $lonR = $Lon * [Math]::PI / 180
$Nrad = $a / [Math]::Sqrt(1 - $e2 * [Math]::Sin($latR) * [Math]::Sin($latR))
$c1 = ($Nrad + $AltKm) * [Math]::Cos($latR) * [Math]::Cos($lonR)
$c2 = ($Nrad + $AltKm) * [Math]::Cos($latR) * [Math]::Sin($lonR)
$c3 = ($Nrad * (1 - $e2) + $AltKm) * [Math]::Sin($latR)
$staEcef = @([double]$c1, [double]$c2, [double]$c3)
$staDiff = if ($stkFacAltKm -ne $null) {
    [Math]::Sqrt([Math]::Pow($staEcef[0] - $gcp[0], 2) + [Math]::Pow($staEcef[1] - $gcp[1], 2) + [Math]::Pow($staEcef[2] - $gcp[2], 2))
} else { 0.0 }
if ($stkFacAltKm -ne $null) {
    Write-Output ("  [站点] STK 站点大地高 = {0:F4} km（XGZ 传入 {1:F4} km）" -f $stkFacAltKm, $AltKm)
}

$maxTeme = 0.0; $maxEcef = 0.0; $tTeme = ""; $tEcef = ""
$maxRange = 0.0; $maxEl = 0.0; $maxAz = 0.0
$count = 0; $matchedTeme = 0; $matchedEcef = 0
$sLat = [Math]::Sin($latR); $cLat = [Math]::Cos($latR)
$sLon = [Math]::Sin($lonR); $cLon = [Math]::Cos($lonR)
foreach ($k in $x.Keys) {
    if ($t.ContainsKey($k)) {
        ++$matchedTeme
        $d = [Math]::Sqrt([Math]::Pow($x[$k][0] - $t[$k][0], 2) + [Math]::Pow($x[$k][1] - $t[$k][1], 2) + [Math]::Pow($x[$k][2] - $t[$k][2], 2))
        if ($d -gt $maxTeme) { $maxTeme = $d; $tTeme = $k }
    }
    if ($e.ContainsKey($k)) {
        ++$matchedEcef
        # XGZ 的 ECEF 在第 4~6 列（索引 3..5）；STK 的 ECEF 在第 1~3 列
        $d = [Math]::Sqrt([Math]::Pow($x[$k][3] - $e[$k][0], 2) + [Math]::Pow($x[$k][4] - $e[$k][1], 2) + [Math]::Pow($x[$k][5] - $e[$k][2], 2))
        if ($d -gt $maxEcef) { $maxEcef = $d; $tEcef = $k }
        # 用 STK 的 ECEF + 站点坐标做 ENU → AER，与 XGZ 输出的 AER 列（索引 6/7/8）比对
        if ($x[$k][7] -ge 5.0) {
            $dx = $e[$k][0] - $staEcef[0]; $dy = $e[$k][1] - $staEcef[1]; $dz = $e[$k][2] - $staEcef[2]
            $rg = [Math]::Sqrt($dx * $dx + $dy * $dy + $dz * $dz)
            $ee = -$sLon * $dx + $cLon * $dy
            $nn = -$sLat * $cLon * $dx - $sLat * $sLon * $dy + $cLat * $dz
            $uu =  $cLat * $cLon * $dx + $cLat * $sLon * $dy + $sLat * $dz
            $elD = [Math]::Asin($uu / $rg) * 180.0 / [Math]::PI
            $azD = [Math]::Atan2($ee, $nn) * 180.0 / [Math]::PI
            if ($azD -lt 0) { $azD += 360.0 }
            $maxRange = [Math]::Max($maxRange, [Math]::Abs($rg - $x[$k][8]))
            $maxEl    = [Math]::Max($maxEl,    [Math]::Abs($elD - $x[$k][7]))
            $dAz = [Math]::Abs($azD - $x[$k][6]); if ($dAz -gt 180) { $dAz = 360 - $dAz }
            $maxAz = [Math]::Max($maxAz, $dAz)
        }
    }
    $count++
}

Write-Output ""
Write-Output "================ 一致性判定 ================"
Write-Output ("  XGZ 点数 / STK 点数 : {0} / {1}" -f $x.Count, $t.Count)
Write-Output ("  时间戳重合点数      : ΔTEME {0} 点, ΔECEF {1} 点" -f $matchedTeme, $matchedEcef)
Write-Output ("  站点 ECEF 差        : {0:F4} m  (XGZ 参数 vs STK 站点；含站高差异)" -f ($staDiff * 1000.0))
if ($matchedTeme -gt 0) {
    Write-Output ("  最大 ΔTEME(|Δr|)    : {0:F4} m  @ {1}" -f ($maxTeme * 1000.0), $tTeme)
} else {
    Write-Output  "  最大 ΔTEME(|Δr|)    : 无重合时刻，无法比对"
}
if ($matchedEcef -gt 0) {
    Write-Output ("  最大 ΔECEF(|Δr|)    : {0:F4} m  @ {1}" -f ($maxEcef * 1000.0), $tEcef)
    Write-Output ("  最大 ΔRange         : {0:F4} m" -f ($maxRange * 1000.0))
    Write-Output ("  最大 Δ仰角          : {0:F6} °" -f $maxEl)
    Write-Output ("  最大 Δ方位角        : {0:F6} °" -f $maxAz)
} else {
    Write-Output  "  最大 ΔECEF/AER      : 无重合时刻，无法比对"
}
Write-Output ""
# 判定前提: 必须有足够的重合时刻（否则"无数据"会被误判成"一致"）
$minMatch = [Math]::Max(3, [int]($x.Count * 0.5))
if ($matchedTeme -lt $minMatch -or $matchedEcef -lt $minMatch) {
    Write-Output ("  [失败] 时间戳重合点不足（需 ≥{0} 点）：两侧时间/历元/时区不一致，或任一侧数据缺失 —— 无法判定一致性" -f $minMatch)
    Write-Output  "  提示: 确认两侧使用同一份 TLE（历元相同）、同一时间范围（UTC）、同一 -Step"
    exit 2
}
$ok = ($maxTeme * 1000.0 -le 100.0) -and ($maxEcef * 1000.0 -le 100.0) `
      -and ($maxRange * 1000.0 -le 100.0) -and ($maxEl -le 0.001) -and ($maxAz -le 0.001)
if ($staDiff * 1000.0 -gt 100.0) {
    Write-Output "  [警告] 站点不一致（>100 m）：XGZ 的 lat/lon/alt 与 STK 站点不是同一位置，先用 -AltKm 对齐站高"
}
if ($ok) { Write-Output "  结论: 一致（同一 TLE + 同一站点下：|Δ位置|≤100 m、|ΔRange|≤100 m、|Δ角|≤0.001°）" }
else      { Write-Output "  结论: 不一致 —— 请检查两侧 TLE 是否同一份、站点坐标是否相同" }
Write-Output "  口径: ΔTEME/ΔECEF 为两套实现的位置矢量差（外部一致性证据）；"
Write-Output "        ΔRange/ΔEl/ΔAz 用 STK 的位置 + XGZ 的站点几何反算，属几何口径自检（不独立于位置差）"
Write-Output "  提示: 两侧 TLE 历元不同是 km 级差异的首要原因；用 xgz_dump 头部打印的 TLE 历元与 STK TLE 文件核对"

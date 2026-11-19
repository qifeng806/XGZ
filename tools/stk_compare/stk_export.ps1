# stk_export.ps1 — 从本机正在运行的 STK 11 导出对比基准数据
#
# 原理: 通过 COM 附加到已运行的 STK 实例（不新建场景、不改动用户数据），
#       读取指定卫星的 Cartesian Position（TEMEOfDate / Fixed）逐点序列，
#       以及指定 Facility 的 ECEF 坐标，写成 CSV 供 compare.ps1 比对。
#
# 用法:
#   .\stk_export.ps1 -OutDir D:\tmp [-Satellite BEIDOU_3M3_43107] [-Facility Facility2] `
#                    [-Start "19 Sep 2026 22:57:00.000"] [-Stop "19 Sep 2026 23:00:00.000"] [-Step 1]
param(
    [string]$Satellite = "BEIDOU_3M3_43107",
    [string]$Facility  = "Facility2",
    [string]$Start     = "19 Sep 2026 22:57:00.000",
    [string]$Stop      = "19 Sep 2026 23:00:00.000",
    [double]$Step      = 1.0,
    [string]$OutDir    = ".",
    # STK 的报告/数据是按"传播器星历"插值得到的: 星历步长越大, 插值误差越大。
    # PropStep > 0 → 临时把传播器步长设为该值（导出后还原），用于隔离插值影响;
    # 0（默认）= 保持场景当前设置不动。
    [double]$PropStep  = 0.0
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }
$OutDir = (Resolve-Path $OutDir).Path

function Export-Frame {
    param($Sat, [string]$Frame, [string]$Path)
    $g  = $Sat.DataProviders.Item("Cartesian Position")
    $pv = $g.Group.Item($Frame)                  # IAgDataPrvTimeVar
    $res = $pv.Exec($Start, $Stop, $Step)
    $t = $res.DataSets.GetDataSetByName("Time").GetValues()
    $x = $res.DataSets.GetDataSetByName("x").GetValues()
    $y = $res.DataSets.GetDataSetByName("y").GetValues()
    $z = $res.DataSets.GetDataSetByName("z").GetValues()
    $lines = New-Object 'System.Collections.Generic.List[string]'
    [void]$lines.Add("Time,x,y,z")
    for ($i = 0; $i -lt $t.Count; $i++) {
        # STK 时间格式 "19 Sep 2026 22:57:00.000000000"（9 位小数）。
        # .NET ParseExact 的 "f" 最多 7 位且要求位数精确匹配 → 先截到 7 位
        # （保留 100 ns 精度）；失败再退回“去掉小数部分”
        $ts = [string]$t[$i]
        $ts7 = [regex]::Replace($ts, '\.(\d{7})\d+$', '.$1')
        $dt = $null
        try {
            $dt = [datetime]::ParseExact($ts7, "dd MMM yyyy HH:mm:ss.fffffff",
                                         [Globalization.CultureInfo]::InvariantCulture)
        } catch {
            $ts0 = [regex]::Replace($ts, '\.\d+$', '')
            $dt = [datetime]::ParseExact($ts0, "dd MMM yyyy HH:mm:ss",
                                         [Globalization.CultureInfo]::InvariantCulture)
        }
        $iso = $dt.ToString("yyyy-MM-dd HH:mm:ss.fff")
        # List.Add 而非 $lines += : 后者是 O(n²)，数万行的逐秒窗口会急剧变慢
        [void]$lines.Add(("{0},{1:R},{2:R},{3:R}" -f $iso, $x[$i], $y[$i], $z[$i]))
    }
    [System.IO.File]::WriteAllLines($Path, $lines, [System.Text.Encoding]::ASCII)
    Write-Output ("  [STK/{0}] {1} 行 → {2}" -f $Frame, ($t.Count), $Path)
}

$app  = [Runtime.InteropServices.Marshal]::GetActiveObject("STK11.Application")
$root = $app.Personality2
$sc   = $root.CurrentScenario
$sat  = $sc.Children.Item($Satellite)
$fac  = $sc.Children.Item($Facility)
if ($null -eq $sat) { throw "场景中找不到卫星对象: $Satellite" }
if ($null -eq $fac) { throw "场景中找不到站点对象: $Facility" }

Write-Output "=== 导出 STK 基准数据 ==="
Write-Output ("  场景: {0}   时间: {1} → {2}" -f $sc.InstanceName, $sc.StartTime, $sc.StopTime)
$origStep = $sat.Propagator.Step
Write-Output ("  卫星: {0}   传播器星历步长: {1} s（STK 的数据/报告按此星历插值）" -f $Satellite, $origStep)

# PropStep > 0: 临时改小星历步长以隔离插值误差。
# 必须 try/finally 还原: 中途抛异常（数据提供器失败/用户取消）时若不还原,
# 会把用户的 STK 场景永久改成小步长（几万点的星历, 拖慢其后续操作）
if ($PropStep -gt 0.0) {
    Write-Output ("  → 临时把传播器步长设为 {0} s（导出后还原为 {1} s）" -f $PropStep, $origStep)
    $sat.Propagator.Step = $PropStep
}
try {
    # 用当前 TLE 重新生成星历（只重算，不改 TLE/坐标系等设置）
    $sat.Propagator.Propagate()
    Write-Output ("  实际使用步长: {0} s" -f $sat.Propagator.Step)

    Export-Frame -Sat $sat -Frame "TEMEOfDate" -Path (Join-Path $OutDir "stk_teme.csv")
    Export-Frame -Sat $sat -Frame "Fixed"      -Path (Join-Path $OutDir "stk_ecef.csv")
} finally {
    if ($PropStep -gt 0.0) {
        $sat.Propagator.Step = $origStep
        $sat.Propagator.Propagate()
        Write-Output ("  已还原传播器步长为 {0} s" -f $sat.Propagator.Step)
    }
}

# 站点 ECEF（fixed provider，无 Group）
$met = "unknown"
try {
    $p = $fac.DataProviders.Item("Cartesian Position")
    $r = $p.Exec()
    $x = $r.DataSets.GetDataSetByName("x").GetValues()[0]
    $y = $r.DataSets.GetDataSetByName("y").GetValues()[0]
    $z = $r.DataSets.GetDataSetByName("z").GetValues()[0]
    ("{0:R},{1:R},{2:R}" -f $x, $y, $z) | Set-Content -Encoding ASCII (Join-Path $OutDir "stk_facility_ecef.csv")
    Write-Output ("  [STK/站点] ECEF = {0:F6}, {1:F6}, {2:F6} km" -f $x, $y, $z)
    $met = "cartesian"
} catch {
    Write-Output "  [STK/站点] Cartesian Position 不可用: $($_.Exception.Message)"
}
if ($met -eq "unknown") {
    try {
        $p = $fac.DataProviders.Item("LLA Position")
        $r = $p.Exec()
        $la = $r.DataSets.GetDataSetByName("Latitude").GetValues()[0]
        $lo = $r.DataSets.GetDataSetByName("Longitude").GetValues()[0]
        $al = $r.DataSets.GetDataSetByName("Altitude").GetValues()[0]
        Write-Output ("  [STK/站点] LLA = {0} {1} {2}" -f $la, $lo, $al)
    } catch {
        Write-Output "  [STK/站点] LLA Position 也不可用，请自行核对站点坐标"
    }
}
Write-Output "  完成 → $OutDir"

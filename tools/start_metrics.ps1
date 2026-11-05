# 启动主机指标采集器（pc_metrics.host）。
#
# 退出码约定：
#   0  正常退出，或用户用 Ctrl+C 正常停止
#   1  环境不满足：找不到可用的 Python、Python 版本过低、缺少 psutil 依赖
#   2  主机采集器启动失败（网卡名/地址/端口/采样间隔配置问题），原因打印在 stderr
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $ProjectRoot

# Python 被 Ctrl+C 中断时的退出码 0xC000013A：
# PowerShell 可能以无符号 3221225786 或有符号 -1073741510 暴露它。
$InterruptExitCodeUnsigned = 3221225786
$InterruptExitCodeSigned = -1073741510

$Python = Get-Command python.exe -ErrorAction SilentlyContinue
if (-not $Python) {
    Write-Host "未找到 Python。请安装 Python 3.10 或更新版本，并确保 python.exe 在 PATH 中。" -ForegroundColor Red
    exit 1
}

# Microsoft Store 的 python.exe 只是占位程序（位于 ...\WindowsApps\ 下），
# 运行后退出码为 9009；必须先识别它，否则会被误报成“Python 版本过低”。
if ($Python.Source -like "*\WindowsApps\*") {
    Write-Host "检测到 Microsoft Store 的 python.exe 占位程序：$($Python.Source)" -ForegroundColor Red
    Write-Host "它不是可用的 Python 解释器（退出码 9009）。请安装真实 Python 3.10+ 并确保 PATH 指向它，"
    Write-Host "或在项目目录改用 py 启动器：py -3 -m pc_metrics.host"
    exit 1
}

& $Python.Source -c "import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)"
if ($LASTEXITCODE -eq 9009) {
    Write-Host "python.exe（$($Python.Source)）返回 9009，可能是 Microsoft Store 占位程序。" -ForegroundColor Red
    Write-Host "请安装真实 Python 3.10+，或在项目目录改用 py 启动器：py -3 -m pc_metrics.host"
    exit 1
}
if ($LASTEXITCODE -ne 0) {
    Write-Host "需要 Python 3.10 或更新版本。请更新 Python 后重试。" -ForegroundColor Red
    exit 1
}

# psutil 版本必须满足 requirements-host.txt 的 >=5.9,<8 区间。
# 用解释器内元组比较，不新增依赖，也不解析 pip 输出。
& $Python.Source -c "import psutil" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "缺少主机采集依赖 psutil（要求 >=5.9,<8）。请在项目目录执行：" -ForegroundColor Yellow
    Write-Host "  python -m pip install -r requirements-host.txt"
    exit 1
}

$PsutilVersion = (& $Python.Source -c "import psutil; print(psutil.__version__)" 2>$null) -join ' '
if ([string]::IsNullOrWhiteSpace($PsutilVersion)) { $PsutilVersion = "(未知)" }

& $Python.Source -c "import psutil; v = tuple(int(p) for p in psutil.__version__.split('.')[:2]); raise SystemExit(0 if (5, 9) <= v < (8, 0) else 1)" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "psutil 版本 $PsutilVersion 不在要求区间 >=5.9,<8 内。请在项目目录执行：" -ForegroundColor Yellow
    Write-Host "  python -m pip install -r requirements-host.txt"
    exit 1
}

& $Python.Source -m pc_metrics.host
$HostExitCode = $LASTEXITCODE

# 正常停止（Ctrl+C）不应显示为失败：该中断码等同于退出码 0。
if ($HostExitCode -eq $InterruptExitCodeUnsigned -or $HostExitCode -eq $InterruptExitCodeSigned) {
    exit 0
}
exit $HostExitCode

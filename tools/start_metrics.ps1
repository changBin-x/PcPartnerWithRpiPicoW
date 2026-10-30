$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $ProjectRoot

$Python = Get-Command python.exe -ErrorAction SilentlyContinue
if (-not $Python) {
    Write-Host "未找到 Python。请安装 Python 3.10 或更新版本，并确保 python.exe 在 PATH 中。" -ForegroundColor Red
    exit 1
}

& $Python.Source -c "import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)"
if ($LASTEXITCODE -ne 0) {
    Write-Host "需要 Python 3.10 或更新版本。请更新 Python 后重试。" -ForegroundColor Red
    exit 1
}

& $Python.Source -c "import psutil"
if ($LASTEXITCODE -ne 0) {
    Write-Host "缺少主机采集依赖。请在项目目录执行：" -ForegroundColor Yellow
    Write-Host "  python -m pip install -r requirements-host.txt"
    exit 1
}

& $Python.Source -m pc_metrics.host
exit $LASTEXITCODE

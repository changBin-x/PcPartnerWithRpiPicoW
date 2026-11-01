[CmdletBinding()]
param(
    [string]$Port = "COM4"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$picoRoot = Join-Path $projectRoot "pico"
$secretsPath = Join-Path $picoRoot "wifi_secrets.py"
$weatherSecretsPath = Join-Path $picoRoot "weather_secrets.py"

if (-not (Test-Path -LiteralPath $secretsPath -PathType Leaf)) {
    Write-Error ("缺少本地 Wi-Fi 配置文件：pico/wifi_secrets.py`n" +
        "请复制 pico/wifi_secrets.example.py 为 pico/wifi_secrets.py，填写 SSID 和密码后重新运行。`n" +
        "该配置文件已加入 .gitignore；请勿提交或粘贴密码。")
    exit 2
}

if (-not (Test-Path -LiteralPath $weatherSecretsPath -PathType Leaf)) {
    Write-Error ("缺少本地天气配置文件：pico/weather_secrets.py`n" +
        "请复制 pico/weather_secrets.example.py 为 pico/weather_secrets.py，填写高德 Web 服务 Key 后重新运行。`n" +
        "该配置文件已加入 .gitignore；部署脚本不会显示其内容。")
    exit 2
}

$packageFiles = @(
    "__init__.py",
    "app.py",
    "amap_root_ca.pem",
    "display.py",
    "font_cn.py",
    "hko_root_ca.pem",
    "lunar.py",
    "lunar_table.bin",
    "st7735.py",
    "time_sync.py",
    "udp_receiver.py",
    "wifi.py",
    "wifi_secrets.py",
    "weather.py",
    "weather_secrets.py"
)

$localFiles = @($packageFiles | ForEach-Object { Join-Path $picoRoot $_ })
$localFiles += (Join-Path $picoRoot "main.py")
$missingFiles = @($localFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
if ($missingFiles.Count -gt 0) {
    $relativeFiles = @($missingFiles | ForEach-Object { $_.Substring($projectRoot.Length + 1) })
    throw "部署前检查失败，以下项目文件不存在：$($relativeFiles -join ', ')"
}

$localMpremote = Join-Path $projectRoot ".venv\Scripts\mpremote.exe"
if (Test-Path -LiteralPath $localMpremote -PathType Leaf) {
    $mpremote = $localMpremote
} else {
    $mpremoteCommand = Get-Command "mpremote" -ErrorAction SilentlyContinue
    if ($null -eq $mpremoteCommand) {
        throw "找不到 mpremote。请安装到 .venv：.venv\Scripts\python.exe -m pip install mpremote"
    }
    $mpremote = $mpremoteCommand.Source
}

# This read-only probe happens before the first filesystem write.
$probe = & $mpremote connect $Port exec "import sys; print(sys.implementation.name); print(sys.version)" 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "无法通过 $Port 连接 Pico。请关闭 Thonny 的串口会话后重试。"
}
$probeText = ($probe | Out-String)
if ($probeText -notmatch "micropython") {
    throw "COM4 设备未报告 MicroPython；部署已停止，没有写入文件。"
}
Write-Host "已连接 $Port 上的 MicroPython 设备。"

$commands = @(
    "connect",
    $Port,
    "exec",
    "import os; names=os.listdir('/'); os.mkdir('/pico') if 'pico' not in names else None; print('PICO_PACKAGE_READY')",
    "+"
)
foreach ($fileName in $packageFiles) {
    $localPath = Join-Path $picoRoot $fileName
    $remotePath = ":/pico/$fileName"
    $commands += @("fs", "cp", $localPath, $remotePath, "+")
}
$commands += @("fs", "cp", (Join-Path $picoRoot "main.py"), ":/main.py", "+")
$commands += "reset"

Write-Host "正在复制 Pico 应用文件并重启设备。配置内容不会显示。"
& $mpremote @commands
if ($LASTEXITCODE -ne 0) {
    throw "部署命令失败。未删除设备上的其他文件；修复问题后可重新运行脚本。"
}
Write-Host "部署命令已结束。设备应从根目录 main.py 自动启动。"

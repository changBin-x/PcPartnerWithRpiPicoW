# 将 pico/ 下的固件文件通过 mpremote 部署到 Pico W。
#
# 退出码约定（调用方与 CI 依赖这些值）：
#   0  部署成功：全部文件已上传，设备已重启
#   1  一般失败：项目文件缺失、找不到 mpremote、串口不存在、连接失败、上传失败或超时
#   2  缺少本地配置：pico/wifi_secrets.py 或 pico/weather_secrets.py 不存在
#
# 兼容 Windows PowerShell 5.1 与 PowerShell 7：不要使用仅 7.x 才有的语法
# （??、&&、三元运算符），也不要把原生程序的 stderr 合并进成功输出流
# （5.1 在 $ErrorActionPreference = "Stop" 下会抛 NativeCommandError）。
[CmdletBinding()]
param(
    [string]$Port = "COM4"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# 把单个命令行参数按 Windows 进程边界（CommandLineToArgvW）的规则加引号。
# 参数在脚本里始终以数组形式保存，只有交给 Windows 进程时才拼成命令行；
# 每个元素单独加引号，路径含空格也不会被拆开，也不会经过任何 shell。
function ConvertTo-ProcessArgument {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Value
    )

    if ($Value.Length -eq 0) {
        return '""'
    }
    if ($Value -notmatch '[ \t\n\v"]') {
        return $Value
    }

    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]'\') {
            $backslashes += 1
            continue
        }
        if ($character -eq [char]'"') {
            if ($backslashes -gt 0) {
                [void]$builder.Append(('\' * (2 * $backslashes)))
            }
            [void]$builder.Append('\"')
            $backslashes = 0
            continue
        }
        if ($backslashes -gt 0) {
            [void]$builder.Append(('\' * $backslashes))
            $backslashes = 0
        }
        [void]$builder.Append($character)
    }
    if ($backslashes -gt 0) {
        [void]$builder.Append(('\' * (2 * $backslashes)))
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

# 运行一次 mpremote，带超时并分别捕获 stdout / stderr。
# 返回对象的 Output 与 ExitCode 供调用方判断；超时后结束进程并标记 TimedOut。
# 不把原生程序的 stderr 合并进输出流：Windows PowerShell 5.1 在
# $ErrorActionPreference = "Stop" 下会把它变成 NativeCommandError 终止错误。
function Invoke-Mpremote {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$MpremotePath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,
        [Parameter(Mandatory = $true)]
        [int]$Timeout
    )

    $argumentLine = @($Arguments | ForEach-Object { ConvertTo-ProcessArgument -Value $_ }) -join ' '

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $MpremotePath
    $startInfo.Arguments = $argumentLine
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo

    $timedOut = $false
    $exitCode = -1
    $standardOutput = ""
    $standardError = ""

    try {
        [void]$process.Start()
        # 异步读取，避免等待期间管道缓冲区写满导致双方互锁。
        $outputTask = $process.StandardOutput.ReadToEndAsync()
        $errorTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($Timeout * 1000)) {
            $timedOut = $true
            try { $process.Kill() } catch { }
            [void]$process.WaitForExit(5000)
        }
        if ($outputTask.Wait(5000)) { $standardOutput = $outputTask.Result }
        if ($errorTask.Wait(5000)) { $standardError = $errorTask.Result }
        if (-not $timedOut) { $exitCode = $process.ExitCode }
    } catch {
        $standardError = $_.Exception.Message
        try { $process.Kill() } catch { }
    } finally {
        $process.Dispose()
    }

    return [pscustomobject]@{
        ExitCode    = $exitCode
        Output      = $standardOutput
        ErrorOutput = $standardError
        TimedOut    = $timedOut
    }
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$picoRoot = Join-Path $projectRoot "pico"
$secretsPath = Join-Path $picoRoot "wifi_secrets.py"
$weatherSecretsPath = Join-Path $picoRoot "weather_secrets.py"

# 缺少本地配置属于可预期的用户错误：用红字提示并以退出码 2 结束。
# 不要改用错误流命令提示：在 $ErrorActionPreference = "Stop" 下它是终止错误，
# 后面的退出码 2 永远不会生效，脚本实际会以退出码 1 结束。
if (-not (Test-Path -LiteralPath $secretsPath -PathType Leaf)) {
    Write-Host "缺少本地 Wi-Fi 配置文件：pico/wifi_secrets.py" -ForegroundColor Red
    Write-Host "请复制 pico/wifi_secrets.example.py 为 pico/wifi_secrets.py，填写 SSID 和密码后重新运行。"
    Write-Host "该配置文件已加入 .gitignore；请勿提交或粘贴密码。"
    exit 2
}

if (-not (Test-Path -LiteralPath $weatherSecretsPath -PathType Leaf)) {
    Write-Host "缺少本地天气配置文件：pico/weather_secrets.py" -ForegroundColor Red
    Write-Host "请复制 pico/weather_secrets.example.py 为 pico/weather_secrets.py，填写高德 Web 服务 Key 后重新运行。"
    Write-Host "该配置文件已加入 .gitignore；部署脚本不会显示其内容。"
    exit 2
}

$packageFiles = @(
    "__init__.py",
    "app.py",
    "amap_root_ca.pem",
    "display.py",
    "font_cn.bin",
    "font_cn.py",
    "glyph_font.py",
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
    Write-Host "部署前检查失败，以下项目文件不存在：" -ForegroundColor Red
    Write-Host ("  " + ($relativeFiles -join ", "))
    exit 1
}

$localMpremote = Join-Path $projectRoot ".venv\Scripts\mpremote.exe"
if (Test-Path -LiteralPath $localMpremote -PathType Leaf) {
    $mpremote = $localMpremote
} else {
    $mpremoteCommand = Get-Command "mpremote" -ErrorAction SilentlyContinue
    if ($null -eq $mpremoteCommand) {
        Write-Host "找不到 mpremote。请安装到 .venv：" -ForegroundColor Red
        Write-Host "  .venv\Scripts\python.exe -m pip install mpremote"
        exit 1
    }
    $mpremote = $mpremoteCommand.Source
}

# 连接前先确认端口真实存在：区分“端口名不存在/拼错”和“mpremote 连不上”。
$portNames = @()
$portEnumerationFailed = $false
try {
    $portNames = @([System.IO.Ports.SerialPort]::GetPortNames())
} catch {
    $portEnumerationFailed = $true
}

if (-not $portEnumerationFailed) {
    if ($portNames.Count -eq 0) {
        Write-Host "未检测到任何串口。请确认 Pico W 已通过 USB 连接，并安装了正确的串口驱动。" -ForegroundColor Red
        Write-Host "设备刚插入时可等待几秒钟后重试；也可以在命令行运行 mode 查看系统串口列表。"
        exit 1
    }
    if ($portNames -notcontains $Port) {
        Write-Host "串口 $Port 不存在或名称拼写有误。当前系统可用串口：$($portNames -join ', ')" -ForegroundColor Red
        Write-Host "请用 -Port 指定实际端口，例如：powershell -ExecutionPolicy Bypass -File tools/deploy_pico.ps1 -Port COM5"
        exit 1
    }
}

# This read-only probe happens before the first filesystem write.
$probeResult = Invoke-Mpremote -MpremotePath $mpremote -Arguments @(
    "connect", $Port, "exec", "import sys; print(sys.implementation.name); print(sys.version)"
) -Timeout 30

if ($probeResult.TimedOut) {
    Write-Host "连接 $Port 超时（30 秒内设备没有响应）。" -ForegroundColor Red
    Write-Host "请关闭 Thonny 或其他占用串口的程序，拔掉 Pico 后重新插入；必要时按住 BOOTSEL 再插拔后重试。"
    exit 1
}
if ($probeResult.ExitCode -ne 0) {
    Write-Host "无法通过 $Port 连接 Pico（mpremote 退出码 $($probeResult.ExitCode)）。" -ForegroundColor Red
    Write-Host "常见原因是 Thonny 的串口会话仍占用 $Port，或设备被其他程序占用；请关闭它们后重试。"
    if (-not [string]::IsNullOrWhiteSpace($probeResult.ErrorOutput)) {
        Write-Host ("mpremote 输出：" + $probeResult.ErrorOutput.Trim())
    }
    exit 1
}
if ($probeResult.Output -notmatch "micropython") {
    Write-Host "$Port 上的设备未报告 MicroPython；部署已停止，没有写入任何文件。" -ForegroundColor Red
    exit 1
}
Write-Host "已连接 $Port 上的 MicroPython 设备。"

# 先建目录并校验哨兵：这一步失败就不继续上传，避免把文件写进错误位置。
$prepareResult = Invoke-Mpremote -MpremotePath $mpremote -Arguments @(
    "connect", $Port, "exec", "import os; names=os.listdir('/'); os.mkdir('/pico') if 'pico' not in names else None; print('PICO_PACKAGE_READY')"
) -Timeout 30

$prepareFailed = $prepareResult.TimedOut -or ($prepareResult.ExitCode -ne 0) -or ($prepareResult.Output -notmatch "PICO_PACKAGE_READY")
if ($prepareFailed) {
    Write-Host "无法在设备上准备 /pico 目录（未收到 PICO_PACKAGE_READY 哨兵）。" -ForegroundColor Red
    if ($prepareResult.TimedOut) {
        Write-Host "准备目录超时（30 秒）。请关闭 Thonny、拔插 Pico（必要时按住 BOOTSEL 后再插拔）后重试。"
    } else {
        Write-Host "设备文件系统可能已满或处于异常状态；请关闭 Thonny 后重试，必要时重新烧录 MicroPython 固件。"
        if (-not [string]::IsNullOrWhiteSpace($prepareResult.ErrorOutput)) {
            Write-Host ("mpremote 输出：" + $prepareResult.ErrorOutput.Trim())
        }
    }
    exit 1
}

$commands = @(
    "connect",
    $Port
)
foreach ($fileName in $packageFiles) {
    $localPath = Join-Path $picoRoot $fileName
    $remotePath = ":/pico/$fileName"
    $commands += @("fs", "cp", $localPath, $remotePath, "+")
}
$commands += @("fs", "cp", (Join-Path $picoRoot "main.py"), ":/main.py", "+")
$commands += "reset"

Write-Host "正在复制 Pico 应用文件并重启设备。配置内容不会显示。"
$uploadResult = Invoke-Mpremote -MpremotePath $mpremote -Arguments $commands -Timeout 180

if ($uploadResult.TimedOut) {
    Write-Host "上传超时（180 秒）。设备可能已写入部分文件，重新运行本脚本是安全的（fs cp 会覆盖同名文件）。" -ForegroundColor Red
    Write-Host "请关闭 Thonny、拔插 Pico（必要时按住 BOOTSEL 后再插拔）后重试。"
    exit 1
}
if ($uploadResult.ExitCode -ne 0) {
    Write-Host "部署命令失败（mpremote 退出码 $($uploadResult.ExitCode)）。未删除设备上的其他文件；修复问题后可重新运行脚本。" -ForegroundColor Red
    if (-not [string]::IsNullOrWhiteSpace($uploadResult.ErrorOutput)) {
        Write-Host ("mpremote 输出：" + $uploadResult.ErrorOutput.Trim())
    }
    exit 1
}
Write-Host "部署命令已结束。设备应从根目录 main.py 自动启动。"
exit 0

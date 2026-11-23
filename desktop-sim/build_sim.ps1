# 用 zig cc 把 sim_bridge.c 连同固件真实源码编译成 DLL。
# 产物：desktop-sim/build/sim_bridge.dll
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot            # 仓库根
$fw   = Join-Path $root "pico_freertos_firmware"
$zig  = Join-Path (Split-Path -Parent (Get-Command python).Source) "..\Lib\site-packages\ziglang\zig.exe"
if (-not (Test-Path $zig)) { $zig = "D:\TeleAgent\data\runtimes\python\Lib\site-packages\ziglang\zig.exe" }

$outDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

$sources = @(
  (Join-Path $PSScriptRoot "sim_bridge.c"),
  "$fw\src\display\ui_render.c",
  "$fw\src\display\ui_theme.c",
  "$fw\src\display\ui_font.c",
  "$fw\src\display\ui_gfx.c",
  "$fw\src\display\ui_layout.c",
  "$fw\src\logic\render_window.c",
  "$fw\src\logic\theme_switch.c",
  "$fw\src\state\system_state.c",
  "$fw\src\network\metrics_parser.c",
  "$fw\src\services\lunar_service.c",
  "$fw\src\services\ntp_service.c",
  "$fw\src\services\weather_service.c",
  "$fw\src\assets\asset_store.c",
  "$fw\tests\cjson\cJSON.c"
)

& $zig cc -target x86_64-windows-gnu -shared -std=c11 -Wall -Wextra -g -O1 `
  "-I$fw\include" "-I$fw\src" "-I$fw\tests\cjson" `
  $sources -o "$outDir\sim_bridge.dll" -lpthread
if ($LASTEXITCODE -ne 0) { throw "sim_bridge.dll 编译失败，退出码 $LASTEXITCODE" }
Write-Output "OK: $outDir\sim_bridge.dll"

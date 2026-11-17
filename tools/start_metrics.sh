#!/bin/bash
#
# 启动主机指标采集器（pc_metrics.host），面向 Linux / Jetson Orin。
#
# 退出码约定：
#   0  正常退出，或用户用 Ctrl+C 正常停止
#   1  环境不满足：找不到可用的 Python、Python 版本过低、缺少 psutil 依赖
#   2  主机采集器启动失败（网卡名/地址/端口/采样间隔配置问题），原因打印在
#      stderr
#
# 未设置 PICO_ETHERNET_INTERFACE 时，默认使用本机 Orin 有线网卡名
# enP8p1s0；Windows 侧仍由 config.py 默认「以太网」或环境变量覆盖。
#
set -euo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${PROJECT_ROOT}"

export PICO_ETHERNET_INTERFACE="${PICO_ETHERNET_INTERFACE:-enP8p1s0}"

resolve_python() {
  if command -v python3 >/dev/null 2>&1; then
    command -v python3
    return 0
  fi
  if command -v python >/dev/null 2>&1; then
    command -v python
    return 0
  fi
  return 1
}

if ! PYTHON="$(resolve_python)"; then
  echo "未找到 Python。请安装 Python 3.10 或更新版本，并确保 python3 在 PATH 中。" >&2
  exit 1
fi

if ! "${PYTHON}" -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)'; then
  echo "需要 Python 3.10 或更新版本。请更新 Python 后重试。" >&2
  exit 1
fi

if ! "${PYTHON}" -c 'import psutil' >/dev/null 2>&1; then
  echo "缺少主机采集依赖 psutil（要求 >=5.9,<8）。请在项目目录执行：" >&2
  echo "  ${PYTHON} -m pip install -r requirements-host.txt" >&2
  exit 1
fi

PSUTIL_VERSION="$("${PYTHON}" -c 'import psutil; print(psutil.__version__)')"
if ! "${PYTHON}" -c \
  'import psutil; v = tuple(int(p) for p in psutil.__version__.split(".")[:2]); raise SystemExit(0 if (5, 9) <= v < (8, 0) else 1)'; then
  echo "psutil 版本 ${PSUTIL_VERSION} 不在要求区间 >=5.9,<8 内。请在项目目录执行：" >&2
  echo "  ${PYTHON} -m pip install -r requirements-host.txt" >&2
  exit 1
fi

set +e
# -u：非 TTY / 管道场景下立即刷出启动横幅与 GPU 提示，便于联调验收。
"${PYTHON}" -u -m pc_metrics.host
HOST_EXIT_CODE=$?
set -e

# host.py 已将 KeyboardInterrupt 映射为 0；若信号直接终止进程，128+SIGINT=130
# 也视为正常停止。
if [[ "${HOST_EXIT_CODE}" -eq 130 ]]; then
  exit 0
fi
exit "${HOST_EXIT_CODE}"

#!/bin/bash
#
# Pico W 固件构建脚本：交叉编译 pico_status_screen，最后打印体积统计。
#
# 可用环境变量覆盖默认值：
#   PICO_SDK_PATH              pico-sdk 根目录
#   FREERTOS_KERNEL_PATH       FreeRTOS-Kernel 根目录
#   BUILD_DIR                  构建输出目录
#   PICOTOOL_FETCH_FROM_GIT_PATH  picotool 自动拉取/编译的缓存目录

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

pico_sdk_path="${PICO_SDK_PATH:-/home/bean/pico-toolchain/pico-sdk}"
freertos_kernel_path="${FREERTOS_KERNEL_PATH:-/home/bean/pico-toolchain/FreeRTOS-Kernel}"
build_dir="${BUILD_DIR:-${script_dir}/build}"
# SDK 2.3.1 在系统里找不到 picotool 时会从 git 拉源码并自行编译；固定一个
# build/ 之外的缓存目录，删掉构建目录后不必重新下载。
picotool_fetch_path="${PICOTOOL_FETCH_FROM_GIT_PATH:-/home/bean/pico-toolchain/picotool}"

die() {
  echo "错误：$*" >&2
  exit 1
}

[[ -d "${pico_sdk_path}" ]] || die "找不到 pico-sdk：${pico_sdk_path}"
[[ -d "${freertos_kernel_path}" ]] || \
  die "找不到 FreeRTOS-Kernel：${freertos_kernel_path}"
command -v arm-none-eabi-gcc >/dev/null || die "PATH 里没有 arm-none-eabi-gcc"

generator_args=()
if command -v ninja >/dev/null; then
  generator_args=(-G Ninja)
fi

# picotool 负责把 elf 转成 uf2。SDK 找不到它时会静默跳过 uf2 生成，或者
# 偷偷从 git 拉源码现编；这里只在「已安装」或「缓存里已有」时启用官方链路，
# 其余情况显式关掉，交给仓库自带的 tools/elf2uf2.py 兜底（离线可用）。
picotool_args=()
if command -v picotool >/dev/null; then
  picotool_args=(-U picotool_FOUND)
elif [[ -x "${picotool_fetch_path}/picotool/picotool" ]]; then
  picotool_args=(-U picotool_FOUND
                 -DPICOTOOL_FETCH_FROM_GIT_PATH="${picotool_fetch_path}")
else
  picotool_args=(-Dpicotool_FOUND=0)
fi

# 运行时密钥只存在于被 .gitignore 忽略的 pico/*_secrets.py 里，构建前从单一
# 源头生成 include/firmware_secrets.h。缺少凭据时生成空字符串，固件据此跳过
# 对应功能，仍然可以完整编译。
echo "== 同步密钥 =="
python3 "${script_dir}/../tools/sync_secrets.py"

echo "== 配置 =="
cmake -S "${script_dir}" -B "${build_dir}" "${generator_args[@]}" \
  "${picotool_args[@]}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPICO_BOARD=pico_w \
  -DPICO_SDK_PATH="${pico_sdk_path}" \
  -DFREERTOS_KERNEL_PATH="${freertos_kernel_path}"

echo "== 编译 =="
cmake --build "${build_dir}" --parallel "$(nproc)"

elf_path="${build_dir}/pico_status_screen.elf"
uf2_path="${build_dir}/pico_status_screen.uf2"
[[ -f "${elf_path}" ]] || die "没有生成 elf：${elf_path}"

if [[ ! -f "${uf2_path}" ]]; then
  elf2uf2="${script_dir}/../tools/elf2uf2.py"
  [[ -f "${elf2uf2}" ]] || die "没有 uf2，且找不到转换脚本：${elf2uf2}"
  python3 "${elf2uf2}" "${elf_path}" "${uf2_path}"
fi

echo "== 产物 =="
ls -l "${uf2_path}"

echo "== 体积 =="
arm-none-eabi-size "${elf_path}"

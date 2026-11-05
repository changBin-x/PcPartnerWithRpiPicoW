#!/bin/bash
#
# 固件源码风格闸门（Google C 风格）。
#
# 检查内容：
#   1. clang-format --dry-run --Werror：pico_freertos_firmware 下所有 .c/.h
#      必须与仓库根的 .clang-format 完全一致。第三方 cJSON、FreeRTOS 配置
#      契约（include/FreeRTOSConfig.h）与 build*/ 构建产物不参与检查。
#   2. 行宽：每行的东亚显示宽度不得超过 80 列（CJK 全角字符按 2 列计），
#      超限时打印“文件:行号:宽度”。
#
# 用法（可在任意目录执行）：
#   ./check_style.sh
#   CLANG_FORMAT=/path/to/clang-format ./check_style.sh
#
# 退出码：0 全部通过；1 存在违规；2 运行环境缺少依赖。

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd -P)"
REL_FIRMWARE="$(basename -- "${SCRIPT_DIR}")"
CLANG_FORMAT="${CLANG_FORMAT:-/usr/lib/llvm-20/bin/clang-format}"
PYTHON="${PYTHON:-python3}"

readonly MAX_COLUMNS=80
readonly REL_FIRMWARE
readonly CLANG_FORMAT
readonly PYTHON

# 列出待检查的源文件（仓库相对路径，排序后输出）。
collect_sources() {
  find "${REL_FIRMWARE}" \
    -type d \( -name 'build*' -o -path '*/tests/cjson' -o -path '*/tests/generated' \) -prune -o \
    -type f \( -name '*.c' -o -name '*.h' \) \
    ! -path "${REL_FIRMWARE}/include/FreeRTOSConfig.h" \
    ! -path "${REL_FIRMWARE}/include/firmware_secrets.h" \
    -print |
    sort
}

main() {
  local tool
  for tool in "${CLANG_FORMAT}" "${PYTHON}"; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
      echo "错误：找不到可执行的 ${tool}" >&2
      echo "      可用 CLANG_FORMAT=/path/to/clang-format 覆盖默认值。" >&2
      exit 2
    fi
  done

  if ! cd -- "${REPO_ROOT}"; then
    echo "错误：无法切换到仓库根目录：${REPO_ROOT}" >&2
    exit 2
  fi

  local sources_output
  local -a sources=()
  sources_output="$(collect_sources)"
  if [[ -z "${sources_output}" ]]; then
    echo "错误：${REL_FIRMWARE} 下没有找到任何 .c/.h 源文件。" >&2
    exit 2
  fi
  mapfile -t sources <<< "${sources_output}"

  # 检查项 1：clang-format 排版必须与仓库根 .clang-format 一致。
  local -a format_failures=()
  local source
  local clang_output
  for source in "${sources[@]}"; do
    if ! clang_output="$("${CLANG_FORMAT}" --dry-run --Werror --style=file \
      "${source}" 2>&1)"; then
      format_failures+=("${source}")
      printf '%s\n' "${clang_output}"
    fi
  done

  # 检查项 2：每行东亚显示宽度不超过 MAX_COLUMNS。
  local width_output=""
  local width_status=0
  width_output="$(
    "${PYTHON}" - "${MAX_COLUMNS}" "${sources[@]}" <<'PY'
import sys
import unicodedata

max_columns = int(sys.argv[1])
violations = 0

for path in sys.argv[2:]:
    with open(path, encoding="utf-8", newline="") as handle:
        for lineno, raw in enumerate(handle, 1):
            width = 0
            for char in raw.rstrip("\r\n"):
                if char == "\t":
                    width += 8 - (width % 8)
                elif unicodedata.combining(char):
                    continue
                elif unicodedata.east_asian_width(char) in ("W", "F"):
                    width += 2
                else:
                    width += 1
            if width > max_columns:
                violations += 1
                print(f"{path}:{lineno}:{width}")

sys.exit(1 if violations else 0)
PY
  )" || width_status=$?

  local width_count=0
  local line
  if [[ ${width_status} -ne 0 ]]; then
    while IFS= read -r line; do
      if [[ -z "${line}" ]]; then
        continue
      fi
      printf '  %s\n' "${line}"
      width_count=$((width_count + 1))
    done <<< "${width_output}"
  fi

  if [[ ${#format_failures[@]} -gt 0 || ${width_count} -gt 0 ]]; then
    {
      echo ""
      echo "FAIL: 代码风格检查未通过"
      if [[ ${#format_failures[@]} -gt 0 ]]; then
        echo "  - clang-format 排版不一致（${#format_failures[@]} 个文件）："
        for source in "${format_failures[@]}"; do
          echo "      ${source}"
        done
        echo "    修复：${CLANG_FORMAT} -i --style=file <文件>"
      fi
      if [[ ${width_count} -gt 0 ]]; then
        echo "  - 行宽超限（${width_count} 处，上限 ${MAX_COLUMNS} 列）："
        echo "    见上方“文件:行号:宽度”列表"
      fi
    } >&2
    exit 1
  fi

  echo "PASS: 代码风格检查通过（${#sources[@]} 个文件，clang-format 一致，行宽 ≤ ${MAX_COLUMNS} 列）"
}

main "$@"

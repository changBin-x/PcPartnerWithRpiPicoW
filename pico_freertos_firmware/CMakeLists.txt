# Pico W(RP2040)FreeRTOS SMP 固件工程。
#
# 关键顺序约束：
#   1. pico_sdk_import.cmake 必须在 project() 之前 include，它负责选择
#      arm-none-eabi 交叉工具链；
#   2. FreeRTOS_Kernel_import.cmake 必须在 pico_sdk_init() 之前 include，
#      它在 SDK 2.x 上只登记 library.cmake，由 pico_sdk_init() 末尾统一
#      创建 FreeRTOS-Kernel / FreeRTOS-Kernel-HeapN 目标；
#   3. 源文件用 GLOB 收集，后续任务往 src/ 下新增 .c 后无需修改本文件。

cmake_minimum_required(VERSION 3.13)

# ---------------------------------------------------------------------------
# 定位工具链：优先使用命令行/缓存变量，其次环境变量。
# ---------------------------------------------------------------------------
if (NOT PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
  set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif ()
if (NOT PICO_SDK_PATH)
  message(FATAL_ERROR
      "PICO_SDK_PATH 未设置：export PICO_SDK_PATH 或传 -DPICO_SDK_PATH=...")
endif ()

if (NOT FREERTOS_KERNEL_PATH AND DEFINED ENV{FREERTOS_KERNEL_PATH})
  set(FREERTOS_KERNEL_PATH "$ENV{FREERTOS_KERNEL_PATH}")
endif ()
if (NOT FREERTOS_KERNEL_PATH)
  message(FATAL_ERROR
      "FREERTOS_KERNEL_PATH 未设置：export FREERTOS_KERNEL_PATH "
      "或传 -DFREERTOS_KERNEL_PATH=...")
endif ()

if (NOT PICO_BOARD)
  set(PICO_BOARD pico_w)
endif ()

# FreeRTOSConfig.h 位于 include/，显式告知 FreeRTOS 端口，避免依赖调用方
# 的 include 搜索路径。
set(FREERTOS_CONFIG_FILE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/include)

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_status_screen C CXX ASM)

include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

# mbedTLS 走本工程的裁剪配置：SDK 自带的 pico_mbedtls_config.h 会
# #include "mbedtls_config.h"，而 mbedTLS 3.6 已把该文件移入 include/mbedtls/
# 子目录，在本机工具链上直接报「找不到头文件」。显式指定自己的配置头既绕开
# 这个问题，也把 TLS 能力裁到「只要一次 HTTPS GET」所需的最小集合。
#
# 必须在 pico_sdk_init() 调用之前赋值：SDK 在为 pico_mbedtls 生成接口编译
# 选项时就读取该变量，晚于初始化则完全失效。
set(PICO_MBEDTLS_CONFIG_FILE
    ${CMAKE_CURRENT_LIST_DIR}/include/mbedtls_project_config.h)

pico_sdk_init()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# 源码收集
# ---------------------------------------------------------------------------
# 用 GLOB 而不是硬编码清单：任务 6/7/8 会并行往 src/ 下新增 .c 文件，
# 丢进目录即可参与构建；CONFIGURE_DEPENDS 让新增文件触发自动重新配置。
file(GLOB_RECURSE FIRMWARE_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_LIST_DIR}/src/*.c)

list(LENGTH FIRMWARE_SOURCES FIRMWARE_SOURCE_COUNT)
if (FIRMWARE_SOURCE_COUNT EQUAL 0)
  message(FATAL_ERROR "src/ 下未找到任何 .c 源文件")
endif ()

# 自研源码统一启用最严告警；只作用于本工程的文件，不会污染 SDK/lwIP。
foreach(firmware_source IN LISTS FIRMWARE_SOURCES)
  set_source_files_properties(${firmware_source} PROPERTIES
      COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-Wformat")
endforeach()

# cJSON 是第三方代码（Host 单测与固件共用同一份源码），用 -w 屏蔽其告警，
# 避免为了让它通过 -Werror 而改动 vendored 代码。
add_library(cjson_lib STATIC ${CMAKE_CURRENT_LIST_DIR}/tests/cjson/cJSON.c)
target_include_directories(cjson_lib PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/tests/cjson)
target_compile_options(cjson_lib PRIVATE -w)

# ---------------------------------------------------------------------------
# 只读资源（字模、农历表、根证书）
# ---------------------------------------------------------------------------
# 资源不复制进本目录，直接引用仓库根 pico/ 下已有的文件，保证 Python 参考
# 实现与固件读到的是同一份数据。用 objcopy 转成 .rodata 段链接进 Flash：
# 资源全部是 const，不占 RAM，也不需要手写几万行的 C 数组。
#
# 符号名由输入文件名派生，例如 font_cn.bin -> _binary_font_cn_bin_start/_end，
# 所以 objcopy 必须在资源所在目录里以「纯文件名」为参数执行。
set(ASSET_DIR ${CMAKE_CURRENT_LIST_DIR}/../pico)
if (NOT EXISTS ${ASSET_DIR}/font_cn.bin)
  message(FATAL_ERROR "缺少资源目录 ${ASSET_DIR}（font_cn.bin 等）")
endif ()

set(ASSET_OBJECTS "")
find_program(PYTHON3_EXECUTABLE NAMES python3)
if (NOT PYTHON3_EXECUTABLE)
  message(FATAL_ERROR "需要 python3 为资源文件追加结尾 NUL 字节")
endif ()

function(embed_asset asset_name)
  set(asset_file ${ASSET_DIR}/${asset_name})
  if (NOT EXISTS ${asset_file})
    message(FATAL_ERROR "缺少资源文件 ${asset_file}")
  endif ()
  # 资源内容原样嵌入，但额外追加一个 NUL 字节：这样 text[size] 恒为 '\0'，
  # 既可以把 PEM 证书直接当 C 字符串交给 mbedTLS，也能让宿主端与设备端的
  # 缓冲区约定完全一致（size 不含该字节）。
  set(staged_dir ${CMAKE_CURRENT_BINARY_DIR}/assets_src)
  set(staged_file ${staged_dir}/${asset_name})
  set(object_file ${CMAKE_CURRENT_BINARY_DIR}/assets/${asset_name}.o)
  # 暂存目录必须在配置期就存在：构建命令以它为工作目录，而 ninja 会在执行
  # 命令之前先 cd 进去，靠命令里的 make_directory 就太晚了。
  file(MAKE_DIRECTORY ${staged_dir} ${CMAKE_CURRENT_BINARY_DIR}/assets)
  add_custom_command(
      OUTPUT ${object_file}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${staged_dir}
                        ${CMAKE_CURRENT_BINARY_DIR}/assets
      COMMAND ${PYTHON3_EXECUTABLE} ${ASSET_DIR}/../tools/pad_asset_nul.py
              ${asset_file} ${staged_file}
      # 符号名由输入文件名派生，因此必须在暂存目录里以纯文件名调用 objcopy，
      # 否则符号会变成 _binary__home_bean_..._start 这种不可用的名字。
      COMMAND ${CMAKE_OBJCOPY} -I binary -O elf32-littlearm -B arm
              --rename-section .data=.rodata,alloc,load,readonly,data,contents
              ${asset_name} ${object_file}
      WORKING_DIRECTORY ${staged_dir}
      DEPENDS ${asset_file}
              ${ASSET_DIR}/../tools/pad_asset_nul.py
      COMMENT "嵌入资源 ${asset_name}"
      VERBATIM)
  set(ASSET_OBJECTS ${ASSET_OBJECTS} ${object_file} PARENT_SCOPE)
endfunction()

embed_asset(font_cn.bin)
embed_asset(lunar_table.bin)
embed_asset(amap_root_ca.pem)
embed_asset(hko_root_ca.pem)

option(DIAGNOSTIC_TEST_PATTERN "生成诊断测试图案固件（上半彩条下半棋盘）" OFF)

add_executable(pico_status_screen ${FIRMWARE_SOURCES} ${ASSET_OBJECTS})

if (DIAGNOSTIC_TEST_PATTERN)
  target_compile_definitions(pico_status_screen PRIVATE DIAGNOSTIC_TEST_PATTERN=1)
endif ()

target_include_directories(pico_status_screen PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src)

# pico_cyw43_arch_lwip_sys_freertos 是 SDK 2.3.1 中 CYW43439 + lwIP +
# FreeRTOS 的官方组合目标（旧文档里的 pico_cyw43_arch_freertos 已不存在）。
target_link_libraries(pico_status_screen PRIVATE
    cjson_lib
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
    hardware_spi
    hardware_watchdog
    pico_cyw43_arch_lwip_sys_freertos
    pico_mbedtls
    pico_stdlib)

pico_enable_stdio_usb(pico_status_screen 1)
pico_enable_stdio_uart(pico_status_screen 0)

# 产出 .uf2/.hex/.bin/.map/.dis。
pico_add_extra_outputs(pico_status_screen)

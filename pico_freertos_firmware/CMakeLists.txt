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

add_executable(pico_status_screen ${FIRMWARE_SOURCES})

target_include_directories(pico_status_screen PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src)

# pico_cyw43_arch_lwip_sys_freertos 是 SDK 2.3.1 中 CYW43439 + lwIP +
# FreeRTOS 的官方组合目标（旧文档里的 pico_cyw43_arch_freertos 已不存在）。
target_link_libraries(pico_status_screen PRIVATE
    cjson_lib
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
    hardware_watchdog
    pico_cyw43_arch_lwip_sys_freertos
    pico_stdlib)

pico_enable_stdio_usb(pico_status_screen 1)
pico_enable_stdio_uart(pico_status_screen 0)

# 产出 .uf2/.hex/.bin/.map/.dis。
pico_add_extra_outputs(pico_status_screen)

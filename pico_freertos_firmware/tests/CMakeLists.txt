cmake_minimum_required(VERSION 3.16)
project(pico_firmware_host_tests C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -g -O1")
# 测试用 assert 做断言，任何构建类型下都不能被 NDEBUG 裁掉。
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -UNDEBUG")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/cjson
)

# ---------------------------------------------------------------- cJSON
add_library(cjson_lib STATIC ${CMAKE_CURRENT_SOURCE_DIR}/cjson/cJSON.c)
target_include_directories(cjson_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cjson)
# Third-party code: do not fail the build on its warnings.
target_compile_options(cjson_lib PRIVATE -w)

# --------------------------------------------------- task 1: system state
add_executable(test_system_state
    test_system_state.c
    ../src/state/system_state.c
)
target_link_libraries(test_system_state pthread)

# ----------------------------------------------- task 2: metrics parser
add_executable(test_metrics_parser
    test_metrics_parser.c
    ../src/network/metrics_parser.c
)
target_link_libraries(test_metrics_parser cjson_lib)

# ----------------------------------------------- task 3: lunar service
add_executable(test_lunar_service
    test_lunar_service.c
    ../src/services/lunar_service.c
)

# -------------------------------------------------- task 4: ui layout
add_executable(test_ui_layout
    test_ui_layout.c
    ../src/display/ui_layout.c
)

enable_testing()

# ------------------------------- task 1: 主题调色板与对比度门禁
add_executable(test_ui_theme
    test_ui_theme.c
    ../src/display/ui_theme.c
)
target_link_libraries(test_ui_theme m)

add_test(NAME ui_theme COMMAND test_ui_theme)

# ------------------------------- task 2: 2/2 采样消抖（纯逻辑）
add_executable(test_button_filter
    test_button_filter.c
    ../src/logic/button_filter.c
)

add_test(NAME button_filter COMMAND test_button_filter)

# ------------------------------- task 3: 12 字节配置打包解析（纯逻辑）
add_executable(test_theme_config
    test_theme_config.c
    ../src/logic/theme_config.c
)

add_test(NAME theme_config COMMAND test_theme_config)

# ------------------- 任务 4: 绕回、渲染窗口与提交状态机（纯逻辑）
add_executable(test_theme_commit_policy
    test_theme_commit_policy.c
    ../src/logic/theme_commit_policy.c
    ../src/logic/theme_switch.c
    ../src/logic/render_window.c
)

add_test(NAME theme_commit_policy COMMAND test_theme_commit_policy)

add_test(NAME system_state   COMMAND test_system_state)
add_test(NAME metrics_parser COMMAND test_metrics_parser)
add_test(NAME lunar_service  COMMAND test_lunar_service)
add_test(NAME ui_layout      COMMAND test_ui_layout)

# ----------------------------------------------- lead: asset store
add_executable(test_asset_store
    test_asset_store.c
    ../src/assets/asset_store.c
)

# ------------------------------------- lead: 协议一致性（跨语言）
add_executable(test_protocol_conformance
    test_protocol_conformance.c
    ../src/network/metrics_parser.c
)
target_link_libraries(test_protocol_conformance cjson_lib)
# 生成的数据集里中文字面量与超长 JSON 行不参与风格闸门。
target_include_directories(test_protocol_conformance PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

# --------------------------------------------------- 网络：Wi-Fi 退避
add_executable(test_wifi_manager
    test_wifi_manager.c
    ../src/network/wifi_manager.c
)

# --------------------------------------------------- 网络：NTP 时间换算
add_executable(test_ntp_service
    test_ntp_service.c
    ../src/services/ntp_service.c
)

# --------------------------------------------------- 网络：天气响应解析
add_executable(test_weather_service
    test_weather_service.c
    ../src/services/weather_service.c
)
target_link_libraries(test_weather_service cjson_lib)

# ============================================================
# 后续任务的测试目标请追加到本行之前，并在下面补 add_test。
# ============================================================

# ------------------------------------- task 8: supervisor monitor（纯逻辑）
add_executable(test_supervisor_monitor
    test_supervisor_monitor.c
    ../src/app/supervisor_monitor.c
)

add_test(NAME supervisor_monitor COMMAND test_supervisor_monitor)

add_test(NAME asset_store    COMMAND test_asset_store)
add_test(NAME protocol_conformance COMMAND test_protocol_conformance)
# ------------------------------------------ task 6: display font engine
add_executable(test_font_engine
    test_font_engine.c
    ../src/display/font_engine.c
    ../src/assets/asset_store.c
)

# ------------------------------------------- task 6: display ui render
add_executable(test_ui_render
    test_ui_render.c
    ../src/display/ui_render.c
    ../src/display/ui_theme.c
    ../src/display/font_engine.c
    ../src/display/ui_layout.c
    ../src/logic/render_window.c
    ../src/assets/asset_store.c
)

add_test(NAME font_engine    COMMAND test_font_engine)
add_test(NAME ui_render      COMMAND test_ui_render)
add_test(NAME wifi_manager    COMMAND test_wifi_manager)
add_test(NAME ntp_service     COMMAND test_ntp_service)
add_test(NAME weather_service COMMAND test_weather_service)

# ------------------------------- 端到端：真实报文字节 → 状态机 → 渲染成图
add_executable(test_pipeline_e2e
    test_pipeline_e2e.c
    ../src/network/metrics_parser.c
    ../src/state/system_state.c
    ../src/display/ui_render.c
    ../src/display/ui_theme.c
    ../src/display/font_engine.c
    ../src/display/ui_layout.c
    ../src/logic/render_window.c
    ../src/assets/asset_store.c
)

target_include_directories(test_pipeline_e2e PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/cjson
)

# 状态机在设备端用 FreeRTOS 互斥量，宿主端用 pthread，故需链接线程库。
find_package(Threads REQUIRED)
target_link_libraries(test_pipeline_e2e PRIVATE Threads::Threads cjson_lib)

add_test(NAME pipeline_e2e COMMAND test_pipeline_e2e)

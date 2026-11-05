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
add_test(NAME system_state   COMMAND test_system_state)
add_test(NAME metrics_parser COMMAND test_metrics_parser)
add_test(NAME lunar_service  COMMAND test_lunar_service)
add_test(NAME ui_layout      COMMAND test_ui_layout)

# ----------------------------------------------- lead: asset store
add_executable(test_asset_store
    test_asset_store.c
    ../src/assets/asset_store.c
)

# ============================================================
# 后续任务的测试目标请追加到本行之前，并在下面补 add_test。
# ============================================================

add_test(NAME asset_store    COMMAND test_asset_store)

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
    ../src/display/font_engine.c
    ../src/display/ui_layout.c
    ../src/assets/asset_store.c
)

add_test(NAME font_engine    COMMAND test_font_engine)
add_test(NAME ui_render      COMMAND test_ui_render)

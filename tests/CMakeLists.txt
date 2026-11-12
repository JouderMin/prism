# PrismTests：控制台单元测试。
# 源码用 GLOB + CONFIGURE_DEPENDS，后续任务新增测试文件无需改本文件。

file(GLOB PRISM_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

juce_add_console_app(PrismTests
    PRODUCT_NAME "PrismTests")

target_sources(PrismTests PRIVATE
    ${PRISM_TEST_SOURCES}
    ${PRISM_DSP_SOURCES})

target_include_directories(PrismTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src")

target_compile_definitions(PrismTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

# 注意：不要定义 JUCE_UNIT_TESTS，否则会把 JUCE 自带的内部测试也注册进来。

target_compile_options(PrismTests PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion)

target_link_libraries(PrismTests PRIVATE
    juce::juce_dsp
    juce::juce_audio_basics
    juce::juce_recommended_config_flags)

# 产物放到 build/<preset>/PrismTests，便于直接 ./build/debug/PrismTests。
set_target_properties(PrismTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

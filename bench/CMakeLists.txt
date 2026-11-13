# PrismBench：DSP 基准工具，随 PRISM_BUILD_BENCH 开关（默认 OFF）。
# 与插件不同，这里直接把 PrismDsp 源码编进可执行目标，测的是引擎本身。

juce_add_console_app(PrismBench
    PRODUCT_NAME "PrismBench")

target_sources(PrismBench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/PrismBench.cpp
    ${PRISM_DSP_SOURCES})

target_include_directories(PrismBench PRIVATE
    "${CMAKE_SOURCE_DIR}/src")

target_compile_definitions(PrismBench PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

prism_set_warnings(PrismBench)

target_link_libraries(PrismBench PRIVATE
    juce::juce_dsp
    juce::juce_audio_basics
    juce::juce_recommended_config_flags
    PrismFftBackend)

# 产物放到 build/<preset>/PrismBench，便于直接 ./build/release/PrismBench。
set_target_properties(PrismBench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

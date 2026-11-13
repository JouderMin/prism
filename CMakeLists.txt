# PRISM —— 七段光电压缩器
# 目标：macOS arm64，AU / VST3 / Standalone 三格式，共用一份源码。
# 构建：cmake --preset release && cmake --build --preset release

cmake_minimum_required(VERSION 3.28)

project(PRISM VERSION 0.1.0 LANGUAGES C CXX)

# ---- C++20 ---------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- 选项 ----------------------------------------------------------------
# 默认 OFF：沙箱不能写 ~/Library。安装与 auval 由验证阶段显式开 ON。
option(PRISM_COPY_PLUGINS "Build 后把插件拷进 ~/Library/Audio/Plug-Ins" OFF)

# AU 只有 Apple 平台可用；Windows CI 显式关掉，只构建 VST3 与 Standalone。
option(PRISM_BUILD_AU "构建 AU 目标（仅 macOS）" ON)

# FFT 后端：默认 pffft（vendored）。Windows x64 上 juce 后端比 pffft 慢 8.5×，
# 故默认 pffft；juce 保留为可选回退（-DPRISM_FFT_BACKEND=juce）。
set(PRISM_FFT_BACKEND "pffft" CACHE STRING "FFT 后端：pffft 或 juce")
set_property(CACHE PRISM_FFT_BACKEND PROPERTY STRINGS juce pffft)

if(NOT PRISM_FFT_BACKEND STREQUAL "juce" AND NOT PRISM_FFT_BACKEND STREQUAL "pffft")
    message(FATAL_ERROR "PRISM_FFT_BACKEND 必须是 juce 或 pffft，当前为 ${PRISM_FFT_BACKEND}")
endif()

# 插件与测试共用同一套警告；MSVC 不认识 -Wextra/-Wpedantic 等 GCC 选项。
function(prism_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    endif()
endfunction()

# ---- JUCE -----------------------------------------------------------------
# 首选 vcpkg 提供的 JUCEConfig.cmake；兜底用 third_party/JUCE 子模块（见
# third_party/README.md，对应 spec R1）。
find_package(JUCE CONFIG QUIET)

if(NOT JUCE_FOUND)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
        message(STATUS "PRISM: 未找到 JUCE 包，使用 third_party/JUCE 子模块")
        add_subdirectory(third_party/JUCE)
    else()
        message(FATAL_ERROR
            "找不到 JUCE。先 `vcpkg install juce --triplet arm64-osx`，"
            "或把 JUCE 8.0.7 放到 third_party/JUCE（见 third_party/README.md）。")
    endif()
endif()

# ---- FFT 后端 -------------------------------------------------------------
# 消费方统一链接 PrismFftBackend；juce 时它为空接口，pffft 时带上
# vendored 静态库、头文件路径与 PRISM_USE_PFFFT 定义。
add_library(PrismFftBackend INTERFACE)

if(PRISM_FFT_BACKEND STREQUAL "pffft")
    add_library(PrismPffft STATIC
        third_party/pffft/src/pffft.c
        third_party/pffft/src/pffft_common.c)

    target_include_directories(PrismPffft PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pffft/include")

    target_include_directories(PrismPffft PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pffft/src")

    # 静态链接：Windows 上避免 PFFFT_EXPORT 展开成 dllimport。
    target_compile_definitions(PrismPffft PUBLIC
        PFFFT_STATIC_DEFINE
        PRISM_USE_PFFFT=1)

    set_target_properties(PrismPffft PROPERTIES
        POSITION_INDEPENDENT_CODE ON)

    # vendored C 源码保持上游原样，不套 PRISM 的严格警告集。
    target_link_libraries(PrismFftBackend INTERFACE PrismPffft)
endif()

# ---- 静态资源 -------------------------------------------------------------
# 界面字标以二进制数据编进目标；生成符号 BinaryData::logo_svg / logo_svgSize。
juce_add_binary_data(PrismBinaryData SOURCES Assets/logo.svg)

# ---- 共用源码 -------------------------------------------------------------
# DSP 源码用 GLOB + CONFIGURE_DEPENDS，后续任务新增文件无需改本文件。
file(GLOB PRISM_DSP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/*.cpp")

set(PRISM_SOURCES
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    ${PRISM_DSP_SOURCES})

# 三个目标共用的 juce_add_plugin 参数；只有 FORMATS、签名方式与麦克风权限不同。
# $1 = 目标名（共享代码库），$2 = 格式（AU/VST3/Standalone），
# $3 = AU 时传 deep（codesign 需要 --deep），其余传 plain。
#
# 注意：juce_add_plugin 建的 ${target} 只是共享代码库，真正的 bundle 目标叫
# ${target}_${kind}（JUCEUtils.cmake 里 _juce_set_plugin_target_properties）。
# 签名必须挂在 bundle 目标上，否则 $<TARGET_BUNDLE_DIR> 会报“不是 bundle”。
function(prism_add_plugin target kind sign_mode)
    juce_add_plugin(${target}
        ${ARGN}
        COMPANY_NAME                "Stargazer"
        BUNDLE_ID                   "com.stargazer.prism"
        PLUGIN_MANUFACTURER_CODE    Stgz
        PLUGIN_CODE                 Prsm
        PRODUCT_NAME                "PRISM"
        VERSION                     "0.1.0"
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     ${PRISM_COPY_PLUGINS})

    target_sources(${target} PRIVATE ${PRISM_SOURCES})

    target_compile_definitions(${target} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    prism_set_warnings(${target})

    target_link_libraries(${target} PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        PrismBinaryData
        PrismFftBackend)

    # Ad-hoc 签名；AU 需要 --deep（spec 8.4）。Windows 没有 codesign。
    if(APPLE)
        set(artefact_target "${target}_${kind}")

        if(sign_mode STREQUAL "deep")
            add_custom_command(TARGET ${artefact_target} POST_BUILD
                COMMAND codesign --force --deep -s - "$<TARGET_BUNDLE_DIR:${artefact_target}>"
                COMMENT "ad-hoc 签名 ${artefact_target}（AU，--deep）"
                VERBATIM)
        else()
            add_custom_command(TARGET ${artefact_target} POST_BUILD
                COMMAND codesign --force -s - "$<TARGET_BUNDLE_DIR:${artefact_target}>"
                COMMENT "ad-hoc 签名 ${artefact_target}"
                VERBATIM)
        endif()
    endif()
endfunction()

# ---- AU -------------------------------------------------------------------
if(APPLE AND PRISM_BUILD_AU)
    prism_add_plugin(PrismAU AU deep FORMATS AU)
endif()

# ---- VST3 -----------------------------------------------------------------
prism_add_plugin(PrismVST3 VST3 plain FORMATS VST3)

# ---- Standalone -----------------------------------------------------------
prism_add_plugin(PrismStandalone Standalone plain
    FORMATS Standalone
    MICROPHONE_PERMISSION_ENABLED TRUE)

# ---- 单元测试 -------------------------------------------------------------
option(PRISM_BUILD_TESTS "构建 PrismTests 控制台测试" ON)

if(PRISM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---- 基准工具 -------------------------------------------------------------
option(PRISM_BUILD_BENCH "构建 PrismBench 基准工具" OFF)

if(PRISM_BUILD_BENCH)
    add_subdirectory(bench)
endif()

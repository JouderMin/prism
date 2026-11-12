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

    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion)

    target_link_libraries(${target} PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        PrismBinaryData)

    # Ad-hoc 签名；AU 需要 --deep（spec 8.4）。
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
endfunction()

# ---- AU -------------------------------------------------------------------
prism_add_plugin(PrismAU AU deep FORMATS AU)

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

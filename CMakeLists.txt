cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
    endif()
endif()

project(DROPII VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DR2_BUILD_TOOLS "Build the editor snapshot tool" OFF)
option(DR2_LTO "Link-time optimisation" ON)
option(DR2_BUILD_PLUGIN "Build the plug-in (off: engine tests only, no JUCE needed)" ON)
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (empty = ./JUCE, ../JUCE or download JUCE 8.0.10)")

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

# ---- the engine: plain C++20, no JUCE ---------------------------------------------
set(ENGINE_SOURCES Source/engine/Transform.cpp)

enable_testing()
add_executable(DropTwoEngineTests tests/EngineTests.cpp ${ENGINE_SOURCES})
target_include_directories(DropTwoEngineTests PRIVATE Source)
if(NOT MSVC)
    target_compile_options(DropTwoEngineTests PRIVATE -O2)
endif()
add_test(NAME engine COMMAND DropTwoEngineTests)

if(NOT DR2_BUILD_PLUGIN)
    return()
endif()

if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE EXCLUDE_FROM_ALL)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt)
    add_subdirectory(JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(DR2_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DR2_FORMATS AU)
endif()

juce_add_plugin(DropTwo
    PRODUCT_NAME "DROP II"
    COMPANY_NAME "Semi"
    BUNDLE_ID "com.semi.droptwo"
    PLUGIN_MANUFACTURER_CODE Semi
    PLUGIN_CODE Drp2
    FORMATS ${DR2_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Generator
    DESCRIPTION "Drop 2 from drop 1: half-time, kick shift, bass lock, register flip, call + response, one identity knob")

juce_add_binary_data(DropTwoData
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        Resources/fonts/Michroma-Regular.ttf
        Resources/fonts/VT323-Regular.ttf)
set_target_properties(DropTwoData PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(PLUGIN_SOURCES ${ENGINE_SOURCES} Source/PluginProcessor.cpp Source/PluginEditor.cpp)
set(DR2_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

target_sources(DropTwo PRIVATE ${PLUGIN_SOURCES})
target_include_directories(DropTwo PRIVATE Source)
target_compile_definitions(DropTwo PUBLIC ${DR2_DEFS})
target_link_libraries(DropTwo
    PRIVATE DropTwoData juce::juce_audio_utils juce::juce_opengl
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(DR2_LTO)
    target_link_libraries(DropTwo PUBLIC juce::juce_recommended_lto_flags)
endif()

# ---- processor tests (JUCE, the real plug-in on a fake host) ------------------------
juce_add_console_app(DropTwoProcessorTests PRODUCT_NAME "DropTwoProcessorTests")
target_sources(DropTwoProcessorTests PRIVATE tests/ProcessorTests.cpp ${PLUGIN_SOURCES})
target_include_directories(DropTwoProcessorTests PRIVATE Source)
target_compile_definitions(DropTwoProcessorTests PRIVATE ${DR2_DEFS} DR2_TOOL=1 DR2_HEADLESS=1 JucePlugin_Name="DROP II"
    JucePlugin_Manufacturer="Semi" JucePlugin_VersionString="0.1.0")
target_link_libraries(DropTwoProcessorTests PRIVATE DropTwoData juce::juce_audio_utils juce::juce_opengl juce::juce_recommended_config_flags)
add_test(NAME processor COMMAND DropTwoProcessorTests)

if(DR2_BUILD_TOOLS)
    juce_add_console_app(DropTwoSnapshot PRODUCT_NAME "DropTwoSnapshot")
    target_sources(DropTwoSnapshot PRIVATE tools/Snapshot.cpp ${PLUGIN_SOURCES})
    target_include_directories(DropTwoSnapshot PRIVATE Source)
    target_compile_definitions(DropTwoSnapshot PRIVATE ${DR2_DEFS} DR2_TOOL=1 JucePlugin_Name="DROP II"
        JucePlugin_Manufacturer="Semi" JucePlugin_VersionString="0.1.0")
    target_link_libraries(DropTwoSnapshot PRIVATE DropTwoData juce::juce_audio_utils juce::juce_opengl juce::juce_recommended_config_flags)
endif()

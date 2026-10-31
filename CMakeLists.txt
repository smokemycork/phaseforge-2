cmake_minimum_required(VERSION 3.22)

# macOS settings must be set BEFORE project()
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary" FORCE)
endif()

project(PhaseForge VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PHASEFORGE_BUILD_TESTS "Build the standalone DSP unit tests" ON)
option(PHASEFORGE_COPY_AFTER_BUILD "Copy the plugin into your system plugin folder after building" OFF)
set(PHASEFORGE_JUCE_PATH "" CACHE PATH "Use an existing JUCE checkout instead of downloading one")

# --------------------------------------------------------------------------------------
#  DSP unit tests (no JUCE needed)
# --------------------------------------------------------------------------------------
if(PHASEFORGE_BUILD_TESTS)
    enable_testing()
    add_executable(phaseforge_dsp_test tests/dsp_test.cpp)
    target_include_directories(phaseforge_dsp_test PRIVATE Source)
    add_test(NAME dsp COMMAND phaseforge_dsp_test)
endif()

# --------------------------------------------------------------------------------------
#  JUCE
# --------------------------------------------------------------------------------------
if(PHASEFORGE_JUCE_PATH)
    add_subdirectory(${PHASEFORGE_JUCE_PATH} JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.14
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# --------------------------------------------------------------------------------------
#  Plugin
# --------------------------------------------------------------------------------------
if(NOT DEFINED PHASEFORGE_FORMATS)
    if(APPLE)
        set(PHASEFORGE_FORMATS VST3 AU Standalone)
    else()
        set(PHASEFORGE_FORMATS VST3 Standalone)
    endif()
endif()

juce_add_plugin(PhaseForge
    COMPANY_NAME                "PhaseForge"
    BUNDLE_ID                   com.phaseforge.phaseforge
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${PHASEFORGE_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE    Pfgn
    PLUGIN_CODE                 Pfrg
    FORMATS                     ${PHASEFORGE_FORMATS}
    PRODUCT_NAME                "PhaseForge"
    VST3_CATEGORIES             Fx Analyzer
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(PhaseForge PRIVATE
    Source/DspCore.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h)

target_compile_definitions(PhaseForge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PhaseForge
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

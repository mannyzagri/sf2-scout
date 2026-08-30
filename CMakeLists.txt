cmake_minimum_required(VERSION 3.22)

# SF2 Scout -- SoundFont audition/reference tool (VST3 + Standalone).
# Naming contract: CLAUDE.md §"Naming contract". PLUGIN_CODE and PRODUCT_NAME
# freeze at the first Cubase load.
project(Sf2Scout VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# /MP must precede the JUCE targets so the JUCE module TUs inherit it.
if(MSVC)
    add_compile_options(/MP)
endif()

# JUCE 8.0.4 -- local source tree, no network. Canonical configure:
#   cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DFETCHCONTENT_SOURCE_DIR_JUCE=C:/rhino/deps/JUCE
#   cmake --build build --config Release --parallel
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Sf2Scout
    VERSION                  ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Mnsh
    PLUGIN_CODE              Sf2s
    COMPANY_NAME             "Menashe Audio"
    BUNDLE_ID                "com.menasheaudio.sf2scout"
    PRODUCT_NAME             "SF2 Scout"
    FORMATS                  VST3 Standalone
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES          Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD  FALSE)

target_sources(Sf2Scout PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/engine/SoundFontBank.cpp
    Source/engine/ScoutEngine.cpp)

target_include_directories(Sf2Scout PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_compile_definitions(Sf2Scout PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    SF2SCOUT_VERSION_STRING="${PROJECT_VERSION}"
    _USE_MATH_DEFINES=1)

# TSF is C compiled as C++; it is warning-noisy under /W4, and JUCE's
# recommended warning flags would make the single TU that includes it fail
# on nothing we can fix upstream. Keep the flags for our code, relax that TU.
if(MSVC)
    set_source_files_properties(Source/engine/SoundFontBank.cpp PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4245;/wd4456;/wd4457;/wd4701;/wd4702;/wd4706;/wd4996;/wd4189")
endif()

target_link_libraries(Sf2Scout
    PRIVATE juce::juce_audio_utils
            juce::juce_gui_basics
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

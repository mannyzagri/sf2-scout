cmake_minimum_required(VERSION 3.22)

# SF2 Scout -- vintage sample player / editor / converter (VST3 + Standalone).
# Scope canon: docs/SCOUT_v2_SPEC.md. Naming contract: CLAUDE.md §"Naming
# contract". PLUGIN_CODE and PRODUCT_NAME freeze at the first Cubase load.
project(Sf2Scout VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# /MP must precede the JUCE targets so the JUCE module TUs inherit it.
if(MSVC)
    add_compile_options(/MP)
endif()

# ---------------------------------------------------------------- libopenmpt
# Vendored libopenmpt 0.8.9 (BSD-3, third_party/libopenmpt/LICENSE). We compile
# the internal soundlib (OpenMPT::CSoundFile) as a static library and read
# ModSample directly -- the public libopenmpt API exposes no sample PCM or loop
# flags. No zlib/mpg123/vorbis: MO3 and compressed-sample formats that need
# them report an error instead of loading; every plain tracker format works.
set(OPENMPT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopenmpt/libopenmpt-0.8.9)
file(GLOB_RECURSE OPENMPT_SOURCES CONFIGURE_DEPENDS
    ${OPENMPT_ROOT}/common/*.cpp
    ${OPENMPT_ROOT}/soundlib/*.cpp
    ${OPENMPT_ROOT}/sounddsp/*.cpp
    ${OPENMPT_ROOT}/src/openmpt/*.cpp)
add_library(openmpt_soundlib STATIC ${OPENMPT_SOURCES})
target_include_directories(openmpt_soundlib PUBLIC
    ${OPENMPT_ROOT}
    ${OPENMPT_ROOT}/src
    ${OPENMPT_ROOT}/common)
target_compile_definitions(openmpt_soundlib PUBLIC LIBOPENMPT_BUILD)
target_compile_features(openmpt_soundlib PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(openmpt_soundlib PRIVATE /W3 /wd4244 /wd4267 /wd4996 /bigobj)
    target_compile_definitions(openmpt_soundlib PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# ---------------------------------------------------------------- engine
# JUCE-free engine sources, shared by the plugin and the harness.
set(SCOUT_ENGINE_SOURCES
    Source/engine/SoundFontBank.cpp
    Source/engine/ScoutEngine.cpp
    Source/engine/WavSample.cpp
    Source/engine/ModuleSource.cpp)

# Engine harness (validator.json dsp stage). Still JUCE-free: it links only the
# engine and openmpt_soundlib. Build: cmake --build build --config Release --target test_engine
add_executable(test_engine tests/test_engine.cpp ${SCOUT_ENGINE_SOURCES})
target_include_directories(test_engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_link_libraries(test_engine PRIVATE openmpt_soundlib)
target_compile_definitions(test_engine PRIVATE _USE_MATH_DEFINES=1)
if(MSVC)
    target_compile_options(test_engine PRIVATE /W3 /EHsc)
endif()

# ---------------------------------------------------------------- JUCE
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
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE      # Slot W: F/P/O, nudge keys, Ctrl+S live on the editor
    VST3_CATEGORIES          Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD  FALSE)

target_sources(Sf2Scout PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${SCOUT_ENGINE_SOURCES})

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
# ModuleSource.cpp includes the OpenMPT soundlib headers, which are not /W4-clean either.
if(MSVC)
    set_source_files_properties(Source/engine/SoundFontBank.cpp PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4245;/wd4456;/wd4457;/wd4701;/wd4702;/wd4706;/wd4996;/wd4189")
    set_source_files_properties(Source/engine/ModuleSource.cpp PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4245;/wd4267;/wd4456;/wd4457;/wd4996;/wd4100;/wd4127;/wd4324;/wd4996")
endif()

target_link_libraries(Sf2Scout
    PRIVATE juce::juce_audio_utils
            juce::juce_gui_basics
            openmpt_soundlib
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

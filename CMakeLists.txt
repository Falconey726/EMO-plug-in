cmake_minimum_required(VERSION 3.22)

project(EmotionMorph VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

option(EMOTION_MORPH_BUILD_TESTS "Build the DSP unit tests" ON)

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if JUCE_DIR is given, otherwise fetch a pinned tag.
# ---------------------------------------------------------------------------
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# AU only exists on macOS. Standalone is handy for quick listening tests.
if(APPLE)
    set(EMOTION_MORPH_FORMATS AU VST3 Standalone)
else()
    set(EMOTION_MORPH_FORMATS VST3)
endif()

juce_add_plugin(EmotionMorph
    COMPANY_NAME                "Falcon Audio"
    PRODUCT_NAME                "Emotion Morph"
    BUNDLE_ID                   com.falconaudio.emotionmorph
    PLUGIN_MANUFACTURER_CODE    Flcn
    PLUGIN_CODE                 EmMo
    FORMATS                     ${EMOTION_MORPH_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Modulation
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(EmotionMorph PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/dsp/MoodEngine.cpp)

target_include_directories(EmotionMorph PRIVATE Source)

target_compile_definitions(EmotionMorph PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# The Standalone wrapper needs juce_audio_utils (device settings UI).
if(APPLE)
    set(EMOTION_MORPH_AUDIO_MODULE juce::juce_audio_utils)
else()
    set(EMOTION_MORPH_AUDIO_MODULE juce::juce_audio_processors)
endif()

target_link_libraries(EmotionMorph
    PRIVATE
        ${EMOTION_MORPH_AUDIO_MODULE}
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests: exercise the mood math and the DSP engine without a host.
# ---------------------------------------------------------------------------
if(EMOTION_MORPH_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(EmotionMorphTests PRODUCT_NAME "EmotionMorphTests")

    target_sources(EmotionMorphTests PRIVATE
        Tests/EngineTests.cpp
        Source/dsp/MoodEngine.cpp)

    target_include_directories(EmotionMorphTests PRIVATE Source)

    target_compile_definitions(EmotionMorphTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

    target_link_libraries(EmotionMorphTests
        PRIVATE
            juce::juce_audio_basics
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME EmotionMorphTests COMMAND EmotionMorphTests)
endif()

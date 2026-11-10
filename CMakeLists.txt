cmake_minimum_required(VERSION 3.22)

# Universal (Apple Silicon + Intel), da macOS 10.15 in su
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

project(Brina VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(BRINA_INSTALL "Copia i plugin nelle cartelle di sistema dopo la build" ON)
option(BRINA_TESTS "Compila il test del motore audio" OFF)

if(APPLE)
    set(BRINA_FORMATS VST3 AU)
else()
    set(BRINA_FORMATS VST3)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.17-security
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Stesso codice, due plugin: lo strumento (traccia MIDI) e l'effetto (traccia audio).
function(brina_plugin target product code is_synth is_fx categories)
    juce_add_plugin(${target}
        PRODUCT_NAME "${product}"
        COMPANY_NAME "Brina Audio"
        BUNDLE_ID "com.brinaaudio.${target}"
        PLUGIN_MANUFACTURER_CODE Brna
        PLUGIN_CODE ${code}
        FORMATS ${BRINA_FORMATS}
        IS_SYNTH ${is_synth}
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES ${categories}
        COPY_PLUGIN_AFTER_BUILD ${BRINA_INSTALL})

    target_sources(${target} PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

    target_compile_definitions(${target} PUBLIC
        BRINA_FX=${is_fx}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_formats
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags)
endfunction()

brina_plugin(Brina   "Brina"    Brin TRUE  0 "Instrument;Sampler")
brina_plugin(BrinaFX "Brina FX" BrFx FALSE 1 "Fx;Delay;Reverb")

if(BRINA_TESTS)
    juce_add_console_app(BrinaTest PRODUCT_NAME "BrinaTest")
    target_sources(BrinaTest PRIVATE Tests/EngineTest.cpp)
    target_compile_definitions(BrinaTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(BrinaTest PRIVATE juce::juce_audio_basics juce::juce_recommended_config_flags)
endif()

if(BRINA_TESTS)
    foreach(plugin Brina BrinaFX)
        juce_add_console_app(${plugin}PluginTest PRODUCT_NAME "${plugin}PluginTest")
        target_sources(${plugin}PluginTest PRIVATE Tests/PluginTest.cpp)
        target_compile_definitions(${plugin}PluginTest PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(${plugin}PluginTest PRIVATE ${plugin} juce::juce_audio_utils)
    endforeach()
endif()

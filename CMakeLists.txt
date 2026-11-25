cmake_minimum_required(VERSION 3.22)
project(SP404Dance VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

juce_add_binary_data(SP404DanceData
    SOURCES
        Resources/sp404.jpg
        Resources/hand_left_1.png
        Resources/hand_left_2.png
)

juce_add_plugin(SP404Dance
    COMPANY_NAME "SP404Dance"
    BUNDLE_ID "com.sp404dance.vst3"
    PLUGIN_MANUFACTURER_CODE Sp4d
    PLUGIN_CODE Sp44
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3
    PRODUCT_NAME "SP404Dance"
)

juce_generate_juce_header(SP404Dance)

target_sources(SP404Dance
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
)

target_link_libraries(SP404Dance
    PRIVATE
        SP404DanceData
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(SP404Dance
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

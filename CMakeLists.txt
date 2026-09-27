# Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3
#
# Compilazione:
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release [-DPT_JUCE_DIR=/percorso/JUCE] [-DPT_ASIO_SDK_DIR=...]
#   cmake --build build
#
# Se PT_JUCE_DIR non e' indicato, JUCE 7.0.12 viene scaricato automaticamente
# da un commit SHA fisso.
# Su Windows, se PT_ASIO_SDK_DIR punta allo Steinberg ASIO SDK (GPLv3), lo
# Standalone viene compilato con il supporto ai driver ASIO.

cmake_minimum_required(VERSION 3.22)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # nessun redistributable VC++ richiesto
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(PedalTrinity VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(PT_VERSION_STRING "1.0.0-beta")
set(PT_JUCE_DIR "" CACHE PATH "Cartella dei sorgenti di JUCE 7 (vuoto = download automatico)")
set(PT_ASIO_SDK_DIR "" CACHE PATH "Cartella dello Steinberg ASIO SDK (solo Windows)")
set(PT_JUCE_GIT_SHA "4f43011b96eb0636104cb3e433894cda98243626")

# ------------------------------------------------------------------ JUCE 7
if(PT_JUCE_DIR)
    add_subdirectory(${PT_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${PT_JUCE_GIT_SHA}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------ plugin
juce_add_plugin(PedalTrinity
    PRODUCT_NAME "Pedal Trinity"
    COMPANY_NAME "FabioNET"
    COMPANY_WEBSITE "https://github.com/fabionet/Pedal-Trinity"
    COMPANY_COPYRIGHT "Copyright (C) 2026 FabioNET - GNU GPL v3"
    BUNDLE_ID com.fabionet.pedaltrinity
    DESCRIPTION "Overdrive, distortion and 7-band graphic EQ"
    VERSION ${PROJECT_VERSION}
    ICON_BIG Resources/icon.png
    PLUGIN_MANUFACTURER_CODE Fbnt
    PLUGIN_CODE PdTr
    FORMATS VST3 LV2 Standalone
    LV2URI "https://github.com/fabionet/Pedal-Trinity"
    VST3_CATEGORIES Fx Distortion EQ
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(PedalTrinity PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Presets.cpp
    Source/StandaloneApp.cpp
    Source/engine/Catalog.cpp
    Source/engine/Chain.cpp
    Source/engine/Circuit.cpp
    Source/engine/Effects.cpp
    Source/engine/FxAmp.cpp
    Source/engine/FxDynamics.cpp
    Source/engine/FxFilters.cpp
    Source/engine/FxModulation.cpp
    Source/engine/FxPitch.cpp
    Source/engine/FxTime.cpp
    Source/engine/FxUtility.cpp
    Source/gui/Assets.cpp
    Source/gui/Cables.cpp
    Source/gui/Controls.cpp
    Source/gui/InfoPanel.cpp
    Source/gui/Meters.cpp
    Source/gui/PedalView.cpp
    Source/gui/Pedalboard.cpp
    Source/gui/Rack.cpp
    Source/gui/Theme.cpp
    Source/gui/OptionsPanel.cpp)

target_include_directories(PedalTrinity PRIVATE Source Source/gui Source/engine)

# immagini renderizzate dei pedali (una per modello del catalogo)
file(GLOB PT_PEDAL_IMAGES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/pedals/*.jpg"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/Resources/pedals/*_a.png")

juce_add_binary_data(PedalTrinityData
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        ${PT_PEDAL_IMAGES}
        Resources/strip_ts_big.png
        Resources/strip_ts_small.png
        Resources/strip_boss.png
        Resources/strip_boss_outer.png
        Resources/strip_boss_inner.png
        Resources/strip_slider.png
        Resources/strip_toggle.png
        Resources/license_gpl3.txt
        Resources/PedalTrinity_Guida.pdf)
set_target_properties(PedalTrinityData PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(PedalTrinity PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
    PT_VERSION_STRING="${PT_VERSION_STRING}")

if(UNIX AND NOT APPLE)
    target_compile_definitions(PedalTrinity PUBLIC JUCE_JACK=1 JUCE_ALSA=1)
endif()

# ------------------------------------------------------------------ ASIO (Windows)
if(WIN32)
    if(PT_ASIO_SDK_DIR AND EXISTS "${PT_ASIO_SDK_DIR}/common/iasiodrv.h")
        message(STATUS "Pedal Trinity: driver ASIO abilitati (${PT_ASIO_SDK_DIR})")
        target_compile_definitions(PedalTrinity PUBLIC JUCE_ASIO=1)
        target_include_directories(PedalTrinity PUBLIC "${PT_ASIO_SDK_DIR}/common")
    else()
        message(WARNING "Pedal Trinity: ASIO SDK non trovato, lo Standalone usera' solo WASAPI/DirectSound")
    endif()
    if(MSVC)
        target_compile_options(PedalTrinity PUBLIC /utf-8)
    endif()
endif()

target_link_libraries(PedalTrinity
    PRIVATE
        PedalTrinityData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

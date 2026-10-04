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

project(PedalTrinity VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)        # NeuralAmpModelerCore richiede la libreria C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(PT_VERSION_STRING "1.2.0-beta")
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

# ------------------------------------------------------------------ Neural Amp Modeler (MIT)
# Core DSP di Steven Atkinson (modelli .nam A1/A2, slimmable), AudioDSPTools (ricampionamento,
# noise gate, filtri del tonestack) ed Eigen: archivi a versione fissa con impronta SHA-256.
# Il loader .namb (TONE3000) e i pezzi del plugin NAM ufficiale sono in Source/third_party.
include(FetchContent)
FetchContent_Declare(nam_core
    URL https://github.com/sdatkinson/NeuralAmpModelerCore/archive/1f42f88535884450104b8711d7595019afa0495b.tar.gz
    URL_HASH SHA256=ac10cdabb0a7408e660d5cc403d1260c966bc839b5b5bb71511b663ca6e4a0cf
    SOURCE_SUBDIR _nessun_cmake)       # v0.5.4: si compilano solo i sorgenti necessari
FetchContent_Declare(nam_dsptools
    URL https://github.com/sdatkinson/AudioDSPTools/archive/0827c6c2fc0deced568536142ea86f189e0b98a1.tar.gz
    URL_HASH SHA256=2557d2ee58481b1bafa63e528b9c6f2b2ae67adb2f55e2c1d13c90ea99b420d5
    SOURCE_SUBDIR _nessun_cmake)
FetchContent_Declare(nam_eigen
    URL https://gitlab.com/libeigen/eigen/-/archive/bc3b39870ecb690a623a3f49149a358b95c5781d/eigen-bc3b39870ecb690a623a3f49149a358b95c5781d.tar.gz
    URL_HASH SHA256=5f6557d1aca6e959a4b955c55f2935261fd30de59c0c79e35bc6d5cbe1de22d1
    SOURCE_SUBDIR _nessun_cmake)
FetchContent_MakeAvailable(nam_core nam_dsptools nam_eigen)

file(GLOB PT_NAM_SOURCES "${nam_core_SOURCE_DIR}/NAM/*.cpp" "${nam_core_SOURCE_DIR}/NAM/wavenet/*.cpp")
add_library(pt_nam STATIC
    ${PT_NAM_SOURCES}
    ${nam_dsptools_SOURCE_DIR}/dsp/dsp.cpp
    ${nam_dsptools_SOURCE_DIR}/dsp/NoiseGate.cpp
    ${nam_dsptools_SOURCE_DIR}/dsp/RecursiveLinearFilter.cpp
    Source/third_party/nam/ToneStack.cpp
    Source/third_party/namb/get_dsp_namb.cpp
    Source/engine/NamWeights.cpp)
target_include_directories(pt_nam SYSTEM PUBLIC
    ${nam_core_SOURCE_DIR}
    ${nam_core_SOURCE_DIR}/Dependencies/nlohmann
    ${nam_eigen_SOURCE_DIR}
    ${nam_dsptools_SOURCE_DIR}
    Source/third_party
    Source/third_party/nam)
target_include_directories(pt_nam PRIVATE Source/engine)
target_compile_definitions(pt_nam PUBLIC NAM_ENABLE_A2_FAST EIGEN_MPL2_ONLY)   # campioni double come il plugin NAM; Eigen solo MPL 2.0
if(MSVC)
    target_compile_definitions(pt_nam PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
    target_compile_options(pt_nam PRIVATE /bigobj /W0)
else()
    target_compile_options(pt_nam PRIVATE -w)
endif()
set_target_properties(pt_nam PROPERTIES POSITION_INDEPENDENT_CODE ON)

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
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(PedalTrinity PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Presets.cpp
    Source/MidiMap.cpp
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
    Source/gui/NamPanel.cpp
    Source/gui/Pedalboard.cpp
    Source/gui/Rack.cpp
    Source/gui/Theme.cpp
    Source/gui/RealMod.cpp
    Source/gui/OptionsPanel.cpp
    Source/gui/MidiPanel.cpp
    Source/engine/FxNam.cpp
    Source/engine/NamSecurity.cpp
    Source/engine/FxClassic.cpp
    Source/engine/FxClassicMod.cpp
    Source/engine/FxClassicTime.cpp
    Source/engine/FxPolyPitch.cpp
    Source/engine/FxInstrument.cpp
    Source/engine/FxClassicB.cpp
    Source/engine/FxClassicBMod.cpp
    Source/engine/FxClassicBTime.cpp
    Source/engine/FxClassicBPitch.cpp
    Source/engine/FxClassicBMulti.cpp)
# banco di filtri complessi (FxSpectral.h): senza errno/trap sulle radici i cicli per banda si vettorizzano
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(Source/engine/FxPolyPitch.cpp Source/engine/FxInstrument.cpp
        Source/engine/FxClassicBTime.cpp Source/engine/FxClassicBPitch.cpp Source/engine/FxClassicBMulti.cpp
        PROPERTIES COMPILE_OPTIONS "-fno-math-errno;-fno-trapping-math")
endif()
# ResamplingContainer.h (AudioDSPTools) ridichiara LanczosResampler come alias: GCC 11-12 lo tratta come
# errore "changes meaning" (permerror), i compilatori piu' recenti no. Solo per i file che lo includono.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(Source/engine/FxNam.cpp Source/engine/NamSecurity.cpp
        PROPERTIES COMPILE_OPTIONS "-fpermissive;-Wno-changes-meaning")
endif()

target_include_directories(PedalTrinity PRIVATE Source Source/gui Source/engine)

# immagini renderizzate dei pedali (una per modello del catalogo)
file(GLOB PT_PEDAL_IMAGES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/pedals/*.jpg"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/Resources/pedals/*_a.png"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/Resources/pedals_real/*.jpg"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/Resources/pedals_real/*_a.png")

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
        Source/third_party/LICENSE-NAM.txt
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
        pt_nam
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

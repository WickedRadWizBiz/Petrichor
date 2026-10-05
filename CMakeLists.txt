cmake_minimum_required(VERSION 3.22)

project(PetrichorPiano VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PETRICHOR_ENGINE_ONLY  "Build only the JUCE-free engine, tests and offline renderer" OFF)
option(PETRICHOR_BUILD_TESTS  "Build the engine tests and the offline renderer" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

#==============================================================================
# Engine: pure C++17, no JUCE. Everything audible lives here.

add_library(petrichor_engine STATIC
    Source/DSP/Atmosphere.h
    Source/DSP/DspCore.h
    Source/DSP/KolmogorovNoise.cpp
    Source/DSP/KolmogorovNoise.h
    Source/DSP/MultipathRumble.cpp
    Source/DSP/MultipathRumble.h
    Source/DSP/PetrichorEngine.cpp
    Source/DSP/PetrichorEngine.h
    Source/DSP/PianoVoice.cpp
    Source/DSP/PianoVoice.h
    Source/DSP/RainTexture.cpp
    Source/DSP/RainTexture.h
    Source/DSP/StormWind.h
    Source/DSP/WindAir.h
)
target_include_directories(petrichor_engine PUBLIC Source/DSP)
target_compile_features(petrichor_engine PUBLIC cxx_std_17)
set_target_properties(petrichor_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(petrichor_engine PRIVATE /W4 $<$<NOT:$<CONFIG:Debug>>:/O2>)
else()
    # The modal string bank is written for auto-vectorisation; keep it optimised even in Debug.
    target_compile_options(petrichor_engine PRIVATE -Wall -Wextra -O3)
endif()

if(PETRICHOR_BUILD_TESTS)
    enable_testing()

    add_executable(PetrichorTests Tests/EngineTests.cpp)
    target_link_libraries(PetrichorTests PRIVATE petrichor_engine)
    add_test(NAME PetrichorEngineTests COMMAND PetrichorTests)

    add_executable(PetrichorRender Tools/RenderDemo.cpp Tools/WavWriter.h)
    target_link_libraries(PetrichorRender PRIVATE petrichor_engine)
endif()

if(PETRICHOR_ENGINE_ONLY)
    return()
endif()

#==============================================================================
# Plug-in (JUCE 8). Set FETCHCONTENT_SOURCE_DIR_JUCE to use a local JUCE checkout.

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

#------------------------------------------------------------------------------
# Frontend: React + Tailwind, built by Vite into a single self-contained index.html
# (scripts, styles and fonts inlined) and embedded in the binary. No network at runtime.

set(FRONTEND_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/frontend)
set(FRONTEND_HTML ${FRONTEND_DIR}/dist/index.html)

file(GLOB_RECURSE FRONTEND_SOURCES CONFIGURE_DEPENDS
    ${FRONTEND_DIR}/src/*
    ${FRONTEND_DIR}/index.html
    ${FRONTEND_DIR}/package.json
    ${FRONTEND_DIR}/vite.config.js
    ${FRONTEND_DIR}/tailwind.config.js
    ${FRONTEND_DIR}/postcss.config.js
)

find_program(NPM_EXECUTABLE NAMES npm npm.cmd)
if(NPM_EXECUTABLE)
    add_custom_command(
        OUTPUT  ${FRONTEND_HTML}
        COMMAND ${CMAKE_COMMAND} -E env "NODE_ENV=production" ${NPM_EXECUTABLE} ci --no-audit --no-fund
        COMMAND ${NPM_EXECUTABLE} run build
        WORKING_DIRECTORY ${FRONTEND_DIR}
        DEPENDS ${FRONTEND_SOURCES} ${FRONTEND_DIR}/package-lock.json
        COMMENT "Building the Petrichor Piano frontend"
        VERBATIM
    )
elseif(NOT EXISTS ${FRONTEND_HTML})
    message(FATAL_ERROR "npm was not found and ${FRONTEND_HTML} does not exist. Install Node.js 18+ or build the frontend manually.")
endif()

juce_add_binary_data(PetrichorFrontend
    HEADER_NAME PetrichorFrontend.h
    NAMESPACE   PetrichorFrontend
    SOURCES     ${FRONTEND_HTML}
)

#------------------------------------------------------------------------------
# Windows needs the WebView2 SDK (NuGet package Microsoft.Web.WebView2) for a modern web view.
if(WIN32)
    option(PETRICHOR_USE_WEBVIEW2 "Use Microsoft WebView2 for the editor on Windows" ON)
else()
    set(PETRICHOR_USE_WEBVIEW2 FALSE)
endif()

juce_add_plugin(PetrichorPiano
    COMPANY_NAME                "PetrichorLabs"
    BUNDLE_ID                   com.petrichorlabs.petrichorpiano
    PLUGIN_MANUFACTURER_CODE    PetL
    PLUGIN_CODE                 PtPn
    PRODUCT_NAME                "Petrichor Piano"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    NEEDS_WEB_BROWSER           TRUE
    NEEDS_WEBVIEW2              ${PETRICHOR_USE_WEBVIEW2}
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    FORMATS                     VST3 AU Standalone
)

target_sources(PetrichorPiano PRIVATE
    Source/Plugin/Parameters.h
    Source/Plugin/PluginEditor.cpp
    Source/Plugin/PluginEditor.h
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginProcessor.h
)

target_compile_definitions(PetrichorPiano PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    $<$<BOOL:${PETRICHOR_USE_WEBVIEW2}>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>
)

target_link_libraries(PetrichorPiano
    PRIVATE
        petrichor_engine
        PetrichorFrontend
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

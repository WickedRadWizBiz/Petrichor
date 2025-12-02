cmake_minimum_required(VERSION 3.22)

# Enable Folder organization in VS
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(Petrichor VERSION 1.0.0)

set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_USE_CURL OFF CACHE BOOL "" FORCE)

# Set default output directories for all targets to avoid deep path issues
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0
)

FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Petrichor
    COMPANY_NAME "PetrichorLabs"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE PetL
    PLUGIN_CODE Petr
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Petrichor"
    NEEDS_WEBVIEW2 TRUE
)

# Force the Shared Code target (named 'Petrichor') to output to the flat lib directory.
# JUCE's default behavior nest this deep in artefacts/, causing path length errors.
set_target_properties(Petrichor PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Also enforce for the VST3 target if accessible (usually Petrichor_VST3)
if(TARGET Petrichor_VST3)
    set_target_properties(Petrichor_VST3 PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

# Explicitly ensure the JuceHeader is generated and linked
juce_generate_juce_header(Petrichor)

# Define the source files
target_sources(Petrichor PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/FrontendAssets.h

    # DSP
    Source/DSP/PhysicalString.cpp
    Source/DSP/PhysicalString.h
    Source/DSP/RainEngine.cpp
    Source/DSP/RainEngine.h
    Source/DSP/HygroEngine.cpp
    Source/DSP/HygroEngine.h
    Source/DSP/ThunderModule.cpp
    Source/DSP/ThunderModule.h
    Source/DSP/WindModule.cpp
    Source/DSP/WindModule.h
    Source/DSP/PolyphonicVoiceManager.cpp
    Source/DSP/PolyphonicVoiceManager.h

    # UI
    Source/UI/WebUI.cpp
    Source/UI/WebUI.h
)

# Organize Sources in IDE Folders
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source PREFIX "Source" FILES
    Source/PluginProcessor.cpp Source/PluginProcessor.h
    Source/PluginEditor.cpp Source/PluginEditor.h
    Source/FrontendAssets.h
    Source/DSP/PhysicalString.cpp Source/DSP/PhysicalString.h
    Source/DSP/RainEngine.cpp Source/DSP/RainEngine.h
    Source/DSP/HygroEngine.cpp Source/DSP/HygroEngine.h
    Source/DSP/ThunderModule.cpp Source/DSP/ThunderModule.h
    Source/DSP/WindModule.cpp Source/DSP/WindModule.h
    Source/DSP/PolyphonicVoiceManager.cpp Source/DSP/PolyphonicVoiceManager.h
    Source/UI/WebUI.cpp Source/UI/WebUI.h
)

# Build Frontend
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist/index.html
    COMMAND npm run build
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/frontend
    COMMENT "Building React Frontend..."
)

add_custom_target(FrontendBuild DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist/index.html)
add_dependencies(Petrichor FrontendBuild)
set_target_properties(FrontendBuild PROPERTIES FOLDER "Frontend")

# Explicitly disable VST2 replacement to avoid VST2 SDK requirements
target_compile_definitions(Petrichor PUBLIC
    JUCE_WEB_BROWSER_WORK_AROUND=1
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Petrichor PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
)

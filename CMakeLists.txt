cmake_minimum_required(VERSION 3.22)

# --- Réglages à personnaliser -------------------------------------------------
set(ACIDMACHINE_PRODUCT_NAME "Acid Machine")       # nom du plugin dans le DAW, et nom du fichier .vst3
set(ACIDMACHINE_COMPANY_NAME "Phragma")            # nom affiché comme éditeur dans le DAW
set(ACIDMACHINE_BUNDLE_ID    "com.phragma.acidmachine")
# -----------------------------------------------------------------------------
# Ne changer ni PLUGIN_MANUFACTURER_CODE ni PLUGIN_CODE : ce sont eux (et pas le nom)
# qui permettent aux DAW de retrouver le plugin dans les projets.

# macOS : binaire universel (Apple Silicon + Intel), compatible macOS 10.13+
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Version minimale de macOS")
if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures macOS")
endif()

# Windows : runtime C++ lié en statique -> aucun "Visual C++ Redistributable" à installer
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(AcidMachine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(ACIDMACHINE_COPY_AFTER_BUILD "Copier le plugin dans le dossier VST3 du système après la compilation" OFF)
option(ACIDMACHINE_BUILD_TESTS "Compiler les tests" OFF)

# --- JUCE (téléchargé automatiquement) ---------------------------------------
# Pour utiliser une copie locale : -DFETCHCONTENT_SOURCE_DIR_JUCE=/chemin/vers/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# --- Plugin ------------------------------------------------------------------
juce_add_plugin(AcidMachine
    PRODUCT_NAME                "${ACIDMACHINE_PRODUCT_NAME}"
    COMPANY_NAME                "${ACIDMACHINE_COMPANY_NAME}"
    BUNDLE_ID                   "${ACIDMACHINE_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE    Phrg
    PLUGIN_CODE                 Acdm
    FORMATS                     VST3
    VST3_CATEGORIES             Instrument Synth
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${ACIDMACHINE_COPY_AFTER_BUILD})

target_sources(AcidMachine PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# Police de l'interface (Chakra Petch, licence OFL : voir Assets/ChakraPetch-OFL.txt), embarquée dans le plugin
juce_add_binary_data(AcidMachineAssets SOURCES
    Assets/ChakraPetch-SemiBold.ttf
    Assets/ChakraPetch-Bold.ttf)
set_target_properties(AcidMachineAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_compile_definitions(AcidMachine PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    ACIDMACHINE_VERSION="${PROJECT_VERSION}")

target_link_libraries(AcidMachine
    PRIVATE
        juce::juce_audio_processors
        AcidMachineAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# --- Tests (optionnels) ------------------------------------------------------
if(ACIDMACHINE_BUILD_TESTS)
    enable_testing()

    # Moteur seul (voix, séquenceur, presets d'usine), sans JUCE
    add_executable(AcidMachineEngineTests Tests/EngineTests.cpp)
    target_include_directories(AcidMachineEngineTests PRIVATE Source)
    add_test(NAME engine COMMAND AcidMachineEngineTests)

    # Plugin complet (paramètres, MIDI, séquenceur calé sur l'hôte, presets, état, interface)
    juce_add_console_app(AcidMachinePluginTests PRODUCT_NAME "AcidMachinePluginTests")
    target_sources(AcidMachinePluginTests PRIVATE
        Tests/PluginTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(AcidMachinePluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        ACIDMACHINE_VERSION="${PROJECT_VERSION}")
    target_link_libraries(AcidMachinePluginTests PRIVATE
        juce::juce_audio_processors
        AcidMachineAssets
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME plugin COMMAND AcidMachinePluginTests)

    # Rendu d'extraits audio hors ligne (Tools/Render.cpp)
    juce_add_console_app(AcidMachineRender PRODUCT_NAME "AcidMachineRender")
    target_sources(AcidMachineRender PRIVATE
        Tools/Render.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(AcidMachineRender PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        ACIDMACHINE_VERSION="${PROJECT_VERSION}")
    target_link_libraries(AcidMachineRender PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
        AcidMachineAssets
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()

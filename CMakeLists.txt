# =============================================================================
#  IDGems Slicer - fichier de construction CMake
#  JUCE est telecharge automatiquement au premier "configure" (pas besoin de git).
# =============================================================================
cmake_minimum_required(VERSION 3.22)

project(IDGemsSlicer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sous Windows : runtime statique (le .vst3 ne depend d'aucune DLL Visual C++ a installer)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- Telechargement de JUCE ---------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(
    JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.15.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(JUCE)

# --- Definition du plugin -----------------------------------------------------
juce_add_plugin(IDGemsSlicer
    COMPANY_NAME                "IDGems"
    COMPANY_WEBSITE             ""
    BUNDLE_ID                   "com.idgems.slicer"
    PRODUCT_NAME                "IDGems Slicer"
    VERSION                     ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE    IDGm
    PLUGIN_CODE                 IGSl
    FORMATS                     VST3 Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(IDGemsSlicer
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Exporter.cpp
        Source/PresetManager.cpp
        Source/UI/IDGemsLookAndFeel.cpp
        Source/UI/MainView.cpp
        Source/UI/WaveformView.cpp
        Source/UI/SlotPanel.cpp
        Source/UI/SliceEditor.cpp
        Source/UI/PlayPanel.cpp
        Source/UI/SequencerView.cpp
        Source/UI/MapView.cpp
        Source/UI/VoiceView.cpp
        Source/UI/FxView.cpp
        Source/UI/RandomView.cpp
        Source/UI/ExportView.cpp
        Source/UI/HeaderBar.cpp
        Source/Engine/Slicer.cpp
        Source/Engine/SampleManager.cpp
        Source/Engine/VoiceEngine.cpp
        Source/Engine/Sequencer.cpp
        Source/Engine/SliceAnalysis.cpp
        Source/Engine/MapStore.cpp
        Source/Engine/FxChain.cpp
        Source/Engine/Master.cpp
        Source/Engine/RandomManager.cpp
        Source/Engine/UndoHistory.cpp)

target_include_directories(IDGemsSlicer PRIVATE Source)

# --- Police personnalisee (optionnelle) ----------------------------------------
# Deposez un fichier .ttf ou .otf dans Resources/Fonts : il est embarque dans le
# plugin (rien a installer sur la machine). Sans fichier, police mono par defaut.
file(GLOB IDGEMS_FONT_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Fonts/*.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Fonts/*.otf"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Fonts/*.TTF"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Fonts/*.OTF")
if(IDGEMS_FONT_FILES)
    message(STATUS "IDGems : police personnalisee trouvee : ${IDGEMS_FONT_FILES}")
    juce_add_binary_data(IDGemsFonts SOURCES ${IDGEMS_FONT_FILES})
    target_link_libraries(IDGemsSlicer PRIVATE IDGemsFonts)
    target_compile_definitions(IDGemsSlicer PRIVATE IDGEMS_HAS_CUSTOM_FONT=1)
else()
    message(STATUS "IDGems : aucune police dans Resources/Fonts (police par defaut)")
endif()

target_compile_definitions(IDGemsSlicer
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(IDGemsSlicer
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Tests automatiques (optionnels) ------------------------------------------
option(IDGEMS_BUILD_TESTS "Compiler le programme de tests" OFF)
if(IDGEMS_BUILD_TESTS)
    add_executable(IDGemsSlicerTests Tests/TestMain.cpp)
    target_compile_definitions(IDGemsSlicerTests PRIVATE $<TARGET_PROPERTY:IDGemsSlicer,COMPILE_DEFINITIONS>)
    target_include_directories(IDGemsSlicerTests PRIVATE $<TARGET_PROPERTY:IDGemsSlicer,INCLUDE_DIRECTORIES>)
    target_compile_definitions(IDGemsSlicerTests PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
    target_link_libraries(IDGemsSlicerTests PRIVATE IDGemsSlicer)
endif()

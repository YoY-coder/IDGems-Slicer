# =============================================================================
#  IDGems Slicer - fichier de construction CMake
#  JUCE est telecharge automatiquement au premier "configure" (pas besoin de git).
# =============================================================================
cmake_minimum_required(VERSION 3.22)

project(IDGemsSlicer VERSION 0.1.0 LANGUAGES C CXX)

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
        Source/UI/IDGemsLookAndFeel.cpp
        Source/UI/MainView.cpp)

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

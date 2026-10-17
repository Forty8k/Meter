# Meter F8K Analyser: a separate small project, it does not touch the Meter plugin build.
cmake_minimum_required(VERSION 3.22)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # no Visual C++ runtime install needed
project(MeterF8KAnalyser VERSION 1.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(F8K_BUILD_GUI "Build the window app (needs GUI libraries on Linux)" ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../JUCE ${CMAKE_BINARY_DIR}/JUCE)

set(F8K_ENGINE_SOURCES src/Capture.cpp src/Estimates.cpp src/OriginalScorer.cpp src/Engine.cpp)
set(F8K_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_MODAL_LOOPS_PERMITTED=1)

add_executable(F8KSelfTest tools/selftest.cpp src/Capture.cpp src/Estimates.cpp src/OriginalScorer.cpp) # no JUCE needed

juce_add_console_app(MeterF8KAnalyserCLI PRODUCT_NAME "MeterF8KAnalyserCLI" COMPANY_NAME "Forty8k")
target_sources(MeterF8KAnalyserCLI PRIVATE src/MainCli.cpp ${F8K_ENGINE_SOURCES})
target_compile_definitions(MeterF8KAnalyserCLI PRIVATE ${F8K_DEFS})
target_link_libraries(MeterF8KAnalyserCLI PRIVATE juce::juce_core juce::juce_audio_formats juce::juce_cryptography
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

if (F8K_BUILD_GUI)
    juce_add_gui_app(MeterF8KAnalyser PRODUCT_NAME "Meter F8K Analyser" COMPANY_NAME "Forty8k" VERSION 1.1.0)
    target_sources(MeterF8KAnalyser PRIVATE src/MainGui.cpp ${F8K_ENGINE_SOURCES})
    target_compile_definitions(MeterF8KAnalyser PRIVATE ${F8K_DEFS} JUCE_APPLICATION_NAME_STRING="Meter F8K Analyser" JUCE_APPLICATION_VERSION_STRING="1.1.0")
    target_link_libraries(MeterF8KAnalyser PRIVATE juce::juce_gui_basics juce::juce_audio_formats juce::juce_cryptography
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

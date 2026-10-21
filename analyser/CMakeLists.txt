# meterDARK Analyser: a separate small project, it does not touch the Meter plugin build.
cmake_minimum_required(VERSION 3.22)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # no Visual C++ runtime install needed
project(meterDARKAnalyser VERSION 1.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(MDARK_BUILD_GUI "Build the window app (needs GUI libraries on Linux)" ON)
set(MDARK_BUILD_NUMBER 0 CACHE STRING "build number (set by GitHub; the app compares it with the newest release)")

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../JUCE ${CMAKE_BINARY_DIR}/JUCE)

set(MDARK_ENGINE_SOURCES src/Capture.cpp src/Estimates.cpp src/OriginalScorer.cpp src/Engine.cpp src/LabHints.cpp src/LeanRecord.cpp src/TagFill.cpp src/TrackStore.cpp)
set(MDARK_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_MODAL_LOOPS_PERMITTED=1 MDARK_BUILD_NUMBER=${MDARK_BUILD_NUMBER})

add_executable(MDarkSelfTest tools/selftest.cpp src/Capture.cpp src/Estimates.cpp src/OriginalScorer.cpp) # no JUCE needed
add_executable(MDarkLevelTest tools/leveltest.cpp src/LabHints.cpp src/Capture.cpp src/Estimates.cpp src/OriginalScorer.cpp) # no JUCE needed
add_executable(MDarkPackTest tools/packtest.cpp src/LabHints.cpp src/Capture.cpp src/Estimates.cpp src/OriginalScorer.cpp) # no JUCE needed
add_executable(MDarkLeanTest tools/leantest.cpp src/LeanRecord.cpp src/LabHints.cpp src/Capture.cpp src/Estimates.cpp src/OriginalScorer.cpp) # no JUCE needed
add_executable(MDarkReplayTest tools/replaytest.cpp src/LabHints.cpp src/Capture.cpp src/Estimates.cpp src/OriginalScorer.cpp) # no JUCE needed

juce_add_console_app(meterDARKAnalyserCLI PRODUCT_NAME "meterDARKAnalyserCLI" COMPANY_NAME "Forty8k")
target_sources(meterDARKAnalyserCLI PRIVATE src/MainCli.cpp ${MDARK_ENGINE_SOURCES})
target_compile_definitions(meterDARKAnalyserCLI PRIVATE ${MDARK_DEFS})
target_link_libraries(meterDARKAnalyserCLI PRIVATE juce::juce_core juce::juce_audio_formats juce::juce_cryptography
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

if (MDARK_BUILD_GUI)
    juce_add_gui_app(meterDARKAnalyser PRODUCT_NAME "meterDARK Analyser" COMPANY_NAME "Forty8k" VERSION 1.1.0
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.png" ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.png")
    target_sources(meterDARKAnalyser PRIVATE src/MainGui.cpp ${MDARK_ENGINE_SOURCES})
    target_compile_definitions(meterDARKAnalyser PRIVATE ${MDARK_DEFS} JUCE_APPLICATION_NAME_STRING="meterDARK Analyser" JUCE_APPLICATION_VERSION_STRING="1.1.0")
    target_link_libraries(meterDARKAnalyser PRIVATE juce::juce_gui_basics juce::juce_audio_devices juce::juce_audio_formats juce::juce_cryptography
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

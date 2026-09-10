cmake_minimum_required(VERSION 3.22)
project(gootar_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(GOOTAR_ENABLE_ASIO "Build the ASIO backend (requires the ASIO SDK in libs/asiosdk)" OFF)

# ---------------------------------------------------------------------------
# Always built: the lock-free model handover and its stress test.
#
# This has no third-party dependencies on purpose, so CI has something real to
# compile and run from day one, before JUCE or NeuralAudio are vendored in.
# ---------------------------------------------------------------------------
add_executable(gootar_model_swapper_test src/ModelSwapperTest.cpp)
target_include_directories(gootar_model_swapper_test PRIVATE src)

find_package(Threads REQUIRED)
target_link_libraries(gootar_model_swapper_test PRIVATE Threads::Threads)

enable_testing()
add_test(NAME model_swapper COMMAND gootar_model_swapper_test)

# ---------------------------------------------------------------------------
# The player itself. Not built until the dependencies are vendored:
#
#   git submodule add https://github.com/juce-framework/JUCE        native/libs/JUCE
#   git submodule add https://github.com/mikeoliphant/NeuralAudio   native/libs/NeuralAudio
#
# Kept behind an existence check so a fresh clone still configures and builds.
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE/CMakeLists.txt"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/NeuralAudio/CMakeLists.txt"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/PluginProcessor.cpp")

  message(STATUS "gootar: JUCE + NeuralAudio found - building the player")
  add_subdirectory(libs/JUCE)
  add_subdirectory(libs/NeuralAudio)

  juce_add_plugin(GootarPlayer
    PRODUCT_NAME "Gootar Player"
    COMPANY_NAME "Gootar"
    # Standalone and VST3 are two targets of one project. Building both from
    # the start costs nothing and removes "wrap as VST3" as a later milestone.
    FORMATS Standalone VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

  target_sources(GootarPlayer PRIVATE src/PluginProcessor.cpp)
  target_include_directories(GootarPlayer PRIVATE src)

  target_compile_definitions(GootarPlayer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_ASIO=$<BOOL:${GOOTAR_ENABLE_ASIO}>)

  target_link_libraries(GootarPlayer PRIVATE
    NeuralAudio
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_optimisation_flags)

  if(GOOTAR_ENABLE_ASIO)
    # Steinberg's SDK, dropped in by hand; not committed to the repo.
    target_include_directories(GootarPlayer PRIVATE libs/asiosdk/common)
  endif()

else()
  message(STATUS
    "gootar: JUCE, NeuralAudio or src/PluginProcessor.cpp missing - building the "
    "swapper test only. See native/README.md.")
endif()

cmake_minimum_required(VERSION 3.22)
project(gootar_native VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(GOOTAR_ENABLE_ASIO "Build the ASIO backend (requires the ASIO SDK in libs/asiosdk)" OFF)
option(GOOTAR_BUILD_APP   "Build the JUCE application (requires libs/JUCE)" ON)

set(GOOTAR_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/libs")
set(GOOTAR_ADT  "${GOOTAR_LIBS}/AudioDSPTools")
set(GOOTAR_NA   "${GOOTAR_LIBS}/NeuralAudio")

# Eigen ships inside NeuralAudio's dependency tree; AudioDSPTools needs it too,
# and using one copy avoids two Eigen versions in one translation unit.
set(GOOTAR_EIGEN "${GOOTAR_NA}/deps/RTNeural/modules/Eigen")

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# The lock-free model handover, and its concurrent stress test.
# No third-party dependencies, so this compiles and runs anywhere.
# ---------------------------------------------------------------------------
add_executable(gootar_model_swapper_test src/ModelSwapperTest.cpp)
target_include_directories(gootar_model_swapper_test PRIVATE src)
target_link_libraries(gootar_model_swapper_test PRIVATE Threads::Threads)
if(WIN32)
  # <windows.h> defines min/max as macros unless told not to, which breaks
  # every std::max in the file with an error that points somewhere else.
  target_compile_definitions(gootar_model_swapper_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

enable_testing()
add_test(NAME model_swapper COMMAND gootar_model_swapper_test)
set_tests_properties(model_swapper PROPERTIES TIMEOUT 300)

# ---------------------------------------------------------------------------
# The DSP engine: the whole signal chain, with no JUCE in it.
#
# Kept framework-free on purpose so it can be tested headlessly against real
# .nam files, on any platform, including under sanitisers while models are
# hot-swapped mid-stream. JUCE only ever does device I/O and UI.
# ---------------------------------------------------------------------------
if(EXISTS "${GOOTAR_NA}/CMakeLists.txt" AND EXISTS "${GOOTAR_ADT}/dsp/dsp.cpp")

  add_subdirectory("${GOOTAR_NA}" "${CMAKE_BINARY_DIR}/NeuralAudio" EXCLUDE_FROM_ALL)

  # AudioDSPTools' own CMakeLists is a standalone project that builds tools we
  # do not want, so the handful of source files we need are compiled directly.
  #
  # NOTE: DSP_SAMPLE_FLOAT is deliberately NOT defined here. ImpulseResponse
  # declares Process(double**) as an override, so AudioDSPTools only compiles
  # coherently with DSP_SAMPLE == double. The chain therefore runs in double,
  # exactly as the stock plugin does, and converts to float only for the model.
  add_library(gootar_engine STATIC
    "${GOOTAR_ADT}/dsp/dsp.cpp"
    "${GOOTAR_ADT}/dsp/NoiseGate.cpp"
    "${GOOTAR_ADT}/dsp/RecursiveLinearFilter.cpp"
    "${GOOTAR_ADT}/dsp/ImpulseResponse.cpp"
    "${GOOTAR_ADT}/dsp/wav.cpp"
    src/dsp/Chain.cpp
    src/dsp/PitchDetector.cpp
    src/dsp/GootarEngine.cpp)

  # PUBLIC is only "src", so anything linking the engine sees GootarEngine.h
  # and nothing else. AudioDSPTools, NeuralAudio and Eigen stay PRIVATE, which
  # is what keeps the global `dsp` namespace from colliding with juce::dsp in
  # the app.
  target_include_directories(gootar_engine PUBLIC src)
  target_include_directories(gootar_engine PRIVATE
    src/dsp
    "${GOOTAR_ADT}/dsp"
    "${GOOTAR_NA}"
    "${GOOTAR_EIGEN}")

  target_link_libraries(gootar_engine PUBLIC Threads::Threads)
  target_link_libraries(gootar_engine PRIVATE NeuralAudio)

  add_executable(gootar_engine_test src/EngineTest.cpp)
  target_link_libraries(gootar_engine_test PRIVATE gootar_engine)
  if(WIN32)
    target_compile_definitions(gootar_engine_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  endif()
  target_compile_definitions(gootar_engine_test PRIVATE
    GOOTAR_TEST_MODEL_DIR="${GOOTAR_NA}/Utils/Models")

  add_test(NAME engine COMMAND gootar_engine_test)
  # These run in well under a second; anything near this ceiling is a hang.
  set_tests_properties(engine PROPERTIES TIMEOUT 300)

else()
  message(STATUS "gootar: submodules missing - run 'git submodule update --init --recursive'")
endif()

# ---------------------------------------------------------------------------
# The JUCE app. Standalone and VST3 are two formats of one target, so there is
# no separate "wrap as VST3" step later.
# ---------------------------------------------------------------------------
if(GOOTAR_BUILD_APP AND TARGET gootar_engine AND EXISTS "${GOOTAR_LIBS}/JUCE/CMakeLists.txt")

  message(STATUS "gootar: JUCE found - building the player")
  add_subdirectory("${GOOTAR_LIBS}/JUCE" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

  juce_add_plugin(GootarPlayer
    VERSION ${PROJECT_VERSION}
    PRODUCT_NAME "Gootar Player"
    PLUGIN_MANUFACTURER_CODE Gtar
    PLUGIN_CODE Gtr1
    COMPANY_NAME "Gootar"
    FORMATS Standalone VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

  target_sources(GootarPlayer PRIVATE
    src/app/PluginProcessor.cpp
    src/app/PluginEditor.cpp
    src/app/PresetIO.cpp
    src/app/ModelLibrary.cpp
    src/app/Theme.cpp
    src/app/Widgets.cpp)

  target_include_directories(GootarPlayer PRIVATE src src/app)

  juce_generate_juce_header(GootarPlayer)

  target_compile_definitions(GootarPlayer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_ASIO=$<BOOL:${GOOTAR_ENABLE_ASIO}>)

  target_link_libraries(GootarPlayer PRIVATE
    gootar_engine
    juce::juce_audio_utils
    juce::juce_cryptography
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

  if(GOOTAR_ENABLE_ASIO)
    target_include_directories(GootarPlayer PRIVATE "${GOOTAR_LIBS}/asiosdk/common")
  endif()

elseif(GOOTAR_BUILD_APP)
  message(STATUS "gootar: libs/JUCE missing - engine and tests only")
endif()

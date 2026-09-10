#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace gootar {

enum class OutputMode { Raw = 0, Normalized = 1, Calibrated = 2 };

/**
 * Everything a preset can set. Mirrors @gootar/preset-schema one-for-one; the
 * ranges are enforced there and re-clamped here so a hand-edited JSON file
 * cannot put the DSP into a strange state.
 */
struct Params
{
    double inputLevelDb = 0.0;              // -20 .. 20
    bool   calibrateInput = false;
    double inputCalibrationLevelDbu = 12.0; // -60 .. 60

    bool   gateEnabled = true;
    double gateThresholdDb = -80.0;         // -100 .. 0

    bool   toneStackEnabled = true;
    double bass = 5.0, mid = 5.0, treble = 5.0; // 0 .. 10

    bool   irEnabled = true;

    double outputLevelDb = 0.0;             // -40 .. 40
    OutputMode outputMode = OutputMode::Normalized;
};

struct ModelInfo
{
    bool        loaded = false;
    std::string fileName;
    std::string architecture;
    float       sampleRate = 0.0f;
    int         receptiveField = -1;
    bool        isStatic = false;
};

/**
 * The complete Gootar signal chain, with no JUCE in sight.
 *
 * Keeping the DSP free of the app framework is what lets it be tested
 * headlessly against real .nam files on any platform, including under
 * sanitisers while models are hot-swapped mid-stream. JUCE's only job is audio
 * device I/O and the UI.
 *
 * The implementation is hidden behind a pointer rather than exposed as members
 * for a concrete reason: AudioDSPTools puts its classes in a global `dsp`
 * namespace, JUCE has `juce::dsp`, and JuceHeader.h pulls `juce` into the
 * global scope. Any header that included both would make every mention of
 * `dsp::` ambiguous. Hiding them here means the app never sees AudioDSPTools
 * or Eigen at all — it only needs this file.
 *
 * Chain order is transcribed from NeuralAmpModeler::ProcessBlock:
 *
 *   input gain -> gate TRIGGER -> model -> gate GAIN -> tone stack
 *     -> IR -> 5 Hz DC blocker -> output gain
 *
 * THREADING
 *   Audio thread : process(), setParams()
 *   Loader thread: stageModel(), stageIR(), collectGarbage()
 * Model and IR handover both go through ModelSwapper, so nothing allocates,
 * blocks or frees on the audio thread.
 */
class GootarEngine
{
public:
    GootarEngine();
    ~GootarEngine();

    GootarEngine (const GootarEngine&) = delete;
    GootarEngine& operator= (const GootarEngine&) = delete;

    /**
     * [Loader/UI thread] Allocate for a given rate and block size.
     *
     * Changing the sample rate invalidates every loaded model: NeuralAudio
     * scales WaveNet dilations at load time only, so models must be reloaded
     * afterwards. modelNeedsReload() reports when that has happened.
     */
    void prepare (double sampleRate, int maxBlockSize);

    double sampleRate() const noexcept;
    int    maxBlockSize() const noexcept;

    // --- audio thread ------------------------------------------------------

    /** Mono in, mono out. May be called with input == output. */
    void process (const float* input, float* output, int numSamples) noexcept;

    /** Cheap enough to call every block; only changed knobs recompute. */
    void setParams (const Params&) noexcept;

    // --- loader thread -----------------------------------------------------

    /**
     * Load a .nam and hand it to the audio thread. The audio thread keeps
     * playing the old model until it picks the new one up, so this is safe to
     * call while sound is coming out.
     */
    bool stageModel (const std::filesystem::path&, std::string& errorOut);
    void clearModel();

    bool stageIR (const std::filesystem::path&, std::string& errorOut);
    void clearIR();

    /** Free models the audio thread has finished with. Loader thread only. */
    void collectGarbage() noexcept;

    /** True if the rate changed since the current model was loaded. */
    bool modelNeedsReload() const noexcept;

    ModelInfo modelInfo() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace gootar

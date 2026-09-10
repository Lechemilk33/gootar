#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ChainSpec.h"

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
    std::string fileName, filePath, architecture;
    float       sampleRate = 0.0f;
    int         receptiveField = -1;
    bool        isStatic = false;
};

/** What the tuner display needs. Filled by analysePitch() off the audio thread. */
struct PitchReading
{
    bool   voiced = false;
    double frequencyHz = 0.0;
    double clarity = 0.0;
    int    midiNote = 0;
    double cents = 0.0;
    double rms = 0.0;
    std::string noteName;
    std::string nearestString;
};

/**
 * The Gootar signal chain, with no JUCE in it.
 *
 * Keeping the DSP free of the app framework is what lets it be tested
 * headlessly against real .nam files on any platform, including under
 * sanitisers while models are hot-swapped mid-stream. JUCE's only job is audio
 * device I/O and the UI.
 *
 * The chain is an ordered list of blocks, not a fixed sequence, so adding an
 * effect or a second model is a list edit rather than a rewrite. The default
 * ordering is exactly the stock plugin's:
 *
 *   input gain -> gate -> model -> tone stack -> IR -> DC blocker -> output
 *
 * The implementation is hidden behind a pointer because AudioDSPTools puts its
 * classes in a global `dsp` namespace, JUCE has juce::dsp, and JuceHeader.h
 * pulls juce into global scope - any header including both would make every
 * `dsp::` ambiguous.
 *
 * THREADING
 *   Audio thread : process(), setParams()
 *   Loader thread: prepare(), loadModel(), loadIR(), setChain(),
 *                  collectGarbage(), analysePitch()
 * Model, IR and whole-chain handover all go through ModelSwapper, so nothing
 * allocates, blocks or frees on the audio thread.
 */
class GootarEngine
{
public:
    GootarEngine();
    ~GootarEngine();

    GootarEngine (const GootarEngine&) = delete;
    GootarEngine& operator= (const GootarEngine&) = delete;

    /**
     * [Loader thread] Allocate for a given rate and block size.
     *
     * NeuralAudio bakes the sample rate into a model when it loads it, so a
     * rate change invalidates everything loaded. modelNeedsReload() reports it.
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

    /** Load into a model slot (0 is the only one in the default chain). */
    bool loadModel (int slot, const std::filesystem::path&, std::string& errorOut);
    void clearModel (int slot);

    bool loadIR (const std::filesystem::path&, std::string& errorOut);
    void clearIR();

    /** Replace the chain layout. Blocks keep their state and loaded models. */
    void setChain (const std::vector<ChainSlot>&);
    std::vector<ChainSlot> currentChain() const;

    /** Free anything the audio thread has finished with. Loader thread only. */
    void collectGarbage() noexcept;

    bool modelNeedsReload() const noexcept;

    int       numModelSlots() const noexcept;
    ModelInfo modelInfo (int slot = 0) const;
    std::string irFileName() const;

    // --- metering and analysis --------------------------------------------

    /** Peak level since the last read, 0..1+. Safe from any thread. */
    float inputPeak() const noexcept;
    float outputPeak() const noexcept;

    /**
     * [Timer/worker thread] Analyse the most recent window of CLEAN input.
     *
     * The tap sits before the model on purpose: distortion piles on harmonics
     * and squashes dynamics, and a pitch tracker fed the amp output reports a
     * confidently wrong note.
     */
    PitchReading analysePitch();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace gootar

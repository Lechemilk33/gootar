/**
 * Headless verification of the full Gootar signal chain against real .nam
 * models (the ones bundled with NeuralAudio: WaveNet Standard/Nano/Feather,
 * A2, and two LSTMs).
 *
 * This exists because the interesting failures in this project are not
 * compile errors. They are: a model that outputs NaN, a chain wired in the
 * wrong order, a gate that never opens, a DC offset that eats headroom, and
 * above all a hot-swap that tears. None of those show up without running real
 * audio through real models, so that is what this does.
 *
 * Run under -fsanitize=thread and -fsanitize=address,undefined to check the
 * concurrency claims rather than trust them.
 */
#include "dsp/ChainSpec.h"
#include "dsp/GootarEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numbers>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check (bool ok, const std::string& what)
{
    std::printf ("  %-58s %s\n", what.c_str(), ok ? "ok" : "FAIL");
    if (! ok)
        ++failures;
}

constexpr double kSampleRate = 48000.0;
constexpr int    kBlock = 64;

std::vector<float> makeSine (int numSamples, double freqHz, float amplitude)
{
    std::vector<float> out (static_cast<size_t> (numSamples));
    for (int i = 0; i < numSamples; ++i)
        out[static_cast<size_t> (i)] =
            amplitude * static_cast<float> (std::sin (2.0 * std::numbers::pi * freqHz * i / kSampleRate));
    return out;
}

/** Root-mean-square, the only level measure that means anything here. */
double rms (const std::vector<float>& v, size_t from = 0)
{
    if (from >= v.size())
        return 0.0;
    double acc = 0.0;
    for (size_t i = from; i < v.size(); ++i)
        acc += static_cast<double> (v[i]) * v[i];
    return std::sqrt (acc / static_cast<double> (v.size() - from));
}

bool allFinite (const std::vector<float>& v)
{
    for (float s : v)
        if (! std::isfinite (s))
            return false;
    return true;
}

/** Largest sample-to-sample jump: how a torn model swap shows up. */
double maxJump (const std::vector<float>& v)
{
    double worst = 0.0;
    for (size_t i = 1; i < v.size(); ++i)
        worst = std::max (worst, std::abs (static_cast<double> (v[i]) - v[i - 1]));
    return worst;
}

/** Run a whole signal through the engine in fixed blocks, as a host would. */
std::vector<float> runBlocks (gootar::GootarEngine& engine, const std::vector<float>& input)
{
    std::vector<float> output (input.size(), 0.0f);
    for (size_t i = 0; i + kBlock <= input.size(); i += kBlock)
        engine.process (input.data() + i, output.data() + i, kBlock);
    return output;
}

gootar::Params flatParams()
{
    gootar::Params p;
    p.gateEnabled = false;      // isolate what is under test
    p.toneStackEnabled = false;
    p.irEnabled = false;
    p.outputMode = gootar::OutputMode::Raw; // no automatic level correction
    return p;
}

std::vector<fs::path> findModels()
{
    std::vector<fs::path> models;
    const fs::path dir { GOOTAR_TEST_MODEL_DIR };
    if (! fs::exists (dir))
        return models;
    for (const auto& e : fs::directory_iterator (dir))
        if (e.path().extension() == ".nam")
            models.push_back (e.path());
    std::sort (models.begin(), models.end());
    return models;
}

// ---------------------------------------------------------------------------

void testChainWithoutModel()
{
    std::printf ("\nchain with no model loaded\n");

    gootar::GootarEngine engine;
    engine.prepare (kSampleRate, kBlock);

    auto p = flatParams();
    engine.setParams (p);

    const auto input = makeSine (kBlock * 200, 220.0, 0.5f);
    const auto output = runBlocks (engine, input);

    check (allFinite (output), "output is finite");

    // With no model and unity gain, only the 5 Hz DC blocker is in the path,
    // so a 220 Hz tone should come through at essentially its original level.
    const double inLevel = rms (input, kBlock * 4);
    const double outLevel = rms (output, kBlock * 4);
    check (std::abs (outLevel - inLevel) / inLevel < 0.02,
           "passes through at unity (DC blocker only)");
}

void testInputAndOutputLevels()
{
    std::printf ("\ninput and output level controls\n");

    gootar::GootarEngine engine;
    engine.prepare (kSampleRate, kBlock);

    const auto input = makeSine (kBlock * 200, 220.0, 0.25f);

    auto p = flatParams();
    engine.setParams (p);
    const double unity = rms (runBlocks (engine, input), kBlock * 4);

    p.outputLevelDb = 6.0;
    engine.setParams (p);
    const double plus6 = rms (runBlocks (engine, input), kBlock * 4);

    const double ratio = plus6 / unity;
    check (std::abs (ratio - 2.0) < 0.02, "+6 dB output doubles amplitude");

    p.outputLevelDb = 0.0;
    p.inputLevelDb = -6.0;
    engine.setParams (p);
    const double minus6 = rms (runBlocks (engine, input), kBlock * 4);
    check (std::abs ((minus6 / unity) - 0.5) < 0.02, "-6 dB input halves amplitude");
}

void testToneStack()
{
    std::printf ("\ntone stack\n");

    gootar::GootarEngine engine;
    engine.prepare (kSampleRate, kBlock);

    // 150 Hz is the bass band's centre frequency.
    const auto bassTone = makeSine (kBlock * 400, 150.0, 0.3f);

    auto p = flatParams();
    p.toneStackEnabled = true;
    engine.setParams (p);
    const double flat = rms (runBlocks (engine, bassTone), kBlock * 8);

    p.bass = 10.0; // +20 dB at 150 Hz
    engine.setParams (p);
    const double boosted = rms (runBlocks (engine, bassTone), kBlock * 8);

    p.bass = 0.0; // -20 dB
    engine.setParams (p);
    const double cut = rms (runBlocks (engine, bassTone), kBlock * 8);

    check (boosted > flat * 5.0, "bass at 10 boosts 150 Hz substantially");
    check (cut < flat * 0.2, "bass at 0 cuts 150 Hz substantially");
    check (allFinite (runBlocks (engine, bassTone)), "tone stack output is finite");
}

void testDcBlocker()
{
    std::printf ("\nDC blocker\n");

    gootar::GootarEngine engine;
    engine.prepare (kSampleRate, kBlock);
    engine.setParams (flatParams());

    // Pure DC: the 5 Hz high-pass should remove nearly all of it.
    std::vector<float> dc (static_cast<size_t> (kBlock) * 2000, 0.5f);
    const auto output = runBlocks (engine, dc);

    const double tail = rms (output, output.size() - kBlock * 10);
    check (tail < 0.01, "steady DC is removed");
    check (allFinite (output), "output is finite");
}

void testNoiseGate()
{
    std::printf ("\nnoise gate\n");

    gootar::GootarEngine engine;
    engine.prepare (kSampleRate, kBlock);

    auto p = flatParams();
    p.gateEnabled = true;
    p.gateThresholdDb = -40.0;
    engine.setParams (p);

    // Quiet noise floor, well under the threshold.
    const auto quiet = makeSine (kBlock * 2000, 220.0, 0.001f);
    const auto gatedOut = runBlocks (engine, quiet);
    const double gatedTail = rms (gatedOut, gatedOut.size() - kBlock * 20);

    // A loud signal, well over it.
    const auto loud = makeSine (kBlock * 2000, 220.0, 0.5f);
    const auto loudOut = runBlocks (engine, loud);
    const double loudTail = rms (loudOut, loudOut.size() - kBlock * 20);

    check (gatedTail < 1e-4, "quiet signal below threshold is gated down");
    check (loudTail > 0.3, "loud signal above threshold passes");
    check (allFinite (gatedOut) && allFinite (loudOut), "gate output is finite");
}

void testEachBundledModel()
{
    std::printf ("\nreal models\n");

    const auto models = findModels();
    check (! models.empty(), "found bundled .nam models to test");

    for (const auto& path : models)
    {
        gootar::GootarEngine engine;
        engine.prepare (kSampleRate, kBlock);
        engine.setParams (flatParams());

        std::string err;
        if (! engine.loadModel (0, path, err))
        {
            check (false, path.filename().string() + " loads (" + err + ")");
            continue;
        }

        const auto input = makeSine (kBlock * 400, 220.0, 0.2f);
        const auto output = runBlocks (engine, input);

        const auto info = engine.modelInfo();
        const double level = rms (output, kBlock * 8);

        const std::string name = path.filename().string();
        const bool ok = allFinite (output) && level > 1e-6 && level < 100.0;
        check (ok, name + " -> finite, audible output");

        // Prewarm is the difference between a clean first block and a burst of
        // garbage. Compare the very first block against the settled tail.
        const double firstBlock = rms ({ output.begin(), output.begin() + kBlock });
        const bool prewarmed = std::isfinite (firstBlock) && firstBlock < level * 20.0;
        check (prewarmed, name + " -> first block is not garbage (prewarm)");

        std::printf ("      %-24s sr=%.0f rf=%d static=%s\n",
                     info.fileName.c_str(), info.sampleRate,
                     info.receptiveField, info.isStatic ? "yes" : "no");

        engine.collectGarbage();
    }
}

void testHotSwapUnderLoad()
{
    std::printf ("\nhot-swap while audio is running\n");

    const auto models = findModels();
    if (models.size() < 2)
    {
        check (false, "need at least two models to test swapping");
        return;
    }

    gootar::GootarEngine engine;
    engine.prepare (kSampleRate, kBlock);
    engine.setParams (flatParams());

    std::string err;
    engine.loadModel (0, models[0], err);

    std::atomic<bool> stop { false };
    std::atomic<long long> blocks { 0 };
    std::atomic<bool> sawNonFinite { false };
    std::atomic<int>  swapsRequested { 0 };

    // Audio thread: never stops, never allocates.
    std::thread audio ([&] {
        const auto input = makeSine (kBlock, 220.0, 0.2f);
        std::vector<float> out (kBlock, 0.0f);
        while (! stop.load (std::memory_order_relaxed))
        {
            engine.process (input.data(), out.data(), kBlock);
            for (float s : out)
                if (! std::isfinite (s))
                    sawNonFinite.store (true);
            blocks.fetch_add (1, std::memory_order_relaxed);
        }
    });

    // Loader thread: behaves like someone spinning through the model browser.
    std::thread loader ([&] {
        for (int round = 0; round < 60; ++round)
        {
            const auto& path = models[static_cast<size_t> (round) % models.size()];
            std::string loadErr;
            if (engine.loadModel (0, path, loadErr))
                swapsRequested.fetch_add (1);
            engine.collectGarbage();
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
        }
    });

    loader.join();
    // Let the audio thread run on a little so late swaps are picked up.
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    stop.store (true);
    audio.join();
    engine.collectGarbage();

    std::printf ("      %lld blocks processed, %d models staged\n",
                 blocks.load(), swapsRequested.load());

    check (blocks.load() > 100, "audio thread kept running throughout");
    check (swapsRequested.load() >= 50, "loader staged models continuously");
    check (! sawNonFinite.load(), "no NaN or inf reached the output");
}


void testPitchDetection()
{
    std::printf ("\npitch detection (tuner)\n");

    // Real note frequencies in standard tuning, low to high.
    struct Case { double hz; const char* name; };
    const Case cases[] = {
        {  82.41, "E2" },   // 6th string, open
        { 110.00, "A2" },   // 5th
        { 146.83, "D3" },   // 4th
        { 196.00, "G3" },   // 3rd
        { 246.94, "B3" },   // 2nd
        { 329.63, "E4" },   // 1st
    };

    for (const auto& c : cases)
    {
        gootar::GootarEngine engine;
        engine.prepare (kSampleRate, kBlock);
        engine.setParams (flatParams());

        // A pure sine is the easy case; a guitar is harmonically rich, which
        // MPM handles better than plain autocorrelation. This at least proves
        // the plumbing, the window size and the note maths.
        const auto tone = makeSine (kBlock * 200, c.hz, 0.3f);
        runBlocks (engine, tone);

        const auto reading = engine.analysePitch();
        const bool ok = reading.voiced
                     && reading.noteName == c.name
                     && std::abs (reading.cents) < 15.0;

        check (ok, std::string (c.name) + " at " + std::to_string ((int) c.hz)
                     + " Hz reads back correctly");
        if (! ok)
            std::printf ("      got voiced=%d note=%s cents=%.1f freq=%.2f\n",
                         (int) reading.voiced, reading.noteName.c_str(),
                         reading.cents, reading.frequencyHz);
    }

    // Silence must not produce a confident reading, or the tuner flickers
    // between notes whenever you stop playing.
    {
        gootar::GootarEngine engine;
        engine.prepare (kSampleRate, kBlock);
        engine.setParams (flatParams());
        const std::vector<float> silence (kBlock * 200, 0.0f);
        runBlocks (engine, silence);
        check (! engine.analysePitch().voiced, "silence reads as unvoiced");
    }

    // A detuned string should report the offset, not snap to the note.
    {
        gootar::GootarEngine engine;
        engine.prepare (kSampleRate, kBlock);
        engine.setParams (flatParams());
        // 110 Hz * 2^(30/1200) = ~111.9 Hz, i.e. 30 cents sharp of A2.
        const auto sharp = makeSine (kBlock * 200, 110.0 * std::pow (2.0, 30.0 / 1200.0), 0.3f);
        runBlocks (engine, sharp);
        const auto reading = engine.analysePitch();
        const bool ok = reading.voiced && reading.noteName == "A2"
                     && reading.cents > 20.0 && reading.cents < 40.0;
        check (ok, "30 cents sharp reads as sharp, not as A2 in tune");
        if (! ok)
            std::printf ("      got note=%s cents=%.1f\n",
                         reading.noteName.c_str(), reading.cents);
    }
}

void testChainEditing()
{
    std::printf ("\nchain editing\n");

    const auto models = findModels();
    if (models.empty())
    {
        check (false, "need a model to test chain edits");
        return;
    }

    gootar::GootarEngine engine;
    engine.prepare (kSampleRate, kBlock);
    engine.setParams (flatParams());

    std::string err;
    check (engine.loadModel (0, models[0], err), "loads a model into slot 0");

    // Force the audio thread to pick the chain up.
    const auto tone = makeSine (kBlock * 20, 220.0, 0.2f);
    runBlocks (engine, tone);
    check (engine.modelInfo (0).loaded, "slot 0 reports the model");

    // Reorder: move the tone stack before the model. The model must survive,
    // because rebuilding a chain must not cost you what is loaded in it.
    auto spec = engine.currentChain();
    auto toneIt = std::find_if (spec.begin(), spec.end(),
        [] (const gootar::ChainSlot& s) { return s.id == "tone"; });
    auto modelIt = std::find_if (spec.begin(), spec.end(),
        [] (const gootar::ChainSlot& s) { return s.id == "model-1"; });
    check (toneIt != spec.end() && modelIt != spec.end(), "standard chain has tone and model");

    if (toneIt != spec.end() && modelIt != spec.end())
    {
        std::iter_swap (toneIt, modelIt);
        engine.setChain (spec);
        const auto out = runBlocks (engine, tone);
        engine.collectGarbage();

        check (engine.modelInfo (0).loaded, "model survives a chain reorder");
        check (allFinite (out), "reordered chain still produces finite audio");
        check (rms (out, kBlock * 4) > 1e-6, "reordered chain still produces audio");
    }

    // Add a second model slot: this is what "pedal into amp" needs, and it
    // must not require a preset format change.
    //
    // Inserted at index 2 it lands EARLIER in the chain than model-1, so it
    // becomes slot 0 - slot indices follow signal order, not creation order.
    // That is the contract the UI and preset loader both depend on, so pin it.
    spec.insert (spec.begin() + 2, { gootar::BlockType::Model, "model-2", true });
    engine.setChain (spec);
    runBlocks (engine, tone);
    check (engine.numModelSlots() == 2, "a second model slot can be added");
    check (engine.modelInfo (1).loaded,
           "the original model is now slot 1, and kept its capture");
    check (! engine.modelInfo (0).loaded, "the newly inserted slot 0 starts empty");

    if (models.size() > 1)
    {
        check (engine.loadModel (0, models[1], err), "loads a capture into the new slot 0");
        const auto out = runBlocks (engine, tone);
        engine.collectGarbage();
        check (allFinite (out) && rms (out, kBlock * 4) > 1e-6,
               "two models in series produce finite audio");
        check (engine.modelInfo (0).loaded && engine.modelInfo (1).loaded,
               "both slots report loaded");
    }
}

} // namespace

int main()
{
    std::printf ("Gootar engine tests (%.0f Hz, %d-sample blocks)\n", kSampleRate, kBlock);

    testChainWithoutModel();
    testInputAndOutputLevels();
    testToneStack();
    testDcBlocker();
    testNoiseGate();
    testEachBundledModel();
    testHotSwapUnderLoad();
    testPitchDetection();
    testChainEditing();

    std::printf ("\n%s\n", failures == 0 ? "ALL PASSED" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}

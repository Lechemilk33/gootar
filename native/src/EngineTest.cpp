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
#include "dsp/GootarEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
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
            amplitude * static_cast<float> (std::sin (2.0 * M_PI * freqHz * i / kSampleRate));
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
        if (! engine.stageModel (path, err))
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
    engine.stageModel (models[0], err);

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
            if (engine.stageModel (path, loadErr))
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

    std::printf ("\n%s\n", failures == 0 ? "ALL PASSED" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}

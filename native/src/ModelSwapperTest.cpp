/**
 * Stress test for ModelSwapper.
 *
 * No JUCE, no NeuralAudio — this compiles today and proves the one piece of
 * the native design that is genuinely hard to get right. Run it under
 * -fsanitize=thread,address to check the claims rather than trust them.
 */
#include "ModelSwapper.h"
#include "TestPlatform.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

namespace
{
std::atomic<int> liveModels { 0 };
std::atomic<int> destroyed { 0 };

/** Stands in for a loaded NAM model: expensive to build, must not be freed
    on the audio thread. */
struct FakeModel
{
    explicit FakeModel (int id_) : id (id_) { liveModels.fetch_add (1); }
    ~FakeModel()
    {
        liveModels.fetch_sub (1);
        destroyed.fetch_add (1);
    }

    float process (float x) const noexcept { return x * gain; }

    int id;
    float gain = 0.5f;
};
} // namespace

int main()
{
    silenceCrashDialogs();
    using namespace std::chrono;

    constexpr int kLoads = 20000;
    std::atomic<bool> loaderDone { false };
    std::atomic<long long> blocks { 0 };
    std::atomic<long long> swaps { 0 };
    std::atomic<double> sink { 0.0 };

    // Scoped so the swapper is destroyed before we check the books: the model
    // still installed as `active` is legitimately alive until then, and its
    // destructor is part of what we are testing.
    {
    gootar::ModelSwapper<FakeModel> swapper;

    // Loader thread: behaves like a user spinning through a model list.
    std::thread loader ([&] {
        for (int i = 0; i < kLoads; ++i)
        {
            swapper.stage (std::make_unique<FakeModel> (i));
            if ((i % 8) == 0)
                swapper.collectRetired();
        }
        loaderDone.store (true);
    });

    // Audio thread: fixed-size blocks, must never touch the allocator.
    std::thread audio ([&] {
        float buffer[64] {};
        while (! loaderDone.load() || swapper.hasStaged())
        {
            if (swapper.applyStaged())
                swaps.fetch_add (1);

            if (auto* m = swapper.current())
            {
                double acc = 0.0;
                for (float& s : buffer)
                {
                    s = m->process (0.25f);
                    acc += s;
                }
                sink.store (acc);
            }
            blocks.fetch_add (1);
        }
        // Drain anything staged after the loop condition was last checked.
        while (swapper.applyStaged())
            swaps.fetch_add (1);
    });

    loader.join();
    audio.join();
    swapper.collectRetired();
    } // swapper destroyed here

    std::printf ("blocks processed : %lld\n", blocks.load());
    std::printf ("models staged    : %d\n", kLoads);
    std::printf ("swaps applied    : %lld\n", swaps.load());
    std::printf ("models destroyed : %d\n", destroyed.load());
    std::printf ("still live       : %d\n", liveModels.load());

    // The swapper destroys coalesced models on the loader thread, so every
    // staged model must eventually be destroyed exactly once, and nothing may
    // outlive the swapper.
    const int live = liveModels.load();
    if (live != 0)
    {
        std::printf ("FAIL: %d models leaked\n", live);
        return 1;
    }
    if (destroyed.load() != kLoads)
    {
        std::printf ("FAIL: destroyed %d of %d models\n", destroyed.load(), kLoads);
        return 1;
    }
    if (swaps.load() == 0)
    {
        std::printf ("FAIL: no swap ever reached the audio thread\n");
        return 1;
    }

    std::printf ("OK\n");
    return 0;
}

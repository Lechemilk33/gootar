#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

#include "../Block.h"

namespace gootar {

/**
 * Shared base for the pedals.
 *
 * Parameters are atomics written by the UI thread and read by the audio
 * thread. Plain floats would work in practice on x86 and be a data race on
 * paper; atomics cost nothing measurable here and mean the sanitiser stays
 * quiet, which is worth more than the nanoseconds.
 *
 * Every pedal owns one mono output buffer, sized in prepare() and never
 * touched again, so process() allocates nothing.
 */
class EffectBlock : public Block
{
public:
    explicit EffectBlock (std::string id) : Block (std::move (id)) {}

    void prepare (double sampleRateHz, int maxBlockSize) override
    {
        rate = sampleRateHz;
        maxFrames = std::max (1, maxBlockSize);
        out.assign (static_cast<size_t> (maxFrames), 0.0);
        outPtr.assign (1, out.data());
        prepareImpl();
    }

    /** 0..1 mix between the untouched input and the processed signal. */
    void setMix (float value) noexcept { mix.store (clamp01 (value), std::memory_order_relaxed); }
    float getMix() const noexcept { return mix.load (std::memory_order_relaxed); }

    /**
     * What knobs this pedal has.
     *
     * The UI renders whatever comes back rather than knowing about specific
     * effects, so adding a pedal is one new file and one line in the registry
     * - no UI work at all. That is the difference between a rig you can extend
     * and one where every new idea means touching five places.
     */
    struct ParamInfo
    {
        const char* key;
        const char* label;
        float min, max, step;
        const char* suffix = "";
    };

    virtual std::vector<ParamInfo> params() const = 0;
    virtual float getParam (const std::string& key) const = 0;
    virtual void  setParam (const std::string& key, float value) = 0;

    /** Every pedal has a mix; declaring it once here saves repeating it. */
    static ParamInfo mixParam() { return { "mix", "Mix", 0.0f, 1.0f, 0.01f }; }

protected:
    virtual void prepareImpl() {}

    static float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }

    /** One-pole coefficient for a given time constant, for envelopes. */
    double onePole (double seconds) const noexcept
    {
        return seconds <= 0.0 ? 0.0 : std::exp (-1.0 / (seconds * rate));
    }

    double rate = 48000.0;
    int    maxFrames = 0;
    std::vector<DSP_SAMPLE>  out;
    std::vector<DSP_SAMPLE*> outPtr;
    std::atomic<float> mix { 1.0f };
};

} // namespace gootar

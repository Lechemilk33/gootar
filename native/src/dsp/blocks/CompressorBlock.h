#pragma once

#include "EffectBlock.h"

namespace gootar {

/**
 * Feed-forward compressor with a soft knee.
 *
 * In front of a capture this does what a compressor in front of a real amp
 * does: evens out picking so quiet notes still push the amp into breakup.
 * After it, it tames the output. Both are useful, which is why it is a block
 * you can put anywhere rather than a fixed stage.
 *
 * Detection is on the rectified signal with separate attack and release, and
 * the gain computation happens in dB - the arithmetic is simpler there and,
 * more to the point, a ratio means what people expect it to mean.
 */
class CompressorBlock : public EffectBlock
{
public:
    explicit CompressorBlock (std::string id) : EffectBlock (std::move (id)) {}

    BlockType type() const noexcept override { return BlockType::Compressor; }
    const char* displayName() const noexcept override { return "Comp"; }

    void setThresholdDb (float v) noexcept { threshold.store (std::clamp (v, -60.0f, 0.0f), std::memory_order_relaxed); }
    void setRatio (float v) noexcept       { ratio.store (std::clamp (v, 1.0f, 20.0f), std::memory_order_relaxed); }
    void setAttackMs (float v) noexcept    { attackMs.store (std::clamp (v, 0.1f, 100.0f), std::memory_order_relaxed); }
    void setReleaseMs (float v) noexcept   { releaseMs.store (std::clamp (v, 10.0f, 1000.0f), std::memory_order_relaxed); }
    void setMakeupDb (float v) noexcept    { makeup.store (std::clamp (v, -12.0f, 24.0f), std::memory_order_relaxed); }

    float getThresholdDb() const noexcept { return threshold.load (std::memory_order_relaxed); }
    float getRatio() const noexcept       { return ratio.load (std::memory_order_relaxed); }
    float getAttackMs() const noexcept    { return attackMs.load (std::memory_order_relaxed); }
    float getReleaseMs() const noexcept   { return releaseMs.load (std::memory_order_relaxed); }
    float getMakeupDb() const noexcept    { return makeup.load (std::memory_order_relaxed); }

    /** How much it is pulling down right now, in dB. For a meter. */
    float gainReductionDb() const noexcept { return reduction.load (std::memory_order_relaxed); }

    void reset() noexcept override { envelope = 0.0; }

    std::vector<ParamInfo> params() const override
    {
        return { { "threshold", "Thresh",  -60.0f, 0.0f,   0.5f, " dB" },
                 { "ratio",     "Ratio",     1.0f, 20.0f,  0.1f, ":1" },
                 { "attack",    "Attack",    0.1f, 100.0f, 0.1f, " ms" },
                 { "release",   "Release",  10.0f, 1000.0f, 1.0f, " ms" },
                 { "makeup",    "Makeup",  -12.0f, 24.0f,  0.1f, " dB" } };
    }

    float getParam (const std::string& key) const override
    {
        if (key == "threshold") return getThresholdDb();
        if (key == "ratio")     return getRatio();
        if (key == "attack")    return getAttackMs();
        if (key == "release")   return getReleaseMs();
        if (key == "makeup")    return getMakeupDb();
        if (key == "mix")       return getMix();
        return 0.0f;
    }

    void setParam (const std::string& key, float v) override
    {
        if (key == "threshold") setThresholdDb (v);
        else if (key == "ratio")   setRatio (v);
        else if (key == "attack")  setAttackMs (v);
        else if (key == "release") setReleaseMs (v);
        else if (key == "makeup")  setMakeupDb (v);
        else if (key == "mix")     setMix (v);
    }


    DSP_SAMPLE** process (DSP_SAMPLE** input, int, int numFrames) noexcept override
    {
        const int n = std::min (numFrames, maxFrames);

        const double thr = threshold.load (std::memory_order_relaxed);
        const double rat = ratio.load (std::memory_order_relaxed);
        const double wet = mix.load (std::memory_order_relaxed);
        const double gain = dbToGain (makeup.load (std::memory_order_relaxed));

        const double att = onePole (attackMs.load (std::memory_order_relaxed) / 1000.0);
        const double rel = onePole (releaseMs.load (std::memory_order_relaxed) / 1000.0);

        constexpr double knee = 6.0;   // dB, soft either side of the threshold
        double worst = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const double dry = input[0][i];
            const double rectified = std::abs (dry);

            // Attack when the signal is rising, release when it is falling.
            const double coeff = rectified > envelope ? att : rel;
            envelope = rectified + coeff * (envelope - rectified);

            // -100 dB floor: log(0) is not a number the audio thread enjoys.
            const double levelDb = envelope > 1e-9
                                     ? 20.0 * std::log10 (envelope)
                                     : -100.0;

            const double over = levelDb - thr;
            double reductionDb = 0.0;
            if (over >= knee * 0.5)
                reductionDb = over - over / rat;
            else if (over > -knee * 0.5)
            {
                // Quadratic knee, so compression eases in instead of switching
                // on - the difference between musical and obviously processed.
                const double x = over + knee * 0.5;
                reductionDb = (1.0 - 1.0 / rat) * x * x / (2.0 * knee);
            }

            worst = std::max (worst, reductionDb);
            const double processed = dry * dbToGain (-reductionDb) * gain;
            out[static_cast<size_t> (i)] = dry + wet * (processed - dry);
        }

        reduction.store (static_cast<float> (worst), std::memory_order_relaxed);
        return outPtr.data();
    }

private:
    std::atomic<float> threshold { -18.0f };
    std::atomic<float> ratio     { 4.0f };
    std::atomic<float> attackMs  { 5.0f };
    std::atomic<float> releaseMs { 120.0f };
    std::atomic<float> makeup    { 0.0f };
    std::atomic<float> reduction { 0.0f };

    double envelope = 0.0;
};

} // namespace gootar

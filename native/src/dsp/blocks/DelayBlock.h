#pragma once

#include "EffectBlock.h"

namespace gootar {

/**
 * Delay with feedback.
 *
 * The delay time is smoothed rather than applied instantly. Jumping the read
 * position produces a click, and sweeping it produces the tape-style pitch
 * bend you actually want when you turn the knob while it is running.
 *
 * Feedback is capped below 1.0 because a runaway feedback loop through
 * headphones is genuinely unpleasant, and a pedal that can do it is a pedal
 * you eventually do it with by accident.
 */
class DelayBlock : public EffectBlock
{
public:
    explicit DelayBlock (std::string id) : EffectBlock (std::move (id)) {}

    BlockType type() const noexcept override { return BlockType::Delay; }
    const char* displayName() const noexcept override { return "Delay"; }

    void setTimeMs (float v) noexcept   { timeMs.store (std::clamp (v, 10.0f, 2000.0f), std::memory_order_relaxed); }
    void setFeedback (float v) noexcept { feedback.store (std::clamp (v, 0.0f, 0.95f), std::memory_order_relaxed); }

    float getTimeMs() const noexcept   { return timeMs.load (std::memory_order_relaxed); }
    float getFeedback() const noexcept { return feedback.load (std::memory_order_relaxed); }

    void reset() noexcept override
    {
        std::fill (line.begin(), line.end(), 0.0);
        writePos = 0;
        smoothedDelay = timeMs.load (std::memory_order_relaxed) * rate / 1000.0;
    }


    std::vector<ParamInfo> params() const override
    {
        return { { "time",     "Time",     10.0f, 2000.0f, 1.0f, " ms" },
                 { "feedback", "Feedback",  0.0f, 0.95f,   0.01f },
                 mixParam() };
    }

    float getParam (const std::string& key) const override
    {
        if (key == "time")     return getTimeMs();
        if (key == "feedback") return getFeedback();
        if (key == "mix")      return getMix();
        return 0.0f;
    }

    void setParam (const std::string& key, float v) override
    {
        if (key == "time")          setTimeMs (v);
        else if (key == "feedback") setFeedback (v);
        else if (key == "mix")      setMix (v);
    }

    DSP_SAMPLE** process (DSP_SAMPLE** input, int, int numFrames) noexcept override
    {
        if (line.empty())
            return input;

        const int n = std::min (numFrames, maxFrames);
        const double target = timeMs.load (std::memory_order_relaxed) * rate / 1000.0;
        const double fb = feedback.load (std::memory_order_relaxed);
        const double wet = mix.load (std::memory_order_relaxed);
        const auto size = static_cast<double> (line.size());

        for (int i = 0; i < n; ++i)
        {
            // Glide towards the new time over roughly 50 ms.
            smoothedDelay += (target - smoothedDelay) * smoothing;

            double readPos = static_cast<double> (writePos) - smoothedDelay;
            while (readPos < 0.0)
                readPos += size;

            // Linear interpolation: the read position is fractional while the
            // time is moving, and rounding it would grind.
            const auto i0 = static_cast<size_t> (readPos);
            const size_t i1 = (i0 + 1) % line.size();
            const double frac = readPos - static_cast<double> (i0);
            const double delayed = line[i0] + frac * (line[i1] - line[i0]);

            const double dry = input[0][i];
            line[static_cast<size_t> (writePos)] = dry + delayed * fb;
            writePos = (writePos + 1) % static_cast<int> (line.size());

            out[static_cast<size_t> (i)] = dry + wet * delayed;
        }
        return outPtr.data();
    }

protected:
    void prepareImpl() override
    {
        // Two seconds of headroom, plus one sample so the interpolator's
        // second tap never wraps onto the value it is about to overwrite.
        line.assign (static_cast<size_t> (rate * 2.0) + 2, 0.0);
        smoothing = 1.0 - std::exp (-1.0 / (0.05 * rate));
        reset();
    }

private:
    std::atomic<float> timeMs   { 350.0f };
    std::atomic<float> feedback { 0.35f };

    std::vector<DSP_SAMPLE> line;
    int    writePos = 0;
    double smoothedDelay = 0.0;
    double smoothing = 0.0;
};

} // namespace gootar

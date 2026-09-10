#pragma once

#include <array>

#include "EffectBlock.h"

namespace gootar {

/**
 * Schroeder/Freeverb-style reverb: parallel damped comb filters into a chain
 * of allpasses.
 *
 * Chosen because it is cheap, well understood, and sounds like a room rather
 * than a metal box. The comb tunings are Freeverb's, scaled from the 44.1 kHz
 * they were designed at to whatever rate we are running - using them unscaled
 * at 48 k is a common mistake that shifts the whole character.
 *
 * The mutually-prime delay lengths are the point: they stop the comb filters
 * lining up and turning the tail into a flutter.
 */
class ReverbBlock : public EffectBlock
{
public:
    explicit ReverbBlock (std::string id) : EffectBlock (std::move (id))
    {
        setMix (0.25f); // a reverb defaulting to fully wet is nobody's friend
    }

    BlockType type() const noexcept override { return BlockType::Reverb; }
    const char* displayName() const noexcept override { return "Verb"; }

    /** 0..1: short room through long hall. */
    void setSize (float v) noexcept    { size.store (std::clamp (v, 0.0f, 1.0f), std::memory_order_relaxed); }
    /** 0..1: how fast the top end decays. Higher is darker. */
    void setDamping (float v) noexcept { damping.store (std::clamp (v, 0.0f, 1.0f), std::memory_order_relaxed); }

    float getSize() const noexcept    { return size.load (std::memory_order_relaxed); }
    float getDamping() const noexcept { return damping.load (std::memory_order_relaxed); }

    void reset() noexcept override
    {
        for (auto& c : combs) c.reset();
        for (auto& a : allpasses) a.reset();
    }


    std::vector<ParamInfo> params() const override
    {
        return { { "size",    "Size",    0.0f, 1.0f, 0.01f },
                 { "damping", "Damping", 0.0f, 1.0f, 0.01f },
                 mixParam() };
    }

    float getParam (const std::string& key) const override
    {
        if (key == "size")    return getSize();
        if (key == "damping") return getDamping();
        if (key == "mix")     return getMix();
        return 0.0f;
    }

    void setParam (const std::string& key, float v) override
    {
        if (key == "size")         setSize (v);
        else if (key == "damping") setDamping (v);
        else if (key == "mix")     setMix (v);
    }

    DSP_SAMPLE** process (DSP_SAMPLE** input, int, int numFrames) noexcept override
    {
        const int n = std::min (numFrames, maxFrames);
        const double wet = mix.load (std::memory_order_relaxed);
        // 0..1 maps to a feedback range that stays short of self-oscillation.
        const double feedback = 0.70 + 0.28 * size.load (std::memory_order_relaxed);
        const double damp = 0.20 + 0.60 * damping.load (std::memory_order_relaxed);

        for (int i = 0; i < n; ++i)
        {
            const double dry = input[0][i];
            const double in = dry * 0.015; // Freeverb's input scaling

            double acc = 0.0;
            for (auto& c : combs)
                acc += c.process (in, feedback, damp);

            for (auto& a : allpasses)
                acc = a.process (acc);

            out[static_cast<size_t> (i)] = dry + wet * acc;
        }
        return outPtr.data();
    }

protected:
    void prepareImpl() override
    {
        // Freeverb's tunings, designed at 44.1 kHz.
        static constexpr int combTuning[]    = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static constexpr int allpassTuning[] = { 556, 441, 341, 225 };

        const double scale = rate / 44100.0;
        for (size_t i = 0; i < combs.size(); ++i)
            combs[i].prepare (static_cast<int> (combTuning[i] * scale));
        for (size_t i = 0; i < allpasses.size(); ++i)
            allpasses[i].prepare (static_cast<int> (allpassTuning[i] * scale));
    }

private:
    /** Comb filter with a one-pole low-pass in the feedback path. */
    struct Comb
    {
        std::vector<double> buffer;
        int pos = 0;
        double store = 0.0;

        void prepare (int length)
        {
            buffer.assign (static_cast<size_t> (std::max (1, length)), 0.0);
            reset();
        }
        void reset() noexcept
        {
            std::fill (buffer.begin(), buffer.end(), 0.0);
            pos = 0;
            store = 0.0;
        }
        double process (double in, double feedback, double damp) noexcept
        {
            const double output = buffer[static_cast<size_t> (pos)];
            // Damping lives here: each pass through the loop loses a little
            // top, which is what makes a tail decay like a room rather than
            // ring like a comb.
            store = output * (1.0 - damp) + store * damp;
            buffer[static_cast<size_t> (pos)] = in + store * feedback;
            pos = (pos + 1) % static_cast<int> (buffer.size());
            return output;
        }
    };

    struct Allpass
    {
        std::vector<double> buffer;
        int pos = 0;

        void prepare (int length)
        {
            buffer.assign (static_cast<size_t> (std::max (1, length)), 0.0);
            reset();
        }
        void reset() noexcept
        {
            std::fill (buffer.begin(), buffer.end(), 0.0);
            pos = 0;
        }
        double process (double in) noexcept
        {
            constexpr double g = 0.5;
            const double buffered = buffer[static_cast<size_t> (pos)];
            const double output = -in + buffered;
            buffer[static_cast<size_t> (pos)] = in + buffered * g;
            pos = (pos + 1) % static_cast<int> (buffer.size());
            return output;
        }
    };

    std::atomic<float> size    { 0.5f };
    std::atomic<float> damping { 0.5f };

    std::array<Comb, 8>    combs;
    std::array<Allpass, 4> allpasses;
};

} // namespace gootar

#pragma once

#include <numbers>

#include "EffectBlock.h"

namespace gootar {

/**
 * Overdrive / clean boost, meant to sit IN FRONT of a capture.
 *
 * This is the most useful pedal to have with NAM, and the reason is worth
 * knowing: a capture models one amp at one setting. You cannot turn its gain
 * knob up. What you CAN do is hit it harder, exactly as you would with a real
 * pedal in front of a real amp - and because the capture responds to input
 * level the way the original amp did, that genuinely works.
 *
 * Shape follows the tube-screamer idea rather than a fuzz:
 *
 *   tighten (high-pass) -> gain -> asymmetric soft clip -> tone tilt -> level
 *
 * The high-pass comes first on purpose. Without it the low end saturates
 * before the mids do, which is the sound of a bad plugin: flubby and
 * indistinct. Every good boost pedal thins the bass before clipping it.
 */
class DriveBlock : public EffectBlock
{
public:
    explicit DriveBlock (std::string id) : EffectBlock (std::move (id)) {}

    BlockType type() const noexcept override { return BlockType::Drive; }
    const char* displayName() const noexcept override { return "Drive"; }

    /** 0..10, like a pedal knob. Near 0 is a clean boost. */
    void setDrive (float v) noexcept   { drive.store (std::clamp (v, 0.0f, 10.0f), std::memory_order_relaxed); }
    /** 0..10; 5 is flat, below tilts dark, above tilts bright. */
    void setTone (float v) noexcept    { tone.store (std::clamp (v, 0.0f, 10.0f), std::memory_order_relaxed); }
    /** Output trim in dB. */
    void setLevelDb (float v) noexcept { level.store (std::clamp (v, -24.0f, 24.0f), std::memory_order_relaxed); }

    float getDrive() const noexcept   { return drive.load (std::memory_order_relaxed); }
    float getTone() const noexcept    { return tone.load (std::memory_order_relaxed); }
    float getLevelDb() const noexcept { return level.load (std::memory_order_relaxed); }

    void reset() noexcept override { hpState = 0.0; lowState = 0.0; }

    std::vector<ParamInfo> params() const override
    {
        return { { "drive", "Drive", 0.0f, 10.0f, 0.1f },
                 { "tone",  "Tone",  0.0f, 10.0f, 0.1f },
                 { "level", "Level", -24.0f, 24.0f, 0.1f, " dB" } };
    }

    float getParam (const std::string& key) const override
    {
        if (key == "drive") return getDrive();
        if (key == "tone")  return getTone();
        if (key == "level") return getLevelDb();
        if (key == "mix")   return getMix();
        return 0.0f;
    }

    void setParam (const std::string& key, float v) override
    {
        if (key == "drive") setDrive (v);
        else if (key == "tone")  setTone (v);
        else if (key == "level") setLevelDb (v);
        else if (key == "mix")   setMix (v);
    }


    DSP_SAMPLE** process (DSP_SAMPLE** input, int, int numFrames) noexcept override
    {
        const int n = std::min (numFrames, maxFrames);

        const double d = drive.load (std::memory_order_relaxed);
        const double t = tone.load (std::memory_order_relaxed) / 10.0;   // 0..1
        const double wet = mix.load (std::memory_order_relaxed);

        // 0..10 maps to 0..30 dB into the clipper.
        const double gain = dbToGain (d * 3.0);
        // Back most of that gain out again, so turning drive up changes the
        // character rather than just getting louder.
        const double makeup = dbToGain (level.load (std::memory_order_relaxed) - d * 1.6);

        // Tilt: split into low and high around 900 Hz and cross-fade their
        // weights. Flat at tone = 5 because both weights land on 1.0.
        const double lowGain  = 1.7 - 1.4 * t;
        const double highGain = 0.3 + 1.4 * t;

        for (int i = 0; i < n; ++i)
        {
            const double dry = input[0][i];

            // Tighten: ~120 Hz high-pass before anything clips.
            hpState += (1.0 - hpCoeff) * (dry - hpState);
            const double tightened = dry - hpState;

            const double driven = tightened * gain;

            // Asymmetric soft clip: slightly harder on the negative half, the
            // way a real diode pair behaves. Perfectly symmetric clipping
            // generates only odd harmonics and sounds sterile.
            const double shaped = driven >= 0.0
                                    ? std::tanh (driven)
                                    : std::tanh (driven * 1.18) / 1.18;

            lowState += (1.0 - tiltCoeff) * (shaped - lowState);
            const double high = shaped - lowState;
            const double processed = (lowState * lowGain + high * highGain) * makeup;

            out[static_cast<size_t> (i)] = dry + wet * (processed - dry);
        }
        return outPtr.data();
    }

protected:
    void prepareImpl() override
    {
        constexpr double twoPi = 2.0 * std::numbers::pi;
        hpCoeff   = std::exp (-twoPi * 120.0 / rate);
        tiltCoeff = std::exp (-twoPi * 900.0 / rate);
        reset();
    }

private:
    std::atomic<float> drive { 3.0f };
    std::atomic<float> tone  { 5.0f };
    std::atomic<float> level { 0.0f };

    double hpCoeff = 0.0, tiltCoeff = 0.0;
    double hpState = 0.0, lowState = 0.0;
};

} // namespace gootar

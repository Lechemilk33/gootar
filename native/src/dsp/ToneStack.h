#pragma once

#include "RecursiveLinearFilter.h"

namespace gootar {

/**
 * The stock plugin's "Basic NAM" tone stack: three peaking biquads in series.
 *
 * Frequencies, Q values and gain scalings are transcribed exactly from
 * dsp::tone_stack::BasicNamToneStack::SetParam in NeuralAmpModelerPlugin.
 * Getting any of these wrong means a preset dialled in here sounds different
 * in the stock plugin, which defeats the point of sharing a format.
 *
 *   bass    150 Hz,  Q 0.707,  gain = 4.0 * (v - 5)   -> +/- 20 dB
 *   mid     425 Hz,  Q 1.5 when cutting / 0.7 when boosting,
 *                    gain = 3.0 * (v - 5)             -> +/- 15 dB
 *   treble 1800 Hz,  Q 0.707,  gain = 2.0 * (v - 5)   -> +/- 10 dB
 *
 * The asymmetric mid Q is deliberate upstream: a wider bell on the boost so a
 * mid bump sounds less honky.
 */
class ToneStack
{
public:
    void prepare (double sampleRateHz)
    {
        sampleRate = sampleRateHz;
        // Force a coefficient refresh on the next process call.
        lastBass = lastMid = lastTreble = -1.0;
    }

    /** Recompute coefficients only when a knob actually moved. Pure
        arithmetic, so it is safe to call from the audio thread. */
    void setKnobs (double bass, double mid, double treble) noexcept
    {
        if (bass != lastBass)
        {
            const double gainDb = 4.0 * (bass - 5.0);
            recursive_linear_filter::BiquadParams p (sampleRate, 150.0, 0.707, gainDb);
            bassFilter.SetParams (p);
            lastBass = bass;
        }
        if (mid != lastMid)
        {
            const double gainDb = 3.0 * (mid - 5.0);
            const double q = gainDb < 0.0 ? 1.5 : 0.7;
            recursive_linear_filter::BiquadParams p (sampleRate, 425.0, q, gainDb);
            midFilter.SetParams (p);
            lastMid = mid;
        }
        if (treble != lastTreble)
        {
            const double gainDb = 2.0 * (treble - 5.0);
            recursive_linear_filter::BiquadParams p (sampleRate, 1800.0, 0.707, gainDb);
            trebleFilter.SetParams (p);
            lastTreble = treble;
        }
    }

    DSP_SAMPLE** process (DSP_SAMPLE** inputs, int numChannels, int numFrames)
    {
        auto** b = bassFilter.Process (inputs, numChannels, numFrames);
        auto** m = midFilter.Process (b, numChannels, numFrames);
        return trebleFilter.Process (m, numChannels, numFrames);
    }

private:
    recursive_linear_filter::Peaking bassFilter, midFilter, trebleFilter;
    double sampleRate = 48000.0;
    double lastBass = -1.0, lastMid = -1.0, lastTreble = -1.0;
};

} // namespace gootar

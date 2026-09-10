#include "PitchDetector.h"

#include <algorithm>
#include <cmath>

namespace gootar {

// --- AnalysisTap -----------------------------------------------------------

void AnalysisTap::prepare (int windowSize)
{
    size = std::max (256, windowSize);
    for (auto& slot : slots)
        slot.assign (static_cast<size_t> (size), 0.0f);
    latest.store (-1);
    writeSlot = 0;
    readSlot = 1;
    fill = 0;
}

void AnalysisTap::push (const float* samples, int numSamples) noexcept
{
    if (size == 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        slots[static_cast<size_t> (writeSlot)][static_cast<size_t> (fill++)] = samples[i];
        if (fill >= size)
        {
            fill = 0;
            // Publish this window and take whatever slot was there before.
            // Both sides only ever swap, so no slot is ever shared.
            writeSlot = latest.exchange (writeSlot, std::memory_order_acq_rel);
            if (writeSlot < 0)
                writeSlot = 2; // first publish: the third slot is still free
        }
    }
}

const float* AnalysisTap::readLatest() noexcept
{
    const int taken = latest.exchange (readSlot, std::memory_order_acq_rel);
    if (taken < 0)
    {
        // Nothing published yet; hand our slot back so the writer keeps one.
        latest.store (-1, std::memory_order_release);
        return nullptr;
    }
    readSlot = taken;
    return slots[static_cast<size_t> (readSlot)].data();
}

// --- PitchDetector ---------------------------------------------------------

void PitchDetector::prepare (double sampleRate, int windowSize)
{
    rate = sampleRate;
    // Guitar range, generously: ~70 Hz (below low E, so a flat string still
    // reads) up to ~1400 Hz (high E around the 12th fret).
    minLag = std::max (2, static_cast<int> (sampleRate / 1400.0));
    maxLag = std::min (windowSize / 2, static_cast<int> (sampleRate / 70.0));
    nsdf.assign (static_cast<size_t> (std::max (1, maxLag + 1)), 0.0);
}

PitchDetector::Result PitchDetector::analyse (const float* samples, int numSamples)
{
    Result result;
    if (samples == nullptr || numSamples <= 0 || maxLag <= minLag)
        return result;

    double energy = 0.0;
    for (int i = 0; i < numSamples; ++i)
        energy += static_cast<double> (samples[i]) * samples[i];
    result.rms = std::sqrt (energy / numSamples);

    // Below this there is nothing to hear, let alone tune.
    if (result.rms < 0.002)
        return result;

    // Normalised square difference: n'(t) = 2*r(t) / m(t)
    const int lagCount = std::min (maxLag, numSamples - 1);
    for (int lag = minLag; lag <= lagCount; ++lag)
    {
        double correlation = 0.0;
        double norm = 0.0;
        const int count = numSamples - lag;
        for (int i = 0; i < count; ++i)
        {
            const double a = samples[i];
            const double b = samples[i + lag];
            correlation += a * b;
            norm += a * a + b * b;
        }
        nsdf[static_cast<size_t> (lag)] = norm > 0.0 ? (2.0 * correlation / norm) : 0.0;
    }

    // Walk past the first negative region, then take the first key maximum
    // that clears a fraction of the best one. Taking the global maximum
    // instead is what makes naive autocorrelation report an octave too low.
    int lag = minLag;
    while (lag <= lagCount && nsdf[static_cast<size_t> (lag)] > 0.0)
        ++lag;

    double bestValue = 0.0;
    std::vector<std::pair<int, double>> peaks;
    for (; lag < lagCount; ++lag)
    {
        const double prev = nsdf[static_cast<size_t> (lag - 1)];
        const double here = nsdf[static_cast<size_t> (lag)];
        const double next = nsdf[static_cast<size_t> (lag + 1)];
        if (here > prev && here >= next && here > 0.0)
        {
            peaks.emplace_back (lag, here);
            bestValue = std::max (bestValue, here);
        }
    }

    if (peaks.empty() || bestValue <= 0.0)
        return result;

    constexpr double kPeakThreshold = 0.85; // fraction of the strongest peak
    int chosenLag = peaks.front().first;
    double chosenValue = peaks.front().second;
    for (const auto& [peakLag, value] : peaks)
        if (value >= kPeakThreshold * bestValue)
        {
            chosenLag = peakLag;
            chosenValue = value;
            break;
        }

    // Parabolic interpolation: the true peak rarely lands on a whole sample,
    // and without this a tuner reads in steps of several cents.
    double refinedLag = chosenLag;
    if (chosenLag > 0 && chosenLag + 1 <= lagCount)
    {
        const double y0 = nsdf[static_cast<size_t> (chosenLag - 1)];
        const double y1 = nsdf[static_cast<size_t> (chosenLag)];
        const double y2 = nsdf[static_cast<size_t> (chosenLag + 1)];
        const double denom = 2.0 * (2.0 * y1 - y0 - y2);
        if (std::abs (denom) > 1e-12)
            refinedLag = chosenLag + (y2 - y0) / denom;
    }

    if (refinedLag <= 0.0)
        return result;

    result.frequencyHz = rate / refinedLag;
    result.clarity = std::clamp (chosenValue, 0.0, 1.0);

    // Unconvincing periodicity is worse than no reading: a tuner that wobbles
    // between two notes while a chord rings out is not usable.
    if (result.clarity < 0.6 || result.frequencyHz < 60.0 || result.frequencyHz > 1600.0)
        return result;

    const double midi = 69.0 + 12.0 * std::log2 (result.frequencyHz / 440.0);
    result.midiNote = static_cast<int> (std::lround (midi));
    result.cents = (midi - result.midiNote) * 100.0;
    result.voiced = true;
    return result;
}

std::string PitchDetector::noteName (int midiNote)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                   "F#", "G", "G#", "A", "A#", "B" };
    if (midiNote < 0 || midiNote > 127)
        return {};
    const int octave = midiNote / 12 - 1;
    return std::string (names[midiNote % 12]) + std::to_string (octave);
}

std::string PitchDetector::nearestOpenString (int midiNote)
{
    // Standard tuning, low to high: E2 A2 D3 G3 B3 E4
    static const std::pair<int, const char*> strings[] = {
        { 40, "6th (E)" }, { 45, "5th (A)" }, { 50, "4th (D)" },
        { 55, "3rd (G)" }, { 59, "2nd (B)" }, { 64, "1st (E)" },
    };
    for (const auto& [note, label] : strings)
        if (std::abs (midiNote - note) <= 1)
            return label;
    return {};
}

} // namespace gootar

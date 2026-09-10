#pragma once

#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace gootar {

/**
 * Hands recent audio from the audio thread to an analyser on another thread,
 * without locks and without the analyser ever seeing a half-written window.
 *
 * Triple buffering: the writer owns one slot, the reader owns one, and one
 * holds the most recent complete window. Each side swaps its slot with that
 * middle one atomically, so neither ever blocks and neither ever touches a
 * slot the other is using. A tuner does not care about dropping windows when
 * the reader is slow - it cares about never analysing garbage.
 */
class AnalysisTap
{
public:
    void prepare (int windowSize);

    /** [Audio thread] Feed samples in. Never allocates, never blocks. */
    void push (const float* samples, int numSamples) noexcept;

    /**
     * [Any other thread] Swap in the newest complete window.
     * @return null until at least one window has been filled.
     */
    const float* readLatest() noexcept;

    int windowSize() const noexcept { return size; }

private:
    std::array<std::vector<float>, 3> slots;
    std::atomic<int> latest { -1 };
    int writeSlot = 0;
    int readSlot = 1;
    int fill = 0;
    int size = 0;
};

/**
 * Monophonic pitch detection via the McLeod Pitch Method (normalised square
 * difference), with parabolic interpolation around the chosen peak.
 *
 * MPM over plain autocorrelation because autocorrelation happily locks onto an
 * octave below on a guitar's low strings, which makes a tuner useless exactly
 * where tuning is hardest.
 *
 * IMPORTANT: feed this the CLEAN signal, before the model.
 * Distortion piles on harmonics and squashes the dynamics a pitch tracker
 * relies on - the same reason the noise gate detects pre-model and applies
 * post. Analysing the amp output would give you a confidently wrong note.
 */
class PitchDetector
{
public:
    struct Result
    {
        bool   voiced = false;   ///< false when nothing convincing was found
        double frequencyHz = 0.0;
        double clarity = 0.0;    ///< 0..1, how periodic the window looked
        int    midiNote = 0;
        double cents = 0.0;      ///< signed offset from the nearest note
        double rms = 0.0;
    };

    void prepare (double sampleRate, int windowSize);

    /** Analyse one window. Not real-time safe; call from a worker or timer. */
    Result analyse (const float* samples, int numSamples);

    /** e.g. "A2", "F#4". Empty for an unvoiced result. */
    static std::string noteName (int midiNote);

    /** Which open guitar string a note is nearest, or empty. Standard tuning. */
    static std::string nearestOpenString (int midiNote);

private:
    double rate = 48000.0;
    int    minLag = 0, maxLag = 0;
    std::vector<double> nsdf;
};

} // namespace gootar

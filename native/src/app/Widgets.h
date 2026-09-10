#pragma once

#include <JuceHeader.h>

#include "../dsp/GootarEngine.h"
#include "Theme.h"

namespace gootar {

/**
 * Input/output level, drawn as a thin bar with a falling peak hold.
 *
 * The point is not precision metering. It is answering "is my interface
 * actually sending signal, and am I clipping" without leaving the app - the
 * two questions that account for most of the time lost setting a rig up.
 */
class LevelMeter : public juce::Component,
                   private juce::Timer
{
public:
    explicit LevelMeter (const juce::String& caption);
    ~LevelMeter() override;

    /** Feed a linear peak (0..1+). Called from the message thread. */
    void setLevel (float linearPeak);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    juce::String label;
    float current = 0.0f;
    float peak = 0.0f;
    int   peakHoldFrames = 0;
};

/**
 * The tuner.
 *
 * Shows the note big, and the cents offset as a bar either side of centre.
 * Nothing else - a tuner that needs reading rather than glancing at is a tuner
 * you stop using.
 *
 * Colour carries the verdict: green inside +/-5 cents, amber inside 20, red
 * beyond. That threshold is deliberate; a guitar is not going to hold better
 * than a few cents, and a tuner that demands perfection just never goes green.
 */
class TunerDisplay : public juce::Component
{
public:
    void setReading (const PitchReading&);
    void paint (juce::Graphics&) override;

private:
    PitchReading reading;
    /** Smoothed so the bar glides instead of twitching between windows. */
    double smoothedCents = 0.0;
    bool   hasSmoothed = false;
};

/**
 * The signal chain, drawn as a row of pills in signal order.
 *
 * Worth the space because the chain is now editable: when a preset can put the
 * tone stack before the model, or run two captures in series, "what is
 * actually in the path right now" stops being obvious.
 */
class ChainStrip : public juce::Component
{
public:
    struct Entry
    {
        juce::String label;
        bool enabled = true;
        bool loaded = true;   ///< false = a slot with nothing in it yet
    };

    void setEntries (juce::Array<Entry>);
    void paint (juce::Graphics&) override;

private:
    juce::Array<Entry> entries;
};

/**
 * Key/value details for whatever is loaded.
 *
 * Fills what would otherwise be dead space under the chain, with the things
 * you actually want when deciding whether a capture is worth keeping:
 * architecture, the rate it was trained at, and how it will be run.
 */
class InfoPanel : public juce::Component
{
public:
    struct Row { juce::String key, value; bool warn = false; };

    void setTitle (const juce::String&);
    void setRows (juce::Array<Row>);
    void paint (juce::Graphics&) override;

private:
    juce::String title { "CAPTURE" };
    juce::Array<Row> rows;
};

} // namespace gootar

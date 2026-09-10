#include "Widgets.h"

namespace gootar {

// --- LevelMeter ------------------------------------------------------------

LevelMeter::LevelMeter (const juce::String& caption) : label (caption)
{
    startTimerHz (30);
}

LevelMeter::~LevelMeter() { stopTimer(); }

void LevelMeter::setLevel (float linearPeak)
{
    current = juce::jlimit (0.0f, 2.0f, linearPeak);
    if (current >= peak)
    {
        peak = current;
        peakHoldFrames = 25; // ~0.8 s at 30 Hz
    }
}

void LevelMeter::timerCallback()
{
    // Decay towards the current level rather than snapping, so a meter that is
    // only updated once per audio buffer still reads smoothly.
    current *= 0.82f;
    if (peakHoldFrames > 0) --peakHoldFrames;
    else peak *= 0.90f;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (theme::textDim);
    g.setFont (theme::font (10.0f));
    g.drawText (label, bounds.removeFromTop (12), juce::Justification::centredLeft, false);

    const auto bar = bounds.reduced (0, 2).toFloat();
    g.setColour (theme::surfaceHigh);
    g.fillRoundedRectangle (bar, 3.0f);

    // dBFS across the bar, floored at -60: linear would put everything you
    // actually play into the top sliver.
    const auto toX = [&bar] (float linear)
    {
        const float db = juce::Decibels::gainToDecibels (linear, -60.0f);
        return juce::jmap (juce::jlimit (-60.0f, 6.0f, db), -60.0f, 6.0f,
                           bar.getX(), bar.getRight());
    };

    if (current > 0.0001f)
    {
        const float x = toX (current);
        g.setColour (current >= 0.99f ? theme::bad
                                      : (current > 0.7f ? theme::warn : theme::good));
        g.fillRoundedRectangle (bar.withRight (x), 3.0f);
    }

    if (peak > 0.0001f)
    {
        const float x = juce::jlimit (bar.getX(), bar.getRight() - 2.0f, toX (peak));
        g.setColour (peak >= 0.99f ? theme::bad : theme::text.withAlpha (0.7f));
        g.fillRect (x, bar.getY(), 2.0f, bar.getHeight());
    }
}

// --- TunerDisplay ----------------------------------------------------------

void TunerDisplay::setReading (const PitchReading& r)
{
    reading = r;
    if (r.voiced)
    {
        // A little smoothing: raw per-window readings jitter by a couple of
        // cents even on a steady note, which looks like the tuner is unsure.
        smoothedCents = hasSmoothed ? (smoothedCents * 0.6 + r.cents * 0.4) : r.cents;
        hasSmoothed = true;
    }
    else
    {
        hasSmoothed = false;
    }
    repaint();
}

void TunerDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().reduced (theme::pad, 8);

    g.setColour (theme::textDim);
    g.setFont (theme::font (10.0f, true));
    g.drawText ("TUNER", bounds.removeFromTop (13), juce::Justification::centredLeft, false);

    if (! reading.voiced)
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (15.0f));
        g.drawText ("play a note", bounds, juce::Justification::centred, false);
        return;
    }

    const double cents = smoothedCents;
    const auto verdict = std::abs (cents) <= 5.0  ? theme::good
                       : std::abs (cents) <= 20.0 ? theme::warn
                                                  : theme::bad;

    auto noteArea = bounds.removeFromTop (juce::jmax (34, bounds.getHeight() / 2));
    g.setColour (verdict);
    g.setFont (theme::font (34.0f, true));
    g.drawText (reading.noteName, noteArea, juce::Justification::centred, false);

    // Cents bar: centre line, a marker either side of it.
    auto barArea = bounds.removeFromTop (16).toFloat();
    g.setColour (theme::surfaceHigh);
    g.fillRoundedRectangle (barArea, 3.0f);

    const float centreX = barArea.getCentreX();
    g.setColour (theme::line.brighter (0.3f));
    g.fillRect (centreX - 0.5f, barArea.getY(), 1.0f, barArea.getHeight());

    const float clamped = (float) juce::jlimit (-50.0, 50.0, cents);
    const float markerX = centreX + (clamped / 50.0f) * (barArea.getWidth() * 0.5f - 4.0f);
    g.setColour (verdict);
    g.fillRoundedRectangle (juce::Rectangle<float> (5.0f, barArea.getHeight())
                                .withCentre ({ markerX, barArea.getCentreY() }), 2.0f);

    juce::String detail;
    detail << juce::String (reading.frequencyHz, 1) << " Hz   "
           << (cents >= 0 ? "+" : "") << juce::String (cents, 0) << "¢";
    if (reading.nearestString.length() > 0)
        detail << "   " << juce::String (reading.nearestString);

    g.setColour (theme::textDim);
    g.setFont (theme::font (11.0f));
    g.drawText (detail, bounds, juce::Justification::centred, false);
}

// --- ChainStrip ------------------------------------------------------------

void ChainStrip::setEntries (juce::Array<Entry> newEntries)
{
    entries = std::move (newEntries);
    repaint();
}

void ChainStrip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().reduced (theme::pad, 6);

    g.setColour (theme::textDim);
    g.setFont (theme::font (10.0f, true));
    g.drawText ("SIGNAL CHAIN", bounds.removeFromTop (13),
                juce::Justification::centredLeft, false);

    if (entries.isEmpty())
        return;

    // Fixed pill height: the panel may be taller than the strip needs, and a
    // block stretched to fill it reads as a fader, not a stage.
    auto row = bounds.removeFromTop (26);
    const int n = entries.size();

    // Fit to the width available rather than assuming it. With seven blocks in
    // a narrow column, fixed pill sizes overflow and the last block - usually
    // the output - silently disappears off the edge.
    int gap = 5, arrow = 8, pill = 0;
    for (const auto& attempt : { std::pair { 5, 8 }, std::pair { 3, 6 }, std::pair { 2, 4 } })
    {
        gap = attempt.first;
        arrow = attempt.second;
        const int spacing = (n - 1) * (arrow + gap * 2);
        pill = (row.getWidth() - spacing) / juce::jmax (1, n);
        if (pill >= 30)
            break;
    }
    pill = juce::jmax (20, pill);

    const int used = n * pill + (n - 1) * (arrow + gap * 2);
    int x = row.getX() + juce::jmax (0, (row.getWidth() - used) / 2);

    for (int i = 0; i < n; ++i)
    {
        const auto& e = entries.getReference (i);
        const juce::Rectangle<float> box ((float) x, (float) row.getY(),
                                          (float) pill, (float) row.getHeight());

        const auto colour = ! e.enabled ? theme::line
                          : e.loaded    ? theme::accent
                                        : theme::textDim;

        g.setColour (e.enabled && e.loaded ? theme::accentDim : theme::surfaceHigh);
        g.fillRoundedRectangle (box, (float) theme::radius - 3.0f);
        g.setColour (colour);
        g.drawRoundedRectangle (box, (float) theme::radius - 3.0f, 1.0f);

        g.setColour (e.enabled ? colour : theme::textDim.withAlpha (0.5f));
        g.setFont (theme::font (pill >= 34 ? 10.0f : 8.5f, true));
        g.drawText (e.label, box.toNearestInt(), juce::Justification::centred, false);

        x += pill;
        if (i + 1 < n)
        {
            g.setColour (theme::line.brighter (0.2f));
            const float cy = box.getCentreY();
            g.drawLine ((float) (x + gap), cy, (float) (x + gap + arrow), cy, 1.0f);
            x += arrow + gap * 2;
        }
    }
}

// --- InfoPanel -------------------------------------------------------------

void InfoPanel::setTitle (const juce::String& t)
{
    if (title != t) { title = t; repaint(); }
}

void InfoPanel::setRows (juce::Array<Row> newRows)
{
    rows = std::move (newRows);
    repaint();
}

void InfoPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().reduced (theme::pad, 8);

    g.setColour (theme::textDim);
    g.setFont (theme::font (10.0f, true));
    g.drawText (title, bounds.removeFromTop (13), juce::Justification::centredLeft, false);
    bounds.removeFromTop (4);

    if (rows.isEmpty())
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.0f));
        g.drawText ("nothing loaded", bounds.removeFromTop (20),
                    juce::Justification::centredLeft, false);
        return;
    }

    for (const auto& row : rows)
    {
        if (bounds.getHeight() < 18)
            break;
        auto line = bounds.removeFromTop (18);

        g.setColour (theme::textDim);
        g.setFont (theme::font (11.0f));
        g.drawText (row.key, line.removeFromLeft (96), juce::Justification::centredLeft, false);

        g.setColour (row.warn ? theme::warn : theme::text);
        g.setFont (theme::font (11.0f));
        g.drawText (row.value, line, juce::Justification::centredLeft, true);
    }
}

} // namespace gootar

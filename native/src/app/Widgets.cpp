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
    computeMetrics();
    repaint();
}

void ChainStrip::setSelected (const juce::String& id)
{
    if (selectedId != id) { selectedId = id; repaint(); }
}

/**
 * Pill geometry, worked out once and used by both painting and hit-testing.
 *
 * Two copies of this arithmetic would drift the moment one is edited, and the
 * symptom would be clicks landing on the wrong pedal - the kind of bug that is
 * maddening to track down because everything looks right.
 */
void ChainStrip::computeMetrics()
{
    const int n = entries.size();
    if (n == 0) { pillWidth = 0; return; }

    auto row = getLocalBounds().reduced (theme::pad, 6);
    row.removeFromTop (13);
    const int available = row.getWidth();

    for (const auto& attempt : { std::pair { 5, 8 }, std::pair { 3, 6 }, std::pair { 2, 4 } })
    {
        gap = attempt.first;
        arrow = attempt.second;
        pillWidth = (available - (n - 1) * (arrow + gap * 2)) / juce::jmax (1, n);
        if (pillWidth >= 30)
            break;
    }
    pillWidth = juce::jmax (20, pillWidth);

    const int used = n * pillWidth + (n - 1) * (arrow + gap * 2);
    originX = row.getX() + juce::jmax (0, (available - used) / 2);
}

juce::Rectangle<int> ChainStrip::boxFor (int index) const
{
    auto row = getLocalBounds().reduced (theme::pad, 6);
    row.removeFromTop (13);
    const int x = originX + index * (pillWidth + arrow + gap * 2);
    return { x, row.getY(), pillWidth, juce::jmin (26, row.getHeight()) };
}

int ChainStrip::indexAt (juce::Point<int> point) const
{
    for (int i = 0; i < entries.size(); ++i)
        if (boxFor (i).expanded (gap, 4).contains (point))
            return i;
    return -1;
}

void ChainStrip::mouseDown (const juce::MouseEvent& event)
{
    const int index = indexAt (event.getPosition());
    if (index < 0)
        return;
    setSelected (entries.getReference (index).id);
    if (onSelect)
        onSelect (selectedId);
}

void ChainStrip::paint (juce::Graphics& g)
{
    auto header = getLocalBounds().reduced (theme::pad, 6).removeFromTop (13);
    g.setColour (theme::textDim);
    g.setFont (theme::font (10.0f, true));
    g.drawText ("SIGNAL CHAIN", header, juce::Justification::centredLeft, false);

    if (entries.isEmpty())
        return;

    computeMetrics();

    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries.getReference (i);
        const auto box = boxFor (i).toFloat();
        const bool isSelected = e.id == selectedId;

        const auto colour = ! e.enabled ? theme::line
                          : e.loaded    ? theme::accent
                                        : theme::textDim;

        g.setColour (e.enabled && e.loaded ? theme::accentDim : theme::surfaceHigh);
        g.fillRoundedRectangle (box, (float) theme::radius - 3.0f);
        g.setColour (isSelected ? theme::text : colour);
        g.drawRoundedRectangle (box, (float) theme::radius - 3.0f, isSelected ? 2.0f : 1.0f);

        g.setColour (e.enabled ? colour : theme::textDim.withAlpha (0.5f));
        g.setFont (theme::font (pillWidth >= 34 ? 10.0f : 8.5f, true));
        g.drawText (e.label, box.toNearestInt(), juce::Justification::centred, false);

        if (i + 1 < entries.size())
        {
            g.setColour (theme::line.brighter (0.2f));
            const float cy = box.getCentreY();
            const float ax = box.getRight() + (float) gap;
            g.drawLine (ax, cy, ax + (float) arrow, cy, 1.0f);
        }
    }
}

// --- PedalEditor -----------------------------------------------------------

void PedalEditor::setPedal (const juce::String& newTitle, const juce::Array<Param>& params)
{
    title = newTitle;
    controls.clear();

    for (const auto& p : params)
    {
        Control control;
        control.key = p.key;

        control.slider = std::make_unique<juce::Slider> (
            juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        control.slider->setRange (p.min, p.max, p.step);
        control.slider->setValue (p.value, juce::dontSendNotification);
        control.slider->setTextValueSuffix (p.suffix);
        control.slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 14);
        control.slider->setColour (juce::Slider::textBoxTextColourId, theme::text);

        const auto key = p.key;
        auto* raw = control.slider.get();
        control.slider->onValueChange = [this, key, raw]
        {
            if (onChange)
                onChange (key, (float) raw->getValue());
        };
        addAndMakeVisible (*control.slider);

        control.label = std::make_unique<juce::Label>();
        control.label->setText (p.label, juce::dontSendNotification);
        control.label->setJustificationType (juce::Justification::centred);
        control.label->setFont (theme::font (9.5f, true));
        control.label->setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (*control.label);

        controls.push_back (std::move (control));
    }

    resized();
    repaint();
}

void PedalEditor::paint (juce::Graphics& g)
{
    auto header = getLocalBounds().reduced (theme::pad, 8).removeFromTop (13);
    g.setColour (theme::textDim);
    g.setFont (theme::font (10.0f, true));
    g.drawText (title.isEmpty() ? "PEDAL" : title.toUpperCase(), header,
                juce::Justification::centredLeft, false);

    if (controls.empty())
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.0f));
        g.drawText ("select something in the chain",
                    getLocalBounds().reduced (theme::pad, 8).withTrimmedTop (16),
                    juce::Justification::centredTop, false);
    }
}

void PedalEditor::resized()
{
    if (controls.empty())
        return;

    auto area = getLocalBounds().reduced (theme::pad, 8);
    area.removeFromTop (18);

    // Wrap into rows of three so five knobs do not become five slivers.
    const int perRow = juce::jlimit (1, 3, (int) controls.size());
    const int cell = juce::jmax (56, area.getWidth() / perRow);
    const int rowHeight = juce::jmin (76, juce::jmax (54, area.getHeight() /
                            (int) std::ceil (controls.size() / (double) perRow)));

    int index = 0;
    while (index < (int) controls.size() && area.getHeight() >= 40)
    {
        auto row = area.removeFromTop (rowHeight);
        for (int i = 0; i < perRow && index < (int) controls.size(); ++i, ++index)
        {
            auto column = row.removeFromLeft (cell);
            controls[(size_t) index].label->setBounds (column.removeFromTop (12));
            controls[(size_t) index].slider->setBounds (column.reduced (2, 0));
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

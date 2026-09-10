#include "Theme.h"

namespace gootar {

GootarLookAndFeel::GootarLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::bg);
    setColour (juce::Label::textColourId, theme::text);
    setColour (juce::Slider::textBoxTextColourId, theme::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, theme::surfaceHigh);
    setColour (juce::TextEditor::outlineColourId, theme::line);
    setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    setColour (juce::TextEditor::textColourId, theme::text);
    setColour (juce::TextEditor::highlightColourId, theme::accentDim);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, theme::line);
    setColour (juce::PopupMenu::backgroundColourId, theme::surfaceHigh);
    setColour (juce::PopupMenu::textColourId, theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, theme::accent);
}

void GootarLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float startAngle, float endAngle,
                                          juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float thickness = juce::jmax (3.0f, radius * 0.22f);
    const float arcRadius = radius - thickness * 0.5f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         startAngle, endAngle, true);
    g.setColour (theme::line);
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Bipolar controls fill outward from twelve o'clock, so "flat" reads as
    // flat at a glance instead of as a half-full knob.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float originAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    if (std::abs (angle - originAngle) > 0.001f)
    {
        juce::Path fill;
        fill.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                            juce::jmin (originAngle, angle), juce::jmax (originAngle, angle), true);
        g.setColour (slider.isEnabled() ? theme::accent : theme::line.brighter (0.1f));
        g.strokePath (fill, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    const juce::Point<float> tip (
        centre.x + arcRadius * std::sin (angle),
        centre.y - arcRadius * std::cos (angle));
    const juce::Point<float> root (
        centre.x + (arcRadius - thickness * 1.4f) * std::sin (angle),
        centre.y - (arcRadius - thickness * 1.4f) * std::cos (angle));

    g.setColour (slider.isEnabled() ? theme::text : theme::textDim);
    g.drawLine ({ root, tip }, juce::jmax (1.5f, thickness * 0.4f));
}

void GootarLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                              const juce::Colour&, bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool accented = button.getToggleState()
                       || button.getProperties()["accent"].equals (juce::var (true));

    auto fill = accented ? theme::accentDim : theme::surfaceHigh;
    if (down) fill = fill.brighter (0.12f);
    else if (highlighted) fill = fill.brighter (0.06f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, (float) theme::radius - 2.0f);
    g.setColour (accented ? theme::accent : theme::line);
    g.drawRoundedRectangle (bounds, (float) theme::radius - 2.0f, 1.0f);
}

void GootarLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                        bool, bool)
{
    const bool accented = button.getToggleState()
                       || button.getProperties()["accent"].equals (juce::var (true));
    g.setColour (! button.isEnabled() ? theme::textDim
                                      : (accented ? theme::accent : theme::text));
    g.setFont (theme::font (13.0f));
    g.drawText (button.getButtonText(), button.getLocalBounds(),
                juce::Justification::centred, false);
}

void GootarLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                          bool highlighted, bool)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();

    g.setColour (on ? theme::accentDim : theme::surfaceHigh.brighter (highlighted ? 0.06f : 0.0f));
    g.fillRoundedRectangle (bounds, (float) theme::radius - 2.0f);
    g.setColour (on ? theme::accent : theme::line);
    g.drawRoundedRectangle (bounds, (float) theme::radius - 2.0f, 1.0f);

    // A small dot rather than a tickbox: at this size a tick is mush, and the
    // dot doubles as an on/off indicator you can read across the room.
    const float dot = 6.0f;
    const auto dotArea = juce::Rectangle<float> (dot, dot)
                            .withCentre ({ bounds.getX() + 14.0f, bounds.getCentreY() });
    g.setColour (on ? theme::accent : theme::line.brighter (0.25f));
    g.fillEllipse (dotArea);

    g.setColour (on ? theme::accent : theme::textDim);
    g.setFont (theme::font (13.0f));
    g.drawText (button.getButtonText(),
                bounds.withTrimmedLeft (26.0f).toNearestInt(),
                juce::Justification::centredLeft, false);
}

void GootarLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                      int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                            .reduced (0.5f);
    g.setColour (theme::surfaceHigh);
    g.fillRoundedRectangle (bounds, (float) theme::radius - 2.0f);
    g.setColour (box.hasKeyboardFocus (false) ? theme::accent : theme::line);
    g.drawRoundedRectangle (bounds, (float) theme::radius - 2.0f, 1.0f);

    juce::Path arrow;
    const float cx = (float) width - 15.0f, cy = (float) height * 0.5f;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.5f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (theme::textDim);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

juce::Font GootarLookAndFeel::getLabelFont (juce::Label&)    { return theme::font (13.0f); }
juce::Font GootarLookAndFeel::getComboBoxFont (juce::ComboBox&) { return theme::font (13.0f); }
juce::Font GootarLookAndFeel::getPopupMenuFont()             { return theme::font (13.0f); }

} // namespace gootar

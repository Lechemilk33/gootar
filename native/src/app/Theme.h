#pragma once

#include <JuceHeader.h>

namespace gootar {

/**
 * One place for every colour and metric, so the app looks like it was designed
 * rather than assembled. Values are deliberately few: two greys for surfaces,
 * one line colour, two text weights, one accent. Anything that needs a sixth
 * grey is usually a layout problem wearing a colour costume.
 */
namespace theme
{
    const juce::Colour bg          { 0xff0d0f13 };
    const juce::Colour surface     { 0xff15181e };
    const juce::Colour surfaceHigh { 0xff1c2029 };
    const juce::Colour line        { 0xff272c36 };
    const juce::Colour text        { 0xffe8eaee };
    const juce::Colour textDim     { 0xff868e9e };
    const juce::Colour accent      { 0xffff8a3d };
    const juce::Colour accentDim   { 0x33ff8a3d };
    const juce::Colour good        { 0xff4ade80 };
    const juce::Colour warn        { 0xfffacc15 };
    const juce::Colour bad         { 0xfff87171 };

    constexpr int pad = 14;
    constexpr int radius = 9;
    constexpr int rowHeight = 30;

    inline juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size,
            bold ? juce::Font::bold : juce::Font::plain));
    }
}

/**
 * Rotary knobs drawn as an arc with a pointer, plus flat rounded buttons.
 *
 * JUCE's stock rotary is a filled pie slice that reads as a pac-man at small
 * sizes and gives no sense of where centre is. An arc with a tick does both,
 * and costs about thirty lines.
 */
class GootarLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GootarLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

} // namespace gootar

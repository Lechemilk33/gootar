#pragma once

#include <JuceHeader.h>

#include "ModelLibrary.h"
#include "PluginProcessor.h"
#include "Theme.h"
#include "Widgets.h"

namespace gootar {

/**
 * Browser-first layout.
 *
 * The whole point of the project is that switching captures is one click that
 * does not interrupt playing, so the model list is the largest thing on screen
 * and the knobs sit under it. That is the opposite of the stock plugin, where
 * the model is a file path you go hunting for through a dialog.
 */
class GootarEditor : public juce::AudioProcessorEditor,
                     private juce::ListBoxModel,
                     private ModelLibrary::Listener,
                     private juce::Timer
{
public:
    explicit GootarEditor (GootarProcessor&);
    ~GootarEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // ListBoxModel
    int  getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;

    // ModelLibrary::Listener
    void libraryChanged() override;
    void libraryScanProgress (int done, int total) override;

    void timerCallback() override;

    /** Load whatever is on this row - the one action the whole UI exists for. */
    void auditionRow (int row);

    void chooseLibraryFolder();
    void chooseIR();
    void savePreset();
    void loadPreset();
    void refreshFilter();
    void refreshStatus();
    void refreshChainStrip();
    void refreshInfoPanel();

    struct Knob
    {
        juce::Slider slider;
        juce::Label  caption;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void addKnob (Knob&, const juce::String& paramID, const juce::String& caption);
    void addToggle (juce::ToggleButton&,
                    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>&,
                    const juce::String& paramID, const juce::String& caption);
    void styleButton (juce::TextButton&, bool accent = false);

    GootarProcessor& processor;
    GootarLookAndFeel lookAndFeel;

    juce::TextButton libraryButton { "Model folder" };
    juce::TextButton irButton      { "Load IR" };
    juce::TextButton clearIRButton { "Clear IR" };
    juce::TextButton saveButton    { "Save preset" };
    juce::TextButton loadButton    { "Open preset" };

    juce::TextEditor searchBox;
    juce::ListBox    modelList { "models", this };
    juce::Label      nowPlaying;
    juce::Label      statusLine;

    TunerDisplay tuner;
    ChainStrip   chainStrip;
    InfoPanel    infoPanel;
    LevelMeter   inputMeter { "IN" };
    LevelMeter   outputMeter { "OUT" };

    Knob inputKnob, gateKnob, bassKnob, midKnob, trebleKnob, outputKnob;
    juce::ToggleButton gateToggle, eqToggle, irToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        gateAttach, eqAttach, irAttach;

    juce::ComboBox outputModeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> outputModeAttach;

    /**
     * Panel rectangles, computed once in resized() and only read by paint().
     *
     * Having paint() work out its own bounds is how a JUCE layout silently
     * drifts: the two copies of the arithmetic agree until one of them is
     * edited, and then panels sit a few pixels off from the controls inside
     * them. One source, two readers.
     */
    struct Layout
    {
        juce::Rectangle<int> list, tuner, meters, chain, info, knobs;
    } layout;

    juce::Array<LibraryItem> filtered;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String scanStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GootarEditor)
};

} // namespace gootar

#pragma once

#include <JuceHeader.h>

#include "ModelLibrary.h"
#include "PluginProcessor.h"

namespace gootar {

/**
 * The browser-first UI.
 *
 * The whole point of the project is that switching models is a single click
 * that does not interrupt playing, so the model list is the main object on
 * screen and the knobs sit underneath it — the opposite of the stock plugin,
 * where the model is a file path you have to go hunting for.
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
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    /** Load whatever is on this row - the one action the whole UI exists for. */
    void auditionRow (int row);
    void selectedRowsChanged (int lastRowSelected) override;

    // ModelLibrary::Listener
    void libraryChanged() override;
    void libraryScanProgress (int done, int total) override;

    void timerCallback() override;

    void chooseLibraryFolder();
    void chooseIR();
    void savePreset();
    void loadPreset();
    void refreshFilter();
    void updateStatus();

    struct KnobAttachment
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void addKnob (KnobAttachment&, const juce::String& paramID, const juce::String& text);
    void addToggle (juce::ToggleButton&,
                    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>&,
                    const juce::String& paramID, const juce::String& text);

    GootarProcessor& processor;

    juce::TextButton libraryButton { "Model folder..." };
    juce::TextButton irButton { "Load IR..." };
    juce::TextButton clearIRButton { "No IR" };
    juce::TextButton savePresetButton { "Save preset" };
    juce::TextButton loadPresetButton { "Load preset" };

    juce::TextEditor searchBox;
    juce::ListBox modelList { "models", this };
    juce::Label statusLabel;
    juce::Label nowPlayingLabel;

    KnobAttachment inputKnob, gateKnob, bassKnob, midKnob, trebleKnob, outputKnob;
    juce::ToggleButton gateToggle, eqToggle, irToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        gateAttach, eqAttach, irAttach;

    juce::ComboBox outputModeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> outputModeAttach;

    juce::Array<LibraryItem> filtered;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String scanStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GootarEditor)
};

} // namespace gootar

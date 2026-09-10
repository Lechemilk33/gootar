#include "PluginEditor.h"

namespace gootar {

namespace {
constexpr int kRowHeight = 26;
constexpr int kPad = 10;

const juce::Colour kBg       { 0xff0e1013 };
const juce::Colour kPanel    { 0xff171a1f };
const juce::Colour kLine     { 0xff2a2f38 };
const juce::Colour kText     { 0xffe6e8ec };
const juce::Colour kMuted    { 0xff8b93a1 };
const juce::Colour kAccent   { 0xffff8a3d };
} // namespace

GootarEditor::GootarEditor (GootarProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    auto& lf = getLookAndFeel();
    lf.setColour (juce::ResizableWindow::backgroundColourId, kBg);
    lf.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    lf.setColour (juce::Slider::thumbColourId, kText);
    lf.setColour (juce::TextEditor::backgroundColourId, kPanel);
    lf.setColour (juce::ListBox::backgroundColourId, kPanel);

    addAndMakeVisible (libraryButton);
    libraryButton.onClick = [this] { chooseLibraryFolder(); };

    addAndMakeVisible (irButton);
    irButton.onClick = [this] { chooseIR(); };

    addAndMakeVisible (clearIRButton);
    clearIRButton.onClick = [this] { processor.clearIR(); updateStatus(); };

    addAndMakeVisible (savePresetButton);
    savePresetButton.onClick = [this] { savePreset(); };

    addAndMakeVisible (loadPresetButton);
    loadPresetButton.onClick = [this] { loadPreset(); };

    addAndMakeVisible (searchBox);
    searchBox.setTextToShowWhenEmpty ("Filter models...", kMuted);
    searchBox.onTextChange = [this] { refreshFilter(); };

    addAndMakeVisible (modelList);
    modelList.setRowHeight (kRowHeight);

    addAndMakeVisible (statusLabel);
    statusLabel.setColour (juce::Label::textColourId, kMuted);
    statusLabel.setFont (juce::FontOptions (12.0f));

    addAndMakeVisible (nowPlayingLabel);
    nowPlayingLabel.setColour (juce::Label::textColourId, kAccent);
    nowPlayingLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));

    addKnob (inputKnob,  "inputLevelDb",    "Input");
    addKnob (gateKnob,   "gateThresholdDb", "Gate");
    addKnob (bassKnob,   "bass",            "Bass");
    addKnob (midKnob,    "mid",             "Mid");
    addKnob (trebleKnob, "treble",          "Treble");
    addKnob (outputKnob, "outputLevelDb",   "Output");

    addToggle (gateToggle, gateAttach, "gateEnabled", "Gate");
    addToggle (eqToggle,   eqAttach,   "eqEnabled",   "EQ");
    addToggle (irToggle,   irAttach,   "irEnabled",   "IR");

    addAndMakeVisible (outputModeBox);
    outputModeBox.addItemList ({ "Raw", "Normalized", "Calibrated" }, 1);
    outputModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.state(), "outputMode", outputModeBox);

    processor.library().addListener (this);
    refreshFilter();
    updateStatus();

    setResizable (true, true);
    setResizeLimits (720, 520, 1600, 1200);
    setSize (900, 640);
    startTimerHz (4);
}

GootarEditor::~GootarEditor()
{
    processor.library().removeListener (this);
}

void GootarEditor::addKnob (KnobAttachment& k, const juce::String& paramID, const juce::String& text)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 16);
    addAndMakeVisible (k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, kMuted);
    k.label.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.state(), paramID, k.slider);
}

void GootarEditor::addToggle (juce::ToggleButton& b,
                              std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& a,
                              const juce::String& paramID, const juce::String& text)
{
    b.setButtonText (text);
    b.setColour (juce::ToggleButton::textColourId, kText);
    addAndMakeVisible (b);
    a = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.state(), paramID, b);
}

void GootarEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    g.setColour (kPanel);
    g.fillRoundedRectangle (getLocalBounds().removeFromBottom (150).reduced (kPad).toFloat(), 6.0f);

    g.setColour (kText);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("Gootar", kPad, 6, 200, 24, juce::Justification::centredLeft);
}

void GootarEditor::resized()
{
    auto area = getLocalBounds().reduced (kPad);
    area.removeFromTop (28); // title

    auto top = area.removeFromTop (30);
    libraryButton.setBounds (top.removeFromLeft (130).reduced (2));
    irButton.setBounds (top.removeFromLeft (110).reduced (2));
    clearIRButton.setBounds (top.removeFromLeft (80).reduced (2));
    loadPresetButton.setBounds (top.removeFromRight (110).reduced (2));
    savePresetButton.setBounds (top.removeFromRight (110).reduced (2));

    area.removeFromTop (6);
    searchBox.setBounds (area.removeFromTop (28));
    area.removeFromTop (6);

    auto bottom = area.removeFromBottom (150);
    statusLabel.setBounds (area.removeFromBottom (20));
    nowPlayingLabel.setBounds (area.removeFromBottom (22));
    modelList.setBounds (area);

    bottom = bottom.reduced (kPad);
    auto toggles = bottom.removeFromRight (110);
    gateToggle.setBounds (toggles.removeFromTop (26));
    eqToggle.setBounds (toggles.removeFromTop (26));
    irToggle.setBounds (toggles.removeFromTop (26));
    toggles.removeFromTop (4);
    outputModeBox.setBounds (toggles.removeFromTop (26));

    KnobAttachment* knobs[] = { &inputKnob, &gateKnob, &bassKnob, &midKnob, &trebleKnob, &outputKnob };
    const int n = static_cast<int> (std::size (knobs));
    const int w = juce::jmax (60, bottom.getWidth() / n);
    for (auto* k : knobs)
    {
        auto cell = bottom.removeFromLeft (w);
        k->label.setBounds (cell.removeFromTop (14));
        k->slider.setBounds (cell.reduced (2));
    }
}

// --- model list ------------------------------------------------------------

int GootarEditor::getNumRows() { return filtered.size(); }

void GootarEditor::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, filtered.size()))
        return;

    const auto& item = filtered.getReference (row);
    const bool isCurrent = item.file == processor.currentModelFile();

    if (selected)
        g.fillAll (kLine);
    if (isCurrent)
    {
        g.setColour (kAccent);
        g.fillRect (0, 0, 3, h);
    }

    g.setColour (isCurrent ? kAccent : kText);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (item.fileName, 12, 0, w - 130, h, juce::Justification::centredLeft);

    g.setColour (kMuted);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (item.isIR ? "IR" : item.sha256.substring (0, 10),
                w - 120, 0, 110, h, juce::Justification::centredRight);
}

void GootarEditor::auditionRow (int row)
{
    if (! juce::isPositiveAndBelow (row, filtered.size()))
        return;

    // This is the whole product: one click, no interruption. The load happens
    // on the loader thread and the audio thread picks it up when it is ready.
    const auto& item = filtered.getReference (row);
    if (item.isIR)
        processor.requestIR (item.file);
    else
        processor.requestModel (item.file);

    updateStatus();
}

void GootarEditor::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    auditionRow (row);
}

void GootarEditor::selectedRowsChanged (int lastRowSelected)
{
    // Keyboard navigation auditions too, so arrowing down the list walks
    // through models exactly the way clicking does.
    auditionRow (lastRowSelected);
}

void GootarEditor::refreshFilter()
{
    const auto query = searchBox.getText().trim();
    filtered.clearQuick();

    for (const auto& item : processor.library().getItems())
    {
        if (query.isEmpty() || item.fileName.containsIgnoreCase (query)
            || item.relPath.containsIgnoreCase (query))
            filtered.add (item);
    }

    modelList.updateContent();
    modelList.repaint();
    updateStatus();
}

void GootarEditor::libraryChanged()
{
    scanStatus.clear();
    refreshFilter();
}

void GootarEditor::libraryScanProgress (int done, int total)
{
    scanStatus = "scanning " + juce::String (done) + "/" + juce::String (total) + "...";
    updateStatus();
}

void GootarEditor::timerCallback()
{
    updateStatus();
    modelList.repaint();
}

void GootarEditor::updateStatus()
{
    const auto info = processor.modelInfo();
    const auto err = processor.lastError();

    if (err.isNotEmpty())
    {
        nowPlayingLabel.setColour (juce::Label::textColourId, juce::Colour (0xfff87171));
        nowPlayingLabel.setText (err, juce::dontSendNotification);
    }
    else if (info.loaded)
    {
        nowPlayingLabel.setColour (juce::Label::textColourId, kAccent);
        nowPlayingLabel.setText (info.fileName, juce::dontSendNotification);
    }
    else
    {
        nowPlayingLabel.setColour (juce::Label::textColourId, kMuted);
        nowPlayingLabel.setText ("no model loaded - pick one above", juce::dontSendNotification);
    }

    juce::StringArray bits;
    if (scanStatus.isNotEmpty())
        bits.add (scanStatus);
    bits.add (juce::String (filtered.size()) + " of "
              + juce::String (processor.library().getNumItems()) + " files");
    if (info.loaded)
        bits.add (juce::String (info.sampleRate, 0) + " Hz model");
    const auto ir = processor.currentIRFile();
    bits.add (ir.existsAsFile() ? "IR: " + ir.getFileName() : "no IR");

    statusLabel.setText (bits.joinIntoString ("  |  "), juce::dontSendNotification);
}

// --- file actions ----------------------------------------------------------

void GootarEditor::chooseLibraryFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose your .nam folder",
                                                   processor.library().getRoot());
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectDirectories,
        [this] (const juce::FileChooser& fc)
        {
            const auto dir = fc.getResult();
            if (dir.isDirectory())
            {
                scanStatus = "scanning...";
                processor.library().setRoot (dir);
                updateStatus();
            }
        });
}

void GootarEditor::chooseIR()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose an impulse response",
                                                   processor.library().getRoot(), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile())
            {
                processor.requestIR (f);
                updateStatus();
            }
        });
}

void GootarEditor::savePreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Save preset", juce::File(), "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File())
                return;
            if (f.getFileExtension().isEmpty())
                f = f.withFileExtension ("json");

            juce::String err;
            if (! processor.savePresetToFile (f, f.getFileNameWithoutExtension(), err))
                juce::NativeMessageBox::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "Could not save preset", err);
            else
                updateStatus();
        });
}

void GootarEditor::loadPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Load preset", juce::File(), "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile())
                return;

            juce::String err;
            if (! processor.loadPresetFromFile (f, err))
                juce::NativeMessageBox::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "Preset loaded with problems", err);
            updateStatus();
        });
}

} // namespace gootar

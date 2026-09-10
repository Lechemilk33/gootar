#include "PluginEditor.h"

#include "../dsp/ChainSpec.h"

namespace gootar {

namespace {
constexpr int kRowHeight = 34;
constexpr int kKnobRow = 96;
constexpr int kSideWidth = 300;
} // namespace

GootarEditor::GootarEditor (GootarProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    styleButton (libraryButton, true);
    libraryButton.onClick = [this] { chooseLibraryFolder(); };
    styleButton (irButton);
    irButton.onClick = [this] { chooseIR(); };
    styleButton (clearIRButton);
    clearIRButton.onClick = [this] { processor.clearIR(); refreshStatus(); };
    styleButton (saveButton);
    saveButton.onClick = [this] { savePreset(); };
    styleButton (loadButton);
    loadButton.onClick = [this] { loadPreset(); };

    addAndMakeVisible (searchBox);
    searchBox.setTextToShowWhenEmpty ("Search captures...", theme::textDim);
    searchBox.setFont (theme::font (14.0f));
    searchBox.onTextChange = [this] { refreshFilter(); };

    addAndMakeVisible (modelList);
    modelList.setRowHeight (kRowHeight);
    modelList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);

    addAndMakeVisible (nowPlaying);
    nowPlaying.setFont (theme::font (15.0f, true));
    nowPlaying.setColour (juce::Label::textColourId, theme::accent);

    addAndMakeVisible (statusLine);
    statusLine.setFont (theme::font (11.0f));
    statusLine.setColour (juce::Label::textColourId, theme::textDim);

    addAndMakeVisible (tuner);
    addAndMakeVisible (infoPanel);

    addAndMakeVisible (chainStrip);
    chainStrip.onSelect = [this] (juce::String id)
    {
        selectedBlockId = id;
        refreshPedalEditor();
    };

    addAndMakeVisible (pedalEditor);
    pedalEditor.onChange = [this] (juce::String key, float value)
    {
        processor.setBlockParam (selectedBlockId, key, value);
    };

    styleButton (addPedalButton, true);
    addPedalButton.onClick = [this] { showAddPedalMenu(); };
    styleButton (removePedalButton);
    removePedalButton.onClick = [this]
    {
        if (selectedBlockId.isNotEmpty())
        {
            processor.removeBlock (selectedBlockId);
            selectedBlockId = {};
            refreshChainStrip();
            refreshPedalEditor();
        }
    };
    styleButton (moveLeftButton);
    moveLeftButton.onClick = [this]
    {
        const int at = indexOfSelected();
        if (at > 0) { processor.moveBlock (selectedBlockId, at - 1); refreshChainStrip(); }
    };
    styleButton (moveRightButton);
    moveRightButton.onClick = [this]
    {
        const int at = indexOfSelected();
        if (at >= 0) { processor.moveBlock (selectedBlockId, at + 1); refreshChainStrip(); }
    };
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    addKnob (inputKnob,  "inputLevelDb",    "INPUT");
    addKnob (gateKnob,   "gateThresholdDb", "GATE");
    addKnob (bassKnob,   "bass",            "BASS");
    addKnob (midKnob,    "mid",             "MID");
    addKnob (trebleKnob, "treble",          "TREBLE");
    addKnob (outputKnob, "outputLevelDb",   "OUTPUT");

    addToggle (gateToggle, gateAttach, "gateEnabled", "Gate");
    addToggle (eqToggle,   eqAttach,   "eqEnabled",   "EQ");
    addToggle (irToggle,   irAttach,   "irEnabled",   "IR");

    addAndMakeVisible (outputModeBox);
    outputModeBox.addItemList ({ "Raw", "Normalized", "Calibrated" }, 1);
    outputModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.state(), "outputMode", outputModeBox);

    processor.library().addListener (this);
    refreshFilter();
    refreshStatus();
    refreshChainStrip();
    refreshInfoPanel();
    refreshPedalEditor();

    setResizable (true, true);
    setResizeLimits (860, 600, 1800, 1400);
    setSize (1000, 720);
    startTimerHz (20);
}

GootarEditor::~GootarEditor()
{
    processor.library().removeListener (this);
    setLookAndFeel (nullptr);
}

void GootarEditor::styleButton (juce::TextButton& b, bool accent)
{
    if (accent)
        b.getProperties().set ("accent", true);
    addAndMakeVisible (b);
}

void GootarEditor::addKnob (Knob& k, const juce::String& paramID, const juce::String& caption)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 15);
    k.slider.setColour (juce::Slider::textBoxTextColourId, theme::text);
    addAndMakeVisible (k.slider);

    k.caption.setText (caption, juce::dontSendNotification);
    k.caption.setJustificationType (juce::Justification::centred);
    k.caption.setFont (theme::font (10.0f, true));
    k.caption.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (k.caption);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.state(), paramID, k.slider);
}

void GootarEditor::addToggle (juce::ToggleButton& b,
                              std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& a,
                              const juce::String& paramID, const juce::String& caption)
{
    b.setButtonText (caption);
    addAndMakeVisible (b);
    a = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.state(), paramID, b);
}

void GootarEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    g.setColour (theme::text);
    g.setFont (theme::font (21.0f, true));
    g.drawText ("Gootar",
                getLocalBounds().reduced (theme::pad).removeFromTop (40).removeFromLeft (110),
                juce::Justification::centredLeft, false);

    const auto panel = [&g] (juce::Rectangle<int> r)
    {
        if (r.isEmpty())
            return;
        g.setColour (theme::surface);
        g.fillRoundedRectangle (r.toFloat(), (float) theme::radius);
        g.setColour (theme::line);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), (float) theme::radius, 1.0f);
    };

    panel (layout.list);
    panel (layout.tuner);
    panel (layout.meters);
    panel (layout.chain);
    panel (layout.pedal);
    panel (layout.info);
    panel (layout.knobs);
}

void GootarEditor::resized()
{
    auto bounds = getLocalBounds().reduced (theme::pad);

    auto header = bounds.removeFromTop (40);
    header.removeFromLeft (118);
    const int buttonW = 108;
    libraryButton.setBounds (header.removeFromLeft (buttonW).reduced (3, 5));
    irButton.setBounds (header.removeFromLeft (90).reduced (3, 5));
    clearIRButton.setBounds (header.removeFromLeft (86).reduced (3, 5));
    loadButton.setBounds (header.removeFromRight (buttonW).reduced (3, 5));
    saveButton.setBounds (header.removeFromRight (buttonW).reduced (3, 5));

    bounds.removeFromTop (4);

    layout.knobs = bounds.removeFromBottom (kKnobRow);
    bounds.removeFromBottom (theme::pad);

    auto side = bounds.removeFromRight (kSideWidth);
    bounds.removeFromRight (theme::pad);
    layout.list = bounds;

    layout.tuner = side.removeFromTop (132);
    side.removeFromTop (theme::pad);
    layout.meters = side.removeFromTop (74);
    side.removeFromTop (theme::pad);
    layout.chain = side.removeFromTop (86);
    side.removeFromTop (theme::pad);
    layout.pedal = side.removeFromTop (juce::jmax (110, side.getHeight() - 118));
    side.removeFromTop (theme::pad);
    layout.info = side;

    // Contents sit inside their panels, from the same rectangles.
    auto left = layout.list.reduced (theme::pad);
    searchBox.setBounds (left.removeFromTop (32));
    left.removeFromTop (8);
    statusLine.setBounds (left.removeFromBottom (16));
    nowPlaying.setBounds (left.removeFromBottom (22));
    left.removeFromBottom (4);
    modelList.setBounds (left);

    tuner.setBounds (layout.tuner);

    auto meterArea = layout.meters.reduced (theme::pad, 11);
    inputMeter.setBounds (meterArea.removeFromTop (meterArea.getHeight() / 2 - 3));
    meterArea.removeFromTop (6);
    outputMeter.setBounds (meterArea);

    // The chain panel holds the strip plus its edit buttons.
    auto chainArea = layout.chain;
    auto chainButtons = chainArea.removeFromBottom (28).reduced (theme::pad, 2);
    chainStrip.setBounds (chainArea);

    addPedalButton.setBounds (chainButtons.removeFromLeft (74).reduced (2, 0));
    removePedalButton.setBounds (chainButtons.removeFromLeft (70).reduced (2, 0));
    moveRightButton.setBounds (chainButtons.removeFromRight (30).reduced (2, 0));
    moveLeftButton.setBounds (chainButtons.removeFromRight (30).reduced (2, 0));

    pedalEditor.setBounds (layout.pedal);
    infoPanel.setBounds (layout.info);

    auto knobs = layout.knobs.reduced (theme::pad, 8);
    auto switches = knobs.removeFromRight (150);
    gateToggle.setBounds (switches.removeFromTop (24).reduced (0, 1));
    eqToggle.setBounds (switches.removeFromTop (24).reduced (0, 1));
    irToggle.setBounds (switches.removeFromTop (24).reduced (0, 1));

    knobs.removeFromRight (theme::pad);
    outputModeBox.setBounds (knobs.removeFromRight (112).withSizeKeepingCentre (112, 28));
    knobs.removeFromRight (theme::pad);

    Knob* all[] = { &inputKnob, &gateKnob, &bassKnob, &midKnob, &trebleKnob, &outputKnob };
    const int count = (int) std::size (all);
    const int cell = juce::jmax (62, knobs.getWidth() / count);
    for (auto* k : all)
    {
        auto column = knobs.removeFromLeft (cell);
        k->caption.setBounds (column.removeFromTop (12));
        k->slider.setBounds (column.reduced (3, 0));
    }
}

// --- model list ------------------------------------------------------------

int GootarEditor::getNumRows() { return filtered.size(); }

void GootarEditor::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, filtered.size()))
        return;

    const auto& item = filtered.getReference (row);
    const bool current = item.file == processor.currentModelFile();

    auto bounds = juce::Rectangle<int> (0, 0, w, h).reduced (1, 1);

    if (current)
    {
        g.setColour (theme::accentDim);
        g.fillRoundedRectangle (bounds.toFloat(), 5.0f);
    }
    else if (selected)
    {
        g.setColour (theme::surfaceHigh);
        g.fillRoundedRectangle (bounds.toFloat(), 5.0f);
    }

    auto text = bounds.reduced (10, 0);

    // Type marker: IRs and captures live in the same list, so they need to be
    // distinguishable without reading the extension.
    auto marker = text.removeFromLeft (22);
    g.setColour (item.isIR ? theme::textDim : (current ? theme::accent : theme::text.withAlpha (0.5f)));
    g.setFont (theme::font (9.0f, true));
    g.drawText (item.isIR ? "IR" : "NAM", marker, juce::Justification::centredLeft, false);

    auto right = text.removeFromRight (86);
    g.setColour (theme::textDim);
    g.setFont (theme::font (10.0f));
    g.drawText (item.sha256.substring (0, 10), right, juce::Justification::centredRight, false);

    g.setColour (current ? theme::accent : theme::text);
    g.setFont (theme::font (13.0f, current));
    g.drawText (item.fileName.upToLastOccurrenceOf (".", false, false),
                text, juce::Justification::centredLeft, true);
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

    refreshStatus();
}

void GootarEditor::listBoxItemClicked (int row, const juce::MouseEvent&) { auditionRow (row); }

void GootarEditor::selectedRowsChanged (int lastRowSelected)
{
    // Keyboard navigation auditions too, so you can hold a chord and walk the
    // list with the arrow keys.
    auditionRow (lastRowSelected);
}

void GootarEditor::refreshFilter()
{
    const auto query = searchBox.getText().trim();
    filtered.clearQuick();

    for (const auto& item : processor.library().getItems())
        if (query.isEmpty()
            || item.fileName.containsIgnoreCase (query)
            || item.relPath.containsIgnoreCase (query))
            filtered.add (item);

    modelList.updateContent();
    modelList.repaint();
    refreshStatus();
}

void GootarEditor::libraryChanged()
{
    scanStatus.clear();
    refreshFilter();
}

void GootarEditor::libraryScanProgress (int done, int total)
{
    scanStatus = "scanning " + juce::String (done) + "/" + juce::String (total);
    refreshStatus();
}

void GootarEditor::timerCallback()
{
    inputMeter.setLevel (processor.inputPeak());
    outputMeter.setLevel (processor.outputPeak());
    tuner.setReading (processor.latestPitch());
    refreshStatus();
    refreshChainStrip();
    refreshInfoPanel();
}

void GootarEditor::refreshInfoPanel()
{
    const auto info = processor.modelInfo();
    juce::Array<InfoPanel::Row> rows;

    if (info.loaded)
    {
        rows.add (InfoPanel::Row { "architecture",
                                   info.architecture.empty()
                                     ? juce::String ("unreported")
                                     : juce::String (info.architecture) });
        rows.add (InfoPanel::Row { "trained at",
                                   juce::String (info.sampleRate, 0) + " Hz" });
        // A model that falls back to the dynamic path costs noticeably more
        // CPU, which is worth flagging rather than leaving you to wonder.
        rows.add (InfoPanel::Row { "runs as",
                                   info.isStatic ? "static (optimised)"
                                                 : "dynamic (fallback)",
                                   ! info.isStatic });
        if (info.receptiveField > 0)
            rows.add (InfoPanel::Row { "receptive field",
                                       juce::String (info.receptiveField) + " samples" });
    }

    const auto ir = processor.currentIRFile();
    rows.add (InfoPanel::Row { "cabinet",
                               ir.existsAsFile() ? ir.getFileName() : juce::String ("none") });
    rows.add (InfoPanel::Row { "model slots",
                               juce::String (juce::jmax (1, processor.numModelSlots())) });

    infoPanel.setRows (std::move (rows));
}

int GootarEditor::indexOfSelected() const
{
    const auto spec = processor.chainSpec();
    for (int i = 0; i < (int) spec.size(); ++i)
        if (juce::String (spec[(size_t) i].id) == selectedBlockId)
            return i;
    return -1;
}

void GootarEditor::showAddPedalMenu()
{
    juce::PopupMenu menu;
    int id = 1;
    std::vector<BlockType> order;
    for (auto type : kAddableBlocks)
    {
        juce::String label;
        switch (type)
        {
            case BlockType::Model:      label = "Amp capture"; break;
            case BlockType::Drive:      label = "Drive / boost"; break;
            case BlockType::Compressor: label = "Compressor"; break;
            case BlockType::Delay:      label = "Delay"; break;
            case BlockType::Reverb:     label = "Reverb"; break;
            case BlockType::ToneStack:  label = "Tone stack"; break;
            case BlockType::IR:         label = "Cabinet IR"; break;
            default: continue;
        }
        menu.addItem (id++, label);
        order.push_back (type);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addPedalButton),
        [this, order] (int choice)
        {
            if (choice <= 0 || choice > (int) order.size())
                return;
            const auto type = order[(size_t) (choice - 1)];
            selectedBlockId = processor.addBlock (type, defaultInsertPosition (type));
            refreshChainStrip();
            refreshPedalEditor();
        });
}

/**
 * Where a newly added pedal should go if you have not said otherwise.
 *
 * Signal order is the whole point of a pedalboard, and the sensible answer
 * depends on the pedal: a drive belongs in FRONT of the amp, because that is
 * how you push a capture harder - it models one amp at one setting, so hitting
 * its input is the only gain control you have. A delay belongs after the cab,
 * where an effects loop would be. Dropping everything at the end would be
 * technically fine and musically wrong.
 *
 * Placement is by TYPE, not by what happens to be selected. Adding a pedal
 * selects it so its knobs appear, so honouring the selection meant a second
 * pedal landed after the first rather than in its own natural place - add a
 * drive then a delay and the delay ends up in front of the amp. Predictable
 * beats clever: every pedal goes where that kind of pedal belongs, and the
 * arrows move it if you disagree.
 */
int GootarEditor::defaultInsertPosition (BlockType type) const
{
    const auto spec = processor.chainSpec();
    const int size = (int) spec.size();

    const auto firstOf = [&spec, size] (BlockType t)
    {
        for (int i = 0; i < size; ++i)
            if (spec[(size_t) i].type == t) return i;
        return -1;
    };
    const auto lastOf = [&spec, size] (BlockType t)
    {
        for (int i = size - 1; i >= 0; --i)
            if (spec[(size_t) i].type == t) return i;
        return -1;
    };

    switch (type)
    {
        case BlockType::Drive:
        case BlockType::Compressor:
        {
            // In front of the amp - or of the gate, if there is no amp yet.
            const int amp = firstOf (BlockType::Model);
            if (amp >= 0) return amp;
            const int gate = firstOf (BlockType::Gate);
            return gate >= 0 ? gate + 1 : 1;
        }
        case BlockType::Delay:
        case BlockType::Reverb:
        {
            // After the cab, where an effects loop lives.
            const int cab = lastOf (BlockType::IR);
            if (cab >= 0) return cab + 1;
            const int amp = lastOf (BlockType::Model);
            return amp >= 0 ? amp + 1 : juce::jmax (0, size - 1);
        }
        case BlockType::Model:
        {
            // A second capture goes after the first: pedal platform into amp.
            const int amp = lastOf (BlockType::Model);
            return amp >= 0 ? amp + 1 : juce::jmax (0, size - 1);
        }
        default:
            break;
    }

    // Everything else lands just before the output stage.
    return juce::jmax (0, size - 1);
}

void GootarEditor::refreshPedalEditor()
{
    chainStrip.setSelected (selectedBlockId);

    if (selectedBlockId.isEmpty())
    {
        pedalEditor.setPedal ({}, {});
        return;
    }

    juce::Array<PedalEditor::Param> params;
    for (const auto& p : processor.blockParams (selectedBlockId))
        params.add (PedalEditor::Param {
            p.key, p.label, p.suffix, p.min, p.max, p.step, p.value });

    // Fixed parts of the amp have no knobs here - theirs are the big ones
    // along the bottom - so say so rather than showing an empty box.
    pedalEditor.setPedal (params.isEmpty() ? selectedBlockId : selectedBlockId, params);
}

void GootarEditor::refreshChainStrip()
{
    juce::Array<ChainStrip::Entry> entries;
    const auto spec = processor.chainSpec();

    for (const auto& slot : spec)
    {
        ChainStrip::Entry entry;
        entry.id = juce::String (slot.id);
        entry.enabled = slot.enabled;
        entry.loaded = true;
        entry.fixed = false;

        switch (slot.type)
        {
            case BlockType::Gain:
                entry.label = slot.id == "input" ? "IN" : "OUT";
                entry.fixed = true;
                break;
            case BlockType::Gate:      entry.label = "GATE"; entry.fixed = true; break;
            case BlockType::ToneStack: entry.label = "TONE"; break;
            case BlockType::DCBlocker: entry.label = "DC";   entry.fixed = true; break;
            case BlockType::IR:
                entry.label = "CAB";
                entry.loaded = processor.currentIRFile().existsAsFile();
                break;
            case BlockType::Model:
                entry.label = "AMP";
                entry.loaded = processor.modelInfo().loaded;
                break;
            case BlockType::Drive:      entry.label = "DRIVE"; break;
            case BlockType::Compressor: entry.label = "COMP";  break;
            case BlockType::Delay:      entry.label = "DELAY"; break;
            case BlockType::Reverb:     entry.label = "VERB";  break;
        }
        entries.add (entry);
    }

    chainStrip.setEntries (std::move (entries));
    chainStrip.setSelected (selectedBlockId);
}

void GootarEditor::refreshStatus()
{
    const auto info = processor.modelInfo();
    const auto error = processor.lastError();

    if (error.isNotEmpty())
    {
        nowPlaying.setColour (juce::Label::textColourId, theme::bad);
        nowPlaying.setText (error, juce::dontSendNotification);
    }
    else if (info.loaded)
    {
        nowPlaying.setColour (juce::Label::textColourId, theme::accent);
        nowPlaying.setText (juce::String (info.fileName)
                                .upToLastOccurrenceOf (".", false, false),
                            juce::dontSendNotification);
    }
    else
    {
        nowPlaying.setColour (juce::Label::textColourId, theme::textDim);
        nowPlaying.setText ("no capture loaded - click one above",
                            juce::dontSendNotification);
    }

    juce::StringArray parts;
    if (scanStatus.isNotEmpty())
        parts.add (scanStatus);
    parts.add (juce::String (filtered.size()) + " of "
                 + juce::String (processor.library().getNumItems()) + " files");

    const auto rate = processor.getSampleRate();
    if (rate > 0.0)
    {
        auto rateText = juce::String (rate / 1000.0, 1) + " kHz";
        // 48 k is what captures are trained at; anything else is worth saying
        // out loud, because it is subtly wrong rather than obviously broken.
        if (std::abs (rate - 48000.0) > 1.0)
            rateText += " (models expect 48)";
        parts.add (rateText);
    }

    const auto ir = processor.currentIRFile();
    parts.add (ir.existsAsFile() ? "IR: " + ir.getFileName() : "no IR");

    statusLine.setText (parts.joinIntoString ("   ·   "), juce::dontSendNotification);
}

// --- file actions ----------------------------------------------------------

void GootarEditor::chooseLibraryFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose your capture folder",
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
                refreshStatus();
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
                refreshStatus();
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
                refreshStatus();
        });
}

void GootarEditor::loadPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Open preset", juce::File(), "*.json");
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
            refreshStatus();
        });
}

} // namespace gootar

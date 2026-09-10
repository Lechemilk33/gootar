#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace gootar {

namespace pid {
const juce::String inputLevel   { "inputLevelDb" };
const juce::String outputLevel  { "outputLevelDb" };
const juce::String outputMode   { "outputMode" };
const juce::String gateOn       { "gateEnabled" };
const juce::String gateThresh   { "gateThresholdDb" };
const juce::String eqOn         { "eqEnabled" };
const juce::String bass         { "bass" };
const juce::String mid          { "mid" };
const juce::String treble       { "treble" };
const juce::String irOn         { "irEnabled" };
const juce::String calibrateOn  { "calibrateInput" };
const juce::String calibrateDbu { "calibrationLevelDbu" };
} // namespace pid

juce::AudioProcessorValueTreeState::ParameterLayout GootarProcessor::makeParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Ranges match the stock plugin exactly (see packages/preset-schema).
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::inputLevel, 1 }, "Input",
        NormalisableRange<float> (-20.0f, 20.0f, 0.1f), 0.0f));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { pid::gateOn, 1 }, "Noise Gate", true));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::gateThresh, 1 }, "Gate Threshold",
        NormalisableRange<float> (-100.0f, 0.0f, 0.1f), -80.0f));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { pid::eqOn, 1 }, "Tone Stack", true));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::bass, 1 }, "Bass",
        NormalisableRange<float> (0.0f, 10.0f, 0.1f), 5.0f));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::mid, 1 }, "Middle",
        NormalisableRange<float> (0.0f, 10.0f, 0.1f), 5.0f));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::treble, 1 }, "Treble",
        NormalisableRange<float> (0.0f, 10.0f, 0.1f), 5.0f));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { pid::irOn, 1 }, "IR", true));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::outputLevel, 1 }, "Output",
        NormalisableRange<float> (-40.0f, 40.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { pid::outputMode, 1 }, "Output Mode",
        StringArray { "Raw", "Normalized", "Calibrated" }, 1));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { pid::calibrateOn, 1 }, "Calibrate Input", false));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::calibrateDbu, 1 }, "Input Calibration",
        NormalisableRange<float> (-60.0f, 60.0f, 0.1f), 12.0f));

    return layout;
}

GootarProcessor::GootarProcessor()
    : juce::AudioProcessor (BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GOOTAR", makeParameterLayout())
{
    loader.startThread (juce::Thread::Priority::normal);

    // First run: find the captures already on this machine instead of opening
    // to an empty list. setStateInformation overrides this if a session is
    // being restored.
    if (const auto guess = ModelLibrary::guessDefaultRoot(); guess.isDirectory())
        modelLibrary.setRoot (guess);

    // Drives both the tuner and the periodic freeing of retired models, off
    // the audio thread.
    startTimer (60);
}

GootarProcessor::~GootarProcessor()
{
    stopTimer();
    loader.signalThreadShouldExit();
    loader.post();
    loader.stopThread (4000);
}

void GootarProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    monoScratch.setSize (1, samplesPerBlock);

    // NeuralAudio bakes the sample rate into a model when it loads it, so a
    // rate change means reloading whatever is currently in the chain.
    if (engine.modelNeedsReload())
    {
        const juce::ScopedLock sl (loadLock);
        if (loadedModel.existsAsFile())
            requestedModel = loadedModel;
    }
    loader.post();
}

bool GootarProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    const auto& in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

Params GootarProcessor::paramsFromState() const
{
    const auto get = [this] (const juce::String& id)
    {
        return apvts.getRawParameterValue (id)->load();
    };

    Params p;
    p.inputLevelDb = get (pid::inputLevel);
    p.calibrateInput = get (pid::calibrateOn) > 0.5f;
    p.inputCalibrationLevelDbu = get (pid::calibrateDbu);
    p.gateEnabled = get (pid::gateOn) > 0.5f;
    p.gateThresholdDb = get (pid::gateThresh);
    p.toneStackEnabled = get (pid::eqOn) > 0.5f;
    p.bass = get (pid::bass);
    p.mid = get (pid::mid);
    p.treble = get (pid::treble);
    p.irEnabled = get (pid::irOn) > 0.5f;
    p.outputLevelDb = get (pid::outputLevel);
    p.outputMode = static_cast<OutputMode> (static_cast<int> (get (pid::outputMode)));
    return p;
}

void GootarProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    if (numSamples <= 0 || numIn <= 0 || numOut <= 0)
        return;

    engine.setParams (paramsFromState());

    // The chain is mono, exactly as in the stock plugin: collapse on the way
    // in, fan out on the way out.
    auto* mono = monoScratch.getWritePointer (0);
    if (numSamples > monoScratch.getNumSamples())
        return; // prepareToPlay was not called with a big enough block

    if (numIn == 1)
    {
        juce::FloatVectorOperations::copy (mono, buffer.getReadPointer (0), numSamples);
    }
    else
    {
        juce::FloatVectorOperations::copy (mono, buffer.getReadPointer (0), numSamples);
        juce::FloatVectorOperations::add (mono, buffer.getReadPointer (1), numSamples);
        juce::FloatVectorOperations::multiply (mono, 0.5f, numSamples);
    }

    engine.process (mono, mono, numSamples);

    for (int ch = 0; ch < numOut; ++ch)
        juce::FloatVectorOperations::copy (buffer.getWritePointer (ch), mono, numSamples);
}

// --- loading ---------------------------------------------------------------

void GootarProcessor::LoaderThread::run()
{
    while (! threadShouldExit())
    {
        juce::File model, ir;
        bool clearIR = false;
        {
            const juce::ScopedLock sl (owner.loadLock);
            model = owner.requestedModel;
            ir = owner.requestedIR;
            clearIR = owner.irClearRequested;
            owner.requestedModel = juce::File();
            owner.requestedIR = juce::File();
            owner.irClearRequested = false;
        }

        if (model.existsAsFile())
        {
            std::string err;
            const bool ok = owner.engine.loadModel (0, model.getFullPathName().toStdString(), err);
            const juce::ScopedLock sl (owner.loadLock);
            if (ok)
            {
                owner.loadedModel = model;
                owner.errorMessage.clear();
            }
            else
            {
                owner.errorMessage = juce::String (err);
            }
        }

        if (clearIR)
        {
            owner.engine.clearIR();
            const juce::ScopedLock sl (owner.loadLock);
            owner.loadedIR = juce::File();
        }
        else if (ir.existsAsFile())
        {
            std::string err;
            const bool ok = owner.engine.loadIR (ir.getFullPathName().toStdString(), err);
            const juce::ScopedLock sl (owner.loadLock);
            if (ok)
            {
                owner.loadedIR = ir;
                owner.errorMessage.clear();
            }
            else
            {
                owner.errorMessage = juce::String (err);
            }
        }

        owner.engine.collectGarbage();

        if (! threadShouldExit())
            wait (-1);
    }
}

void GootarProcessor::requestModel (const juce::File& f)
{
    {
        const juce::ScopedLock sl (loadLock);
        requestedModel = f;
    }
    loader.post();
}

void GootarProcessor::requestIR (const juce::File& f)
{
    {
        const juce::ScopedLock sl (loadLock);
        requestedIR = f;
        irClearRequested = false;
    }
    loader.post();
}

void GootarProcessor::clearIR()
{
    {
        const juce::ScopedLock sl (loadLock);
        irClearRequested = true;
        requestedIR = juce::File();
    }
    loader.post();
}

void GootarProcessor::timerCallback()
{
    // Belt and braces: the loader frees retired models after each request, but
    // a swap can land after the last one, so sweep periodically too.
    engine.collectGarbage();

    // Pitch analysis is a few hundred microseconds of autocorrelation - fine
    // here, absolutely not on the audio thread.
    auto reading = engine.analysePitch();
    const juce::ScopedLock sl (pitchLock);
    lastPitch = std::move (reading);
}

PitchReading GootarProcessor::latestPitch() const
{
    const juce::ScopedLock sl (pitchLock);
    return lastPitch;
}

juce::File GootarProcessor::currentModelFile() const
{
    const juce::ScopedLock sl (loadLock);
    return loadedModel;
}

juce::File GootarProcessor::currentIRFile() const
{
    const juce::ScopedLock sl (loadLock);
    return loadedIR;
}

juce::String GootarProcessor::lastError() const
{
    const juce::ScopedLock sl (loadLock);
    return errorMessage;
}

// --- presets ---------------------------------------------------------------

namespace {
AssetRef makeRef (const juce::File& f, const juce::File& root)
{
    AssetRef ref;
    if (! f.existsAsFile())
        return ref;
    ref.sha256 = ModelLibrary::hashFile (f);
    ref.fileName = f.getFileName();
    ref.sizeBytes = f.getSize();
    if (root.isDirectory() && f.isAChildOf (root))
        ref.relPath = f.getRelativePathFrom (root).replaceCharacter ('\\', '/');
    return ref;
}
} // namespace

Preset GootarProcessor::capturePreset (const juce::String& name) const
{
    auto p = Preset::makeDefault();
    p.name = name.isNotEmpty() ? name : "Untitled";
    p.params = paramsFromState();
    p.sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;

    const auto root = modelLibrary.getRoot();
    p.model = makeRef (currentModelFile(), root);

    const auto irFile = currentIRFile();
    if (irFile.existsAsFile())
    {
        p.ir = makeRef (irFile, root);
        p.hasIR = p.ir.isValid();
    }
    return p;
}

bool GootarProcessor::applyPreset (const Preset& p, juce::String& errorOut)
{
    const auto set = [this] (const juce::String& id, float value)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    set (pid::inputLevel, (float) p.params.inputLevelDb);
    set (pid::calibrateOn, p.params.calibrateInput ? 1.0f : 0.0f);
    set (pid::calibrateDbu, (float) p.params.inputCalibrationLevelDbu);
    set (pid::gateOn, p.params.gateEnabled ? 1.0f : 0.0f);
    set (pid::gateThresh, (float) p.params.gateThresholdDb);
    set (pid::eqOn, p.params.toneStackEnabled ? 1.0f : 0.0f);
    set (pid::bass, (float) p.params.bass);
    set (pid::mid, (float) p.params.mid);
    set (pid::treble, (float) p.params.treble);
    set (pid::irOn, p.params.irEnabled ? 1.0f : 0.0f);
    set (pid::outputLevel, (float) p.params.outputLevelDb);

    if (auto* modeParam = apvts.getParameter (pid::outputMode))
        modeParam->setValueNotifyingHost (
            modeParam->convertTo0to1 ((float) static_cast<int> (p.params.outputMode)));

    // Resolve the model by hash first; fall back to filename so a preset from
    // another machine still finds a plausible match rather than silently
    // playing nothing.
    juce::StringArray missing;

    if (p.model.isValid())
    {
        auto f = modelLibrary.resolve (p.model.sha256);
        if (! f.existsAsFile())
            f = modelLibrary.resolveByName (p.model.fileName);
        if (f.existsAsFile())
            requestModel (f);
        else
            missing.add ("model \"" + p.model.fileName + "\"");
    }

    if (p.hasIR && p.ir.isValid())
    {
        auto f = modelLibrary.resolve (p.ir.sha256);
        if (! f.existsAsFile())
            f = modelLibrary.resolveByName (p.ir.fileName);
        if (f.existsAsFile())
            requestIR (f);
        else
            missing.add ("IR \"" + p.ir.fileName + "\"");
    }
    else
    {
        clearIR();
    }

    if (! missing.isEmpty())
    {
        errorOut = "not in this library: " + missing.joinIntoString (", ");
        return false;
    }
    return true;
}

bool GootarProcessor::savePresetToFile (const juce::File& file, const juce::String& name,
                                        juce::String& errorOut)
{
    return presetIO::writeToFile (capturePreset (name), file, errorOut);
}

bool GootarProcessor::loadPresetFromFile (const juce::File& file, juce::String& errorOut)
{
    Preset p;
    if (! presetIO::readFromFile (file, p, errorOut))
        return false;
    return applyPreset (p, errorOut);
}

// --- host state ------------------------------------------------------------

void GootarProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    // Remember the library and what was loaded, so reopening lands where you
    // left off instead of silent with an empty browser.
    state.setProperty ("libraryRoot", modelLibrary.getRoot().getFullPathName(), nullptr);
    state.setProperty ("modelFile", currentModelFile().getFullPathName(), nullptr);
    state.setProperty ("irFile", currentIRFile().getFullPathName(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void GootarProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (tree);

    const juce::File root { tree.getProperty ("libraryRoot").toString() };
    if (root.isDirectory())
        modelLibrary.setRoot (root);

    const juce::File model { tree.getProperty ("modelFile").toString() };
    if (model.existsAsFile())
        requestModel (model);

    const juce::File ir { tree.getProperty ("irFile").toString() };
    if (ir.existsAsFile())
        requestIR (ir);
}

juce::AudioProcessorEditor* GootarProcessor::createEditor()
{
    return new GootarEditor (*this);
}

} // namespace gootar

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new gootar::GootarProcessor();
}

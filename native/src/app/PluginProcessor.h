#pragma once

#include <JuceHeader.h>

#include "../dsp/GootarEngine.h"
#include "ModelLibrary.h"
#include "PresetIO.h"

namespace gootar {

/**
 * The Gootar player.
 *
 * All the DSP lives in GootarEngine, which knows nothing about JUCE. This class
 * is the plumbing: parameters, a background loader thread, the model library,
 * and preset save/load.
 *
 * The one rule everything here exists to protect: processBlock() never
 * allocates, never blocks and never touches the filesystem. Model loading
 * happens on LoaderThread and reaches the audio thread through
 * GootarEngine's ModelSwapper.
 */
class GootarProcessor : public juce::AudioProcessor,
                        private juce::Timer
{
public:
    GootarProcessor();
    ~GootarProcessor() override;

    // --- AudioProcessor ----------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Gootar Player"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- Gootar ------------------------------------------------------------
    juce::AudioProcessorValueTreeState& state() { return apvts; }
    ModelLibrary& library() { return modelLibrary; }

    /** Hot-swap: returns immediately, the load happens on the loader thread. */
    void requestModel (const juce::File&);
    void requestIR (const juce::File&);
    void clearIR();

    juce::File currentModelFile() const;
    juce::File currentIRFile() const;
    juce::String lastError() const;
    ModelInfo modelInfo() const { return engine.modelInfo (0); }

    /** The chain as it currently stands, for the UI strip. */
    std::vector<ChainSlot> chainSpec() const { return engine.currentChain(); }
    int numModelSlots() const { return engine.numModelSlots(); }

    // --- the pedalboard ----------------------------------------------------
    juce::String addBlock (BlockType type, int position)
    {
        return juce::String (engine.addBlock (type, position));
    }
    void removeBlock (const juce::String& id) { engine.removeBlock (id.toStdString()); }
    void moveBlock (const juce::String& id, int pos) { engine.moveBlock (id.toStdString(), pos); }
    void setBlockEnabled (const juce::String& id, bool on)
    {
        engine.setBlockEnabled (id.toStdString(), on);
    }
    std::vector<GootarEngine::ParamDescriptor> blockParams (const juce::String& id) const
    {
        return engine.blockParams (id.toStdString());
    }
    void setBlockParam (const juce::String& id, const juce::String& key, float value)
    {
        engine.setBlockParam (id.toStdString(), key.toStdString(), value);
    }

    float inputPeak() const noexcept { return engine.inputPeak(); }
    float outputPeak() const noexcept { return engine.outputPeak(); }

    /**
     * Latest tuner reading. Refreshed on this processor's timer rather than in
     * the editor, so closing the window does not stop it and the plugin has a
     * consistent answer whether or not anyone is looking.
     */
    PitchReading latestPitch() const;

    /** Build a preset from the current state, and apply one. */
    Preset capturePreset (const juce::String& name) const;
    bool applyPreset (const Preset&, juce::String& errorOut);

    bool savePresetToFile (const juce::File&, const juce::String& name, juce::String& errorOut);
    bool loadPresetFromFile (const juce::File&, juce::String& errorOut);

    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

private:
    void timerCallback() override;
    Params paramsFromState() const;

    /**
     * Serialises model and IR loading onto one background thread. Requests
     * coalesce: spinning the browser stages only what you land on, because
     * ModelSwapper destroys any staged model the audio thread never picked up.
     */
    class LoaderThread : public juce::Thread
    {
    public:
        explicit LoaderThread (GootarProcessor& o) : juce::Thread ("gootar loader"), owner (o) {}
        void run() override;
        void post() { notify(); }

    private:
        GootarProcessor& owner;
    };

    GootarEngine engine;
    ModelLibrary modelLibrary;
    juce::AudioProcessorValueTreeState apvts;
    LoaderThread loader { *this };

    // Guards the request/current file fields, which are touched by the message
    // and loader threads only — never by the audio thread.
    mutable juce::CriticalSection loadLock;
    juce::File requestedModel, requestedIR, loadedModel, loadedIR;
    bool irClearRequested = false;
    juce::String errorMessage;

    juce::AudioBuffer<float> monoScratch;

    mutable juce::CriticalSection pitchLock;
    PitchReading lastPitch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GootarProcessor)
};

} // namespace gootar

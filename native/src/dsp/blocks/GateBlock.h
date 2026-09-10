#pragma once

#include <memory>

#include "NoiseGate.h"

#include "../Block.h"

namespace gootar {

/**
 * The split noise gate.
 *
 * The gate detects on the clean input and applies its gain reduction after the
 * model. That is not an implementation detail to tidy away — a distorted
 * signal barely changes level as a note decays, so a gate placed after the
 * model chatters. Detecting before it gives the gate a signal that actually
 * falls away.
 *
 * Because it is two points in the chain, it is two blocks sharing one state
 * object. GateTriggerBlock measures; GateGainBlock applies. AudioDSPTools'
 * Trigger pushes its computed reduction to registered Gain listeners, so the
 * two stay in step with no plumbing of our own.
 *
 * They are one thing in the preset format and in the UI. Only the engine knows
 * there are two of them.
 */
struct GateState
{
    ::dsp::noise_gate::Trigger trigger;
    ::dsp::noise_gate::Gain    gain;

    GateState() { trigger.AddListener (&gain); }

    void setSampleRate (double sr) { trigger.SetSampleRate (sr); sampleRate = sr; }

    void setThresholdDb (double db) noexcept
    {
        // Everything but the threshold is hard-coded in the stock plugin's
        // ProcessBlock; keeping these identical keeps the gate feeling the same.
        const ::dsp::noise_gate::TriggerParams params (
            0.01,   // time
            db,     // threshold
            0.1,    // ratio
            0.005,  // open
            0.01,   // hold
            0.05);  // close
        trigger.SetParams (params);
    }

    double sampleRate = 48000.0;
};

class GateTriggerBlock : public Block
{
public:
    GateTriggerBlock (std::string id, std::shared_ptr<GateState> s)
        : Block (std::move (id)), state (std::move (s)) {}

    BlockType type() const noexcept override { return BlockType::Gate; }
    const char* displayName() const noexcept override { return "Gate"; }

    void prepare (double sampleRate, int) override { state->setSampleRate (sampleRate); }

    DSP_SAMPLE** process (DSP_SAMPLE** input, int numChannels, int numFrames) noexcept override
    {
        return state->trigger.Process (input, static_cast<size_t> (numChannels),
                                       static_cast<size_t> (numFrames));
    }

private:
    std::shared_ptr<GateState> state;
};

class GateGainBlock : public Block
{
public:
    GateGainBlock (std::string id, std::shared_ptr<GateState> s)
        : Block (std::move (id)), state (std::move (s)) {}

    BlockType type() const noexcept override { return BlockType::Gate; }
    const char* displayName() const noexcept override { return "Gate"; }

    void prepare (double, int) override {}

    DSP_SAMPLE** process (DSP_SAMPLE** input, int numChannels, int numFrames) noexcept override
    {
        return state->gain.Process (input, static_cast<size_t> (numChannels),
                                    static_cast<size_t> (numFrames));
    }

private:
    std::shared_ptr<GateState> state;
};

} // namespace gootar

#pragma once

#include "../Block.h"
#include "../ToneStack.h"

namespace gootar {

/** Bass / mid / treble, matching the stock plugin's BasicNamToneStack. */
class ToneStackBlock : public Block
{
public:
    explicit ToneStackBlock (std::string id) : Block (std::move (id)) {}

    BlockType type() const noexcept override { return BlockType::ToneStack; }
    const char* displayName() const noexcept override { return "Tone"; }

    void prepare (double sampleRate, int) override { stack.prepare (sampleRate); }

    void setKnobs (double bass, double mid, double treble) noexcept
    {
        stack.setKnobs (bass, mid, treble);
    }

    DSP_SAMPLE** process (DSP_SAMPLE** input, int numChannels, int numFrames) noexcept override
    {
        return stack.process (input, numChannels, numFrames);
    }

private:
    ToneStack stack;
};

} // namespace gootar

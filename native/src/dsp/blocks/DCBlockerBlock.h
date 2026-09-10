#pragma once

#include "RecursiveLinearFilter.h"

#include "../Block.h"

namespace gootar {

/** 5 Hz high-pass, kDCBlockerFrequency in the stock plugin. */
class DCBlockerBlock : public Block
{
public:
    explicit DCBlockerBlock (std::string id) : Block (std::move (id)) {}

    BlockType type() const noexcept override { return BlockType::DCBlocker; }
    const char* displayName() const noexcept override { return "DC"; }

    void prepare (double sampleRate, int) override
    {
        const recursive_linear_filter::HighPassParams params (sampleRate, 5.0);
        filter.SetParams (params);
    }

    DSP_SAMPLE** process (DSP_SAMPLE** input, int numChannels, int numFrames) noexcept override
    {
        return filter.Process (input, static_cast<size_t> (numChannels),
                               static_cast<size_t> (numFrames));
    }

private:
    recursive_linear_filter::HighPass filter;
};

} // namespace gootar

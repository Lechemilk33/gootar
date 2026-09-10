#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "../Block.h"

namespace gootar {

/**
 * A level control. Used for both input and output stages.
 *
 * The extra dB offset exists for the model's own calibration: NeuralAudio
 * reports a recommended input adjustment and a recommended output adjustment,
 * and those have to fold into the same gain rather than becoming a separate
 * multiply.
 */
class GainBlock : public Block
{
public:
    explicit GainBlock (std::string id) : Block (std::move (id)) {}

    BlockType type() const noexcept override { return BlockType::Gain; }
    const char* displayName() const noexcept override { return "Level"; }

    void prepare (double, int maxBlockSize) override
    {
        buffer.assign (static_cast<size_t> (std::max (1, maxBlockSize)), 0.0);
        pointers.assign (1, buffer.data());
        maxFrames = std::max (1, maxBlockSize);
    }

    void setLevelDb (double db) noexcept { levelDb = db; }
    void setCalibrationOffsetDb (double db) noexcept { offsetDb = db; }
    double levelDbValue() const noexcept { return levelDb; }

    DSP_SAMPLE** process (DSP_SAMPLE** input, int, int numFrames) noexcept override
    {
        const int n = std::min (numFrames, maxFrames);
        const double gain = std::pow (10.0, (levelDb + offsetDb) / 20.0);
        for (int i = 0; i < n; ++i)
            buffer[static_cast<size_t> (i)] = input[0][i] * gain;
        return pointers.data();
    }

private:
    std::vector<DSP_SAMPLE>  buffer;
    std::vector<DSP_SAMPLE*> pointers;
    int    maxFrames = 0;
    double levelDb = 0.0;
    double offsetDb = 0.0;
};

} // namespace gootar

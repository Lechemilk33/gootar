#pragma once

#include <string>
#include <vector>

namespace gootar {

/**
 * The public description of a signal chain: what blocks, in what order.
 *
 * Deliberately separate from Block.h and Chain.h, which pull in AudioDSPTools
 * and therefore its global `dsp` namespace. The app needs to describe and edit
 * a chain; it must never need the DSP headers to do so, or `dsp::` becomes
 * ambiguous against `juce::dsp` everywhere it looks.
 *
 * Adding an effect means: a value here, a case in BlockRegistry::getOrCreate,
 * and a case in the app's preset reader/writer. Three obvious places you can
 * read beats a registry with reflection at this size.
 */
enum class BlockType
{
    // The fixed parts of an amp rig.
    Gain,        // input or output level
    Gate,        // split noise gate: detects pre-model, applies post-model
    Model,       // a NAM capture
    ToneStack,   // bass / mid / treble
    IR,          // impulse response
    DCBlocker,   // 5 Hz high-pass

    // Pedals. Put them anywhere: a drive in front of the capture pushes it
    // the way a real pedal pushes a real amp, a delay after the cab sounds
    // like a delay in an effects loop.
    Drive,
    Compressor,
    Delay,
    Reverb,
};

/** Everything a user can add to their board, in menu order. */
inline constexpr BlockType kAddableBlocks[] = {
    BlockType::Model, BlockType::Drive, BlockType::Compressor,
    BlockType::Delay, BlockType::Reverb, BlockType::ToneStack, BlockType::IR,
};

const char* toString (BlockType) noexcept;
bool blockTypeFromString (const std::string&, BlockType&) noexcept;

/** One entry in a chain as the user (and the preset file) describes it. */
struct ChainSlot
{
    BlockType   type = BlockType::Model;
    std::string id;
    bool        enabled = true;
};

/** The chain the stock plugin implements, and what a new preset starts as. */
std::vector<ChainSlot> standardChainSpec();

} // namespace gootar

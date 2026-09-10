#pragma once

#include <memory>
#include <string>

#include "dsp.h" // DSP_SAMPLE

#include "ChainSpec.h"

namespace gootar {

/**
 * One stage of the signal chain.
 *
 * Blocks follow the AudioDSPTools convention: process() takes input pointers
 * and returns pointers to wherever the result actually landed, which may be
 * the block's own buffer. That lets a chain be a fold over blocks with no
 * copying between stages.
 *
 * THREADING
 *   prepare(), and anything that allocates, are loader/message thread only.
 *   process() is audio thread and must not allocate, lock or touch the disk.
 */
class Block
{
public:
    explicit Block (std::string blockId) : id (std::move (blockId)) {}
    virtual ~Block() = default;

    Block (const Block&) = delete;
    Block& operator= (const Block&) = delete;

    virtual BlockType type() const noexcept = 0;
    virtual const char* displayName() const noexcept = 0;

    /** [Loader thread] Allocate for this rate and block size. */
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;

    /** [Audio thread] Clear filter history and any internal state. */
    virtual void reset() noexcept {}

    /** [Audio thread] Returns the buffers holding this block's output. */
    virtual DSP_SAMPLE** process (DSP_SAMPLE** input, int numChannels, int numFrames) noexcept = 0;

    /**
     * Stable across a chain rebuild. When the user reorders their board, the
     * new chain steals the existing block with the same id rather than
     * constructing a fresh one — so filter history, gate state and loaded
     * models all survive the edit instead of clicking.
     */
    const std::string& blockId() const noexcept { return id; }

    /** Bypassed blocks stay in the chain and keep their state. */
    bool enabled = true;

private:
    std::string id;
};

using BlockPtr = std::unique_ptr<Block>;

} // namespace gootar

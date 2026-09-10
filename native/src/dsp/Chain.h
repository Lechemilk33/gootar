#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Block.h"
#include "ChainSpec.h"

namespace gootar {

class ModelBlock;
class IRBlock;
class ToneStackBlock;
class GainBlock;
struct GateState;

/**
 * Owns every block that exists, keyed by id. The loader thread creates blocks
 * here; nothing else ever owns one.
 *
 * WHY OWNERSHIP LIVES HERE AND NOT IN THE CHAIN
 * The obvious design - each Chain owns its blocks, and rebuilding moves the
 * unchanged ones across - races. Rebuilding happens on the loader thread while
 * the audio thread is still running the *old* chain, so moving a block out of
 * it (and shrinking its vector) corrupts the sequence being iterated.
 *
 * Separating ownership from ordering removes the problem entirely: a rebuild
 * only ever constructs a new list of pointers, and the old chain is left
 * exactly as the audio thread found it until it is retired. Blocks outlive any
 * individual chain, so a reorder costs no state - filter history, gate
 * position and loaded models all simply stay where they are.
 */
class BlockRegistry
{
public:
    BlockRegistry() = default;
    ~BlockRegistry();

    BlockRegistry (const BlockRegistry&) = delete;
    BlockRegistry& operator= (const BlockRegistry&) = delete;

    /** [Loader thread] Fetch the block with this id, creating it if new. */
    Block* getOrCreate (BlockType, const std::string& id, double sampleRate, int maxBlockSize);

    /** [Loader thread] Look up an existing block, or null. Creates nothing. */
    Block* find (const std::string& id) const noexcept;

    /** [Loader thread] Re-prepare everything for a new rate or block size. */
    void prepareAll (double sampleRate, int maxBlockSize);

    /** [Loader thread] Free models and IRs the audio thread has finished with. */
    void collectGarbage() noexcept;

    /**
     * [Loader thread] Destroy blocks no live chain references any more.
     *
     * Only safe once every retired chain has been collected, so the engine
     * calls it after the swapper's garbage collection, never before.
     */
    void prune (const std::vector<std::string>& liveIds);

    std::shared_ptr<GateState>& gateState() noexcept { return gate; }

private:
    std::map<std::string, BlockPtr> blocks;
    std::shared_ptr<GateState> gate;
};

/**
 * An ordering over registry-owned blocks, and the fold that runs audio
 * through them. Holds no ownership, so building one is cheap and swapping one
 * in is just a pointer handover.
 *
 * THE GATE
 * A gate is one slot to the user but two points in the signal: it detects on
 * the clean input and applies after the last model, because gating a distorted
 * signal chatters. build() expands the single slot into a trigger at the
 * slot's position and a gain stage after the final model. Nothing above this
 * class needs to know there are two.
 */
class Chain
{
public:
    Chain() = default;

    /** [Loader thread] Realise a spec against the registry. */
    static std::unique_ptr<Chain> build (const std::vector<ChainSlot>& spec,
                                         BlockRegistry& registry,
                                         double sampleRate,
                                         int maxBlockSize);

    /** [Audio thread] Run the fold. Disabled blocks are skipped, not removed. */
    DSP_SAMPLE** process (DSP_SAMPLE** input, int numChannels, int numFrames) noexcept;

    const std::vector<Block*>& all() const noexcept { return order; }

    /**
     * Typed handles, resolved once when the chain is built.
     *
     * The audio thread applies parameters through these. Searching the chain
     * or using dynamic_cast per buffer would work and would also be exactly
     * the kind of thing that quietly costs you a dropout at 64 samples.
     */
    ModelBlock*     modelAt (int slotIndex) const noexcept;
    int             numModels() const noexcept { return static_cast<int> (cachedModels.size()); }
    const std::vector<ModelBlock*>& models() const noexcept { return cachedModels; }
    IRBlock*        firstIR() const noexcept { return cachedIR; }
    ToneStackBlock* firstToneStack() const noexcept { return cachedToneStack; }
    GainBlock*      inputGain() const noexcept { return cachedInputGain; }
    GainBlock*      outputGain() const noexcept { return cachedOutputGain; }

    void setGateThresholdDb (double) noexcept;
    void setEnabled (BlockType, bool) noexcept;

    /** Ids this chain references, for BlockRegistry::prune. */
    std::vector<std::string> referencedIds() const;

private:
    std::vector<Block*> order;
    std::shared_ptr<GateState> gate;
    std::vector<ModelBlock*> cachedModels;
    GainBlock*      cachedInputGain = nullptr;
    GainBlock*      cachedOutputGain = nullptr;
    IRBlock*        cachedIR = nullptr;
    ToneStackBlock* cachedToneStack = nullptr;
};

} // namespace gootar

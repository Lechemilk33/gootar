#include "Chain.h"

#include <algorithm>

#include "blocks/CompressorBlock.h"
#include "blocks/DCBlockerBlock.h"
#include "blocks/DelayBlock.h"
#include "blocks/DriveBlock.h"
#include "blocks/GainBlock.h"
#include "blocks/GateBlock.h"
#include "blocks/IRBlock.h"
#include "blocks/ModelBlock.h"
#include "blocks/ReverbBlock.h"
#include "blocks/ToneStackBlock.h"

namespace gootar {

const char* toString (BlockType t) noexcept
{
    switch (t)
    {
        case BlockType::Gain:      return "gain";
        case BlockType::Gate:      return "gate";
        case BlockType::Model:     return "model";
        case BlockType::ToneStack: return "toneStack";
        case BlockType::IR:        return "ir";
        case BlockType::DCBlocker: return "dcBlocker";
        case BlockType::Drive:      return "drive";
        case BlockType::Compressor: return "compressor";
        case BlockType::Delay:      return "delay";
        case BlockType::Reverb:     return "reverb";
    }
    return "unknown";
}

bool blockTypeFromString (const std::string& s, BlockType& out) noexcept
{
    if (s == "gain")      { out = BlockType::Gain;      return true; }
    if (s == "gate")      { out = BlockType::Gate;      return true; }
    if (s == "model")     { out = BlockType::Model;     return true; }
    if (s == "toneStack") { out = BlockType::ToneStack; return true; }
    if (s == "ir")        { out = BlockType::IR;        return true; }
    if (s == "dcBlocker") { out = BlockType::DCBlocker; return true; }
    if (s == "drive")      { out = BlockType::Drive;      return true; }
    if (s == "compressor") { out = BlockType::Compressor; return true; }
    if (s == "delay")      { out = BlockType::Delay;      return true; }
    if (s == "reverb")     { out = BlockType::Reverb;     return true; }
    return false;
}

std::vector<ChainSlot> standardChainSpec()
{
    return {
        { BlockType::Gain,      "input",   true },
        { BlockType::Gate,      "gate",    true },
        { BlockType::Model,     "model-1", true },
        { BlockType::ToneStack, "tone",    true },
        { BlockType::IR,        "ir",      true },
        { BlockType::DCBlocker, "dc",      true },
        { BlockType::Gain,      "output",  true },
    };
}

// --- BlockRegistry ---------------------------------------------------------

BlockRegistry::~BlockRegistry() = default;

Block* BlockRegistry::getOrCreate (BlockType type, const std::string& id,
                                   double sampleRate, int maxBlockSize)
{
    if (auto it = blocks.find (id); it != blocks.end() && it->second->type() == type)
        return it->second.get();

    BlockPtr block;
    switch (type)
    {
        case BlockType::Gain:      block = std::make_unique<GainBlock> (id); break;
        case BlockType::ToneStack: block = std::make_unique<ToneStackBlock> (id); break;
        case BlockType::Model:     block = std::make_unique<ModelBlock> (id); break;
        case BlockType::IR:        block = std::make_unique<IRBlock> (id); break;
        case BlockType::DCBlocker: block = std::make_unique<DCBlockerBlock> (id); break;
        case BlockType::Drive:      block = std::make_unique<DriveBlock> (id); break;
        case BlockType::Compressor: block = std::make_unique<CompressorBlock> (id); break;
        case BlockType::Delay:      block = std::make_unique<DelayBlock> (id); break;
        case BlockType::Reverb:     block = std::make_unique<ReverbBlock> (id); break;
        case BlockType::Gate:
            if (! gate)
                gate = std::make_shared<GateState>();
            block = (id.size() > 5 && id.compare (id.size() - 5, 5, "-gain") == 0)
                      ? BlockPtr (std::make_unique<GateGainBlock> (id, gate))
                      : BlockPtr (std::make_unique<GateTriggerBlock> (id, gate));
            break;
    }
    if (! block)
        return nullptr;

    block->prepare (sampleRate, maxBlockSize);
    auto* raw = block.get();
    blocks[id] = std::move (block);
    return raw;
}

Block* BlockRegistry::find (const std::string& id) const noexcept
{
    const auto it = blocks.find (id);
    return it != blocks.end() ? it->second.get() : nullptr;
}

void BlockRegistry::prepareAll (double sampleRate, int maxBlockSize)
{
    for (auto& [id, block] : blocks)
        block->prepare (sampleRate, maxBlockSize);
}

void BlockRegistry::collectGarbage() noexcept
{
    for (auto& [id, block] : blocks)
    {
        if (auto* m = dynamic_cast<ModelBlock*> (block.get())) m->collectGarbage();
        else if (auto* ir = dynamic_cast<IRBlock*> (block.get())) ir->collectGarbage();
    }
}

void BlockRegistry::prune (const std::vector<std::string>& liveIds)
{
    for (auto it = blocks.begin(); it != blocks.end();)
    {
        const bool live = std::find (liveIds.begin(), liveIds.end(), it->first) != liveIds.end();
        it = live ? std::next (it) : blocks.erase (it);
    }
}

// --- Chain -----------------------------------------------------------------

std::unique_ptr<Chain> Chain::build (const std::vector<ChainSlot>& spec,
                                     BlockRegistry& registry,
                                     double sampleRate,
                                     int maxBlockSize)
{
    auto chain = std::make_unique<Chain>();

    for (const auto& slot : spec)
    {
        auto* block = registry.getOrCreate (slot.type, slot.id, sampleRate, maxBlockSize);
        if (block == nullptr)
            continue;
        block->enabled = slot.enabled;
        chain->order.push_back (block);
    }

    chain->gate = registry.gateState();

    // Expand the gate's second half: it applies after the last model, since
    // gating a distorted signal chatters. With no model it sits right after
    // the trigger - a no-op, but it keeps the chain well-formed.
    const auto triggerIt = std::find_if (chain->order.begin(), chain->order.end(),
        [] (Block* b) { return b->type() == BlockType::Gate; });

    if (triggerIt != chain->order.end())
    {
        auto lastModel = chain->order.end();
        for (auto it = chain->order.begin(); it != chain->order.end(); ++it)
            if ((*it)->type() == BlockType::Model)
                lastModel = it;

        const std::string gainId = (*triggerIt)->blockId() + "-gain";
        const bool gateOn = (*triggerIt)->enabled;
        const auto insertPos = ((lastModel != chain->order.end()) ? lastModel : triggerIt) + 1;

        if (auto* gainBlock = registry.getOrCreate (BlockType::Gate, gainId,
                                                    sampleRate, maxBlockSize))
        {
            gainBlock->enabled = gateOn;
            chain->order.insert (insertPos, gainBlock);
        }
    }

    // Resolve every typed handle once, here on the loader thread.
    for (auto* block : chain->order)
    {
        if (auto* g = dynamic_cast<GainBlock*> (block))
        {
            if (block->blockId() == "input")  chain->cachedInputGain = g;
            if (block->blockId() == "output") chain->cachedOutputGain = g;
        }
        else if (auto* m = dynamic_cast<ModelBlock*> (block))
            chain->cachedModels.push_back (m);
        else if (auto* ir = dynamic_cast<IRBlock*> (block))
        {
            if (chain->cachedIR == nullptr) chain->cachedIR = ir;
        }
        else if (auto* t = dynamic_cast<ToneStackBlock*> (block))
        {
            if (chain->cachedToneStack == nullptr) chain->cachedToneStack = t;
        }
    }

    return chain;
}

DSP_SAMPLE** Chain::process (DSP_SAMPLE** input, int numChannels, int numFrames) noexcept
{
    DSP_SAMPLE** stage = input;
    for (auto* block : order)
        if (block->enabled)
            stage = block->process (stage, numChannels, numFrames);
    return stage;
}

ModelBlock* Chain::modelAt (int slotIndex) const noexcept
{
    if (slotIndex < 0 || slotIndex >= static_cast<int> (cachedModels.size()))
        return nullptr;
    return cachedModels[static_cast<size_t> (slotIndex)];
}

void Chain::setGateThresholdDb (double db) noexcept
{
    if (gate)
        gate->setThresholdDb (db);
}

void Chain::setEnabled (BlockType type, bool on) noexcept
{
    // Both halves of the gate flip together, or it detects without applying.
    for (auto* block : order)
        if (block->type() == type)
            block->enabled = on;
}

std::vector<std::string> Chain::referencedIds() const
{
    std::vector<std::string> ids;
    ids.reserve (order.size());
    for (auto* block : order)
        ids.push_back (block->blockId());
    return ids;
}

} // namespace gootar

#include "GootarEngine.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

#include "../ModelSwapper.h"
#include "Chain.h"
#include "PitchDetector.h"
#include "blocks/EffectBlock.h"
#include "blocks/GainBlock.h"
#include "blocks/IRBlock.h"
#include "blocks/ModelBlock.h"
#include "blocks/ToneStackBlock.h"

namespace gootar {

namespace {

inline double clampd (double v, double lo, double hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/** One mono channel throughout, as in the stock plugin. */
constexpr int kChannels = 1;

/** ~43 ms at 48 k: long enough to resolve a low E, short enough to feel live. */
constexpr int kAnalysisWindow = 2048;

} // namespace

struct GootarEngine::Impl
{
    BlockRegistry registry;
    ModelSwapper<Chain> chainSwapper;
    std::vector<ChainSlot> spec = standardChainSpec();
    mutable std::mutex specMutex;

    std::vector<DSP_SAMPLE>  monoBuffer;
    std::vector<DSP_SAMPLE*> monoPointers;
    std::vector<float>       analysisScratch;

    Params params;

    double currentSampleRate = 48000.0;
    int    currentMaxBlock = 512;
    bool   prepared = false;

    std::atomic<float> peakIn { 0.0f };
    std::atomic<float> peakOut { 0.0f };

    AnalysisTap   tap;
    PitchDetector detector;

    ModelBlock* modelForSlot (int slot);
    IRBlock*    irBlock();
};

namespace {

/** Id of the Nth block of a given type in a spec, or empty. */
std::string slotId (const std::vector<ChainSlot>& spec, BlockType type, int index)
{
    int seen = 0;
    for (const auto& slot : spec)
        if (slot.type == type && seen++ == index)
            return slot.id;
    return {};
}

} // namespace

GootarEngine::GootarEngine() : impl (std::make_unique<Impl>()) {}
GootarEngine::~GootarEngine() = default;

double GootarEngine::sampleRate() const noexcept { return impl->currentSampleRate; }
int    GootarEngine::maxBlockSize() const noexcept { return impl->currentMaxBlock; }

void GootarEngine::prepare (double sampleRateHz, int maxBlock)
{
    auto& s = *impl;
    s.currentSampleRate = sampleRateHz;
    s.currentMaxBlock = std::max (1, maxBlock);

    s.monoBuffer.assign (static_cast<size_t> (s.currentMaxBlock), 0.0);
    s.monoPointers.assign (kChannels, nullptr);
    s.monoPointers[0] = s.monoBuffer.data();

    s.registry.prepareAll (s.currentSampleRate, s.currentMaxBlock);

    s.tap.prepare (kAnalysisWindow);
    s.detector.prepare (s.currentSampleRate, kAnalysisWindow);
    s.analysisScratch.assign (kAnalysisWindow, 0.0f);

    std::vector<ChainSlot> specCopy;
    {
        std::lock_guard<std::mutex> lock (s.specMutex);
        specCopy = s.spec;
    }
    s.chainSwapper.stage (Chain::build (specCopy, s.registry,
                                        s.currentSampleRate, s.currentMaxBlock));
    s.prepared = true;
}

void GootarEngine::setParams (const Params& p) noexcept
{
    auto& s = *impl;
    auto& q = s.params;
    q = p;
    q.inputLevelDb  = clampd (p.inputLevelDb, -20.0, 20.0);
    q.outputLevelDb = clampd (p.outputLevelDb, -40.0, 40.0);
    q.gateThresholdDb = clampd (p.gateThresholdDb, -100.0, 0.0);
    q.bass   = clampd (p.bass, 0.0, 10.0);
    q.mid    = clampd (p.mid, 0.0, 10.0);
    q.treble = clampd (p.treble, 0.0, 10.0);
    q.inputCalibrationLevelDbu = clampd (p.inputCalibrationLevelDbu, -60.0, 60.0);
}

void GootarEngine::process (const float* input, float* output, int numSamples) noexcept
{
    auto& s = *impl;
    if (! s.prepared || numSamples <= 0)
        return;

    // Never run past what prepare() allocated. Anything more is a host bug,
    // but silently corrupting memory is not the way to report it.
    const int n = std::min (numSamples, s.currentMaxBlock);

    s.chainSwapper.applyStaged();
    auto* chain = s.chainSwapper.current();
    if (chain == nullptr)
    {
        for (int i = 0; i < numSamples; ++i)
            output[i] = 0.0f;
        return;
    }

    const auto& params = s.params;

    // Push the CLEAN input to the tuner before anything touches it.
    s.tap.push (input, n);

    float inPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float v = input[i];
        inPeak = std::max (inPeak, std::abs (v));
        s.monoBuffer[static_cast<size_t> (i)] = static_cast<DSP_SAMPLE> (v);
    }
    s.peakIn.store (inPeak, std::memory_order_relaxed);

    // --- push parameters into the blocks (pointer chasing only) -----------
    const auto& models = chain->models();
    ModelBlock* firstModel = models.empty() ? nullptr : models.front();

    if (auto* gain = chain->inputGain())
    {
        gain->setLevelDb (params.inputLevelDb);
        gain->setCalibrationOffsetDb (
            (params.calibrateInput && firstModel != nullptr && firstModel->isLoaded())
                ? static_cast<double> (firstModel->inputAdjustmentDb())
                : 0.0);
    }

    chain->setGateThresholdDb (params.gateThresholdDb);
    chain->setEnabled (BlockType::Gate, params.gateEnabled);
    chain->setEnabled (BlockType::ToneStack, params.toneStackEnabled);
    chain->setEnabled (BlockType::IR, params.irEnabled);

    if (auto* tone = chain->firstToneStack())
        tone->setKnobs (params.bass, params.mid, params.treble);

    if (auto* gain = chain->outputGain())
    {
        gain->setLevelDb (params.outputLevelDb);
        // Normalisation follows the LAST model in the chain, since that is
        // what sets the level actually leaving the amp.
        ModelBlock* lastModel = models.empty() ? nullptr : models.back();
        gain->setCalibrationOffsetDb (
            (params.outputMode != OutputMode::Raw && lastModel != nullptr && lastModel->isLoaded())
                ? static_cast<double> (lastModel->outputAdjustmentDb())
                : 0.0);
    }

    // --- run the chain -----------------------------------------------------
    DSP_SAMPLE** result = chain->process (s.monoPointers.data(), kChannels, n);

    float outPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const auto v = static_cast<float> (result[0][i]);
        outPeak = std::max (outPeak, std::abs (v));
        output[i] = v;
    }
    s.peakOut.store (outPeak, std::memory_order_relaxed);

    for (int i = n; i < numSamples; ++i)
        output[i] = 0.0f;
}

// --- loading ---------------------------------------------------------------

/**
 * Resolve a model slot on the LOADER thread.
 *
 * Deliberately not via chainSwapper.current(): a chain that has been staged is
 * not current until the audio thread picks it up, so anything routed through
 * the live chain fails for the first few milliseconds after prepare() - which
 * is exactly when a preset tries to load its model. Blocks are owned by the
 * registry, so the loader thread can resolve them from the spec without
 * involving the audio thread at all.
 */
ModelBlock* GootarEngine::Impl::modelForSlot (int slot)
{
    std::string id;
    {
        std::lock_guard<std::mutex> lock (specMutex);
        id = slotId (spec, BlockType::Model, slot);
    }
    if (id.empty())
        return nullptr;
    return dynamic_cast<ModelBlock*> (
        registry.getOrCreate (BlockType::Model, id, currentSampleRate, currentMaxBlock));
}

IRBlock* GootarEngine::Impl::irBlock()
{
    std::string id;
    {
        std::lock_guard<std::mutex> lock (specMutex);
        id = slotId (spec, BlockType::IR, 0);
    }
    if (id.empty())
        return nullptr;
    return dynamic_cast<IRBlock*> (
        registry.getOrCreate (BlockType::IR, id, currentSampleRate, currentMaxBlock));
}

bool GootarEngine::loadModel (int slot, const std::filesystem::path& path, std::string& errorOut)
{
    auto* block = impl->modelForSlot (slot);
    if (block == nullptr)
    {
        errorOut = "no model slot " + std::to_string (slot) + " in this chain";
        return false;
    }
    return block->load (path, impl->params.inputCalibrationLevelDbu, errorOut);
}

void GootarEngine::clearModel (int slot)
{
    if (auto* block = impl->modelForSlot (slot))
        block->unload();
}

bool GootarEngine::loadIR (const std::filesystem::path& path, std::string& errorOut)
{
    auto* ir = impl->irBlock();
    if (ir == nullptr) { errorOut = "no IR block in this chain"; return false; }
    return ir->load (path, errorOut);
}

void GootarEngine::clearIR()
{
    if (auto* ir = impl->irBlock())
        ir->unload();
}

void GootarEngine::setChain (const std::vector<ChainSlot>& spec)
{
    auto& s = *impl;
    {
        std::lock_guard<std::mutex> lock (s.specMutex);
        s.spec = spec;
    }
    s.chainSwapper.stage (Chain::build (spec, s.registry, s.currentSampleRate, s.currentMaxBlock));
}

std::vector<ChainSlot> GootarEngine::currentChain() const
{
    std::lock_guard<std::mutex> lock (impl->specMutex);
    return impl->spec;
}

namespace {

/** A readable, unique id for a newly added block: "drive-1", "drive-2"... */
std::string makeBlockId (const std::vector<ChainSlot>& spec, BlockType type)
{
    const std::string base = toString (type);
    for (int n = 1; n < 1000; ++n)
    {
        const auto candidate = base + "-" + std::to_string (n);
        const bool taken = std::any_of (spec.begin(), spec.end(),
            [&] (const ChainSlot& s) { return s.id == candidate; });
        if (! taken)
            return candidate;
    }
    return base;
}

} // namespace

std::string GootarEngine::addBlock (BlockType type, int position)
{
    auto spec = currentChain();
    const auto id = makeBlockId (spec, type);
    const int clamped = std::clamp (position, 0, static_cast<int> (spec.size()));
    spec.insert (spec.begin() + clamped, ChainSlot { type, id, true });
    setChain (spec);
    return id;
}

void GootarEngine::removeBlock (const std::string& id)
{
    auto spec = currentChain();
    spec.erase (std::remove_if (spec.begin(), spec.end(),
                                [&] (const ChainSlot& s) { return s.id == id; }),
                spec.end());
    setChain (spec);
}

void GootarEngine::moveBlock (const std::string& id, int newPosition)
{
    auto spec = currentChain();
    const auto it = std::find_if (spec.begin(), spec.end(),
                                  [&] (const ChainSlot& s) { return s.id == id; });
    if (it == spec.end())
        return;

    const ChainSlot slot = *it;
    spec.erase (it);
    const int clamped = std::clamp (newPosition, 0, static_cast<int> (spec.size()));
    spec.insert (spec.begin() + clamped, slot);
    setChain (spec);
}

void GootarEngine::setBlockEnabled (const std::string& id, bool enabled)
{
    auto spec = currentChain();
    for (auto& slot : spec)
        if (slot.id == id)
            slot.enabled = enabled;
    setChain (spec);
}

std::vector<GootarEngine::ParamDescriptor>
GootarEngine::blockParams (const std::string& id) const
{
    std::vector<ParamDescriptor> out;
    auto* effect = dynamic_cast<EffectBlock*> (impl->registry.find (id));
    if (effect == nullptr)
        return out;

    for (const auto& info : effect->params())
        out.push_back (ParamDescriptor {
            info.key, info.label, info.suffix,
            info.min, info.max, info.step, effect->getParam (info.key) });
    return out;
}

void GootarEngine::setBlockParam (const std::string& id, const std::string& key, float value)
{
    if (auto* effect = dynamic_cast<EffectBlock*> (impl->registry.find (id)))
        effect->setParam (key, value);
}

void GootarEngine::collectGarbage() noexcept
{
    auto& s = *impl;
    s.chainSwapper.collectRetired();
    s.registry.collectGarbage();
    // Pruning must come after the retired chains are gone, or a chain the
    // audio thread still holds could reference a block we just destroyed.
    if (auto* chain = s.chainSwapper.current())
        s.registry.prune (chain->referencedIds());
}

bool GootarEngine::modelNeedsReload() const noexcept
{
    const auto spec = currentChain();
    for (const auto& slot : spec)
        if (slot.type == BlockType::Model)
            if (auto* m = dynamic_cast<ModelBlock*> (impl->registry.find (slot.id)))
                if (m->needsReload())
                    return true;
    return false;
}

int GootarEngine::numModelSlots() const noexcept
{
    const auto spec = currentChain();
    int n = 0;
    for (const auto& slot : spec)
        if (slot.type == BlockType::Model)
            ++n;
    return n;
}

ModelInfo GootarEngine::modelInfo (int slot) const
{
    ModelInfo out;
    const auto spec = currentChain();
    const auto id = slotId (spec, BlockType::Model, slot);
    if (id.empty())
        return out;
    auto* block = dynamic_cast<ModelBlock*> (impl->registry.find (id));
    if (block == nullptr)
        return out;

    const auto info = block->info();
    out.loaded = info.loaded;
    out.fileName = info.fileName;
    out.filePath = info.filePath;
    out.architecture = info.architecture;
    out.sampleRate = info.sampleRate;
    out.receptiveField = info.receptiveField;
    out.isStatic = info.isStatic;
    return out;
}

std::string GootarEngine::irFileName() const
{
    const auto spec = currentChain();
    const auto id = slotId (spec, BlockType::IR, 0);
    if (id.empty())
        return {};
    if (auto* ir = dynamic_cast<IRBlock*> (impl->registry.find (id)))
        return ir->info().fileName;
    return {};
}

float GootarEngine::inputPeak() const noexcept
{
    return impl->peakIn.load (std::memory_order_relaxed);
}

float GootarEngine::outputPeak() const noexcept
{
    return impl->peakOut.load (std::memory_order_relaxed);
}

PitchReading GootarEngine::analysePitch()
{
    auto& s = *impl;
    PitchReading reading;

    const float* window = s.tap.readLatest();
    if (window == nullptr)
        return reading;

    const auto result = s.detector.analyse (window, s.tap.windowSize());
    reading.voiced = result.voiced;
    reading.frequencyHz = result.frequencyHz;
    reading.clarity = result.clarity;
    reading.midiNote = result.midiNote;
    reading.cents = result.cents;
    reading.rms = result.rms;
    if (result.voiced)
    {
        reading.noteName = PitchDetector::noteName (result.midiNote);
        reading.nearestString = PitchDetector::nearestOpenString (result.midiNote);
    }
    return reading;
}

} // namespace gootar

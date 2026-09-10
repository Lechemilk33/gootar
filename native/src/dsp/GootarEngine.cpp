#include "GootarEngine.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

#include "ImpulseResponse.h"
#include "NoiseGate.h"
#include "RecursiveLinearFilter.h"

#include "../ModelSwapper.h"
#include "ToneStack.h"

#include "NeuralAudio/NeuralModel.h"

namespace gootar {

namespace {

inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }

inline double clampd (double v, double lo, double hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/** Gate trigger constants, hard-coded in the stock plugin's ProcessBlock.
    Only the threshold is a parameter. */
constexpr double kGateTime      = 0.01;
constexpr double kGateRatio     = 0.1;
constexpr double kGateOpenTime  = 0.005;
constexpr double kGateHoldTime  = 0.01;
constexpr double kGateCloseTime = 0.05;

/** kDCBlockerFrequency */
constexpr double kDcBlockerHz = 5.0;

/** One mono channel throughout, as in the stock plugin. */
constexpr int kChannels = 1;

} // namespace

struct GootarEngine::Impl
{
    ModelSwapper<NeuralAudio::NeuralModel> modelSwapper;
    ModelSwapper<::dsp::ImpulseResponse>   irSwapper;

    ::dsp::noise_gate::Trigger gateTrigger;
    ::dsp::noise_gate::Gain    gateGain;
    ToneStack                  toneStack;
    recursive_linear_filter::HighPass dcBlocker;

    std::vector<DSP_SAMPLE>  monoBuffer;
    std::vector<DSP_SAMPLE*> monoPointers;
    std::vector<float>       modelIn, modelOut;

    Params params;

    double currentSampleRate = 48000.0;
    int    currentMaxBlock = 512;
    bool   prepared = false;
    double lastDcBlockerRate = -1.0;

    std::atomic<double> loadedModelRate { 0.0 };
    mutable std::mutex  infoMutex;
    ModelInfo           info;

    Impl()
    {
        // The trigger pushes its computed gain reduction to the gain stage,
        // which is what lets the gate detect pre-model and apply post-model.
        gateTrigger.AddListener (&gateGain);
    }
};

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

    s.modelIn.assign (static_cast<size_t> (s.currentMaxBlock), 0.0f);
    s.modelOut.assign (static_cast<size_t> (s.currentMaxBlock), 0.0f);

    s.toneStack.prepare (s.currentSampleRate);
    s.gateTrigger.SetSampleRate (s.currentSampleRate);

    s.lastDcBlockerRate = -1.0; // force the DC blocker to recompute
    s.prepared = true;
}

void GootarEngine::setParams (const Params& p) noexcept
{
    auto& q = impl->params;
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

    // Pick up a staged model/IR. The only place a swap happens.
    s.modelSwapper.applyStaged();
    s.irSwapper.applyStaged();

    auto* model = s.modelSwapper.current();
    auto* ir    = s.irSwapper.current();
    const auto& params = s.params;

    // --- 1. input level (and input calibration) ---------------------------
    double inputGainDb = params.inputLevelDb;
    if (params.calibrateInput && model != nullptr)
        inputGainDb += static_cast<double> (model->GetRecommendedInputDBAdjustment());

    const double inGain = dbToGain (inputGainDb);
    for (int i = 0; i < n; ++i)
        s.monoBuffer[static_cast<size_t> (i)] = static_cast<DSP_SAMPLE> (input[i]) * inGain;

    // --- 2. gate TRIGGER, on the clean input ------------------------------
    DSP_SAMPLE** stage = s.monoPointers.data();
    if (params.gateEnabled)
    {
        const ::dsp::noise_gate::TriggerParams triggerParams (
            kGateTime, params.gateThresholdDb, kGateRatio,
            kGateOpenTime, kGateHoldTime, kGateCloseTime);
        s.gateTrigger.SetParams (triggerParams);
        s.gateTrigger.SetSampleRate (s.currentSampleRate);
        stage = s.gateTrigger.Process (s.monoPointers.data(), kChannels, static_cast<size_t> (n));
    }

    // --- 3. the model ------------------------------------------------------
    if (model != nullptr)
    {
        for (int i = 0; i < n; ++i)
            s.modelIn[static_cast<size_t> (i)] = static_cast<float> (stage[0][i]);

        model->Process (s.modelIn.data(), s.modelOut.data(), static_cast<size_t> (n));

        for (int i = 0; i < n; ++i)
            s.monoBuffer[static_cast<size_t> (i)] =
                static_cast<DSP_SAMPLE> (s.modelOut[static_cast<size_t> (i)]);
    }
    else if (stage != s.monoPointers.data())
    {
        // No model: carry the gate's output forward unchanged.
        for (int i = 0; i < n; ++i)
            s.monoBuffer[static_cast<size_t> (i)] = stage[0][i];
    }
    stage = s.monoPointers.data();

    // --- 4. gate GAIN, after the model ------------------------------------
    if (params.gateEnabled)
        stage = s.gateGain.Process (stage, kChannels, static_cast<size_t> (n));

    // --- 5. tone stack -----------------------------------------------------
    if (params.toneStackEnabled)
    {
        s.toneStack.setKnobs (params.bass, params.mid, params.treble);
        stage = s.toneStack.process (stage, kChannels, n);
    }

    // --- 6. IR -------------------------------------------------------------
    if (params.irEnabled && ir != nullptr)
        stage = ir->Process (stage, kChannels, static_cast<size_t> (n));

    // --- 7. DC blocker (5 Hz high-pass) ------------------------------------
    if (s.lastDcBlockerRate != s.currentSampleRate)
    {
        const recursive_linear_filter::HighPassParams hp (s.currentSampleRate, kDcBlockerHz);
        s.dcBlocker.SetParams (hp);
        s.lastDcBlockerRate = s.currentSampleRate;
    }
    stage = s.dcBlocker.Process (stage, kChannels, static_cast<size_t> (n));

    // --- 8. output level ---------------------------------------------------
    double outputGainDb = params.outputLevelDb;
    if (params.outputMode != OutputMode::Raw && model != nullptr)
        outputGainDb += static_cast<double> (model->GetRecommendedOutputDBAdjustment());

    const double outGain = dbToGain (outputGainDb);
    for (int i = 0; i < n; ++i)
        output[i] = static_cast<float> (stage[0][i] * outGain);

    // A host that asked for more samples than we prepared for gets silence in
    // the tail rather than stale memory.
    for (int i = n; i < numSamples; ++i)
        output[i] = 0.0f;
}

bool GootarEngine::stageModel (const std::filesystem::path& path, std::string& errorOut)
{
    auto& s = *impl;

    NeuralAudio::NeuralModelLoader loader;
    loader.SetExternalSampleRate (static_cast<int> (s.currentSampleRate));
    loader.SetDefaultMaxAudioBufferSize (s.currentMaxBlock);
    loader.SetAudioInputLevelDBu (static_cast<float> (s.params.inputCalibrationLevelDbu));

    // doPrewarm defaults to true: these are stateful recurrent nets and the
    // first samples out of a cold model are garbage. NeuralAudio handles it.
    NeuralAudio::NeuralModel* raw = nullptr;
    try
    {
        raw = loader.CreateFromFile (path);
    }
    catch (const std::exception& e)
    {
        errorOut = e.what();
        return false;
    }

    if (raw == nullptr)
    {
        errorOut = "could not load \"" + path.filename().string() + "\"";
        return false;
    }

    raw->SetMaxAudioBufferSize (s.currentMaxBlock);
    raw->SetAudioInputLevelDBu (static_cast<float> (s.params.inputCalibrationLevelDbu));

    {
        std::lock_guard<std::mutex> lock (s.infoMutex);
        s.info.loaded = true;
        s.info.fileName = path.filename().string();
        s.info.architecture = raw->GetMetadata ("architecture");
        s.info.sampleRate = raw->GetSampleRate();
        s.info.receptiveField = raw->GetReceptiveFieldSize();
        s.info.isStatic = raw->IsStatic();
    }
    s.loadedModelRate.store (s.currentSampleRate);

    s.modelSwapper.stage (std::unique_ptr<NeuralAudio::NeuralModel> (raw));
    return true;
}

void GootarEngine::clearModel()
{
    impl->modelSwapper.stage (nullptr);
    std::lock_guard<std::mutex> lock (impl->infoMutex);
    impl->info = ModelInfo {};
}

bool GootarEngine::stageIR (const std::filesystem::path& path, std::string& errorOut)
{
    try
    {
        auto ir = std::make_unique<::dsp::ImpulseResponse> (path.string().c_str(),
                                                            impl->currentSampleRate);
        if (ir->GetWavState() != ::dsp::wav::LoadReturnCode::SUCCESS)
        {
            errorOut = "could not read IR \"" + path.filename().string() + "\"";
            return false;
        }
        impl->irSwapper.stage (std::move (ir));
        return true;
    }
    catch (const std::exception& e)
    {
        errorOut = e.what();
        return false;
    }
}

void GootarEngine::clearIR()
{
    impl->irSwapper.stage (nullptr);
}

void GootarEngine::collectGarbage() noexcept
{
    impl->modelSwapper.collectRetired();
    impl->irSwapper.collectRetired();
}

bool GootarEngine::modelNeedsReload() const noexcept
{
    const double loadedAt = impl->loadedModelRate.load();
    return loadedAt > 0.0 && loadedAt != impl->currentSampleRate;
}

ModelInfo GootarEngine::modelInfo() const
{
    std::lock_guard<std::mutex> lock (impl->infoMutex);
    return impl->info;
}

} // namespace gootar

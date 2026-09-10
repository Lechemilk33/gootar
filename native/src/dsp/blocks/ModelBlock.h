#pragma once

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "NeuralAudio/NeuralModel.h"

#include "../../ModelSwapper.h"
#include "../Block.h"

namespace gootar {

/**
 * A NAM capture in the chain.
 *
 * Each model block owns its own ModelSwapper, so swapping the capture in one
 * slot is gapless and does not disturb anything else in the chain. That is why
 * hot-swap survives the chain being editable: rebuilding the chain is a rare,
 * structural change, while changing which model is loaded is the thing you do
 * constantly and must never hear.
 */
class ModelBlock : public Block
{
public:
    explicit ModelBlock (std::string id) : Block (std::move (id)) {}

    BlockType type() const noexcept override { return BlockType::Model; }
    const char* displayName() const noexcept override { return "Model"; }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        rate = sampleRate;
        maxFrames = std::max (1, maxBlockSize);
        floatIn.assign (static_cast<size_t> (maxFrames), 0.0f);
        floatOut.assign (static_cast<size_t> (maxFrames), 0.0f);
        outBuffer.assign (static_cast<size_t> (maxFrames), 0.0);
        outPointers.assign (1, outBuffer.data());
    }

    DSP_SAMPLE** process (DSP_SAMPLE** input, int, int numFrames) noexcept override
    {
        swapper.applyStaged();
        auto* model = swapper.current();
        if (model == nullptr)
            return input;

        const int n = std::min (numFrames, maxFrames);
        for (int i = 0; i < n; ++i)
            floatIn[static_cast<size_t> (i)] = static_cast<float> (input[0][i]);

        model->Process (floatIn.data(), floatOut.data(), static_cast<size_t> (n));

        for (int i = 0; i < n; ++i)
            outBuffer[static_cast<size_t> (i)] =
                static_cast<DSP_SAMPLE> (floatOut[static_cast<size_t> (i)]);

        return outPointers.data();
    }

    /** [Loader thread] Load a .nam and hand it over. Not real-time safe. */
    bool load (const std::filesystem::path& path, double inputCalibrationDbu, std::string& errorOut)
    {
        NeuralAudio::NeuralModelLoader loader;
        loader.SetExternalSampleRate (static_cast<int> (rate));
        loader.SetDefaultMaxAudioBufferSize (maxFrames);
        loader.SetAudioInputLevelDBu (static_cast<float> (inputCalibrationDbu));

        NeuralAudio::NeuralModel* raw = nullptr;
        try
        {
            // doPrewarm defaults to true: these are stateful recurrent nets and
            // the first samples out of a cold model are garbage.
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

        raw->SetMaxAudioBufferSize (maxFrames);
        raw->SetAudioInputLevelDBu (static_cast<float> (inputCalibrationDbu));

        {
            std::lock_guard<std::mutex> lock (infoMutex);
            loadedName = path.filename().string();
            loadedPath = path.string();
            architecture = raw->GetMetadata ("architecture");
            modelSampleRate = raw->GetSampleRate();
            receptiveField = raw->GetReceptiveFieldSize();
            isStatic = raw->IsStatic();
            inputAdjustDb = raw->GetRecommendedInputDBAdjustment();
            outputAdjustDb = raw->GetRecommendedOutputDBAdjustment();
            hasModel = true;
        }
        loadedAtRate.store (rate);
        atomicInputAdjust.store (raw->GetRecommendedInputDBAdjustment(), std::memory_order_relaxed);
        atomicOutputAdjust.store (raw->GetRecommendedOutputDBAdjustment(), std::memory_order_relaxed);
        loadedFlag.store (true, std::memory_order_relaxed);

        swapper.stage (std::unique_ptr<NeuralAudio::NeuralModel> (raw));
        return true;
    }

    void unload()
    {
        swapper.stage (nullptr);
        loadedFlag.store (false, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock (infoMutex);
        hasModel = false;
        loadedName.clear();
        loadedPath.clear();
    }

    void collectGarbage() noexcept { swapper.collectRetired(); }

    /**
     * Calibration figures, readable from the audio thread.
     *
     * info() takes a mutex, which is fine for the UI and not fine per block.
     * These mirror the same values as plain atomics so output normalisation
     * can fold them into the gain every buffer.
     */
    bool  isLoaded() const noexcept { return loadedFlag.load (std::memory_order_relaxed); }
    float inputAdjustmentDb() const noexcept { return atomicInputAdjust.load (std::memory_order_relaxed); }
    float outputAdjustmentDb() const noexcept { return atomicOutputAdjust.load (std::memory_order_relaxed); }

    /** NeuralAudio bakes the rate in at load time, so a rate change needs a reload. */
    bool needsReload() const noexcept
    {
        const double at = loadedAtRate.load();
        return at > 0.0 && at != rate;
    }

    struct Info
    {
        bool loaded = false;
        std::string fileName, filePath, architecture;
        float sampleRate = 0.0f;
        int receptiveField = -1;
        bool isStatic = false;
        float inputAdjustDb = 0.0f, outputAdjustDb = 0.0f;
    };

    Info info() const
    {
        std::lock_guard<std::mutex> lock (infoMutex);
        return { hasModel, loadedName, loadedPath, architecture, modelSampleRate,
                 receptiveField, isStatic, inputAdjustDb, outputAdjustDb };
    }

private:
    ModelSwapper<NeuralAudio::NeuralModel> swapper;

    std::vector<float>       floatIn, floatOut;
    std::vector<DSP_SAMPLE>  outBuffer;
    std::vector<DSP_SAMPLE*> outPointers;

    double rate = 48000.0;
    int    maxFrames = 0;

    std::atomic<double> loadedAtRate { 0.0 };
    std::atomic<bool>   loadedFlag { false };
    std::atomic<float>  atomicInputAdjust { 0.0f };
    std::atomic<float>  atomicOutputAdjust { 0.0f };
    mutable std::mutex  infoMutex;
    bool        hasModel = false;
    std::string loadedName, loadedPath, architecture;
    float       modelSampleRate = 0.0f;
    int         receptiveField = -1;
    bool        isStatic = false;
    float       inputAdjustDb = 0.0f, outputAdjustDb = 0.0f;
};

} // namespace gootar

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

#include "ImpulseResponse.h"

#include "../../ModelSwapper.h"
#include "../Block.h"

namespace gootar {

/**
 * Cabinet impulse response.
 *
 * Uses AudioDSPTools' own convolution rather than juce::dsp::Convolution, so
 * an IR sounds identical here and in the stock plugin. Like ModelBlock it has
 * its own swapper, so changing cabs mid-playing is gapless too.
 */
class IRBlock : public Block
{
public:
    explicit IRBlock (std::string id) : Block (std::move (id)) {}

    BlockType type() const noexcept override { return BlockType::IR; }
    const char* displayName() const noexcept override { return "IR"; }

    void prepare (double sampleRate, int) override { rate = sampleRate; }

    DSP_SAMPLE** process (DSP_SAMPLE** input, int numChannels, int numFrames) noexcept override
    {
        swapper.applyStaged();
        auto* ir = swapper.current();
        if (ir == nullptr)
            return input;
        return ir->Process (input, static_cast<size_t> (numChannels),
                            static_cast<size_t> (numFrames));
    }

    bool load (const std::filesystem::path& path, std::string& errorOut)
    {
        try
        {
            auto ir = std::make_unique<::dsp::ImpulseResponse> (path.string().c_str(), rate);
            if (ir->GetWavState() != ::dsp::wav::LoadReturnCode::SUCCESS)
            {
                errorOut = "could not read IR \"" + path.filename().string() + "\"";
                return false;
            }
            swapper.stage (std::move (ir));
            {
                std::lock_guard<std::mutex> lock (infoMutex);
                loadedName = path.filename().string();
                loadedPath = path.string();
                hasIR = true;
            }
            return true;
        }
        catch (const std::exception& e)
        {
            errorOut = e.what();
            return false;
        }
    }

    void unload()
    {
        swapper.stage (nullptr);
        std::lock_guard<std::mutex> lock (infoMutex);
        hasIR = false;
        loadedName.clear();
        loadedPath.clear();
    }

    void collectGarbage() noexcept { swapper.collectRetired(); }

    struct Info { bool loaded = false; std::string fileName, filePath; };

    Info info() const
    {
        std::lock_guard<std::mutex> lock (infoMutex);
        return { hasIR, loadedName, loadedPath };
    }

private:
    ModelSwapper<::dsp::ImpulseResponse> swapper;
    double rate = 48000.0;

    mutable std::mutex infoMutex;
    bool hasIR = false;
    std::string loadedName, loadedPath;
};

} // namespace gootar

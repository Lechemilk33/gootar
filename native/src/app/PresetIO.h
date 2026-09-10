#pragma once

#include <JuceHeader.h>

#include "../dsp/ChainSpec.h"
#include "../dsp/GootarEngine.h"

namespace gootar {

/** How a preset points at a .nam or .wav: by content hash, never by path. */
struct AssetRef
{
    juce::String sha256;
    juce::String fileName;
    juce::int64  sizeBytes = 0;
    /** Path relative to the library root, kept only as a hint for the resolver. */
    juce::String relPath;

    bool isValid() const { return sha256.length() == 64; }
};

/**
 * The native mirror of @gootar/preset-schema.
 *
 * Field names and nesting must match the TypeScript definitions exactly, or a
 * preset built in the librarian will not open here. The authoritative spec is
 * packages/preset-schema/schema/gootar-preset-v1.schema.json, generated from
 * the zod source.
 */
/** One stage of the board, as a preset stores it. */
struct PresetChainBlock
{
    BlockType   type = BlockType::Model;
    juce::String id;
    bool         enabled = true;
    /** Pedal knobs. Free-form, so a new pedal needs no format change. */
    std::vector<std::pair<juce::String, double>> params;
    /** Which capture or cab this slot holds; only for model and ir blocks. */
    AssetRef     ref;
    bool         hasRef = false;
};

struct Preset
{
    /**
     * 2 added the board. Version 1 files still load - they describe the stock
     * chain, so an empty chain is filled in with the standard one.
     */
    static constexpr int kSchemaVersion = 2;
    static constexpr int kMinReadableVersion = 1;

    juce::String id;
    juce::String name { "Untitled" };
    juce::StringArray tags;
    juce::String notes;
    juce::String createdAt;
    juce::String updatedAt;
    double sampleRate = 48000.0;

    Params params;
    double slim = 0.0;

    AssetRef model;
    AssetRef ir;
    bool hasIR = false;

    /** The board in signal order. Empty means the stock chain. */
    std::vector<PresetChainBlock> chain;

    /** A preset with everything at the stock plugin's defaults. */
    static Preset makeDefault();
};

namespace presetIO {

juce::String toJsonString (const Preset&);
bool fromJsonString (const juce::String& json, Preset& out, juce::String& errorOut);

bool writeToFile (const Preset&, const juce::File&, juce::String& errorOut);
bool readFromFile (const juce::File&, Preset& out, juce::String& errorOut);

juce::String outputModeToString (OutputMode);
OutputMode outputModeFromString (const juce::String&, OutputMode fallback = OutputMode::Normalized);

} // namespace presetIO
} // namespace gootar

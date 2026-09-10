#pragma once

#include <JuceHeader.h>

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
struct Preset
{
    static constexpr int kSchemaVersion = 1;

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

#include "PresetIO.h"

namespace gootar {

Preset Preset::makeDefault()
{
    Preset p;
    p.id = juce::Uuid().toDashedString();
    p.createdAt = juce::Time::getCurrentTime().toISO8601 (true);
    p.updatedAt = p.createdAt;
    return p;
}

namespace presetIO {

juce::String outputModeToString (OutputMode m)
{
    switch (m)
    {
        case OutputMode::Raw:        return "raw";
        case OutputMode::Calibrated: return "calibrated";
        case OutputMode::Normalized: break;
    }
    return "normalized";
}

OutputMode outputModeFromString (const juce::String& s, OutputMode fallback)
{
    if (s == "raw")        return OutputMode::Raw;
    if (s == "normalized") return OutputMode::Normalized;
    if (s == "calibrated") return OutputMode::Calibrated;
    return fallback;
}

namespace {

juce::var assetToVar (const AssetRef& ref)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("sha256", ref.sha256);
    o->setProperty ("fileName", ref.fileName);
    o->setProperty ("sizeBytes", ref.sizeBytes);

    juce::Array<juce::var> sources;
    if (ref.relPath.isNotEmpty())
    {
        auto* src = new juce::DynamicObject();
        src->setProperty ("kind", "local");
        src->setProperty ("relPath", ref.relPath);
        sources.add (juce::var (src));
    }
    o->setProperty ("sources", sources);
    return juce::var (o);
}

AssetRef assetFromVar (const juce::var& v)
{
    AssetRef ref;
    if (auto* o = v.getDynamicObject())
    {
        ref.sha256 = o->getProperty ("sha256").toString();
        ref.fileName = o->getProperty ("fileName").toString();
        ref.sizeBytes = static_cast<juce::int64> (o->getProperty ("sizeBytes"));

        // Take the first local source as a resolution hint; ignore the rest.
        if (auto* sources = o->getProperty ("sources").getArray())
            for (const auto& s : *sources)
                if (auto* so = s.getDynamicObject())
                    if (so->getProperty ("kind").toString() == "local")
                    {
                        ref.relPath = so->getProperty ("relPath").toString();
                        break;
                    }
    }
    return ref;
}

double getNum (const juce::var& parent, const char* key, double fallback)
{
    if (auto* o = parent.getDynamicObject())
        if (o->hasProperty (key))
            return static_cast<double> (o->getProperty (key));
    return fallback;
}

bool getBool (const juce::var& parent, const char* key, bool fallback)
{
    if (auto* o = parent.getDynamicObject())
        if (o->hasProperty (key))
            return static_cast<bool> (o->getProperty (key));
    return fallback;
}

juce::var getObj (const juce::var& parent, const char* key)
{
    if (auto* o = parent.getDynamicObject())
        return o->getProperty (key);
    return {};
}

} // namespace

juce::String toJsonString (const Preset& p)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("schemaVersion", Preset::kSchemaVersion);
    root->setProperty ("id", p.id);
    root->setProperty ("name", p.name);

    juce::Array<juce::var> tags;
    for (const auto& t : p.tags)
        tags.add (t);
    root->setProperty ("tags", tags);

    root->setProperty ("notes", p.notes);
    root->setProperty ("createdAt", p.createdAt);
    root->setProperty ("updatedAt", juce::Time::getCurrentTime().toISO8601 (true));
    root->setProperty ("sampleRate", p.sampleRate);

    auto* input = new juce::DynamicObject();
    input->setProperty ("levelDb", p.params.inputLevelDb);
    input->setProperty ("calibrate", p.params.calibrateInput);
    input->setProperty ("calibrationLevelDbu", p.params.inputCalibrationLevelDbu);
    root->setProperty ("input", juce::var (input));

    auto* gate = new juce::DynamicObject();
    gate->setProperty ("enabled", p.params.gateEnabled);
    gate->setProperty ("thresholdDb", p.params.gateThresholdDb);
    root->setProperty ("gate", juce::var (gate));

    auto* slot = new juce::DynamicObject();
    slot->setProperty ("slotId", "slot-1");
    slot->setProperty ("enabled", true);
    slot->setProperty ("ref", assetToVar (p.model));
    slot->setProperty ("slim", p.slim);
    juce::Array<juce::var> models;
    models.add (juce::var (slot));
    root->setProperty ("models", models);

    auto* tone = new juce::DynamicObject();
    tone->setProperty ("enabled", p.params.toneStackEnabled);
    tone->setProperty ("bass", p.params.bass);
    tone->setProperty ("mid", p.params.mid);
    tone->setProperty ("treble", p.params.treble);
    root->setProperty ("toneStack", juce::var (tone));

    auto* ir = new juce::DynamicObject();
    ir->setProperty ("enabled", p.params.irEnabled);
    ir->setProperty ("ref", p.hasIR ? assetToVar (p.ir) : juce::var());
    root->setProperty ("ir", juce::var (ir));

    auto* out = new juce::DynamicObject();
    out->setProperty ("levelDb", p.params.outputLevelDb);
    out->setProperty ("mode", outputModeToString (p.params.outputMode));
    root->setProperty ("output", juce::var (out));

    return juce::JSON::toString (juce::var (root), false);
}

bool fromJsonString (const juce::String& json, Preset& out, juce::String& errorOut)
{
    juce::var parsed;
    const auto result = juce::JSON::parse (json, parsed);
    if (result.failed())
    {
        errorOut = result.getErrorMessage();
        return false;
    }
    auto* root = parsed.getDynamicObject();
    if (root == nullptr)
    {
        errorOut = "preset is not a JSON object";
        return false;
    }

    const int version = static_cast<int> (root->getProperty ("schemaVersion"));
    if (version != Preset::kSchemaVersion)
    {
        errorOut = "unsupported preset schemaVersion " + juce::String (version)
                 + " (this build reads version " + juce::String (Preset::kSchemaVersion) + ")";
        return false;
    }

    Preset p = Preset::makeDefault();
    p.id = root->getProperty ("id").toString();
    p.name = root->getProperty ("name").toString();
    p.notes = root->getProperty ("notes").toString();
    p.createdAt = root->getProperty ("createdAt").toString();
    p.updatedAt = root->getProperty ("updatedAt").toString();
    p.sampleRate = getNum (parsed, "sampleRate", 48000.0);

    p.tags.clear();
    if (auto* tags = root->getProperty ("tags").getArray())
        for (const auto& t : *tags)
            p.tags.add (t.toString());

    const auto input = getObj (parsed, "input");
    p.params.inputLevelDb = getNum (input, "levelDb", 0.0);
    p.params.calibrateInput = getBool (input, "calibrate", false);
    p.params.inputCalibrationLevelDbu = getNum (input, "calibrationLevelDbu", 12.0);

    const auto gate = getObj (parsed, "gate");
    p.params.gateEnabled = getBool (gate, "enabled", true);
    p.params.gateThresholdDb = getNum (gate, "thresholdDb", -80.0);

    const auto tone = getObj (parsed, "toneStack");
    p.params.toneStackEnabled = getBool (tone, "enabled", true);
    p.params.bass = getNum (tone, "bass", 5.0);
    p.params.mid = getNum (tone, "mid", 5.0);
    p.params.treble = getNum (tone, "treble", 5.0);

    const auto irObj = getObj (parsed, "ir");
    p.params.irEnabled = getBool (irObj, "enabled", true);
    if (auto* iro = irObj.getDynamicObject())
    {
        const auto refVar = iro->getProperty ("ref");
        if (refVar.getDynamicObject() != nullptr)
        {
            p.ir = assetFromVar (refVar);
            p.hasIR = p.ir.isValid();
        }
    }

    const auto outObj = getObj (parsed, "output");
    p.params.outputLevelDb = getNum (outObj, "levelDb", 0.0);
    if (auto* oo = outObj.getDynamicObject())
        p.params.outputMode = outputModeFromString (oo->getProperty ("mode").toString());

    // Only the first model slot is played today; the format carries an array so
    // chaining (pedal -> amp) needs no format change later.
    if (auto* models = root->getProperty ("models").getArray())
        if (! models->isEmpty())
            if (auto* slot = models->getReference (0).getDynamicObject())
            {
                p.model = assetFromVar (slot->getProperty ("ref"));
                p.slim = static_cast<double> (slot->getProperty ("slim"));
            }

    out = p;
    return true;
}

bool writeToFile (const Preset& p, const juce::File& file, juce::String& errorOut)
{
    if (! file.getParentDirectory().createDirectory())
    {
        errorOut = "could not create " + file.getParentDirectory().getFullPathName();
        return false;
    }
    if (! file.replaceWithText (toJsonString (p)))
    {
        errorOut = "could not write " + file.getFullPathName();
        return false;
    }
    return true;
}

bool readFromFile (const juce::File& file, Preset& out, juce::String& errorOut)
{
    if (! file.existsAsFile())
    {
        errorOut = file.getFullPathName() + " does not exist";
        return false;
    }
    return fromJsonString (file.loadFileAsString(), out, errorOut);
}

} // namespace presetIO
} // namespace gootar

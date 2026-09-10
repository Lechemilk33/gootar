#include "ModelLibrary.h"

namespace gootar {

namespace {
juce::String cacheKeyFor (const juce::File& f)
{
    return f.getFullPathName() + "|" + juce::String (f.getSize()) + "|"
         + juce::String (f.getLastModificationTime().toMilliseconds());
}
} // namespace

ModelLibrary::ModelLibrary()
    : juce::Thread ("gootar library scan")
{
}

ModelLibrary::~ModelLibrary()
{
    stopThread (4000);
}

void ModelLibrary::cancelScan()
{
    stopThread (4000);
}

void ModelLibrary::setRoot (const juce::File& root)
{
    stopThread (4000);
    rootFolder = root;
    loadCache();
    startThread (juce::Thread::Priority::low);
}

juce::String ModelLibrary::hashFile (const juce::File& f)
{
    juce::FileInputStream stream (f);
    if (! stream.openedOk())
        return {};
    return juce::SHA256 (stream).toHexString();
}

juce::File ModelLibrary::guessDefaultRoot()
{
    const auto hasCaptures = [] (const juce::File& dir)
    {
        if (! dir.isDirectory())
            return false;
        juce::Array<juce::File> found;
        dir.findChildFiles (found, juce::File::findFiles, true, "*.nam");
        return ! found.isEmpty();
    };

    if (auto override_ = juce::SystemStats::getEnvironmentVariable ("GOOTAR_MODEL_DIR", {});
        override_.isNotEmpty())
    {
        const juce::File dir { override_ };
        if (dir.isDirectory())
            return dir;
    }

    using SL = juce::File::SpecialLocationType;
    const juce::File bases[] = {
        juce::File::getSpecialLocation (SL::userDocumentsDirectory),
        juce::File::getSpecialLocation (SL::userMusicDirectory),
        juce::File::getSpecialLocation (SL::userHomeDirectory),
    };
    // Ordered by how likely they are to be the real library, not alphabetically.
    const char* names[] = {
        "NAM Models", "NAM", "Neural Amp Modeler", "Tone3000", "TONE3000",
        "Captures", "Amp Models",
    };

    for (const auto& base : bases)
        for (const auto* name : names)
            if (const auto candidate = base.getChildFile (name); hasCaptures (candidate))
                return candidate;

    // Downloads is a last resort: it is where models land, but scanning a
    // whole Downloads folder is slow and mostly finds nothing.
    if (const auto downloads = juce::File::getSpecialLocation (SL::userHomeDirectory)
                                  .getChildFile ("Downloads")
                                  .getChildFile ("NAM");
        hasCaptures (downloads))
        return downloads;

    return {};
}

void ModelLibrary::run()
{
    if (! rootFolder.isDirectory())
        return;

    juce::Array<juce::File> found;
    rootFolder.findChildFiles (found, juce::File::findFiles, true, "*.nam");
    rootFolder.findChildFiles (found, juce::File::findFiles, true, "*.wav");

    juce::Array<LibraryItem> scanned;
    const int total = found.size();
    int done = 0;

    for (const auto& f : found)
    {
        if (threadShouldExit())
            return;

        LibraryItem item;
        item.file = f;
        item.fileName = f.getFileName();
        item.relPath = f.getRelativePathFrom (rootFolder).replaceCharacter ('\\', '/');
        item.sizeBytes = f.getSize();
        item.isIR = f.hasFileExtension ("wav");

        // Re-hash only what actually changed. With hundreds of models this is
        // the difference between a scan you wait for and one you do not notice.
        const auto key = cacheKeyFor (f);
        if (hashCache.contains (key))
            item.sha256 = hashCache[key];
        else
        {
            item.sha256 = hashFile (f);
            if (item.sha256.isNotEmpty())
                hashCache.set (key, item.sha256);
        }

        if (item.sha256.isNotEmpty())
            scanned.add (item);

        ++done;
        if ((done % 16) == 0 || done == total)
        {
            const int d = done;
            juce::MessageManager::callAsync ([this, d, total]
            {
                listeners.call ([d, total] (Listener& l) { l.libraryScanProgress (d, total); });
            });
        }
    }

    {
        const juce::ScopedLock sl (itemsLock);
        items = scanned;
    }
    saveCache();

    juce::MessageManager::callAsync ([this]
    {
        listeners.call ([] (Listener& l) { l.libraryChanged(); });
    });
}

juce::Array<LibraryItem> ModelLibrary::getItems() const
{
    const juce::ScopedLock sl (itemsLock);
    return items;
}

int ModelLibrary::getNumItems() const
{
    const juce::ScopedLock sl (itemsLock);
    return items.size();
}

juce::File ModelLibrary::resolve (const juce::String& sha256) const
{
    if (sha256.isEmpty())
        return {};
    const juce::ScopedLock sl (itemsLock);
    for (const auto& item : items)
        if (item.sha256 == sha256)
            return item.file;
    return {};
}

juce::File ModelLibrary::resolveByName (const juce::String& fileName) const
{
    if (fileName.isEmpty())
        return {};
    const juce::ScopedLock sl (itemsLock);
    for (const auto& item : items)
        if (item.fileName.equalsIgnoreCase (fileName))
            return item.file;
    return {};
}

juce::File ModelLibrary::cacheFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Gootar")
             .getChildFile ("library-hash-cache.json");
}

void ModelLibrary::loadCache()
{
    hashCache.clear();
    const auto f = cacheFile();
    if (! f.existsAsFile())
        return;

    juce::var parsed;
    if (juce::JSON::parse (f.loadFileAsString(), parsed).failed())
        return;
    if (auto* o = parsed.getDynamicObject())
        for (const auto& prop : o->getProperties())
            hashCache.set (prop.name.toString(), prop.value.toString());
}

void ModelLibrary::saveCache() const
{
    auto* o = new juce::DynamicObject();
    for (juce::HashMap<juce::String, juce::String>::Iterator i (hashCache); i.next();)
        o->setProperty (juce::Identifier (i.getKey()), i.getValue());

    const auto f = cacheFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::JSON::toString (juce::var (o), false));
}

} // namespace gootar

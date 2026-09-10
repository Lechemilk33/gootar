#pragma once

#include <JuceHeader.h>

namespace gootar {

/**
 * An indexed .nam or .wav on disk.
 *
 * The hash is the identity. Everything else — path, name, size — is a hint
 * that helps find the file again but never decides whether it is the right one.
 */
struct LibraryItem
{
    juce::String sha256;
    juce::String fileName;
    juce::String relPath;   // relative to the library root
    juce::File   file;
    juce::int64  sizeBytes = 0;
    bool         isIR = false;
};

/**
 * Scans a folder of models, hashes them, and answers "where is the file with
 * this hash?".
 *
 * Hashing hundreds of files takes a moment, so results are cached to disk
 * against (path, size, modification time). Rescanning an unchanged library is
 * then effectively free, and a file that changed gets re-hashed automatically.
 *
 * Scanning happens on a background thread; the audio thread never touches this.
 */
class ModelLibrary : private juce::Thread
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void libraryChanged() = 0;
        virtual void libraryScanProgress (int done, int total) = 0;
    };

    ModelLibrary();
    ~ModelLibrary() override;

    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    /** Kick off a background scan. Safe to call while one is running. */
    void setRoot (const juce::File& root);
    juce::File getRoot() const { return rootFolder; }

    bool isScanning() const { return isThreadRunning(); }
    void cancelScan();

    /** Snapshot of everything indexed. Cheap to call from the message thread. */
    juce::Array<LibraryItem> getItems() const;
    int getNumItems() const;

    /** Resolve a preset's model reference to a real file, or an empty File. */
    juce::File resolve (const juce::String& sha256) const;

    /**
     * Fall back to a filename match when the hash is unknown, so a preset from
     * a machine whose files differ slightly still finds something plausible.
     * Returns an empty File when nothing matches.
     */
    juce::File resolveByName (const juce::String& fileName) const;

    static juce::String hashFile (const juce::File&);

private:
    void run() override;
    void loadCache();
    void saveCache() const;
    juce::File cacheFile() const;

    juce::File rootFolder;
    juce::ListenerList<Listener> listeners;

    mutable juce::CriticalSection itemsLock;
    juce::Array<LibraryItem> items;
    juce::HashMap<juce::String, juce::String> hashCache; // "path|size|modTime" -> sha256

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModelLibrary)
};

} // namespace gootar

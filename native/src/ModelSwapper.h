#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>

namespace gootar {

/**
 * Lock-free handover of a heap object (a loaded NAM model) to the audio thread.
 *
 * WHY THIS EXISTS AT MILESTONE 2 AND NOT MILESTONE 6
 * --------------------------------------------------
 * Hot-swapping models without interrupting playing is the product. It is not a
 * feature that can be bolted on later, because the naive version —
 *
 *     void setModel(const std::string& path) {
 *         model = loader.CreateFromFile(path);   // allocates, does file I/O
 *     }
 *
 * — is wrong in a way that only shows up as dropouts, and fixing it means
 * restructuring however much DSP code sits around it. Building the indirection
 * now costs this one header. Retrofitting it costs the audio engine.
 *
 * THE RULES IT ENFORCES
 * ---------------------
 *   - The audio thread never allocates, never frees, never blocks, never
 *     touches the filesystem.
 *   - The loader thread never blocks the audio thread.
 *   - Deletion of a replaced model happens on the loader thread, after the
 *     audio thread has demonstrably stopped using it.
 *
 * This is the same shape as the stock plugin's _ApplyDSPStaging(), and the same
 * job Stompbox does with RCU.
 *
 * USAGE
 * -----
 *   Loader thread:  swapper.stage(std::move(newlyLoadedModel));
 *                   swapper.collectRetired();          // periodically
 *
 *   Audio thread:   swapper.applyStaged();             // once per block, top
 *                   if (auto* m = swapper.current()) m->Process(in, out, n);
 *
 * THREADING CONTRACT
 *   Exactly one audio thread and one loader thread.
 *
 *   The loader MUST keep calling collectRetired() while anything is staged.
 *   applyStaged() refuses when every retire slot is full, and only the loader
 *   empties them - so a loader that stages a model and then stops collecting
 *   leaves that model stuck forever. That is deliberate: declining the swap is
 *   the only alternative to freeing on the audio thread. The engine satisfies
 *   this by collecting on a timer as well as after each load.
 */
template <typename T, std::size_t RetireSlots = 4>
class ModelSwapper
{
public:
    ModelSwapper() = default;

    ModelSwapper (const ModelSwapper&) = delete;
    ModelSwapper& operator= (const ModelSwapper&) = delete;

    ~ModelSwapper()
    {
        delete active;
        delete staged.exchange (nullptr, std::memory_order_acquire);
        collectRetired();
    }

    /**
     * [Loader thread] Hand a freshly loaded model over.
     *
     * If a previous staged model has not been picked up yet, it is destroyed
     * here — the audio thread never saw it, so deleting it on this thread is
     * safe. That makes rapid browsing (spinning a list, loading every model you
     * pass) coalesce to the latest instead of queueing up.
     */
    void stage (std::unique_ptr<T> next) noexcept
    {
        T* previous = staged.exchange (next.release(), std::memory_order_acq_rel);
        delete previous;
    }

    /**
     * [Audio thread] Promote a staged model, if there is one. RT-safe: at most
     * one atomic exchange and one atomic store, no allocation, no free.
     *
     * @return true if a swap happened this call.
     */
    bool applyStaged() noexcept
    {
        if (staged.load (std::memory_order_acquire) == nullptr)
            return false;

        // Check for somewhere to park the outgoing model BEFORE taking the new
        // one. Only this thread ever fills a retire slot and only the loader
        // ever empties one, so a slot seen free stays free — the check cannot
        // go stale. Failing here means the loader has fallen behind on
        // collectRetired(); declining the swap and retrying next block is
        // correct, and infinitely better than freeing on the audio thread.
        if (active != nullptr && ! hasFreeRetireSlot())
            return false;

        // A single exchange, not load-then-CAS: stage() also exchanges, so
        // exactly one of the two threads can come away owning this pointer.
        // A CAS that lost here would leave us adopting a model the loader had
        // already destroyed.
        T* next = staged.exchange (nullptr, std::memory_order_acq_rel);
        if (next == nullptr)
            return false;

        if (active != nullptr)
            retire (active); // Guaranteed to succeed; we checked above.

        active = next;
        return true;
    }

    /** [Audio thread] The model to process with. May be null (pass through). */
    T* current() const noexcept { return active; }

    /**
     * [Loader thread] Destroy models the audio thread has finished with.
     * Call from the same thread that calls stage(), on any cadence you like —
     * a timer, or right after each load.
     */
    void collectRetired() noexcept
    {
        for (auto& slot : retired)
            delete slot.exchange (nullptr, std::memory_order_acq_rel);
    }

    /** [Any thread] True if a model is waiting to be picked up. */
    bool hasStaged() const noexcept
    {
        return staged.load (std::memory_order_acquire) != nullptr;
    }

private:
    /** [Audio thread] Is there room to park an outgoing model? */
    bool hasFreeRetireSlot() const noexcept
    {
        for (auto& slot : retired)
            if (slot.load (std::memory_order_acquire) == nullptr)
                return true;
        return false;
    }

    /** [Audio thread] Park a discarded model for the loader thread to free. */
    bool retire (T* old) noexcept
    {
        for (auto& slot : retired)
        {
            T* expected = nullptr;
            if (slot.compare_exchange_strong (expected, old,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire))
                return true;
        }
        return false;
    }

    /** Owned by the audio thread alone; needs no atomicity. */
    T* active = nullptr;

    std::atomic<T*> staged { nullptr };
    std::array<std::atomic<T*>, RetireSlots> retired {};
};

} // namespace gootar

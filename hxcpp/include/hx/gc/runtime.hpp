#pragma once

#ifndef NOVA_HX_GC_RUNTIME_HPP_INCLUDED
#define NOVA_HX_GC_RUNTIME_HPP_INCLUDED

#include <hx/gc/abi.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>

namespace hx::gc {

enum class NumaPolicy : std::uint8_t {
    Disabled,
    Local,
    Interleave,
};

enum class LargePagePolicy : std::uint8_t {
    Disabled,
    Transparent,
    Explicit,
};

enum class AllocationServiceLevel : std::uint8_t {
    Latency,
    Balanced,
    Throughput,
};

struct RuntimeConfig final {
    using RelocationBeforeCopyHook = void (*)(void* context, void* object);
    using RemarkTailHook = void (*)(void* context);

    std::size_t regionBytes = 2U * 1024U * 1024U;
    std::size_t tlabBytes = 64U * 1024U;
    std::size_t largeObjectThreshold = 128U * 1024U;
    std::uint32_t maximumRegions = 4096;
    // Zero reserves maximumRegions * regionBytes as one contiguous virtual
    // heap arena. maximumRegions therefore sizes the default byte capacity;
    // it is not a hard count of logical Large/YoungLarge subregions, which can
    // be much smaller than regionBytes. The arena remains the hard limit.
    std::size_t maximumAddressSpaceBytes = 0;
    std::uint32_t handshakeTimeoutMilliseconds = 5000;
    std::uint32_t allocationStallTimeoutMilliseconds = 5000;
    AllocationServiceLevel allocationServiceLevel =
        AllocationServiceLevel::Balanced;
    // Zero selects a topology-aware default.  Explicit values make stress
    // tests and latency-sensitive game builds reproducible.
    std::uint32_t markWorkerCount = 0;
    std::uint32_t relocationWorkerCount = 0;
    std::uint32_t maximumPauseMilliseconds = 10;
    // Objects younger than this threshold remain in survivor regions.  The
    // adaptive policy moves the threshold within [1, maximumTenuringThreshold]
    // according to survivor-space pressure after every young cycle.
    std::uint16_t initialTenuringThreshold = 2;
    std::uint16_t maximumTenuringThreshold = 15;
    std::uint32_t survivorTargetPercent = 50;
    bool adaptiveTenuring = true;
    // Empty normal regions keep their virtual reservation and return physical
    // pages after this delay. Zero decommits immediately while still allowing
    // the reservation to be reused without address-space churn.
    std::uint32_t uncommitDelayMilliseconds = 300000;
    NumaPolicy numaPolicy = NumaPolicy::Local;
    LargePagePolicy largePagePolicy = LargePagePolicy::Transparent;
    // Test/verification fault injection. Zero disables it; otherwise every Nth
    // remembered-set publication is diverted to the exact overflow scan.
    std::uint32_t rememberedSetFailureEvery = 0;
    RelocationBeforeCopyHook relocationBeforeCopyHook = nullptr;
    void* relocationHookContext = nullptr;
    // Deterministic verification hook invoked after concurrent marker workers
    // have exited but before the remark handshake. Production leaves it null;
    // stress tests use it to populate and verify the parallel tail drain.
    RemarkTailHook remarkTailHook = nullptr;
    void* remarkTailHookContext = nullptr;
};

struct FinalizerStats final {
    std::uint64_t completed = 0;
    std::uint64_t failed = 0;
};

struct CollectionStats final {
    std::uint64_t epoch = 0;
    std::uint64_t roots = 0;
    std::uint64_t markedObjects = 0;
    std::uint64_t markedBytes = 0;
    std::uint64_t deadObjects = 0;
    std::uint64_t reclaimedRegions = 0;
    // Active mutators capture their exact shadow-stack slots themselves while
    // acknowledging the initial-mark handshake.  These counters make that
    // JEP-376-style split visible to tests and runtime telemetry.
    std::uint64_t threadRootSnapshots = 0;
    std::uint64_t threadRootSlots = 0;
    std::uint32_t remarkWorkers = 0;
    std::uint64_t stolenMarkTasks = 0;
    FinalizerStats finalizers{};
    std::uint64_t finalizerMicros = 0;
};

struct YoungCollectionStats final {
    std::uint64_t epoch = 0;
    std::uint64_t roots = 0;
    std::uint64_t rememberedRegions = 0;
    std::uint64_t rememberedBitmapBytes = 0;
    std::uint64_t actOnceRememberedStores = 0;
    std::uint64_t markedObjects = 0;
    std::uint64_t markedBytes = 0;
    std::uint64_t deadObjects = 0;
    std::uint64_t reclaimedRegions = 0;
    std::uint64_t reclaimedBytes = 0;
    std::uint64_t promotedRegions = 0;
    std::uint64_t promotedObjects = 0;
    std::uint64_t promotedBytes = 0;
    std::uint64_t inPlacePromotedRegions = 0;
    // Why a nominally moving Young cycle degraded to retaining source
    // regions. These counters distinguish an expected native pin from the
    // remembered-set/queue failures that caused the game's large promotion
    // spikes before its first Full epoch.
    std::uint64_t initialRememberedOverflowRegions = 0;
    std::uint64_t finalRememberedOverflowRegions = 0;
    std::uint64_t recoveredRememberedOverflowRegions = 0;
    std::uint64_t pinnedSourceRegions = 0;
    std::uint64_t evacuationFallbackRegions = 0;
    std::uint32_t forceInPlaceReasonMask = 0;
    std::uint64_t survivorRegions = 0;
    std::uint64_t survivorObjects = 0;
    std::uint64_t survivorBytes = 0;
    std::uint16_t tenuringThreshold = 0;
    std::uint16_t nextTenuringThreshold = 0;
    std::uint32_t traceWorkers = 0;
    std::uint64_t handshakeMicros = 0;
    std::uint64_t initialPauseMicros = 0;
    std::uint64_t concurrentMarkMicros = 0;
    std::uint64_t finalPauseMicros = 0;
    std::uint64_t traceMicros = 0;
    std::uint64_t evacuateMicros = 0;
    std::uint64_t remapMicros = 0;
    std::uint64_t pauseMicros = 0;
    bool concurrentMark = false;
    bool metadataDegraded = false;
    bool youngMarkMetadataOverflow = false;
    bool forcedInPlace = false;
    bool noMoveRequested = false;
    std::uint64_t threadRootSnapshots = 0;
    std::uint64_t threadRootSlots = 0;
    FinalizerStats finalizers{};
    std::uint64_t finalizerMicros = 0;
};

struct RelocationStats final {
    std::uint64_t relocatedObjects = 0;
    std::uint64_t relocatedBytes = 0;
    std::uint64_t sourceRegions = 0;
    std::uint64_t destinationRegions = 0;
    // Source Regions compacted with a downward in-place slide because no
    // separate relocation Region could be reserved.
    std::uint64_t inPlaceRegions = 0;
    std::uint64_t forwardedStores = 0;
    std::uint64_t failedRegions = 0;
    bool degraded = false;
};

struct AllocationStats final {
    std::uint64_t allocatedObjects = 0;
    std::uint64_t allocatedBytes = 0;
    std::uint64_t tlabFastAllocations = 0;
    std::uint64_t tlabRefills = 0;
    std::uint64_t createdRegions = 0;
    std::uint64_t emergencyCollections = 0;
    std::uint64_t allocationStalls = 0;
    std::uint64_t allocationStallMicros = 0;
    std::uint64_t allocationStallTimeouts = 0;
    std::uint32_t maximumAllocationStallWaiters = 0;
};

struct HeapStats final {
    std::uint64_t usedBytes = 0;
    std::uint64_t reservedBytes = 0;
    std::uint64_t committedBytes = 0;
    std::uint64_t reusableBytes = 0;
    std::uint64_t addressSpaceReservedBytes = 0;
    std::uint64_t largePageAdvisedBytes = 0;
    std::uint64_t numaPlacedBytes = 0;
    std::uint64_t memoryPolicyFallbacks = 0;
    std::uint64_t rememberedBitmapBytes = 0;
    std::uint64_t youngMarkBitmapBytes = 0;
    std::uint64_t isolatedPinnedObjects = 0;
    std::uint64_t pinIsolationFallbacks = 0;
    std::uint64_t edenBytes = 0;
    std::uint64_t survivorBytes = 0;
    std::uint64_t oldBytes = 0;
    std::uint64_t pinnedBytes = 0;
    std::uint64_t largeBytes = 0;
    std::uint64_t youngLargeBytes = 0;
    std::uint32_t activeRegions = 0;
    std::uint32_t reusableRegions = 0;
    std::uint32_t adaptiveMarkWorkers = 0;
    std::uint32_t adaptiveRelocationWorkers = 0;
    std::uint16_t tenuringThreshold = 0;
};

class Runtime final {
public:
    static Runtime& instance();

    void initialize(const RuntimeConfig& config = {});
    void shutdown();
    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::size_t attachedThreadCount() const noexcept;

    void attachCurrentThread();
    void detachCurrentThread();
    [[nodiscard]] bool currentThreadAttached() const noexcept;
    void enterNativeBlocking();
    void leaveNativeBlocking();
    [[nodiscard]] bool beginManagedExecution(
        RootThreadState& owner);
    void endManagedExecution(RootThreadState& owner) noexcept;
    void pushRootFrame(RootFrame& frame);
    void popRootFrame(RootFrame& frame);
    void popForeignRootFrame(RootThreadState& owner,
                             RootFrame& frame) noexcept;
    void releaseForeignFastScopedPin(RootThreadState& owner,
                                     void* object) noexcept;
    void registerStaticRoot(RefSlot& slot);
    void unregisterStaticRoot(RefSlot& slot);
    void safepointPoll();

    [[nodiscard]] void* allocate(const TypeDescriptor& type);
    [[nodiscard]] void* allocateVariable(const TypeDescriptor& type,
                                         std::size_t payloadBytes);
    void publish(void* object);
    void finishConstruction(void* object);

    [[nodiscard]] Ref makeRef(void* object) const noexcept;
    [[nodiscard]] void* loadBarrier(RefSlot& slot);
    void storeBarrier(void* owner, RefSlot& slot, void* value);
    // Refresh a non-heap cache of a managed reference.  Unlike an exact root
    // or object field, the previous cache contents are not a live edge and
    // may still contain an address from a reclaimed relocation source.
    void refreshUntrackedReference(RefSlot& slot, void* value);
    [[nodiscard]] void* acquireScopedPin(RefSlot& rootedReference);
    void releaseScopedPin(void* object) noexcept;
    // Returns the stable address. A normal object may be isolated into a
    // dedicated Pinned Region, so callers that retain a raw pointer must use
    // the returned value rather than the pre-pin address.
    void* pinForNative(void* object);
    [[nodiscard]] const TypeDescriptor* typeOf(void* object);
    [[nodiscard]] bool isInstanceOf(void* object,
                                    const TypeDescriptor& expected);
    [[nodiscard]] std::uint64_t identityOf(void* object);

    [[nodiscard]] Handle createHandle(void* object, HandleKind kind);
    [[nodiscard]] void* resolveHandle(Handle handle);
    void releaseHandle(Handle handle);
    // Native libraries commonly retain an opaque Haxe object pointer across
    // asynchronous callbacks. A moving collector cannot treat that pointer as
    // an untracked raw address: keep the object both strongly reachable and
    // permanently pinned until the callback registration is released.
    [[nodiscard]] void* retainNativeCallback(void* object);
    void releaseNativeCallback(void* object);
    // A critical-memory-pressure request makes Soft handles weak for the next
    // collection. Weak, cleared Soft, and Phantom handles are published to a
    // generation-safe reference queue.
    void requestSoftReferenceClearing() noexcept;
    [[nodiscard]] bool referenceEnqueued(Handle handle) const;
    [[nodiscard]] bool pollReferenceQueue(Handle& handle);

    [[nodiscard]] EphemeronHandle createEphemeron(void* key, void* value);
    [[nodiscard]] void* resolveEphemeronKey(EphemeronHandle handle);
    [[nodiscard]] void* resolveEphemeronValue(EphemeronHandle handle);
    void releaseEphemeron(EphemeronHandle handle);
    [[nodiscard]] FinalizerStats drainFinalizers();
    void addFinalizer(void* object,
                      std::function<void(void*)> callback);

    [[nodiscard]] YoungCollectionStats collectYoung();
    [[nodiscard]] CollectionStats collectBootstrap();
    [[nodiscard]] RelocationStats compactBootstrap();
    void beginConcurrentRelocation();
    [[nodiscard]] RelocationStats finishConcurrentRelocation();
    [[nodiscard]] bool concurrentRelocationActive() const noexcept;
    [[nodiscard]] AllocationStats allocationStats() const noexcept;
    [[nodiscard]] HeapStats heapStats() const;
    [[nodiscard]] std::uint64_t usedBytes() const;
    [[nodiscard]] std::uint64_t reservedBytes() const;
    [[nodiscard]] std::uint64_t committedBytes() const;
    [[nodiscard]] std::uint64_t reusableBytes() const;
    void uncommitUnusedMemory(bool force = false);
    void startConcurrentMark();
    [[nodiscard]] CollectionStats finishConcurrentMark();
    [[nodiscard]] bool concurrentMarkActive() const noexcept;
    [[nodiscard]] View goodView() const noexcept;

private:
    static Runtime& initializeInstance();
    [[nodiscard]] const TypeDescriptor* typeOfSlow(void* object);
    [[nodiscard]] void* isolateForPinnedHandle(void* object);
    [[nodiscard]] void* allocateVariableOnce(const TypeDescriptor& type,
                                             std::size_t payloadBytes);
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    struct Impl;
    Impl* impl_;
};

// Runtime lifetime is process-wide. After the first serialized construction,
// generated code can load the already-published pointer directly instead of
// paying an out-of-line call plus a function-static guard on every ObjectPtr
// construction, assignment and barrier slow path. Runtime initialization
// happens before Haxe worker threads are created, so thread creation publishes
// this immutable pointer to them.
extern Runtime* runtimeInstanceFast;

inline Runtime& Runtime::instance() {
    Runtime* runtime = runtimeInstanceFast;
    return runtime != nullptr ? *runtime : initializeInstance();
}

// The mutator installs this pointer when it attaches. Reserving the root
// snapshot up front makes normal generated root scopes a few pointer/size
// operations; the runtime slow path grows the reserve or shades roots while a
// concurrent mark is active.
extern thread_local RootThreadState* currentThread;
extern std::atomic<bool> rootFrameBarrierActive;
extern std::atomic<bool> scopedPinBarrierActive;
extern std::atomic<bool> safepointRequested;
inline constexpr std::uint32_t FastBarrierViewMask =
    static_cast<std::uint32_t>(Ref::ViewMask);
inline constexpr std::uint32_t FastBarrierYoungActive = 1U << 2U;
inline constexpr std::uint32_t FastBarrierRelocationActive = 1U << 3U;
inline constexpr std::uint32_t FastBarrierViewTransition = 1U << 4U;
inline constexpr std::uint32_t FastBarrierGcPhase = 1U << 5U;
inline constexpr std::uint32_t FastBarrierMarkActive = 1U << 6U;
inline constexpr std::uint32_t FastBarrierSlowMask =
    FastBarrierRelocationActive | FastBarrierViewTransition |
    FastBarrierGcPhase;
inline constexpr std::uint32_t FastStoreBarrierSlowMask =
    FastBarrierYoungActive | FastBarrierRelocationActive |
    FastBarrierViewTransition | FastBarrierGcPhase |
    FastBarrierMarkActive;
extern std::atomic<std::uint32_t> fastBarrierState;
extern thread_local bool gcBarrierSlowPath;
extern std::atomic<std::uintptr_t> managedArenaStartFast;
extern std::atomic<std::uintptr_t> managedArenaEndFast;

#if defined(_MSC_VER)
#define HX_NOVAGC_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define HX_NOVAGC_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define HX_NOVAGC_ALWAYS_INLINE inline
#endif

[[nodiscard]] HX_NOVAGC_ALWAYS_INLINE const TypeDescriptor*
Runtime::typeOf(void* object) {
    if (object == nullptr)
        return nullptr;
    const std::uint32_t state =
        fastBarrierState.load(std::memory_order_acquire);
    if (currentThread != nullptr &&
        (state & FastBarrierSlowMask) == 0) {
        // ObjectPtr::get and ScopedPin already provide a current raw address.
        // Outside a relocation/view transition the descriptor is immutable,
        // so keep the overwhelmingly common RTTI query entirely in the
        // generated caller.
        const auto* header = reinterpret_cast<const ObjectHeader*>(
            static_cast<const std::byte*>(object) - sizeof(ObjectHeader));
        return header->type;
    }
    return typeOfSlow(object);
}

[[nodiscard]] HX_NOVAGC_ALWAYS_INLINE bool Runtime::isInstanceOf(
        void* object, const TypeDescriptor& expected) {
    for (const TypeDescriptor* type = typeOf(object); type != nullptr;
         type = type->super) {
        if (type == &expected)
            return true;
    }
    return false;
}

[[nodiscard]] HX_NOVAGC_ALWAYS_INLINE Ref makeRefFast(
        void* object) noexcept {
    if (object == nullptr)
        return Ref{};
    const std::uint32_t state =
        fastBarrierState.load(std::memory_order_acquire);
    return Ref::encode(object, static_cast<View>(
        state & FastBarrierViewMask));
}

// Generated ObjectPtr reads overwhelmingly observe an already-healed slot.
// Keep that path header-only: one slot acquire and one coherent collector-state
// acquire. View changes and concurrent relocation publish a slow bit before
// they can make an address unsafe, so mismatches retain the complete healing
// path. Concurrent young marking does not move its immutable source cohort;
// evacuation and complete remapping run while every mutator is parked.
[[nodiscard]] HX_NOVAGC_ALWAYS_INLINE void* loadBarrierFast(RefSlot& slot) {
    const std::uint64_t observed = slot.load(std::memory_order_acquire);
    if (observed == 0)
        return nullptr;
    const std::uint32_t state =
        fastBarrierState.load(std::memory_order_acquire);
    if (currentThread != nullptr &&
        (state & FastBarrierSlowMask) == 0 &&
        (observed & Ref::ViewMask) ==
            static_cast<std::uint64_t>(state & FastBarrierViewMask)) {
        return Ref(observed).address();
    }
    return Runtime::instance().loadBarrier(slot);
}

// ObjectPtr and Dynamic copy/assignment operators generally do not know the
// managed owner of their destination slot. The overwhelming majority of
// those slots are exact stack roots, generated statics, or native side-table
// cells. Outside every tracing/moving phase they need neither SATB nor an
// old-to-young card. Reject them with the immutable reserved-arena bounds and
// publish the already-coloured reference directly; heap slots and every
// active collector phase retain the complete Runtime barrier.
HX_NOVAGC_ALWAYS_INLINE void storeBarrierFast(
        void* owner, RefSlot& slot, void* value) {
    const std::uint32_t state =
        fastBarrierState.load(std::memory_order_acquire);
    if (owner == nullptr && (state & FastStoreBarrierSlowMask) == 0) {
        const std::uintptr_t arenaStart =
            managedArenaStartFast.load(std::memory_order_relaxed);
        const std::uintptr_t arenaEnd =
            managedArenaEndFast.load(std::memory_order_relaxed);
        const std::uintptr_t slotAddress =
            reinterpret_cast<std::uintptr_t>(&slot);
        if (arenaStart != 0 && arenaEnd >= arenaStart + sizeof(RefSlot) &&
            (slotAddress < arenaStart ||
             slotAddress > arenaEnd - sizeof(RefSlot))) {
            slot.store(Ref::encode(value, static_cast<View>(
                state & FastBarrierViewMask)).bits(),
                std::memory_order_release);
            return;
        }
    }
    Runtime::instance().storeBarrier(owner, slot, value);
}

// ObjectPtr-to-ObjectPtr assignment used to heal/decode the source and then
// recolour/encode it again even when both slots were ordinary roots and the
// collector was completely idle.  Copy the already-coloured reference in one
// pass on that overwhelmingly common path.  A stale view is still a valid,
// non-moving address while every slow bit is clear; the next read heals it.
// Active marking/young/relocation phases and destinations inside the managed
// arena retain the complete load + store barrier pair.
HX_NOVAGC_ALWAYS_INLINE void copyBarrierFast(
        void* owner, RefSlot& slot, const RefSlot& source) {
    const std::uint64_t observed = source.load(std::memory_order_acquire);
    const std::uint32_t state =
        fastBarrierState.load(std::memory_order_acquire);
    if (owner == nullptr && (state & FastStoreBarrierSlowMask) == 0) {
        const std::uintptr_t arenaStart =
            managedArenaStartFast.load(std::memory_order_relaxed);
        const std::uintptr_t arenaEnd =
            managedArenaEndFast.load(std::memory_order_relaxed);
        const std::uintptr_t slotAddress =
            reinterpret_cast<std::uintptr_t>(&slot);
        if (arenaStart != 0 && arenaEnd >= arenaStart + sizeof(RefSlot) &&
            (slotAddress < arenaStart ||
             slotAddress > arenaEnd - sizeof(RefSlot))) {
            slot.store(observed, std::memory_order_release);
            return;
        }
    }

    RefSlot& mutableSource = const_cast<RefSlot&>(source);
    void* value = observed == 0 ? nullptr : loadBarrierFast(mutableSource);
    Runtime::instance().storeBarrier(owner, slot, value);
}

class RootScope final {
public:
    HX_NOVAGC_ALWAYS_INLINE RootScope(
            RefSlot** slots, std::uint32_t count, std::uint32_t mapId = 0)
        : frame_{} {
        frame_.slots = slots;
        frame_.slotCount = count;
        frame_.mapId = mapId;
        RootThreadState* state = currentThread;
        ownerState_ = state;

        // Generated managed code normally enters with an already-active
        // mutator state.  Link that top-of-stack frame immediately and avoid
        // the native re-entry/exception path in tens of thousands of tiny
        // RootedValue scopes.
        if (state != nullptr && !state->nativeBlockedLocal &&
            (count == 0 || slots != nullptr)) {
            const std::size_t oldCount = state->rootSlotCount;
            const std::size_t newCount = oldCount + count;
            if (newCount >= oldCount &&
                newCount <= state->rootSnapshotCapacity &&
                !rootFrameBarrierActive.load(std::memory_order_acquire)) {
                frame_.previous = state->roots;
                state->roots = &frame_;
                state->rootSlotCount = newCount;
                fastState_ = state;
                return;
            }
        }

        pushSlow(slots, count);
    }
    HX_NOVAGC_ALWAYS_INLINE ~RootScope() {
        // The normal generated-code case is strictly nested, stays on its
        // creating managed thread, and did not temporarily re-enter from
        // native code.  Handle it before the foreign/out-of-order machinery.
        if (fastState_ != nullptr && fastState_ == currentThread &&
            managedState_ == nullptr && !fastState_->nativeBlockedLocal &&
            fastState_->roots == &frame_) {
            if (fastState_->rootSlotCount < frame_.slotCount)
                std::terminate();
            fastState_->roots = frame_.previous;
            fastState_->rootSlotCount -= frame_.slotCount;
            frame_.previous = nullptr;
            return;
        }

        popSlow();
    }

    RootScope(const RootScope&) = delete;
    RootScope& operator=(const RootScope&) = delete;

private:
    void pushSlow(RefSlot** slots, std::uint32_t count);
    void popSlow() noexcept;

    RootFrame frame_;
    RootThreadState* ownerState_ = nullptr;
    RootThreadState* fastState_ = nullptr;
    RootThreadState* managedState_ = nullptr;
};

#undef HX_NOVAGC_ALWAYS_INLINE

} // namespace hx::gc

#endif // NOVA_HX_GC_RUNTIME_HPP_INCLUDED

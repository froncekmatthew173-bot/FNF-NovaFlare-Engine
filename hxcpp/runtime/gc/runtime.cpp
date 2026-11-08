#include <hx/gc/runtime.hpp>
#include <hx/gc/internal.hpp>

#include <algorithm>
#include <bit>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <exception>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/mempolicy.h>
#include <sys/syscall.h>
#endif
#endif

namespace hx::gc {
namespace {

constexpr std::size_t HeapAlignment = 64;
constexpr std::uint64_t AllocationCounterFlushBytes = 64U * 1024U;
constexpr std::uint64_t AllocationCounterFlushObjects = 256U;
constexpr std::uint64_t IdentityBlockSize = 4096U;
std::atomic<bool> concurrentMarkWorkerIdle{true};
std::atomic<bool> concurrentRelocationWorkerDone{true};
std::atomic<std::uint64_t> lastInitialMarkPauseMicros{0};
std::atomic<std::uint64_t> lastRemarkPauseMicros{0};
std::atomic<std::uint64_t> lastRelocationSelectPauseMicros{0};
std::atomic<std::uint64_t> lastRelocationRemapPauseMicros{0};
std::atomic<bool> selectiveAutomaticRelocation{false};
bool deadTypeCensusEnabled = false;
std::mutex deadTypeCensusMutex;
std::string lastDeadTypeCensus;
std::mutex handshakeDiagnosticMutex;
std::string lastHandshakeDiagnostic;
std::atomic<std::uint64_t> nextThreadSequence{1};
thread_local bool emergencyCollectionActive = false;
thread_local bool gcPhaseActive = false;
thread_local bool gcCycleActive = false;

void publishFastBarrierView(std::atomic<View>& target, View next) noexcept {
    fastBarrierState.fetch_or(FastBarrierViewTransition,
                              std::memory_order_acq_rel);
    target.store(next, std::memory_order_release);
    std::uint32_t observed =
        fastBarrierState.load(std::memory_order_acquire);
    for (;;) {
        const std::uint32_t desired =
            (observed & ~(FastBarrierViewMask |
                          FastBarrierViewTransition)) |
            static_cast<std::uint32_t>(next);
        if (fastBarrierState.compare_exchange_weak(
                observed, desired, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return;
        }
    }
}

[[nodiscard]] std::unique_lock<std::mutex> acquireGcCycle(
        Runtime& runtime, std::mutex& mutex) {
    if (gcCycleActive)
        throw std::logic_error("recursive hxcpp GC cycle");
    std::unique_lock lock(mutex, std::defer_lock);
    while (!lock.try_lock()) {
        // A full mark and relocation deliberately span two public calls.  A
        // competing mutator must remain handshake-cooperative while it waits
        // for that complete logical cycle to publish ownership release.
        if (runtime.currentThreadAttached())
            runtime.safepointPoll();
        std::this_thread::yield();
    }
    return lock;
}

struct GcCycleScope final {
    explicit GcCycleScope(std::unique_lock<std::mutex>&& lock)
        : lock_(std::move(lock)) {
        gcCycleActive = true;
    }
    ~GcCycleScope() { gcCycleActive = false; }
    GcCycleScope(const GcCycleScope&) = delete;
    GcCycleScope& operator=(const GcCycleScope&) = delete;

private:
    std::unique_lock<std::mutex> lock_;
};

[[nodiscard]] std::unique_lock<std::mutex> acquireGcPhase(
        Runtime& runtime, std::mutex& mutex) {
    if (gcPhaseActive)
        throw std::logic_error("recursive hxcpp GC phase transition");
    std::unique_lock lock(mutex, std::defer_lock);
    while (!lock.try_lock()) {
        // Emergency allocation recovery calls the Runtime directly and can
        // race the automatic collector.  Waiting mutators must continue to
        // answer the active collector's safepoint handshake or both cycles
        // deadlock while contending for this transition lock.
        if (runtime.currentThreadAttached())
            runtime.safepointPoll();
        std::this_thread::yield();
    }
    return lock;
}

struct GcPhaseScope final {
    GcPhaseScope() {
        fastBarrierState.fetch_or(FastBarrierGcPhase,
                                  std::memory_order_acq_rel);
        gcPhaseActive = true;
        gcBarrierSlowPath = true;
    }
    ~GcPhaseScope() {
        gcBarrierSlowPath = false;
        gcPhaseActive = false;
        fastBarrierState.fetch_and(~FastBarrierGcPhase,
                                   std::memory_order_release);
    }
    GcPhaseScope(const GcPhaseScope&) = delete;
    GcPhaseScope& operator=(const GcPhaseScope&) = delete;
};

void recordPauseMicros(
        std::atomic<std::uint64_t>& destination,
        std::chrono::steady_clock::time_point started) noexcept {
    destination.store(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count()),
        std::memory_order_release);
}

[[nodiscard]] constexpr std::size_t alignUp(std::size_t value,
                                             std::size_t alignment) noexcept {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

[[nodiscard]] ObjectHeader* headerOf(void* object) noexcept {
    return reinterpret_cast<ObjectHeader*>(
        static_cast<std::byte*>(object) - sizeof(ObjectHeader));
}

[[nodiscard]] void* payloadOf(ObjectHeader* header) noexcept {
    return reinterpret_cast<std::byte*>(header) + sizeof(ObjectHeader);
}

[[nodiscard]] bool currentStackContains(const void* address,
                                        std::size_t bytes) noexcept {
#if defined(_WIN32)
    // TEB access is an inline GS/FS read on Windows, so this is cheap enough
    // for the write-barrier fast path.  StackLimit is the current committed
    // lower bound and StackBase is the stable upper bound; when a guard page
    // grows the stack, the next call observes the updated limit.
    const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    const auto end = start + bytes;
    const auto low = reinterpret_cast<std::uintptr_t>(tib->StackLimit);
    const auto high = reinterpret_cast<std::uintptr_t>(tib->StackBase);
    return end >= start && start >= low && end <= high;
#else
    (void)address;
    (void)bytes;
    return false;
#endif
}

} // namespace

extern thread_local RootThreadState* currentThread;
std::atomic<bool> rootFrameBarrierActive{false};
// Generated receiver pins normally use the fixed per-mutator ledger. Moving
// phases publish this barrier only after their initial handshake, then scan
// the stable ledger and force every pin created after resume onto the counted
// object-header protocol until final remap completes.
std::atomic<bool> scopedPinBarrierActive{false};
std::atomic<bool> safepointRequested{false};

struct Runtime::Impl final {
    enum class RegionKind : std::uint8_t {
        Eden,
        Survivor,
        YoungLarge,
        Old,
        Pinned,
        Large,
        Relocation,
    };

    struct VirtualAddressSpace final {
        struct Span final {
            std::size_t offset = 0;
            std::size_t bytes = 0;
        };

        explicit VirtualAddressSpace(std::size_t requestedBytes)
            : bytes(alignUp(requestedBytes, pageSize())), memory(reserve(bytes)) {
            freeSpans.push_back(Span{0, bytes});
        }

        ~VirtualAddressSpace() { release(memory, bytes); }

        VirtualAddressSpace(const VirtualAddressSpace&) = delete;
        VirtualAddressSpace& operator=(const VirtualAddressSpace&) = delete;

        [[nodiscard]] std::byte* acquire(std::size_t requestedBytes) {
            const std::size_t needed = alignUp(requestedBytes, pageSize());
            auto best = freeSpans.end();
            for (auto iterator = freeSpans.begin(); iterator != freeSpans.end();
                 ++iterator) {
                if (iterator->bytes < needed)
                    continue;
                if (best == freeSpans.end() || iterator->bytes < best->bytes)
                    best = iterator;
            }
            if (best == freeSpans.end())
                throw std::bad_alloc();
            std::byte* result = memory + best->offset;
            best->offset += needed;
            best->bytes -= needed;
            if (best->bytes == 0)
                freeSpans.erase(best);
            assignedBytes += needed;
            return result;
        }

        void relinquish(std::byte* address, std::size_t releasedBytes) noexcept {
            if (address == nullptr || address < memory ||
                address >= memory + bytes) {
                return;
            }
            const std::size_t offset = static_cast<std::size_t>(address - memory);
            const std::size_t length = alignUp(releasedBytes, pageSize());
            if (offset > bytes || length > bytes - offset)
                return;
            try {
                auto position = std::lower_bound(
                    freeSpans.begin(), freeSpans.end(), offset,
                    [](const Span& span, std::size_t candidate) {
                        return span.offset < candidate;
                    });
                position = freeSpans.insert(position, Span{offset, length});
                if (position != freeSpans.begin()) {
                    auto previous = std::prev(position);
                    if (previous->offset + previous->bytes == position->offset) {
                        previous->bytes += position->bytes;
                        position = freeSpans.erase(position);
                        position = previous;
                    }
                }
                auto next = std::next(position);
                if (next != freeSpans.end() &&
                    position->offset + position->bytes == next->offset) {
                    position->bytes += next->bytes;
                    freeSpans.erase(next);
                }
                assignedBytes -= std::min(assignedBytes, length);
            } catch (...) {
                // Region destruction must remain noexcept. Losing a reusable
                // subspan only reduces capacity until shutdown; it cannot
                // expose overlapping live Regions.
            }
        }

        [[nodiscard]] static std::size_t pageSize() noexcept {
#if defined(_WIN32)
            SYSTEM_INFO info{};
            GetSystemInfo(&info);
            return static_cast<std::size_t>(info.dwAllocationGranularity);
#else
            const long value = ::sysconf(_SC_PAGESIZE);
            return value > 0 ? static_cast<std::size_t>(value) : 4096U;
#endif
        }

        [[nodiscard]] static std::byte* reserve(std::size_t size) {
#if defined(_WIN32)
            void* result = VirtualAlloc(nullptr, size, MEM_RESERVE,
                                        PAGE_READWRITE);
            if (result == nullptr)
                throw std::bad_alloc();
            return static_cast<std::byte*>(result);
#else
            void* result = ::mmap(nullptr, size, PROT_NONE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (result == MAP_FAILED)
                throw std::bad_alloc();
            return static_cast<std::byte*>(result);
#endif
        }

        static void release(std::byte* address, std::size_t size) noexcept {
            if (address == nullptr)
                return;
#if defined(_WIN32)
            (void)size;
            VirtualFree(address, 0, MEM_RELEASE);
#else
            ::munmap(address, size);
#endif
        }

        std::size_t bytes = 0;
        std::byte* memory = nullptr;
        std::size_t assignedBytes = 0;
        std::vector<Span> freeSpans;
    };

    struct Region final {
        explicit Region(std::size_t requestedBytes, std::uint32_t inIndex,
                        VirtualAddressSpace* inAddressSpace,
                        NumaPolicy inNumaPolicy,
                        LargePagePolicy inLargePagePolicy,
                        std::uint16_t inNumaNode)
            : bytes(alignUp(requestedBytes,
                            std::max(HeapAlignment, pageSize()))),
              index(inIndex),
              addressSpace(inAddressSpace),
              memory(addressSpace != nullptr ? addressSpace->acquire(bytes)
                                             : reserve(bytes)),
              objectStartWordCount((bytes / alignof(ObjectHeader) + 63U) / 64U),
              numaPolicy(inNumaPolicy), largePagePolicy(inLargePagePolicy),
              numaNode(inNumaNode) {
            try {
                objectStarts =
                    std::make_unique<std::atomic<std::uint64_t>[]>(
                        objectStartWordCount);
                youngMarks =
                    std::make_unique<std::atomic<std::uint64_t>[]>(
                        objectStartWordCount);
            } catch (...) {
                if (addressSpace != nullptr)
                    addressSpace->relinquish(memory, bytes);
                else
                    release(memory, bytes);
                memory = nullptr;
                throw;
            }
            for (std::size_t index = 0; index < objectStartWordCount; ++index) {
                objectStarts[index].store(0, std::memory_order_relaxed);
                youngMarks[index].store(0, std::memory_order_relaxed);
            }
        }

        ~Region() {
            if (addressSpace != nullptr) {
                (void)decommitAll();
                addressSpace->relinquish(memory, bytes);
            } else {
                release(memory, bytes);
            }
        }

        Region(const Region&) = delete;
        Region& operator=(const Region&) = delete;

        std::size_t bytes;
        std::size_t cursor = 0;
        std::size_t committed = 0;
        std::uint32_t index;
        VirtualAddressSpace* addressSpace = nullptr;
        std::byte* memory;
        std::size_t objectStartWordCount;
        std::unique_ptr<std::atomic<std::uint64_t>[]> objectStarts;
        // Young and old/full cycles deliberately do not share mark metadata.
        // Full liveness remains on ObjectHeader::markEpoch while a source
        // Region's concurrent young cycle owns this independent bitmap.
        std::unique_ptr<std::atomic<std::uint64_t>[]> youngMarks;
        NumaPolicy numaPolicy = NumaPolicy::Disabled;
        LargePagePolicy largePagePolicy = LargePagePolicy::Disabled;
        std::uint16_t numaNode = 0;
        bool largePageAdvised = false;
        bool numaPlaced = false;
        bool largePagePolicyAttempted = false;
        bool numaPolicyAttempted = false;
        std::uint64_t memoryPolicyFallbacks = 0;
        std::unique_ptr<std::atomic<std::uint64_t>[]>
            rememberedBitmapStorage[2];
        std::atomic<std::atomic<std::uint64_t>*> rememberedBitmap[2]{};
        std::mutex rememberedBitmapMutex;
        std::atomic<std::uint64_t> liveObjects{0};
        std::atomic<std::uint64_t> liveBytes{0};
        std::atomic<std::uint64_t> pinnedObjects{0};
        // Normal TLAB allocation lands in nursery regions.  A young
        // collection either reclaims the complete region or promotes it as a
        // unit, so mutators only need a single old-to-young card bit here.
        std::atomic<bool> young{true};
        std::atomic<bool> youngDirty{false};
        std::atomic<bool> youngOverflow{false};
        std::atomic<RegionKind> kind{RegionKind::Eden};
        bool relocationSource = false;
        // A concurrent young cycle snapshots an immutable nursery cohort.
        // TLAB refill must not hand any remaining space in that cohort back
        // to a mutator after the initial pause; allocations made while the
        // marker runs belong to the next Eden cohort.
        bool youngCollectionSource = false;
        // Concurrent relocation assigns each source region to exactly one
        // worker.  These flags are written by that worker and consumed only
        // after all relocation threads have joined, so they do not need to be
        // atomic.  Keeping the state on the region lets an allocation failure
        // retain a partially evacuated source without discarding successfully
        // completed sources from the same cycle.
        bool relocationComplete = false;
        bool relocationFailed = false;
        bool retired = false;
        bool largeOnly = false;
        std::uint32_t retiredEpoch = 0;

        void setKind(RegionKind value) noexcept {
            kind.store(value, std::memory_order_release);
            young.store(value == RegionKind::Eden ||
                            value == RegionKind::Survivor ||
                            value == RegionKind::YoungLarge,
                        std::memory_order_release);
        }

        [[nodiscard]] bool isYoung() const noexcept {
            const RegionKind value = kind.load(std::memory_order_acquire);
            return value == RegionKind::Eden ||
                   value == RegionKind::Survivor ||
                   value == RegionKind::YoungLarge;
        }

        [[nodiscard]] bool acceptsTlab() const noexcept {
            return kind.load(std::memory_order_acquire) == RegionKind::Eden &&
                   !retired && !relocationSource &&
                   !youngCollectionSource && !largeOnly;
        }

        [[nodiscard]] static std::size_t pageSize() noexcept {
#if defined(_WIN32)
            SYSTEM_INFO info{};
            GetSystemInfo(&info);
            return static_cast<std::size_t>(info.dwPageSize);
#else
            const long value = ::sysconf(_SC_PAGESIZE);
            return value > 0 ? static_cast<std::size_t>(value) : 4096U;
#endif
        }

        [[nodiscard]] static std::byte* reserve(std::size_t size) {
#if defined(_WIN32)
            void* result = VirtualAlloc(nullptr, size, MEM_RESERVE,
                                        PAGE_READWRITE);
            if (result == nullptr)
                throw std::bad_alloc();
            return static_cast<std::byte*>(result);
#else
            void* result = ::mmap(nullptr, size, PROT_NONE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (result == MAP_FAILED)
                throw std::bad_alloc();
            return static_cast<std::byte*>(result);
#endif
        }

        static void release(std::byte* address, std::size_t size) noexcept {
            if (address == nullptr)
                return;
#if defined(_WIN32)
            (void)size;
            VirtualFree(address, 0, MEM_RELEASE);
#else
            ::munmap(address, size);
#endif
        }

        void ensureCommitted(std::size_t required) {
            if (required <= committed)
                return;
            const std::size_t page = pageSize();
            const std::size_t target = std::min(
                bytes, (required + page - 1U) / page * page);
#if defined(_WIN32)
            void* committedAddress = nullptr;
            if (numaPolicy != NumaPolicy::Disabled) {
                numaPolicyAttempted = true;
                committedAddress = VirtualAllocExNuma(
                    GetCurrentProcess(), memory + committed,
                    target - committed, MEM_COMMIT, PAGE_READWRITE,
                    numaNode);
                if (committedAddress != nullptr)
                    numaPlaced = true;
            }
            if (committedAddress == nullptr) {
                if (numaPolicy != NumaPolicy::Disabled)
                    ++memoryPolicyFallbacks;
                committedAddress = VirtualAlloc(
                    memory + committed, target - committed,
                    MEM_COMMIT, PAGE_READWRITE);
            }
            if (committedAddress == nullptr) {
                throw std::bad_alloc();
            }
            if (largePagePolicy != LargePagePolicy::Disabled &&
                !largePagePolicyAttempted) {
                // Windows explicit large pages require committing the whole
                // allocation with MEM_LARGE_PAGES and a privileged token,
                // which conflicts with ZGC-style on-demand commit/uncommit.
                // Keep the bounded commit path and expose the policy fallback.
                largePagePolicyAttempted = true;
                ++memoryPolicyFallbacks;
            }
#else
            if (::mprotect(memory + committed, target - committed,
                           PROT_READ | PROT_WRITE) != 0) {
                throw std::bad_alloc();
            }
#if defined(MADV_HUGEPAGE)
            if (largePagePolicy != LargePagePolicy::Disabled) {
                largePagePolicyAttempted = true;
                if (::madvise(memory + committed, target - committed,
                              MADV_HUGEPAGE) == 0) {
                    largePageAdvised = true;
                    if (largePagePolicy == LargePagePolicy::Explicit)
                        ++memoryPolicyFallbacks;
                } else {
                    ++memoryPolicyFallbacks;
                }
            }
#else
            if (largePagePolicy != LargePagePolicy::Disabled &&
                !largePagePolicyAttempted) {
                largePagePolicyAttempted = true;
                ++memoryPolicyFallbacks;
            }
#endif
            if (numaPolicy == NumaPolicy::Local) {
                // Anonymous writable pages are first-touched by the allocating
                // mutator, which is the native local-node policy on POSIX.
                numaPolicyAttempted = true;
                numaPlaced = true;
            } else if (numaPolicy == NumaPolicy::Interleave) {
                numaPolicyAttempted = true;
#if defined(__linux__) && defined(SYS_mbind) && \
        defined(SYS_get_mempolicy) && defined(MPOL_F_MEMS_ALLOWED)
                constexpr unsigned long NodeBits =
                    sizeof(unsigned long) * 8U;
                unsigned long allowedNodes = 0;
                const long queried = ::syscall(
                    SYS_get_mempolicy, nullptr, &allowedNodes, NodeBits,
                    nullptr, MPOL_F_MEMS_ALLOWED);
                const long bound = queried == 0 && allowedNodes != 0
                    ? ::syscall(SYS_mbind, memory + committed,
                                target - committed, MPOL_INTERLEAVE,
                                &allowedNodes, NodeBits, 0UL)
                    : -1;
                if (bound == 0)
                    numaPlaced = true;
                else
                    ++memoryPolicyFallbacks;
#else
                ++memoryPolicyFallbacks;
#endif
            }
#endif
            committed = target;
        }

        [[nodiscard]] bool decommitAll() noexcept {
            if (committed == 0)
                return true;
#if defined(_WIN32)
            if (!VirtualFree(memory, committed, MEM_DECOMMIT))
                return false;
#else
            if (::madvise(memory, committed, MADV_DONTNEED) != 0 ||
                ::mprotect(memory, committed, PROT_NONE) != 0) {
                return false;
            }
#endif
            committed = 0;
            return true;
        }

        void resetForReuse() noexcept {
            // Committed pages are retained until the uncommit delay expires.
            // Clear them off-pause now so a reused bump region preserves the
            // allocator invariant that every new payload begins as all-zero.
            if (committed != 0)
                std::memset(memory, 0, committed);
            for (std::size_t wordIndex = 0;
                 wordIndex < objectStartWordCount; ++wordIndex) {
                objectStarts[wordIndex].store(0, std::memory_order_relaxed);
                youngMarks[wordIndex].store(0, std::memory_order_relaxed);
            }
            cursor = 0;
            liveObjects.store(0, std::memory_order_relaxed);
            liveBytes.store(0, std::memory_order_relaxed);
            pinnedObjects.store(0, std::memory_order_relaxed);
            youngDirty.store(false, std::memory_order_relaxed);
            youngOverflow.store(false, std::memory_order_relaxed);
            clearRememberedBitmap(0);
            clearRememberedBitmap(1);
            relocationSource = false;
            youngCollectionSource = false;
            relocationComplete = false;
            relocationFailed = false;
            retired = false;
            largeOnly = false;
            largePageAdvised = false;
            numaPlaced = false;
            largePagePolicyAttempted = false;
            numaPolicyAttempted = false;
            memoryPolicyFallbacks = 0;
            retiredEpoch = 0;
            setKind(RegionKind::Eden);
        }

        void recordObject(ObjectHeader* header) noexcept {
            const std::size_t byteOffset =
                reinterpret_cast<std::byte*>(header) - memory;
            const std::size_t unit = byteOffset / alignof(ObjectHeader);
            objectStarts[unit / 64U].fetch_or(
                std::uint64_t{1} << (unit % 64U), std::memory_order_release);
        }

        void clearYoungMarks() noexcept {
            for (std::size_t wordIndex = 0;
                 wordIndex < objectStartWordCount; ++wordIndex) {
                youngMarks[wordIndex].store(0, std::memory_order_relaxed);
            }
        }

        [[nodiscard]] bool tryMarkYoung(ObjectHeader* header) noexcept {
            const std::size_t byteOffset =
                reinterpret_cast<std::byte*>(header) - memory;
            const std::size_t unit = byteOffset / alignof(ObjectHeader);
            const std::uint64_t bit = std::uint64_t{1} << (unit % 64U);
            return (youngMarks[unit / 64U].fetch_or(
                bit, std::memory_order_acq_rel) & bit) == 0;
        }

        [[nodiscard]] bool isYoungMarked(
                const ObjectHeader* header) const noexcept {
            const std::size_t byteOffset =
                reinterpret_cast<const std::byte*>(header) - memory;
            const std::size_t unit = byteOffset / alignof(ObjectHeader);
            const std::uint64_t bit = std::uint64_t{1} << (unit % 64U);
            return (youngMarks[unit / 64U].load(
                std::memory_order_acquire) & bit) != 0;
        }

        [[nodiscard]] std::uint64_t countObjects() const noexcept {
            std::uint64_t result = 0;
            for (std::size_t wordIndex = 0;
                 wordIndex < objectStartWordCount; ++wordIndex) {
                result += std::popcount(objectStarts[wordIndex].load(
                    std::memory_order_acquire));
            }
            return result;
        }

        [[nodiscard]] std::vector<ObjectHeader*> snapshotObjects() const {
            std::vector<ObjectHeader*> result;
            for (std::size_t wordIndex = 0;
                 wordIndex < objectStartWordCount; ++wordIndex) {
                std::uint64_t word =
                    objectStarts[wordIndex].load(std::memory_order_acquire);
                while (word != 0) {
                    const unsigned bit = std::countr_zero(word);
                    const std::size_t unit = wordIndex * 64U + bit;
                    const std::size_t offset = unit * alignof(ObjectHeader);
                    if (offset < bytes)
                        result.push_back(reinterpret_cast<ObjectHeader*>(
                            memory + offset));
                    word &= word - 1U;
                }
            }
            return result;
        }

        [[nodiscard]] ObjectHeader* objectAtPayload(
                const void* candidate) const noexcept {
            const auto* address = static_cast<const std::byte*>(candidate);
            if (address < memory + sizeof(ObjectHeader) ||
                address >= memory + cursor) {
                return nullptr;
            }
            const auto* headerAddress = address - sizeof(ObjectHeader);
            const std::size_t byteOffset = static_cast<std::size_t>(
                headerAddress - memory);
            if (byteOffset % alignof(ObjectHeader) != 0)
                return nullptr;
            const std::size_t unit = byteOffset / alignof(ObjectHeader);
            const std::uint64_t word = objectStarts[unit / 64U].load(
                std::memory_order_acquire);
            if ((word & (std::uint64_t{1} << (unit % 64U))) == 0)
                return nullptr;
            auto* header = reinterpret_cast<ObjectHeader*>(
                memory + byteOffset);
            return payloadOf(header) == candidate ? header : nullptr;
        }

        [[nodiscard]] ObjectHeader* objectContaining(
                const void* candidate) const noexcept {
            const auto* address = static_cast<const std::byte*>(candidate);
            if (address < memory || address >= memory + cursor)
                return nullptr;

            const std::size_t byteOffset =
                static_cast<std::size_t>(address - memory);
            std::size_t unit = byteOffset / alignof(ObjectHeader);
            std::size_t wordIndex = unit / 64U;
            unsigned bitIndex = static_cast<unsigned>(unit % 64U);
            for (;;) {
                std::uint64_t word =
                    objectStarts[wordIndex].load(std::memory_order_acquire);
                if (bitIndex != 63U) {
                    word &= (std::uint64_t{1} << (bitIndex + 1U)) - 1U;
                }
                if (word != 0) {
                    const unsigned highest =
                        63U - static_cast<unsigned>(std::countl_zero(word));
                    const std::size_t startUnit = wordIndex * 64U + highest;
                    auto* header = reinterpret_cast<ObjectHeader*>(
                        memory + startUnit * alignof(ObjectHeader));
                    const auto* payload =
                        static_cast<const std::byte*>(payloadOf(header));
                    if (address >= payload &&
                        address + sizeof(RefSlot) <=
                            payload + header->payloadBytes) {
                        return header;
                    }
                    return nullptr;
                }
                if (wordIndex == 0)
                    return nullptr;
                --wordIndex;
                bitIndex = 63U;
            }
        }

        enum class RememberResult : std::uint8_t {
            First,
            Already,
            Overflow,
        };

        [[nodiscard]] std::size_t rememberedBitmapWordCount() const noexcept {
            const std::size_t slots =
                (bytes + sizeof(RefSlot) - 1U) / sizeof(RefSlot);
            return (slots + 63U) / 64U;
        }

        [[nodiscard]] RememberResult rememberSlot(
                RefSlot* slot, std::uint8_t epoch) noexcept {
            const auto* address = reinterpret_cast<const std::byte*>(slot);
            if (epoch > 1U || address < memory ||
                address + sizeof(RefSlot) > memory + bytes) {
                return RememberResult::Overflow;
            }
            auto* bitmap = rememberedBitmap[epoch].load(
                std::memory_order_acquire);
            if (bitmap == nullptr) {
                try {
                    auto candidate =
                        std::make_unique<std::atomic<std::uint64_t>[]>(
                            rememberedBitmapWordCount());
                    for (std::size_t index = 0;
                         index < rememberedBitmapWordCount(); ++index) {
                        candidate[index].store(0, std::memory_order_relaxed);
                    }
                    std::scoped_lock bitmapLock(rememberedBitmapMutex);
                    bitmap = rememberedBitmap[epoch].load(
                        std::memory_order_relaxed);
                    if (bitmap == nullptr) {
                        rememberedBitmapStorage[epoch] =
                            std::move(candidate);
                        bitmap = rememberedBitmapStorage[epoch].get();
                        rememberedBitmap[epoch].store(
                            bitmap, std::memory_order_release);
                    }
                } catch (...) {
                    return RememberResult::Overflow;
                }
            }
            const std::size_t slotIndex = static_cast<std::size_t>(
                address - memory) / sizeof(RefSlot);
            const std::uint64_t mask =
                std::uint64_t{1} << (slotIndex % 64U);
            const std::uint64_t previous = bitmap[slotIndex / 64U].fetch_or(
                mask, std::memory_order_acq_rel);
            return (previous & mask) == 0
                ? RememberResult::First
                : RememberResult::Already;
        }

        void clearRememberedBitmap(std::uint8_t epoch) noexcept {
            if (epoch > 1U)
                return;
            auto* bitmap = rememberedBitmap[epoch].load(
                std::memory_order_acquire);
            if (bitmap == nullptr)
                return;
            for (std::size_t index = 0;
                 index < rememberedBitmapWordCount(); ++index) {
                bitmap[index].store(0, std::memory_order_relaxed);
            }
        }

        [[nodiscard]] std::uint64_t rememberedBitmapBytes() const noexcept {
            std::uint64_t result = 0;
            for (std::uint8_t epoch = 0; epoch < 2U; ++epoch) {
                if (rememberedBitmap[epoch].load(
                        std::memory_order_acquire) != nullptr) {
                    result += rememberedBitmapWordCount() *
                              sizeof(std::uint64_t);
                }
            }
            return result;
        }
    };

    struct ScopedPinRecord final {
        void* object = nullptr;
        // Pins acquired after concurrent relocation has started still use the
        // object-header protocol because the copy worker may already own the
        // source region.  Inactive-cycle pins need only be published in this
        // per-thread list: every moving phase first parks all mutators and can
        // exclude the containing region before copying anything.
        bool counted = false;
    };

    struct ThreadState final : RootThreadState {
        std::thread::id id;
        std::uint64_t sequence = 0;
#if defined(_WIN32)
        DWORD nativeThreadId = 0;
#else
        std::uint64_t nativeThreadId = 0;
#endif
        std::atomic<bool> parked{false};
        // Blocking helpers are deliberately composable: SSL, socket, file and
        // synchronization adapters can call one another while the mutator is
        // outside managed code.  Only the outermost transition publishes the
        // root snapshot and only its matching exit makes the thread runnable
        // to a safepoint handshake again.
        std::size_t nativeBlockingDepth = 0;
        std::atomic<std::uint64_t> observedHandshakeEpoch{0};
        Region* tlabRegion = nullptr;
        std::byte* tlabCursor = nullptr;
        std::byte* tlabLimit = nullptr;
        std::uint64_t identityCursor = 0;
        std::uint64_t identityLimit = 0;
        std::uint64_t pendingAllocatedObjects = 0;
        std::uint64_t pendingAllocatedBytes = 0;
        std::uint64_t pendingTlabFastAllocations = 0;
        std::vector<ScopedPinRecord> scopedPins;
        std::vector<std::pair<RefSlot*, ObjectHeader*>> rememberedBuffer;
        // The owning mutator copies its exact RootFrame values here before it
        // acknowledges a safepoint handshake.  The collector can then process
        // thread roots without walking another thread's live stack.  Capacity
        // is reserved when frames are pushed, so handshake capture is
        // allocation-free.
        std::vector<std::uint64_t> rootSnapshot;
        std::uint64_t rootSnapshotEpoch = 0;
    };

    struct HandleCell final {
        RefSlot reference{0};
        HandleKind kind = HandleKind::Strong;
        std::uint32_t generation = 1;
        bool active = false;
        bool enqueued = false;
    };

    struct NativeCallbackPin final {
        Handle handle{};
        std::size_t retainCount = 0;
    };

    struct EphemeronCell final {
        RefSlot key{0};
        RefSlot value{0};
        std::uint32_t generation = 1;
        bool active = false;
    };

    enum class FinalizerState : std::uint8_t {
        Registered,
        Pending,
        Running,
        Finalized,
        Cleared,
    };

    struct FinalizerCell final {
        RefSlot object{0};
        FinalizerState state = FinalizerState::Registered;
        std::function<void(void*)> callback;
    };

    struct FreeRegion final {
        std::unique_ptr<Region> region;
        std::chrono::steady_clock::time_point releasedAt{};
    };

    RuntimeConfig config{};
    bool running = false;
    std::atomic<View> view{View::Marked0};
    std::uint32_t markEpoch = 0;
    std::uint32_t youngMarkEpoch = 0;
    // Full-mark liveness floor used by diagnostics.  Normal generational
    // tracing uses the exact owner set below and never walks every object in a
    // dirty mixed region.
    std::uint32_t lastFullMarkEpoch = 0;
    std::uint16_t currentTenuringThreshold = 2;
    std::uint32_t nextRegionIndex = 1;
    std::atomic<std::uint64_t> nextIdentity{1};
    // phaseTransitionMutex protects individual handshakes.  collectionCycleMutex
    // additionally owns split start/finish cycles so a mutator's emergency
    // recovery cannot enter between an automatic collector's start and finish.
    std::mutex collectionCycleMutex;
    std::mutex phaseTransitionMutex;
    std::optional<std::unique_lock<std::mutex>> concurrentMarkCycleLock;
    std::optional<std::unique_lock<std::mutex>> relocationCycleLock;
    std::mutex heapMutex;
    std::mutex threadMutex;
    std::mutex markMutex;
    std::mutex handleMutex;
    std::mutex nativeCallbackMutex;
    std::mutex staticRootMutex;
    std::mutex ephemeronMutex;
    std::mutex finalizerMutex;
    std::mutex finalizerDrainMutex;
    std::mutex relocationMutex;
    std::mutex rememberedMutex;
    std::mutex emergencyMutex;
    std::condition_variable emergencyCondition;
    std::atomic<bool> emergencyServiceActive{false};
    std::atomic<std::uint32_t> allocationStallWaiters{0};
    std::atomic<std::uint32_t> maximumAllocationStallWaiters{0};
    std::atomic<std::uint64_t> allocationStalls{0};
    std::atomic<std::uint64_t> allocationStallMicros{0};
    std::atomic<std::uint64_t> allocationStallTimeouts{0};
    std::condition_variable markCondition;
    std::mutex handshakeMutex;
    // Generated root push/pop stays lock-free on its owning mutator. Only the
    // uncommon destruction of a transferred native RAII owner takes this
    // mutex, while collectors take it when reading live root/pin ledgers.
    std::mutex rootPublicationMutex;
    std::condition_variable handshakeCondition;
    std::atomic<bool> parkRequested{false};
    std::atomic<std::uint64_t> requestedHandshakeEpoch{0};
    std::atomic<std::uint64_t> resumedHandshakeEpoch{0};
    std::deque<ObjectHeader*> markQueue;
    std::vector<std::thread> markerThreads;
    std::mutex youngMarkMutex;
    std::condition_variable youngMarkCondition;
    std::deque<ObjectHeader*> youngMarkQueue;
    std::vector<std::thread> youngMarkerThreads;
    std::unordered_set<Region*> youngMarkRegions;
    std::size_t activeYoungMarkWorkers = 0;
    bool youngMarkClosing = false;
    std::exception_ptr youngMarkFailure;
    std::atomic<bool> youngMarkActive{false};
    std::atomic<bool> youngMarkMetadataOverflow{false};
    std::atomic<std::uint64_t> concurrentYoungMarkedObjects{0};
    std::atomic<std::uint64_t> concurrentYoungMarkedBytes{0};
    std::atomic<std::uint32_t> adaptiveMarkWorkerTarget{0};
    std::atomic<std::uint32_t> adaptiveRelocationWorkerTarget{0};
    std::vector<std::thread> relocationThreads;
    std::size_t activeMarkWorkers = 0;
    std::atomic<std::size_t> nextRelocationSource{0};
    std::atomic<std::size_t> liveRelocationWorkers{0};
    std::exception_ptr markFailure;
    std::atomic<bool> markActive{false};
    bool markClosing = false;
    bool markSealed = false;
    std::atomic<std::uint64_t> concurrentRoots{0};
    std::atomic<std::uint64_t> concurrentThreadRootSnapshots{0};
    std::atomic<std::uint64_t> concurrentThreadRootSlots{0};
    std::atomic<std::uint64_t> concurrentMarkedObjects{0};
    std::atomic<std::uint64_t> concurrentMarkedBytes{0};
    std::atomic<bool> relocationActive{false};
    RelocationStats relocationStats{};
    std::vector<Region*> relocationSources;
    std::vector<Region*> relocationDestinations;
    std::exception_ptr relocationFailure;
    std::atomic<std::uint64_t> allocatedObjects{0};
    std::atomic<std::uint64_t> allocatedBytes{0};
    std::atomic<std::uint64_t> tlabFastAllocations{0};
    std::atomic<std::uint64_t> tlabRefills{0};
    std::atomic<std::uint64_t> createdRegions{0};
    std::atomic<std::uint64_t> emergencyCollections{0};
    std::atomic<std::uint64_t> rememberedFaultCounter{0};
    std::atomic<std::uint64_t> isolatedPinnedObjects{0};
    std::atomic<std::uint64_t> pinIsolationFallbacks{0};
    std::atomic<std::uint64_t> actOnceRememberedStores{0};
    std::atomic<std::uint8_t> activeRememberedEpoch{0};
    std::uint64_t lastReportedActOnceRememberedStores = 0;
    // Declared before Region containers so reverse destruction releases every
    // subspan before the process-wide virtual arena itself.
    std::unique_ptr<VirtualAddressSpace> addressSpace;
    std::uint16_t nextNumaNode = 0;
    std::vector<std::unique_ptr<Region>> regions;
    std::vector<FreeRegion> freeRegions;
    std::vector<std::unique_ptr<ThreadState>> threads;
    std::vector<std::unique_ptr<HandleCell>> handles;
    std::vector<std::uint32_t> freeHandleIndices;
    std::unordered_map<void*, NativeCallbackPin> nativeCallbackPins;
    std::deque<Handle> referenceQueue;
    std::atomic<bool> clearSoftReferencesRequested{false};
    bool clearSoftReferencesThisCycle = false;
    std::vector<RefSlot*> staticRoots;
    std::vector<std::unique_ptr<EphemeronCell>> ephemerons;
    std::vector<std::unique_ptr<FinalizerCell>> finalizers;
    // Two exact slot sets let a young collection atomically detach the
    // remembered-set snapshot while mutators populate the next epoch.  The
    // current young collector parks mutators, but retaining this protocol is
    // required before its tracing/evacuation phases can become concurrent.
    std::unordered_map<RefSlot*, ObjectHeader*> rememberedSlots;
    std::unordered_map<RefSlot*, ObjectHeader*> rememberedSnapshot;

    void trimFreeRegions(bool force) noexcept {
        const auto now = std::chrono::steady_clock::now();
        const auto delay = std::chrono::milliseconds(
            config.uncommitDelayMilliseconds);
        for (FreeRegion& free : freeRegions) {
            if (free.region->committed == 0)
                continue;
            if (force || now - free.releasedAt >= delay)
                (void)free.region->decommitAll();
        }
    }

    [[nodiscard]] std::unique_ptr<Region> acquireRegion(
            std::size_t requestedBytes, RegionKind kindValue) {
        trimFreeRegions(false);
        const auto found = std::min_element(
            freeRegions.begin(), freeRegions.end(),
            [requestedBytes](const FreeRegion& left,
                             const FreeRegion& right) {
                const bool leftFits = left.region->bytes >= requestedBytes;
                const bool rightFits = right.region->bytes >= requestedBytes;
                if (leftFits != rightFits)
                    return leftFits;
                return left.region->bytes < right.region->bytes;
            });
        if (found != freeRegions.end() &&
            found->region->bytes >= requestedBytes) {
            auto result = std::move(found->region);
            freeRegions.erase(found);
            result->setKind(kindValue);
            return result;
        }

        std::uint16_t numaNode = 0;
#if defined(_WIN32)
        if (config.numaPolicy == NumaPolicy::Local) {
            PROCESSOR_NUMBER processor{};
            GetCurrentProcessorNumberEx(&processor);
            (void)GetNumaProcessorNodeEx(&processor, &numaNode);
        } else if (config.numaPolicy == NumaPolicy::Interleave) {
            ULONG highestNode = 0;
            if (GetNumaHighestNodeNumber(&highestNode) && highestNode != 0) {
                numaNode = static_cast<std::uint16_t>(
                    nextNumaNode++ % (highestNode + 1U));
            }
        }
#endif
        auto createRegion = [&] {
            auto result = std::make_unique<Region>(
                requestedBytes, nextRegionIndex++, addressSpace.get(),
                config.numaPolicy, config.largePagePolicy, numaNode);
            result->setKind(kindValue);
            createdRegions.fetch_add(1, std::memory_order_relaxed);
            return result;
        };
        try {
            return createRegion();
        } catch (const std::bad_alloc&) {
            if (freeRegions.empty())
                throw;
            // maximumRegions defines the default arena byte capacity.  It is
            // not a logical object-count cap: dedicated Large/YoungLarge
            // reservations can be much smaller than regionBytes, and a real
            // NF startup legitimately owns more than 4096 of them while using
            // only a fraction of the 8 GiB arena.  If the byte arena is
            // fragmented by differently-sized cached reservations, release
            // those empty subspans, let VirtualAddressSpace coalesce them,
            // and retry once before reporting a genuine capacity failure.
            freeRegions.clear();
            return createRegion();
        }
    }

    void recycleRegions(
            std::vector<std::unique_ptr<Region>>& reclaimed) {
        if (reclaimed.empty())
            return;
        std::scoped_lock heapLock(heapMutex);
        const auto now = std::chrono::steady_clock::now();
        for (auto& region : reclaimed) {
            if (region == nullptr)
                continue;
            region->resetForReuse();
            freeRegions.push_back(FreeRegion{std::move(region), now});
        }
        reclaimed.clear();
        trimFreeRegions(false);
    }

    [[nodiscard]] static bool handleIsStrongRoot(
            HandleKind kind, bool clearSoft) noexcept {
        return kind == HandleKind::Strong || kind == HandleKind::Pinned ||
               (kind == HandleKind::Soft && !clearSoft);
    }

    [[nodiscard]] static bool handleNeedsClearing(
            HandleKind kind, bool clearSoft) noexcept {
        return kind == HandleKind::Weak || kind == HandleKind::Phantom ||
               (kind == HandleKind::Soft && clearSoft);
    }

    void enqueueReference(std::uint32_t index, HandleCell& cell) {
        cell.reference.store(0, std::memory_order_release);
        if (cell.enqueued)
            return;
        cell.enqueued = true;
        referenceQueue.push_back(Handle{index, cell.generation});
    }

    [[nodiscard]] std::uint32_t configuredMarkWorkers() const noexcept {
        if (config.markWorkerCount != 0)
            return config.markWorkerCount;
        const unsigned hardware = std::thread::hardware_concurrency();
        const std::uint32_t topologyDefault =
            std::max(1U, hardware > 2U ? hardware / 2U : 1U);
        const std::uint32_t feedbackTarget =
            adaptiveMarkWorkerTarget.load(std::memory_order_relaxed);
        // Starting dozens of workers for a one-region heap costs more than it
        // can save.  Grow concurrency with available region work and let the
        // next cycle adapt again as the heap expands or contracts.
        const std::uint32_t usefulWorkers = std::max<std::uint32_t>(
            1U, static_cast<std::uint32_t>(regions.size()));
        return std::min(feedbackTarget == 0 ? topologyDefault : feedbackTarget,
                        usefulWorkers);
    }

    [[nodiscard]] std::uint32_t configuredRelocationWorkers() const noexcept {
        if (config.relocationWorkerCount != 0)
            return config.relocationWorkerCount;
        const std::uint32_t feedbackTarget =
            adaptiveRelocationWorkerTarget.load(std::memory_order_relaxed);
        return std::min<std::uint32_t>(
            feedbackTarget == 0 ? configuredMarkWorkers() : feedbackTarget,
            std::max<std::uint32_t>(
                1U, static_cast<std::uint32_t>(relocationSources.size())));
    }

    void adaptWorkerTarget(std::atomic<std::uint32_t>& target,
                           bool explicitlyConfigured,
                           std::uint64_t pauseMicros,
                           std::uint64_t workItems) noexcept {
        if (explicitlyConfigured)
            return;
        const unsigned hardware = std::thread::hardware_concurrency();
        const std::uint32_t ceiling = std::max(
            1U, hardware > 1U ? hardware - 1U : 1U);
        std::uint32_t current = target.load(std::memory_order_relaxed);
        if (current == 0)
            current = std::max(1U, hardware > 2U ? hardware / 2U : 1U);
        const std::uint64_t goal =
            static_cast<std::uint64_t>(config.maximumPauseMilliseconds) *
            1000U;
        std::uint32_t desired = current;
        if (pauseMicros > goal && workItems > current * 128ULL) {
            desired = std::min(ceiling, std::max(current + 1U,
                                                current * 3U / 2U));
        } else if (pauseMicros < goal / 3U && current > 1U &&
                   workItems < current * 256ULL) {
            desired = std::max(1U, current - 1U);
        }
        target.store(desired, std::memory_order_relaxed);
    }

    void enqueueYoungMark(void* object) noexcept {
        if (object == nullptr ||
            !youngMarkActive.load(std::memory_order_acquire)) {
            return;
        }
        ObjectHeader* header = headerOf(object);
        auto* region = reinterpret_cast<Region*>(
            static_cast<std::uintptr_t>(header->reserved0));
        if (region == nullptr || !youngMarkRegions.contains(region) ||
            (header->flags.load(std::memory_order_acquire) &
             ObjectPublished) == 0) {
            return;
        }
        if (region->tryMarkYoung(header)) {
                concurrentYoungMarkedObjects.fetch_add(
                    1, std::memory_order_relaxed);
                concurrentYoungMarkedBytes.fetch_add(
                    header->payloadBytes, std::memory_order_relaxed);
                try {
                    std::scoped_lock markLock(youngMarkMutex);
                    youngMarkQueue.push_back(header);
                    youngMarkCondition.notify_one();
                } catch (...) {
                    // The mark bit is already committed. Final-pause overflow
                    // recovery scans every marked source object to a fixed
                    // point, so queue metadata OOM cannot lose its children.
                    youngMarkMetadataOverflow.store(
                        true, std::memory_order_release);
                }
        }
    }

    void flushAllocationCounters(ThreadState& state) noexcept {
        if (state.pendingAllocatedObjects != 0) {
            allocatedObjects.fetch_add(state.pendingAllocatedObjects,
                                       std::memory_order_relaxed);
            state.pendingAllocatedObjects = 0;
        }
        if (state.pendingAllocatedBytes != 0) {
            allocatedBytes.fetch_add(state.pendingAllocatedBytes,
                                     std::memory_order_relaxed);
            state.pendingAllocatedBytes = 0;
        }
        if (state.pendingTlabFastAllocations != 0) {
            tlabFastAllocations.fetch_add(
                state.pendingTlabFastAllocations, std::memory_order_relaxed);
            state.pendingTlabFastAllocations = 0;
        }
    }

    void flushRememberedBuffer(ThreadState& state) noexcept {
        if (state.rememberedBuffer.empty())
            return;
        std::scoped_lock rememberedLock(rememberedMutex);
        for (const auto& [slot, owner] : state.rememberedBuffer) {
            try {
                rememberedSlots.insert_or_assign(slot, owner);
            } catch (...) {
                auto* region = reinterpret_cast<Region*>(
                    static_cast<std::uintptr_t>(owner->reserved0));
                if (region != nullptr)
                    region->youngOverflow.store(true,
                                                std::memory_order_release);
            }
        }
        state.rememberedBuffer.clear();
    }

    void flushAllRememberedBuffers() {
        std::scoped_lock threadLock(threadMutex);
        for (const auto& state : threads)
            flushRememberedBuffer(*state);
    }

    void captureRootSnapshot(ThreadState& state,
                             std::uint64_t epoch) {
        std::scoped_lock publicationLock(rootPublicationMutex);
        std::size_t actualCount = 0;
        for (RootFrame* frame = state.roots; frame != nullptr;
             frame = frame->previous) {
            if (frame->slotCount != 0 && frame->slots == nullptr)
                throw std::logic_error("root frame has no slot table");
            const std::size_t next = actualCount + frame->slotCount;
            if (next < actualCount)
                throw std::length_error("exact root slot count overflow");
            actualCount = next;
        }
        // The linked chain is authoritative. Repair the fast-path cache while
        // retaining the traversal consistency checks below.
        state.rootSlotCount = actualCount;
        state.rootSnapshot.resize(actualCount);
        std::size_t output = 0;
        for (RootFrame* frame = state.roots; frame != nullptr;
             frame = frame->previous) {
            if (frame->slotCount != 0 && frame->slots == nullptr)
                throw std::logic_error("root frame has no slot table");
            for (std::uint32_t index = 0; index < frame->slotCount; ++index) {
                if (frame->slots[index] == nullptr)
                    throw std::logic_error(
                        "exact root map contains a null slot entry");
                if (output >= state.rootSnapshot.size())
                    throw std::logic_error(
                        "exact root snapshot size changed during handshake");
                state.rootSnapshot[output++] =
                    frame->slots[index]->load(std::memory_order_acquire);
            }
        }
        if (output != state.rootSnapshot.size())
            throw std::logic_error("exact root snapshot size mismatch");
        state.rootSnapshotEpoch = epoch;
    }

    void accountRegionLive(ObjectHeader* header) noexcept {
        auto* region = reinterpret_cast<Region*>(
            static_cast<std::uintptr_t>(header->reserved0));
        if (region == nullptr)
            return;
        region->liveObjects.fetch_add(1, std::memory_order_relaxed);
        region->liveBytes.fetch_add(header->totalBytes,
                                    std::memory_order_relaxed);
    }

    void rememberOldToYoung(void* owner, RefSlot& slot, void* value) noexcept {
        if (value == nullptr)
            return;

        Region* ownerRegion = nullptr;
        ObjectHeader* ownerHeader = nullptr;
        if (owner != nullptr) {
            ownerHeader = headerOf(owner);
            ownerRegion = reinterpret_cast<Region*>(
                static_cast<std::uintptr_t>(ownerHeader->reserved0));
            // New objects overwhelmingly point at other nursery objects while
            // they are being constructed.  Such edges require no remembered
            // card, so reject them before touching the target header.
            if (ownerRegion != nullptr &&
                ownerRegion->young.load(std::memory_order_acquire)) {
                return;
            }
        } else {
            // Generated ObjectPtr/Dynamic assignment operators do not carry
            // their containing Haxe object. Infer the coarse owner card from
            // the destination slot address.  Most owner-less stores are local
            // temporaries in generated render/update functions.  They are
            // exact stack roots, never old heap owners; rejecting them through
            // the TEB avoids a heap lock plus an O(region-count) scan on every
            // ObjectPtr assignment. New-object construction normally hits the
            // current TLAB as the second fast path. Only genuine old-object
            // updates reach the region-table scan.
            auto* state = static_cast<ThreadState*>(currentThread);
            auto* address = reinterpret_cast<std::byte*>(&slot);
            // Every managed Region is carved from one contiguous reserved
            // virtual arena.  Stack roots, generated static roots and native
            // side-table slots are therefore rejected with two integer
            // comparisons instead of taking heapMutex and walking every
            // Region.  Flixel's draw-stack heads are static roots reassigned
            // several times per rendered frame, making this the dominant
            // steady-state write-barrier path in real games.
            const auto slotAddress = reinterpret_cast<std::uintptr_t>(address);
            const auto arenaStart = addressSpace == nullptr
                ? 0U
                : reinterpret_cast<std::uintptr_t>(addressSpace->memory);
            const auto arenaEnd = addressSpace == nullptr
                ? 0U
                : arenaStart + addressSpace->bytes;
            if (addressSpace == nullptr || slotAddress < arenaStart ||
                slotAddress > arenaEnd - sizeof(slot)) {
                return;
            }
            if (currentStackContains(address, sizeof(slot)))
                return;
            if (state != nullptr && state->tlabRegion != nullptr &&
                address >= state->tlabRegion->memory &&
                address < state->tlabRegion->memory +
                              state->tlabRegion->bytes) {
                return;
            }
            std::scoped_lock heapLock(heapMutex);
            for (const auto& region : regions) {
                if (address >= region->memory &&
                    address < region->memory + region->bytes) {
                    ownerRegion = region.get();
                    ownerHeader = ownerRegion->objectContaining(address);
                    break;
                }
            }
        }
        auto* valueRegion = reinterpret_cast<Region*>(
            static_cast<std::uintptr_t>(headerOf(value)->reserved0));
        if (valueRegion == nullptr ||
            !valueRegion->young.load(std::memory_order_acquire))
            return;
        if (ownerHeader != nullptr && ownerRegion != nullptr &&
            !ownerRegion->young.load(std::memory_order_acquire)) {
            ownerRegion->youngDirty.store(true, std::memory_order_release);
            const std::uint8_t rememberedEpoch =
                activeRememberedEpoch.load(std::memory_order_acquire);
            const Region::RememberResult remembered =
                ownerRegion->rememberSlot(&slot, rememberedEpoch);
            if (remembered == Region::RememberResult::Overflow) {
                ownerRegion->youngOverflow.store(true,
                                                 std::memory_order_release);
                return;
            }
            if (remembered == Region::RememberResult::Already) {
                actOnceRememberedStores.fetch_add(
                    1, std::memory_order_relaxed);
                return;
            }
            if (config.rememberedSetFailureEvery != 0 &&
                (rememberedFaultCounter.fetch_add(
                     1, std::memory_order_relaxed) + 1U) %
                        config.rememberedSetFailureEvery == 0) {
                ownerRegion->youngOverflow.store(true,
                                                 std::memory_order_release);
                return;
            }
            auto* state = static_cast<ThreadState*>(currentThread);
            if (state != nullptr) {
                try {
                    state->rememberedBuffer.emplace_back(&slot, ownerHeader);
                    if (state->rememberedBuffer.size() >= 64U)
                        flushRememberedBuffer(*state);
                    return;
                } catch (...) {
                    ownerRegion->youngOverflow.store(
                        true, std::memory_order_release);
                    return;
                }
            }
            try {
                std::scoped_lock rememberedLock(rememberedMutex);
                rememberedSlots.insert_or_assign(&slot, ownerHeader);
            } catch (...) {
                ownerRegion->youngOverflow.store(true,
                                                 std::memory_order_release);
            }
        }
    }

    [[nodiscard]] std::pair<Region*, ObjectHeader*> ownerOfSlot(
            RefSlot* slot) const noexcept {
        const auto* address = reinterpret_cast<const std::byte*>(slot);
        for (const auto& region : regions) {
            if (address < region->memory ||
                address + sizeof(RefSlot) > region->memory + region->cursor) {
                continue;
            }
            return {region.get(), region->objectContaining(address)};
        }
        return {nullptr, nullptr};
    }

    void pruneRememberedOwners(std::uint32_t liveEpoch) {
        std::scoped_lock rememberedLock(rememberedMutex);
        std::erase_if(rememberedSlots,
            [liveEpoch](const auto& entry) {
            ObjectHeader* header = entry.second;
            auto* region = reinterpret_cast<Region*>(
                static_cast<std::uintptr_t>(header->reserved0));
            return region == nullptr || region->retired ||
                   header->markEpoch.load(std::memory_order_acquire) != liveEpoch;
        });
    }

    using ExternalForwardingMap = std::unordered_map<void*, void*>;

    void remapRememberedOwners(
            const ExternalForwardingMap* externalForwarding = nullptr) {
        std::scoped_lock rememberedLock(rememberedMutex);
        const std::uint8_t epoch =
            activeRememberedEpoch.load(std::memory_order_acquire);
        for (const auto& region : regions)
            region->clearRememberedBitmap(epoch);
        std::unordered_map<RefSlot*, ObjectHeader*> remapped;
        remapped.reserve(rememberedSlots.size());
        auto markOwnerOverflow = [](ObjectHeader* header) noexcept {
            if (header == nullptr)
                return;
            auto* region = reinterpret_cast<Region*>(
                static_cast<std::uintptr_t>(header->reserved0));
            if (region == nullptr || region->retired || region->isYoung())
                return;
            region->youngDirty.store(true, std::memory_order_release);
            region->youngOverflow.store(true, std::memory_order_release);
        };
        for (const auto& [originalSlot, originalHeader] : rememberedSlots) {
            if (originalSlot == nullptr || originalHeader == nullptr)
                continue;
            RefSlot* slot = originalSlot;
            ObjectHeader* header = originalHeader;
            auto* oldPayload = static_cast<std::byte*>(payloadOf(header));
            auto* slotAddress = reinterpret_cast<std::byte*>(slot);
            const bool validOriginalOffset =
                slotAddress >= oldPayload &&
                slotAddress + sizeof(RefSlot) <=
                    oldPayload + header->payloadBytes;
            const auto external = externalForwarding == nullptr
                ? ExternalForwardingMap::const_iterator{}
                : externalForwarding->find(oldPayload);
            if (externalForwarding != nullptr &&
                external != externalForwarding->end()) {
                auto* newPayload = static_cast<std::byte*>(external->second);
                ObjectHeader* newHeader = headerOf(newPayload);
                if (!validOriginalOffset) {
                    markOwnerOverflow(newHeader);
                    continue;
                }
                const std::size_t offset = static_cast<std::size_t>(
                    slotAddress - oldPayload);
                if (offset + sizeof(RefSlot) > newHeader->payloadBytes) {
                    markOwnerOverflow(newHeader);
                    continue;
                }
                slot = reinterpret_cast<RefSlot*>(newPayload + offset);
                header = newHeader;
            } else {
                const std::uint64_t forwarding =
                    header->forwarding.load(std::memory_order_acquire);
                if (forwarding != 0) {
                    auto* newPayload = static_cast<std::byte*>(
                        Ref(forwarding).address());
                    ObjectHeader* newHeader = headerOf(newPayload);
                    if (!validOriginalOffset) {
                        markOwnerOverflow(newHeader);
                        continue;
                    }
                    const auto offset = slotAddress - oldPayload;
                    if (static_cast<std::size_t>(offset) + sizeof(RefSlot) >
                        newHeader->payloadBytes) {
                        markOwnerOverflow(newHeader);
                        continue;
                    }
                    slot = reinterpret_cast<RefSlot*>(newPayload + offset);
                    header = newHeader;
                } else if (!validOriginalOffset) {
                    markOwnerOverflow(header);
                    continue;
                }
            }
            auto* ownerRegion = reinterpret_cast<Region*>(
                static_cast<std::uintptr_t>(header->reserved0));
            auto* currentPayload = static_cast<std::byte*>(payloadOf(header));
            auto* currentSlot = reinterpret_cast<std::byte*>(slot);
            if (ownerRegion == nullptr || ownerRegion->retired ||
                ownerRegion->isYoung() ||
                ownerRegion->objectAtPayload(currentPayload) != header ||
                currentSlot < currentPayload ||
                currentSlot + sizeof(RefSlot) >
                    currentPayload + header->payloadBytes) {
                markOwnerOverflow(header);
                continue;
            }
            if (ownerRegion->rememberSlot(slot, epoch) ==
                    Region::RememberResult::Overflow) {
                ownerRegion->youngOverflow.store(
                    true, std::memory_order_release);
            }
            try {
                remapped.insert_or_assign(slot, header);
            } catch (...) {
                ownerRegion->youngOverflow.store(
                    true, std::memory_order_release);
            }
        }
        rememberedSlots.swap(remapped);
    }

    void remapRememberedOwner(ObjectHeader* original,
                              ObjectHeader* relocated) {
        if (original == nullptr || relocated == nullptr)
            return;

        // Pin isolation moves exactly one owner. Rebuilding every entry in the
        // global remembered map is unnecessary and made each native image/audio
        // pin proportional to the whole remembered set. Preserve all unrelated
        // nodes and recreate only fields whose containing object moved.
        std::scoped_lock rememberedLock(rememberedMutex);
        std::vector<std::pair<RefSlot*, ObjectHeader*>> remapped;
        auto* oldPayload = static_cast<std::byte*>(payloadOf(original));
        auto* newPayload = static_cast<std::byte*>(payloadOf(relocated));
        auto* destination = reinterpret_cast<Region*>(
            static_cast<std::uintptr_t>(relocated->reserved0));

        for (auto iterator = rememberedSlots.begin();
             iterator != rememberedSlots.end();) {
            if (iterator->second != original) {
                ++iterator;
                continue;
            }

            auto* slotAddress = reinterpret_cast<std::byte*>(iterator->first);
            if (slotAddress >= oldPayload &&
                slotAddress + sizeof(RefSlot) <=
                    oldPayload + original->payloadBytes) {
                const std::size_t offset = static_cast<std::size_t>(
                    slotAddress - oldPayload);
                remapped.emplace_back(
                    reinterpret_cast<RefSlot*>(newPayload + offset), relocated);
            } else if (destination != nullptr) {
                // Corrupt/stale metadata must degrade to the existing exact
                // young-cycle fallback, never leave a pointer into the moved
                // source in the active remembered map.
                destination->youngOverflow.store(
                    true, std::memory_order_release);
            }
            iterator = rememberedSlots.erase(iterator);
        }

        if (destination == nullptr || remapped.empty())
            return;
        const std::uint8_t epoch =
            activeRememberedEpoch.load(std::memory_order_acquire);
        for (const auto& [slot, header] : remapped) {
            if (destination->rememberSlot(slot, epoch) ==
                    Region::RememberResult::Overflow) {
                destination->youngOverflow.store(
                    true, std::memory_order_release);
            }
            try {
                rememberedSlots.insert_or_assign(slot, header);
            } catch (...) {
                destination->youngOverflow.store(
                    true, std::memory_order_release);
            }
        }
        destination->youngDirty.store(true, std::memory_order_release);
    }

    void publishRememberedSlot(RefSlot* slot,
                               ObjectHeader* owner) noexcept {
        if (slot == nullptr || owner == nullptr)
            return;
        auto* region = reinterpret_cast<Region*>(
            static_cast<std::uintptr_t>(owner->reserved0));
        if (region == nullptr || region->retired || region->isYoung())
            return;
        const std::uint8_t epoch =
            activeRememberedEpoch.load(std::memory_order_acquire);
        if (region->rememberSlot(slot, epoch) ==
            Region::RememberResult::Overflow) {
            region->youngOverflow.store(true, std::memory_order_release);
            return;
        }
        try {
            std::scoped_lock rememberedLock(rememberedMutex);
            rememberedSlots.insert_or_assign(slot, owner);
            region->youngDirty.store(true, std::memory_order_release);
        } catch (...) {
            region->youngOverflow.store(true, std::memory_order_release);
        }
    }

    void rememberRelocatedOwner(ObjectHeader* relocated,
                                Region* sourceRegion) {
        if (relocated == nullptr || sourceRegion == nullptr)
            return;
        // Relocation destinations are old.  Moving a live object out of a
        // young region (or a coarse-dirty old region) can therefore create an
        // old-to-young edge without executing a generated store barrier.
        // Stock hxcpp rebuilds this information while moving survivors; keep
        // the exact relocated owner so the next young trace and remap sees it.
        // Always rescan a moved/promoted old owner.  The old optimization
        // skipped clean old sources, which was safe only while every young
        // survivor was promoted in one pass.  With real survivor regions an
        // old owner can acquire an old-to-survivor edge solely because the
        // target was copied, so source dirtiness no longer proves anything.
        auto* destination = reinterpret_cast<Region*>(
            static_cast<std::uintptr_t>(relocated->reserved0));
        if (destination == nullptr ||
            destination->young.load(std::memory_order_acquire)) {
            return;
        }
        class RememberVisitor final : public TraceVisitor {
        public:
            RememberVisitor(Impl& impl, ObjectHeader* relocated,
                            bool& remembered, bool& overflow)
                : impl_(impl), relocated_(relocated),
                  remembered_(remembered), overflow_(overflow) {}
            void visit(RefSlot& slot) override {
                const Ref value(slot.load(std::memory_order_acquire));
                if (value.isNull())
                    return;
                auto* targetRegion = reinterpret_cast<Region*>(
                    static_cast<std::uintptr_t>(
                        headerOf(value.address())->reserved0));
                if (targetRegion != nullptr && targetRegion->young.load(
                        std::memory_order_acquire)) {
                    try {
                        impl_.publishRememberedSlot(&slot, relocated_);
                        remembered_ = true;
                    } catch (...) {
                        // The edge is already installed in the managed
                        // object. Preserve correctness without requiring more
                        // metadata by forcing an exact descriptor scan of the
                        // complete owner region in the next young cycle.
                        overflow_ = true;
                    }
                }
            }
        private:
            Impl& impl_;
            ObjectHeader* relocated_ = nullptr;
            bool& remembered_;
            bool& overflow_;
        };
        auto* payload = static_cast<std::byte*>(payloadOf(relocated));
        bool remembered = false;
        bool overflow = false;
        RememberVisitor visitor(*this, relocated, remembered, overflow);
        for (const TypeDescriptor* type = relocated->type; type != nullptr;
             type = type->super) {
            for (std::uint32_t index = 0; index < type->referenceOffsetCount;
                 ++index) {
                auto& slot = *reinterpret_cast<RefSlot*>(
                    payload + type->referenceOffsets[index]);
                visitor.visit(slot);
            }
            if (type->trace != nullptr)
                type->trace(payload, relocated->payloadBytes, visitor);
        }
        if (remembered)
            destination->youngDirty.store(true, std::memory_order_release);
        if (overflow)
            destination->youngOverflow.store(true,
                                             std::memory_order_release);
    }

    static void invalidateTlab(ThreadState& state) noexcept {
        state.tlabRegion = nullptr;
        state.tlabCursor = nullptr;
        state.tlabLimit = nullptr;
    }

    void invalidateTlabsFor(Region* region) noexcept {
        for (const auto& state : threads) {
            if (state->tlabRegion == region)
                invalidateTlab(*state);
        }
    }

    void parkMutators(ThreadState* collector = nullptr) {
        const auto handshakeStarted = std::chrono::steady_clock::now();
        std::unique_lock handshakeLock(handshakeMutex);
        // Generated functions test this single process-wide flag before
        // entering the full epoch protocol. Publish it before advancing the
        // request so no mutator can miss a new handshake.
        safepointRequested.store(true, std::memory_order_release);
        const std::uint64_t targetEpoch =
            requestedHandshakeEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
        // parkMutators performs several operations that can throw after the
        // request becomes globally visible (root-snapshot growth, diagnostic
        // bookkeeping and remembered-buffer publication).  Its callers can
        // only install their own resume guards after this function returns.
        // If one of those internal operations throws first, every other
        // mutator otherwise remains parked on an epoch nobody can resume.
        // Keep rollback inside the request itself so an incomplete handshake
        // is always released, including dynamic pin-isolation handshakes.
        struct FailedHandshakeResume final {
            Impl* impl = nullptr;
            std::uint64_t epoch = 0;
            bool armed = true;

            ~FailedHandshakeResume() noexcept {
                if (!armed)
                    return;
                impl->resumedHandshakeEpoch.store(epoch,
                                                   std::memory_order_release);
                impl->parkRequested.store(false, std::memory_order_release);
                safepointRequested.store(false, std::memory_order_release);
                impl->handshakeCondition.notify_all();
            }
        } failedHandshakeResume{this, targetEpoch};
        // Do not clear a slow-handshake record here.  A young collection has
        // both an initial and a final handshake; clearing on the second one
        // discarded the useful late-thread record before telemetry could read
        // it.  The facade consumes the record after the complete GC cycle.
        parkRequested.store(true, std::memory_order_release);
        // A mutator is allowed to synchronously initiate a collection. It is
        // already stopped inside the collector, so acknowledge its own
        // handshake epoch here instead of waiting for it to poll itself.
        if (collector != nullptr) {
            captureRootSnapshot(*collector, targetEpoch);
            collector->observedHandshakeEpoch.store(
                targetEpoch, std::memory_order_release);
        }
        handshakeCondition.notify_all();
        const auto timeout = std::chrono::milliseconds(
            config.handshakeTimeoutMilliseconds);
        const auto deadline = handshakeStarted + timeout;
        const auto diagnosticThreshold =
            handshakeStarted + std::chrono::milliseconds(10);
        std::unordered_set<std::uint64_t> lateSequences;
        auto allArrived = [this, targetEpoch, collector] {
            std::scoped_lock threadLock(threadMutex);
            return std::all_of(
                threads.begin(), threads.end(),
                [targetEpoch, collector](
                        const std::unique_ptr<ThreadState>& state) {
                    if (state->nativeBlocked.load(
                            std::memory_order_acquire) &&
                        state->managedExecutionDepth.load(
                            std::memory_order_acquire) == 0) {
                        return true;
                    }

                    const bool acknowledged =
                        state->observedHandshakeEpoch.load(
                            std::memory_order_acquire) >= targetEpoch;
                    const bool exact = state->rootSnapshotEpoch == targetEpoch;
                    const bool stopped = state.get() == collector ||
                        state->parked.load(std::memory_order_acquire);
                    if (acknowledged && exact && stopped)
                        return true;

                    if (acknowledged) {
                        // An old resume used to be able to release a mutator
                        // at the edge of the next request.  Do not accept its
                        // epoch alone as a stopped-world acknowledgement: make
                        // the generated safepoint path take and publish a fresh
                        // exact snapshot for this request.
                        state->observedHandshakeEpoch.store(
                            targetEpoch - 1U, std::memory_order_release);
                    }
                    return false;
                });
        };
        bool arrived = allArrived();
        while (!arrived && std::chrono::steady_clock::now() < deadline) {
            const auto now = std::chrono::steady_clock::now();
            const auto nextWake = std::min(
                deadline, now + std::chrono::milliseconds(5));
            handshakeCondition.wait_until(handshakeLock, nextWake);
            arrived = allArrived();
            if (!arrived &&
                std::chrono::steady_clock::now() >= diagnosticThreshold) {
                std::scoped_lock threadLock(threadMutex);
                for (const auto& state : threads) {
                    if (!(state->nativeBlocked.load(
                              std::memory_order_acquire) &&
                          state->managedExecutionDepth.load(
                              std::memory_order_acquire) == 0) &&
                        state->observedHandshakeEpoch.load(
                            std::memory_order_acquire) < targetEpoch) {
                        lateSequences.insert(state->sequence);
                    }
                }
            }
        }
        const auto waitMicros = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - handshakeStarted).count());
        if (waitMicros >= 10000U) {
            std::ostringstream output;
            output << "novagc:slow_handshake epoch=" << targetEpoch
                   << " wait_us=" << waitMicros
                   << " collector_seq="
                   << (collector != nullptr ? collector->sequence : 0U)
                   << " collector_tid="
                   << (collector != nullptr ? collector->nativeThreadId : 0U)
                   << " late_threads=";
            bool first = true;
            std::scoped_lock threadLock(threadMutex);
            for (const auto& state : threads) {
                if (!lateSequences.contains(state->sequence))
                    continue;
                if (!first)
                    output << ',';
                first = false;
                output << "seq:" << state->sequence
                       << "/tid:" << state->nativeThreadId
                       << "/observed:"
                       << state->observedHandshakeEpoch.load(
                              std::memory_order_acquire)
                       << "/native:"
                       << (state->nativeBlocked.load(
                               std::memory_order_acquire) ? 1 : 0)
                       << "/managed:"
                       << state->managedExecutionDepth.load(
                              std::memory_order_acquire)
                       << "/roots:" << (state->roots != nullptr ? 1 : 0);
            }
            if (first)
                output << "<resolved-before-sample>";
            std::scoped_lock diagnosticLock(handshakeDiagnosticMutex);
            lastHandshakeDiagnostic = output.str();
        }
        if (!arrived) {
            resumedHandshakeEpoch.store(targetEpoch, std::memory_order_release);
            parkRequested.store(false, std::memory_order_release);
            safepointRequested.store(false, std::memory_order_release);
            handshakeCondition.notify_all();
            std::string diagnostic;
            {
                std::scoped_lock diagnosticLock(handshakeDiagnosticMutex);
                diagnostic = lastHandshakeDiagnostic;
            }
            throw std::runtime_error(
                "hxcpp GC safepoint handshake timed out" +
                (diagnostic.empty() ? std::string{} : ": " + diagnostic));
        }
        // A native-blocked thread promises not to touch managed stack slots.
        // It cannot execute the handshake callback itself, so refresh its
        // snapshot here while its RootFrames are stable. Active mutators have
        // already published their own snapshot before acknowledging.
        {
            std::scoped_lock threadLock(threadMutex);
            for (const auto& state : threads) {
                if (state.get() == collector)
                    continue;
                const bool nativeBlocked = state->nativeBlocked.load(
                    std::memory_order_acquire);
                const std::size_t managedDepth =
                    state->managedExecutionDepth.load(
                        std::memory_order_acquire);
                if (nativeBlocked && managedDepth == 0) {
                    captureRootSnapshot(*state, targetEpoch);
                } else if (nativeBlocked && managedDepth != 0 &&
                           !state->parked.load(std::memory_order_acquire)) {
                    // allArrived may accept a native-blocked thread at depth
                    // zero immediately before a native callback increments
                    // managedExecutionDepth.  parkMutators still owns
                    // handshakeMutex here, so beginManagedExecution is blocked
                    // inside its mandatory safepointPoll and generated code has
                    // not resumed. RootFrames are therefore just as stable as
                    // they were at the accepted native boundary. Snapshot that
                    // boundary instead of reporting an escaped mutator; after
                    // resume the callback's poll observes the next epoch before
                    // it is allowed into managed code.
                    captureRootSnapshot(*state, targetEpoch);
                } else if (state->rootSnapshotEpoch != targetEpoch ||
                           !state->parked.load(std::memory_order_acquire)) {
                    // The release/acquire acknowledgement normally publishes
                    // the snapshot written by safepointPoll.  A thread that
                    // attached at the edge of two consecutive handshakes can
                    // nevertheless arrive with the new epoch while its empty
                    // attachment snapshot still carries the previous one.
                    // It is already parked and cannot mutate RootFrames, so
                    // refresh the exact snapshot here instead of aborting an
                    // otherwise valid collection.
                    if (!state->parked.load(std::memory_order_acquire))
                        throw std::logic_error(
                            "mutator escaped a completed exact-root handshake");
                    captureRootSnapshot(*state, targetEpoch);
                }
            }
        }
        flushAllRememberedBuffers();
        failedHandshakeResume.armed = false;
    }

    void resumeMutators() noexcept {
        {
            std::scoped_lock handshakeLock(handshakeMutex);
            resumedHandshakeEpoch.store(
                requestedHandshakeEpoch.load(std::memory_order_acquire),
                std::memory_order_release);
            parkRequested.store(false, std::memory_order_release);
            safepointRequested.store(false, std::memory_order_release);
        }
        handshakeCondition.notify_all();
    }
};

#if defined(_MSC_VER)
#define HX_NOVAGC_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define HX_NOVAGC_NOINLINE __attribute__((noinline))
#else
#define HX_NOVAGC_NOINLINE
#endif

namespace {

// Keep the phase-heavy barrier machinery out of Runtime::storeBarrier's
// overwhelmingly common stable-generation path.  Runtime::Impl is private, so
// accept the already-authorized pointer through template deduction instead of
// widening the public runtime ABI with a new member declaration.
template <typename RuntimeImpl>
HX_NOVAGC_NOINLINE void storeBarrierSlow(
        RuntimeImpl* impl, void* owner, RefSlot& slot, void* value) {
    const auto makeRef = [impl](void* object) noexcept {
        return Ref::encode(
            object, impl->view.load(std::memory_order_acquire));
    };

    if (impl->youngMarkActive.load(std::memory_order_acquire)) {
        const Ref previous(slot.load(std::memory_order_acquire));
        impl->enqueueYoungMark(previous.isNull()
            ? nullptr : previous.address());
        impl->enqueueYoungMark(value);
    }
    if (impl->relocationActive.load(std::memory_order_acquire)) {
        // Generated ObjectPtr/Dynamic assignment operators do not always
        // carry their containing object.  During normal execution the
        // remembered-set barrier can infer that owner from the destination
        // address, but concurrent relocation needs the same inference before
        // the store: otherwise a write through an already-forwarded source
        // slot is silently lost.  Heap and relocation state are locked
        // together because a worker can publish a destination region and a
        // forwarding pointer in the same critical section.
        std::unique_lock<std::mutex> heapLock(impl->heapMutex,
                                              std::defer_lock);
        std::unique_lock<std::mutex> relocationLock(impl->relocationMutex,
                                                    std::defer_lock);
        if (owner == nullptr) {
            for (;;) {
                if (std::try_lock(heapLock, relocationLock) == -1)
                    break;
                Runtime::instance().safepointPoll();
                std::this_thread::yield();
            }
            const auto [ownerRegion, inferredHeader] =
                impl->ownerOfSlot(&slot);
            (void)ownerRegion;
            if (inferredHeader == nullptr) {
                // Stack roots, static roots, and native side-table slots are
                // not relative to a relocatable managed owner.
                relocationLock.unlock();
                heapLock.unlock();
            } else {
                owner = payloadOf(inferredHeader);
            }
        } else {
            while (!relocationLock.try_lock()) {
                Runtime::instance().safepointPoll();
                std::this_thread::yield();
            }
        }

        if (owner != nullptr) {
            auto* ownerHeader = headerOf(owner);
            const std::uint64_t forwarded =
                ownerHeader->forwarding.load(std::memory_order_acquire);
            if (forwarded != 0) {
                auto* oldPayload = static_cast<std::byte*>(owner);
                auto* slotAddress = reinterpret_cast<std::byte*>(&slot);
                const std::ptrdiff_t offset = slotAddress - oldPayload;
                if (offset < 0 ||
                    static_cast<std::size_t>(offset) + sizeof(RefSlot) >
                        ownerHeader->payloadBytes) {
                    throw std::logic_error(
                        "relocation store slot is outside its owner payload");
                }
                auto* newPayload =
                    static_cast<std::byte*>(Ref(forwarded).address());
                auto& forwardedSlot = *reinterpret_cast<RefSlot*>(
                    newPayload + offset);
                impl->rememberOldToYoung(
                    newPayload, forwardedSlot, value);
                forwardedSlot.store(
                    makeRef(value).bits(), std::memory_order_release);
                ++impl->relocationStats.forwardedStores;
                return;
            }
            // The worker cannot copy this owner until relocationLock is
            // released, so publishing into the source slot here is ordered
            // before memcpy. This also avoids repeating the O(region-count)
            // owner inference for owner-less generated assignments.
            impl->rememberOldToYoung(owner, slot, value);
            slot.store(makeRef(value).bits(), std::memory_order_release);
            return;
        }
    }
    // Scalar Dynamic stores and freshly-zeroed reference fields do not have an
    // old or new edge to preserve.  Avoid serializing those overwhelmingly
    // common stores on markMutex during a concurrent mark.
    if (value == nullptr && slot.load(std::memory_order_relaxed) == 0)
        return;
    if (!impl->markActive.load(std::memory_order_acquire)) {
        impl->rememberOldToYoung(owner, slot, value);
        slot.store(makeRef(value).bits(), std::memory_order_release);
        return;
    }

    // Preserve the global GC lock order: heapMutex must never be acquired
    // while markMutex is held.  Owner-less stores (notably exact stack roots
    // published by CFFI callback threads) may need heapMutex to prove that the
    // destination slot is not inside an old heap region.  finishConcurrentMark
    // deliberately takes heapMutex before markMutex during remark, so doing
    // this lookup under markMutex creates an AB/BA deadlock.
    //
    // Dirtying before publishing the new edge is also the required remembered-
    // set ordering: a young collection may conservatively rescan an owner that
    // does not end up receiving the edge, but it must never observe the edge
    // without first being able to discover its owner.
    impl->rememberOldToYoung(owner, slot, value);

    std::scoped_lock markLock(impl->markMutex);
    auto enqueue = [impl](void* candidate) {
        if (candidate == nullptr)
            return;
        ObjectHeader* header = headerOf(candidate);
        std::uint32_t observed =
            header->markEpoch.load(std::memory_order_acquire);
        while (observed != impl->markEpoch) {
            if (header->markEpoch.compare_exchange_weak(
                    observed, impl->markEpoch, std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                impl->markQueue.push_back(header);
                impl->accountRegionLive(header);
                concurrentMarkWorkerIdle.store(false,
                                               std::memory_order_release);
                impl->concurrentMarkedObjects.fetch_add(
                    1, std::memory_order_relaxed);
                impl->concurrentMarkedBytes.fetch_add(
                    header->payloadBytes, std::memory_order_relaxed);
                break;
            }
        }
    };

    // SATB preserves the overwritten value; insertion marking also closes
    // constructor/native publication gaps until the generated barrier audit
    // proves SATB-only paths for every store category.
    const Ref old(slot.load(std::memory_order_acquire));
    enqueue(old.isNull() ? nullptr : old.address());
    enqueue(value);
    slot.store(makeRef(value).bits(), std::memory_order_release);
    impl->markCondition.notify_one();
}

} // namespace

#undef HX_NOVAGC_NOINLINE

// Keep the TLS declaration independent of the private pimpl type. Every use
// is converted back only inside Runtime member functions.
thread_local RootThreadState* currentThread = nullptr;
thread_local bool gcBarrierSlowPath = false;
std::atomic<std::uint32_t> fastBarrierState{
    static_cast<std::uint32_t>(View::Marked0)};
std::atomic<std::uintptr_t> managedArenaStartFast{0};
std::atomic<std::uintptr_t> managedArenaEndFast{0};
Runtime* runtimeInstanceFast = nullptr;

Runtime& Runtime::initializeInstance() {
    // The Haxe thread API intentionally creates detached process-lifetime
    // threads, matching original hxcpp. If main returns while one is blocked
    // in native I/O, a normal function-static destructor would tear the heap
    // out from under that thread before ExitProcess terminates it. Explicit
    // shutdown still reclaims a quiescent runtime; the singleton shell itself
    // is therefore deliberately process-lifetime storage.
    static Runtime* runtime = [] {
        Runtime* created = new Runtime;
        runtimeInstanceFast = created;
        return created;
    }();
    return *runtime;
}

void RootScope::pushSlow(RefSlot** slots, std::uint32_t count) {
    RootThreadState* state = ownerState_;
    if (state != nullptr && state->nativeBlockedLocal &&
        state->managedExecutionDepth.load(std::memory_order_acquire) == 0 &&
        Runtime::instance().beginManagedExecution(*state)) {
        managedState_ = state;
    }
    const std::size_t oldCount =
        state == nullptr ? 0U : state->rootSlotCount;
    const std::size_t newCount = oldCount + count;
    try {
        if (state != nullptr && (count == 0 || slots != nullptr) &&
            newCount >= oldCount &&
            newCount <= state->rootSnapshotCapacity &&
            !rootFrameBarrierActive.load(std::memory_order_acquire)) {
            frame_.previous = state->roots;
            state->roots = &frame_;
            state->rootSlotCount = newCount;
            fastState_ = state;
        } else {
            Runtime::instance().pushRootFrame(frame_);
        }
    } catch (...) {
        if (managedState_ != nullptr) {
            Runtime::instance().endManagedExecution(*managedState_);
            managedState_ = nullptr;
        }
        throw;
    }
}

void RootScope::popSlow() noexcept {
    RootThreadState* temporaryManagedState = nullptr;
    if (ownerState_ == currentThread && ownerState_ != nullptr &&
        ownerState_->nativeBlockedLocal &&
        ownerState_->managedExecutionDepth.load(
            std::memory_order_acquire) == 0) {
        try {
            if (Runtime::instance().beginManagedExecution(*ownerState_))
                temporaryManagedState = ownerState_;
        } catch (...) {
            std::terminate();
        }
    }

    if (ownerState_ != currentThread) {
        // Slow-path frames can be transferred just like fast-path frames.
        // Always remember the creating state so foreign destruction unlinks
        // the correct chain under root snapshot publication.
        if (ownerState_ == nullptr)
            std::terminate();
        Runtime::instance().popForeignRootFrame(*ownerState_, frame_);
        fastState_ = nullptr;
    } else if (fastState_ != nullptr) {
        // Scoped pins may be embedded in containers whose elements are
        // destroyed out of construction order. Unlink this exact frame;
        // assigning roots=previous here would discard younger frames and
        // leave rootSlotCount inconsistent with the real chain.
        if (fastState_->roots == &frame_) {
            fastState_->roots = frame_.previous;
        } else {
            RootFrame** link = &fastState_->roots;
            while (*link != nullptr && *link != &frame_)
                link = &((*link)->previous);
            if (*link != &frame_)
                std::terminate();
            *link = frame_.previous;
        }
        if (fastState_->rootSlotCount < frame_.slotCount)
            std::terminate();
        fastState_->rootSlotCount -= frame_.slotCount;
        frame_.previous = nullptr;
    } else {
        Runtime::instance().popRootFrame(frame_);
    }

    if (temporaryManagedState != nullptr)
        Runtime::instance().endManagedExecution(*temporaryManagedState);
    if (managedState_ != nullptr)
        Runtime::instance().endManagedExecution(*managedState_);
}

Runtime::Runtime() : impl_(new Impl) {}

Runtime::~Runtime() {
    // Process teardown must never throw. Explicit shutdown remains responsible
    // for detecting attached mutators in tests and embedding applications.
    if (impl_->running) {
        std::unique_lock heapLock(impl_->heapMutex);
        std::unique_lock threadLock(impl_->threadMutex);
        impl_->regions.clear();
        impl_->freeRegions.clear();
        managedArenaStartFast.store(0, std::memory_order_release);
        managedArenaEndFast.store(0, std::memory_order_release);
        impl_->addressSpace.reset();
        impl_->running = false;
    }
    delete impl_;
}

void Runtime::initialize(const RuntimeConfig& config) {
    std::scoped_lock heapLock(impl_->heapMutex);
    if (impl_->running)
        throw std::logic_error("hxcpp GC runtime is already initialized");
    if (config.regionBytes < 64U * 1024U || config.maximumRegions == 0 ||
        config.tlabBytes < alignof(ObjectHeader) ||
        config.tlabBytes > config.regionBytes ||
        config.markWorkerCount > 256 || config.relocationWorkerCount > 256 ||
        config.maximumPauseMilliseconds == 0 ||
        config.allocationStallTimeoutMilliseconds == 0 ||
        config.initialTenuringThreshold == 0 ||
        config.maximumTenuringThreshold == 0 ||
        config.initialTenuringThreshold > config.maximumTenuringThreshold ||
        config.survivorTargetPercent == 0 ||
        config.survivorTargetPercent > 100 ||
        (config.maximumAddressSpaceBytes != 0 &&
         config.maximumAddressSpaceBytes < config.regionBytes))
        throw std::invalid_argument("invalid hxcpp GC region configuration");
    if (config.maximumAddressSpaceBytes == 0 &&
        config.maximumRegions >
            std::numeric_limits<std::size_t>::max() / config.regionBytes) {
        throw std::invalid_argument("hxcpp GC address space size overflow");
    }
    impl_->config = config;
    impl_->view.store(View::Marked0, std::memory_order_release);
    fastBarrierState.store(static_cast<std::uint32_t>(View::Marked0),
                           std::memory_order_release);
    impl_->markEpoch = 0;
    impl_->youngMarkEpoch = 0;
    impl_->adaptiveMarkWorkerTarget.store(0, std::memory_order_relaxed);
    impl_->adaptiveRelocationWorkerTarget.store(0,
                                                 std::memory_order_relaxed);
    impl_->lastFullMarkEpoch = 0;
    impl_->currentTenuringThreshold = config.initialTenuringThreshold;
    impl_->nextRegionIndex = 1;
    impl_->nextNumaNode = 0;
    impl_->nextIdentity.store(1, std::memory_order_release);
    impl_->regions.clear();
    impl_->freeRegions.clear();
    impl_->addressSpace.reset();
    const std::size_t addressSpaceBytes =
        config.maximumAddressSpaceBytes != 0
            ? config.maximumAddressSpaceBytes
            : config.regionBytes * config.maximumRegions;
    impl_->addressSpace =
        std::make_unique<Impl::VirtualAddressSpace>(addressSpaceBytes);
    const std::uintptr_t arenaStart = reinterpret_cast<std::uintptr_t>(
        impl_->addressSpace->memory);
    managedArenaStartFast.store(arenaStart, std::memory_order_release);
    managedArenaEndFast.store(arenaStart + impl_->addressSpace->bytes,
                              std::memory_order_release);
    {
        std::scoped_lock rememberedLock(impl_->rememberedMutex);
        impl_->rememberedSlots.clear();
        impl_->rememberedSnapshot.clear();
    }
    {
        std::scoped_lock handleLock(impl_->handleMutex);
        impl_->handles.clear();
        impl_->freeHandleIndices.clear();
        impl_->referenceQueue.clear();
    }
    impl_->markActive.store(false, std::memory_order_release);
    rootFrameBarrierActive.store(false, std::memory_order_release);
    scopedPinBarrierActive.store(false, std::memory_order_release);
    impl_->clearSoftReferencesRequested.store(false,
                                               std::memory_order_release);
    impl_->clearSoftReferencesThisCycle = false;
    impl_->requestedHandshakeEpoch.store(0, std::memory_order_release);
    impl_->resumedHandshakeEpoch.store(0, std::memory_order_release);
    impl_->allocatedObjects.store(0, std::memory_order_release);
    impl_->allocatedBytes.store(0, std::memory_order_release);
    impl_->tlabFastAllocations.store(0, std::memory_order_release);
    impl_->tlabRefills.store(0, std::memory_order_release);
    impl_->createdRegions.store(0, std::memory_order_release);
    impl_->emergencyCollections.store(0, std::memory_order_release);
    impl_->emergencyServiceActive.store(false, std::memory_order_release);
    impl_->allocationStallWaiters.store(0, std::memory_order_release);
    impl_->maximumAllocationStallWaiters.store(0,
                                               std::memory_order_release);
    impl_->allocationStalls.store(0, std::memory_order_release);
    impl_->allocationStallMicros.store(0, std::memory_order_release);
    impl_->allocationStallTimeouts.store(0, std::memory_order_release);
    impl_->rememberedFaultCounter.store(0, std::memory_order_release);
    impl_->isolatedPinnedObjects.store(0, std::memory_order_release);
    impl_->pinIsolationFallbacks.store(0, std::memory_order_release);
    impl_->actOnceRememberedStores.store(0, std::memory_order_release);
    impl_->activeRememberedEpoch.store(0, std::memory_order_release);
    impl_->lastReportedActOnceRememberedStores = 0;
    concurrentMarkWorkerIdle.store(true, std::memory_order_release);
    concurrentRelocationWorkerDone.store(true, std::memory_order_release);
    lastInitialMarkPauseMicros.store(0, std::memory_order_release);
    lastRemarkPauseMicros.store(0, std::memory_order_release);
    lastRelocationSelectPauseMicros.store(0, std::memory_order_release);
    lastRelocationRemapPauseMicros.store(0, std::memory_order_release);
    selectiveAutomaticRelocation.store(false, std::memory_order_release);
    const char* census = std::getenv("HXCPP_NOVAGC_TYPE_TELEMETRY");
    deadTypeCensusEnabled = census != nullptr && *census != '\0' &&
        std::strcmp(census, "0") != 0 &&
        std::strcmp(census, "false") != 0;
    {
        std::scoped_lock censusLock(deadTypeCensusMutex);
        lastDeadTypeCensus.clear();
    }
    {
        std::scoped_lock diagnosticLock(handshakeDiagnosticMutex);
        lastHandshakeDiagnostic.clear();
    }
    nextThreadSequence.store(1, std::memory_order_release);
    impl_->running = true;
}

void Runtime::shutdown() {
    {
        std::scoped_lock threadLock(impl_->threadMutex);
        if (!impl_->threads.empty())
            throw std::logic_error("cannot shut down hxcpp GC with attached threads");
    }
    std::scoped_lock heapLock(impl_->heapMutex);
    {
        std::scoped_lock rememberedLock(impl_->rememberedMutex);
        impl_->rememberedSlots.clear();
        impl_->rememberedSnapshot.clear();
    }
    impl_->regions.clear();
    impl_->freeRegions.clear();
    managedArenaStartFast.store(0, std::memory_order_release);
    managedArenaEndFast.store(0, std::memory_order_release);
    impl_->addressSpace.reset();
    impl_->running = false;
}

bool Runtime::initialized() const noexcept { return impl_->running; }

std::size_t Runtime::attachedThreadCount() const noexcept {
    std::scoped_lock threadLock(impl_->threadMutex);
    return impl_->threads.size();
}

void Runtime::attachCurrentThread() {
    if (!impl_->running)
        throw std::logic_error("hxcpp GC runtime is not initialized");
    if (currentThread != nullptr)
        throw std::logic_error("thread is already attached to hxcpp GC");

    auto state = std::make_unique<Impl::ThreadState>();
    // Receiver pins are extremely frequent in generated Haxe expressions.
    // Keep the normal path allocation-free after the first few expressions.
    state->scopedPins.reserve(64);
    state->rememberedBuffer.reserve(64);
    // Keep ordinary generated root scopes allocation-free. Extremely deep
    // call/root stacks transparently grow through pushRootFrame's slow path.
    state->rootSnapshot.reserve(4096);
    state->rootSnapshotCapacity = state->rootSnapshot.capacity();
    state->id = std::this_thread::get_id();
    state->sequence = nextThreadSequence.fetch_add(1,
                                                   std::memory_order_relaxed);
#if defined(_WIN32)
    state->nativeThreadId = GetCurrentThreadId();
#else
    state->nativeThreadId = state->sequence;
#endif
    Impl::ThreadState* raw = state.get();

    // Register under the handshake lock so a foreign callback cannot appear
    // after parkMutators has declared the world stopped.  If attachment races
    // an active pause, the new thread joins the registry as already parked and
    // does not return to native/generated code until the collector resumes it.
    std::unique_lock handshakeLock(impl_->handshakeMutex);
    const std::uint64_t handshakeEpoch =
        impl_->requestedHandshakeEpoch.load(std::memory_order_acquire);
    const bool mustPark = impl_->parkRequested.load(std::memory_order_acquire);
    state->observedHandshakeEpoch.store(handshakeEpoch,
                                        std::memory_order_release);
    state->rootSnapshotEpoch = handshakeEpoch;
    state->parked.store(mustPark, std::memory_order_release);
    {
        std::scoped_lock threadLock(impl_->threadMutex);
        impl_->threads.push_back(std::move(state));
    }
    currentThread = raw;

    if (mustPark) {
        impl_->handshakeCondition.notify_all();
        impl_->handshakeCondition.wait(handshakeLock, [this, handshakeEpoch] {
            return impl_->resumedHandshakeEpoch.load(
                       std::memory_order_acquire) >= handshakeEpoch;
        });
        raw->parked.store(false, std::memory_order_release);
    }
}

void Runtime::detachCurrentThread() {
    if (currentThread == nullptr)
        throw std::logic_error("thread is not attached to hxcpp GC");
    auto* state = static_cast<Impl::ThreadState*>(currentThread);
    if (state->roots != nullptr) {
        std::ostringstream message;
        message << "cannot detach thread with active exact roots";
        std::size_t depth = 0;
        for (const RootFrame* frame = state->roots;
             frame != nullptr && depth < 64;
             frame = frame->previous, ++depth) {
            message << (depth == 0 ? ": " : ", ")
                    << "frame[" << depth << "]@" << frame
                    << " slots=" << frame->slotCount
                    << " map=" << frame->mapId;
        }
        message << " depth>=" << (depth == 64 ? "64" : std::to_string(depth));
        throw std::logic_error(message.str());
    }
    if (!state->scopedPins.empty() || state->fastScopedPinCount != 0)
        throw std::logic_error("cannot detach thread with active scoped pins");
    if (state->nativeBlockingDepth != 0)
        throw std::logic_error(
            "cannot detach thread inside a native blocking scope");

    std::scoped_lock threadLock(impl_->threadMutex);
    impl_->flushAllocationCounters(*state);
    impl_->flushRememberedBuffer(*state);
    const auto found = std::find_if(
        impl_->threads.begin(), impl_->threads.end(),
        [](const std::unique_ptr<Impl::ThreadState>& state) {
            return state.get() == currentThread;
        });
    if (found == impl_->threads.end())
        throw std::logic_error("attached hxcpp GC thread was not registered");
    impl_->threads.erase(found);
    currentThread = nullptr;
    // A collector may be waiting for this thread's handshake acknowledgement;
    // detachment is an equivalent terminal state, so make it re-check the
    // registry immediately instead of waiting for its timeout.
    impl_->handshakeCondition.notify_all();
}

bool Runtime::currentThreadAttached() const noexcept {
    return currentThread != nullptr;
}

void Runtime::enterNativeBlocking() {
    auto* state = static_cast<Impl::ThreadState*>(currentThread);
    if (state == nullptr)
        throw std::logic_error("native blocking requires an attached thread");
    if (state->nativeBlockingDepth == std::numeric_limits<std::size_t>::max())
        throw std::overflow_error("native blocking depth overflow");
    if (state->nativeBlockingDepth++ != 0)
        return;
    // Entering native code freezes this mutator's RootFrame chain. A release
    // publication is sufficient for parkMutators' acquire scan and root
    // capture; serializing every tiny OpenGL Prime call through the
    // process-wide handshake mutex made that mutex the render bottleneck.
    // Do not copy the complete root stack here: parkMutators captures a
    // blocked thread exactly once while the chain is stable.
    state->nativeBlockedLocal = true;
    state->nativeBlocked.store(true, std::memory_order_release);
    if (impl_->parkRequested.load(std::memory_order_acquire))
        impl_->handshakeCondition.notify_all();
}

void Runtime::leaveNativeBlocking() {
    auto* state = static_cast<Impl::ThreadState*>(currentThread);
    if (state == nullptr)
        throw std::logic_error("native blocking requires an attached thread");
    if (state->nativeBlockingDepth == 0)
        throw std::logic_error("native blocking scopes are unbalanced");
    if (--state->nativeBlockingDepth != 0)
        return;

    if (impl_->parkRequested.load(std::memory_order_acquire)) {
        // Once parkMutators has observed nativeBlocked it captures this
        // thread's roots while holding handshakeMutex. Do not clear the bit in
        // the middle of that capture; wait for the handshake boundary, then
        // immediately acknowledge/park through safepointPoll below.
        std::scoped_lock handshakeLock(impl_->handshakeMutex);
        state->nativeBlocked.store(false, std::memory_order_release);
        if (impl_->parkRequested.load(std::memory_order_acquire))
            impl_->handshakeCondition.notify_all();
    } else {
        state->nativeBlocked.store(false, std::memory_order_release);
    }
    state->nativeBlockedLocal = false;
    safepointPoll();
}

bool Runtime::beginManagedExecution(RootThreadState& owner) {
    auto* state = static_cast<Impl::ThreadState*>(currentThread);
    if (state == nullptr || state != &owner)
        throw std::logic_error(
            "managed execution scope requires its attached owner thread");
    if (!state->nativeBlockedLocal)
        return false;

    const std::size_t previous = state->managedExecutionDepth.fetch_add(
        1, std::memory_order_acq_rel);
    if (previous == std::numeric_limits<std::size_t>::max()) {
        state->managedExecutionDepth.fetch_sub(1, std::memory_order_release);
        throw std::overflow_error("managed execution depth overflow");
    }
    try {
        // Increment first so parkMutators cannot accept nativeBlocked as this
        // thread's acknowledgement while a direct native callback is entering.
        // If a request won that race, the poll parks before generated code can
        // publish a RootFrame or touch the managed heap.
        safepointPoll();
    } catch (...) {
        if (state->managedExecutionDepth.fetch_sub(
                1, std::memory_order_acq_rel) == 1 &&
            impl_->parkRequested.load(std::memory_order_acquire)) {
            impl_->handshakeCondition.notify_all();
        }
        throw;
    }
    return true;
}

void Runtime::endManagedExecution(RootThreadState& owner) noexcept {
    auto* state = static_cast<Impl::ThreadState*>(currentThread);
    if (state == nullptr || state != &owner)
        std::terminate();
    const std::size_t previous = state->managedExecutionDepth.fetch_sub(
        1, std::memory_order_acq_rel);
    if (previous == 0)
        std::terminate();
    if (previous == 1 &&
        impl_->parkRequested.load(std::memory_order_acquire)) {
        // ManagedMutatorScope is declared before generated RootScopes and is
        // therefore destroyed after them.  At depth zero the native contract
        // is restored and the root chain is stable for collector-side capture.
        impl_->handshakeCondition.notify_all();
    }
}

void Runtime::pushRootFrame(RootFrame& frame) {
    if (currentThread == nullptr)
        throw std::logic_error("exact root frame requires an attached thread");
    if (frame.slotCount != 0 && frame.slots == nullptr)
        throw std::invalid_argument("root frame has no slot table");
    std::scoped_lock publicationLock(impl_->rootPublicationMutex);
    auto* state = static_cast<Impl::ThreadState*>(currentThread);
    const std::size_t newCount = state->rootSlotCount + frame.slotCount;
    if (newCount < state->rootSlotCount)
        throw std::length_error("exact root slot count overflow");
    // Reserve before publishing the frame. A later safepoint can therefore
    // snapshot roots without allocating or exposing a partially-linked frame.
    state->rootSnapshot.reserve(newCount);
    state->rootSnapshotCapacity = state->rootSnapshot.capacity();
    frame.previous = state->roots;
    state->roots = &frame;
    state->rootSlotCount = newCount;

    // A function may enter after the initial root snapshot. Shade every
    // pre-populated slot as the frame becomes visible; subsequent generated
    // assignments use storeBarrier and maintain the invariant incrementally.
    if (impl_->markActive.load(std::memory_order_acquire) ||
        impl_->youngMarkActive.load(std::memory_order_acquire)) {
        for (std::uint32_t index = 0; index < frame.slotCount; ++index) {
            if (frame.slots[index] == nullptr)
                throw std::logic_error(
                    "exact root map contains a null slot entry");
            void* object = loadBarrier(*frame.slots[index]);
            storeBarrier(nullptr, *frame.slots[index], object);
        }
    }
}

void Runtime::popRootFrame(RootFrame& frame) {
    auto* state = static_cast<Impl::ThreadState*>(currentThread);
    if (state == nullptr)
        throw std::logic_error("exact root frame requires an attached thread");
    std::scoped_lock publicationLock(impl_->rootPublicationMutex);
    RootFrame** link = &state->roots;
    while (*link != nullptr && *link != &frame)
        link = &((*link)->previous);
    if (*link != &frame)
        throw std::logic_error("exact root frame is not registered");
    if (state->rootSlotCount < frame.slotCount)
        throw std::logic_error("exact root slot count underflow");
    state->rootSlotCount -= frame.slotCount;
    *link = frame.previous;
    frame.previous = nullptr;
}

void Runtime::popForeignRootFrame(RootThreadState& owner,
                                  RootFrame& frame) noexcept {
    std::scoped_lock publicationLock(impl_->rootPublicationMutex);
    RootFrame** link = &owner.roots;
    while (*link != nullptr && *link != &frame)
        link = &((*link)->previous);
    if (*link != &frame || owner.rootSlotCount < frame.slotCount)
        std::terminate();
    owner.rootSlotCount -= frame.slotCount;
    *link = frame.previous;
    frame.previous = nullptr;
}

void Runtime::releaseForeignFastScopedPin(RootThreadState& owner,
                                          void* object) noexcept {
    if (object == nullptr)
        return;
    std::scoped_lock publicationLock(impl_->rootPublicationMutex);
    const std::size_t count = owner.fastScopedPinCount;
    for (std::size_t index = count; index != 0; --index) {
        if (owner.fastScopedPins[index - 1U] != object)
            continue;
        for (std::size_t cursor = index; cursor < count; ++cursor)
            owner.fastScopedPins[cursor - 1U] =
                owner.fastScopedPins[cursor];
        owner.fastScopedPins[count - 1U] = nullptr;
        --owner.fastScopedPinCount;
        return;
    }
    std::terminate();
}

void Runtime::registerStaticRoot(RefSlot& slot) {
    // Static roots are published by lazy function-local initializers while a
    // concurrent cycle may already be tracing.  Serialize publication with
    // the collector's final root snapshot: either this root is completely
    // registered and shaded before the final pause closes its marker queue,
    // or registration waits until relocation/reclamation has completed.
    // Do not take heapMutex here.  Collection pauses acquire heapMutex before
    // rootPublicationMutex, so doing the reverse would introduce a lock-order
    // cycle with the initial young/full pause.
    std::scoped_lock publicationLock(impl_->rootPublicationMutex);
    std::scoped_lock markLock(impl_->markMutex);
    std::scoped_lock staticLock(impl_->staticRootMutex);
    if (std::find(impl_->staticRoots.begin(), impl_->staticRoots.end(), &slot) !=
        impl_->staticRoots.end())
        throw std::logic_error("static root slot is already registered");
    impl_->staticRoots.push_back(&slot);

    void* object = loadBarrier(slot);
    impl_->enqueueYoungMark(object);
    if (object == nullptr ||
        !impl_->markActive.load(std::memory_order_acquire))
        return;
    ObjectHeader* header = headerOf(object);
    std::uint32_t observed =
        header->markEpoch.load(std::memory_order_acquire);
    while (observed != impl_->markEpoch) {
        if (header->markEpoch.compare_exchange_weak(
                observed, impl_->markEpoch, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            impl_->markQueue.push_back(header);
            impl_->accountRegionLive(header);
            concurrentMarkWorkerIdle.store(false,
                                           std::memory_order_release);
            impl_->concurrentRoots.fetch_add(1, std::memory_order_relaxed);
            impl_->concurrentMarkedObjects.fetch_add(1,
                                                     std::memory_order_relaxed);
            impl_->concurrentMarkedBytes.fetch_add(
                header->payloadBytes, std::memory_order_relaxed);
            impl_->markCondition.notify_one();
            break;
        }
    }
}

void Runtime::unregisterStaticRoot(RefSlot& slot) {
    // Match registration and root remapping.  A slot cannot disappear while
    // a stop-the-world snapshot is walking the static root table.
    std::scoped_lock publicationLock(impl_->rootPublicationMutex);
    std::scoped_lock markLock(impl_->markMutex);
    std::scoped_lock staticLock(impl_->staticRootMutex);
    const auto found =
        std::find(impl_->staticRoots.begin(), impl_->staticRoots.end(), &slot);
    if (found == impl_->staticRoots.end())
        throw std::logic_error("static root slot is not registered");
    impl_->staticRoots.erase(found);
}

void Runtime::safepointPoll() {
    auto* state = static_cast<Impl::ThreadState*>(currentThread);
    if (state == nullptr)
        return;
    for (;;) {
        // The overwhelmingly common case must not take the handshake mutex.
        // Allocation refills and generated function/loop polls call this very
        // frequently, while a stop request changes only once per collection
        // pause.  Acquire ordering makes the collector's request visible
        // before the mutator publishes its acknowledgement below.
        const std::uint64_t fastRequested =
            impl_->requestedHandshakeEpoch.load(std::memory_order_acquire);
        if (state->observedHandshakeEpoch.load(std::memory_order_acquire) >=
            fastRequested) {
            return;
        }
        std::unique_lock handshakeLock(impl_->handshakeMutex);
        const std::uint64_t requested =
            impl_->requestedHandshakeEpoch.load(std::memory_order_acquire);
        if (state->observedHandshakeEpoch.load(std::memory_order_acquire) >=
            requested)
            return;

        impl_->captureRootSnapshot(*state, requested);
        state->parked.store(true, std::memory_order_release);
        state->observedHandshakeEpoch.store(requested,
                                            std::memory_order_release);
        impl_->handshakeCondition.notify_all();
        impl_->handshakeCondition.wait(handshakeLock, [this, requested] {
            return impl_->resumedHandshakeEpoch.load(
                       std::memory_order_acquire) >= requested;
        });
        state->parked.store(false, std::memory_order_release);
        handshakeLock.unlock();
        // If the collector issued another handshake before this thread fully
        // left the previous one, acknowledge that distinct epoch here rather
        // than allowing a stale parked bit to satisfy both requests.
    }
}

void* Runtime::allocate(const TypeDescriptor& type) {
    return allocateVariable(type, type.fixedPayloadBytes);
}

void* Runtime::allocateVariable(const TypeDescriptor& type,
                                std::size_t payloadBytes) {
    try {
    try {
        return allocateVariableOnce(type, payloadBytes);
    } catch (const std::bad_alloc&) {
        if (emergencyCollectionActive || gcPhaseActive)
            throw;
    }

    bool emergencyOwner = false;
    for (;;) {
        bool expected = false;
        if (impl_->emergencyServiceActive.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            emergencyOwner = true;
            break;
        }

        const auto stallStarted = std::chrono::steady_clock::now();
        const std::uint32_t waiters =
            impl_->allocationStallWaiters.fetch_add(
                1, std::memory_order_acq_rel) + 1U;
        std::uint32_t observedMaximum =
            impl_->maximumAllocationStallWaiters.load(
                std::memory_order_relaxed);
        while (observedMaximum < waiters &&
               !impl_->maximumAllocationStallWaiters.compare_exchange_weak(
                   observedMaximum, waiters, std::memory_order_relaxed,
                   std::memory_order_relaxed)) {
        }
        impl_->allocationStalls.fetch_add(1, std::memory_order_relaxed);

        std::uint64_t timeoutMilliseconds =
            impl_->config.allocationStallTimeoutMilliseconds;
        if (impl_->config.allocationServiceLevel ==
            AllocationServiceLevel::Latency) {
            timeoutMilliseconds = std::max<std::uint64_t>(
                1U, timeoutMilliseconds / 2U);
        } else if (impl_->config.allocationServiceLevel ==
                   AllocationServiceLevel::Throughput) {
            timeoutMilliseconds = std::min<std::uint64_t>(
                timeoutMilliseconds * 2U,
                std::numeric_limits<std::uint32_t>::max());
        }
        const auto deadline = stallStarted +
            std::chrono::milliseconds(timeoutMilliseconds);
        bool timedOut = false;
        {
            std::unique_lock stallLock(impl_->emergencyMutex);
            while (impl_->emergencyServiceActive.load(
                    std::memory_order_acquire)) {
                impl_->emergencyCondition.wait_for(
                    stallLock, std::chrono::milliseconds(2));
                stallLock.unlock();
                safepointPoll();
                timedOut = std::chrono::steady_clock::now() >= deadline;
                stallLock.lock();
                if (timedOut)
                    break;
            }
        }
        impl_->allocationStallWaiters.fetch_sub(
            1, std::memory_order_acq_rel);
        impl_->allocationStallMicros.fetch_add(
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - stallStarted).count()),
            std::memory_order_relaxed);
        if (timedOut && impl_->emergencyServiceActive.load(
                std::memory_order_acquire)) {
            impl_->allocationStallTimeouts.fetch_add(
                1, std::memory_order_relaxed);
            throw std::bad_alloc();
        }

        // The service thread may have reclaimed enough capacity for every
        // waiter. Retry before competing to become the next service owner.
        try {
            return allocateVariableOnce(type, payloadBytes);
        } catch (const std::bad_alloc&) {
        }
    }

    struct EmergencyServiceGuard final {
        Impl* impl = nullptr;
        ~EmergencyServiceGuard() {
            impl->emergencyServiceActive.store(
                false, std::memory_order_release);
            impl->emergencyCondition.notify_all();
        }
    } serviceGuard{impl_};
    (void)emergencyOwner;

    // A waiter can win service ownership after the preceding service cycle;
    // retry once more before doing another collection.
    try {
        return allocateVariableOnce(type, payloadBytes);
    } catch (const std::bad_alloc&) {
    }

    if (impl_->markActive.load(std::memory_order_acquire) ||
        impl_->youngMarkActive.load(std::memory_order_acquire) ||
        impl_->relocationActive.load(std::memory_order_acquire)) {
        // An allocation failure while another collector owns the heap is a
        // stall, not proof of exhaustion. Cooperatively answer its safepoints,
        // wait for the active phase to publish completion, then retry before
        // entering the emergency Young/Full sequence. The old direct throw
        // made startup fail whenever a fast mutator exhausted its TLAB during
        // an automatic concurrent-young cycle.
        const auto phaseStallStarted = std::chrono::steady_clock::now();
        const std::uint32_t waiters =
            impl_->allocationStallWaiters.fetch_add(
                1, std::memory_order_acq_rel) + 1U;
        std::uint32_t observedMaximum =
            impl_->maximumAllocationStallWaiters.load(
                std::memory_order_relaxed);
        while (observedMaximum < waiters &&
               !impl_->maximumAllocationStallWaiters.compare_exchange_weak(
                   observedMaximum, waiters, std::memory_order_relaxed,
                   std::memory_order_relaxed)) {
        }
        impl_->allocationStalls.fetch_add(1, std::memory_order_relaxed);

        std::uint64_t timeoutMilliseconds =
            impl_->config.allocationStallTimeoutMilliseconds;
        if (impl_->config.allocationServiceLevel ==
            AllocationServiceLevel::Latency) {
            timeoutMilliseconds = std::max<std::uint64_t>(
                1U, timeoutMilliseconds / 2U);
        } else if (impl_->config.allocationServiceLevel ==
                   AllocationServiceLevel::Throughput) {
            timeoutMilliseconds = std::min<std::uint64_t>(
                timeoutMilliseconds * 2U,
                std::numeric_limits<std::uint32_t>::max());
        }
        const auto deadline = phaseStallStarted +
            std::chrono::milliseconds(timeoutMilliseconds);
        bool timedOut = false;
        while (impl_->markActive.load(std::memory_order_acquire) ||
               impl_->youngMarkActive.load(std::memory_order_acquire) ||
               impl_->relocationActive.load(std::memory_order_acquire)) {
            safepointPoll();
            if (std::chrono::steady_clock::now() >= deadline) {
                timedOut = true;
                break;
            }
            std::this_thread::yield();
        }
        impl_->allocationStallWaiters.fetch_sub(
            1, std::memory_order_acq_rel);
        impl_->allocationStallMicros.fetch_add(
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() -
                    phaseStallStarted).count()),
            std::memory_order_relaxed);
        if (timedOut) {
            impl_->allocationStallTimeouts.fetch_add(
                1, std::memory_order_relaxed);
            impl_->clearSoftReferencesRequested.store(
                true, std::memory_order_release);
            throw std::bad_alloc();
        }
        try {
            return allocateVariableOnce(type, payloadBytes);
        } catch (const std::bad_alloc&) {
            // The completed phase did not free a suitable Region. The current
            // thread owns emergency service and may now run the normal OOM
            // recovery order without overlapping another GC transition.
        }
    }

    struct EmergencyGuard final {
        EmergencyGuard() { emergencyCollectionActive = true; }
        ~EmergencyGuard() { emergencyCollectionActive = false; }
    } guard;
    impl_->emergencyCollections.fetch_add(1, std::memory_order_relaxed);
    const bool throughput = impl_->config.allocationServiceLevel ==
        AllocationServiceLevel::Throughput;
    const bool latency = impl_->config.allocationServiceLevel ==
        AllocationServiceLevel::Latency;
    if (!latency)
        requestSoftReferenceClearing();

    if (!throughput) {
        try {
            (void)collectYoung();
        } catch (const std::bad_alloc&) {
            // A completely live nursery can have no evacuation reserve.  The
            // full cycle below can reclaim old regions or produce the final,
            // explicit OOM after all recoverable references have been cleared.
        }
        try {
            return allocateVariableOnce(type, payloadBytes);
        } catch (const std::bad_alloc&) {
        }
    }

    requestSoftReferenceClearing();
    startConcurrentMark();
    (void)finishConcurrentMark();
    if (throughput) {
        try {
            (void)collectYoung();
        } catch (const std::bad_alloc&) {
        }
    }
    return allocateVariableOnce(type, payloadBytes);
    } catch (const std::bad_alloc&) {
        std::uint64_t activeRegions = UINT64_MAX;
        std::uint64_t reusableRegions = UINT64_MAX;
        if (!gcPhaseActive && impl_->heapMutex.try_lock()) {
            activeRegions = impl_->regions.size();
            reusableRegions = impl_->freeRegions.size();
            impl_->heapMutex.unlock();
        }
        std::fprintf(stderr,
            "novagc:allocation_oom type=%s payload_bytes=%zu "
            "emergency=%d phase=%d cycle=%d mark=%d young=%d relocation=%d "
            "service=%d active_regions=%llu reusable_regions=%llu "
            "created_regions=%llu allocated_bytes=%llu\n",
            type.name != nullptr ? type.name : "<unknown>", payloadBytes,
            emergencyCollectionActive ? 1 : 0, gcPhaseActive ? 1 : 0,
            gcCycleActive ? 1 : 0,
            impl_->markActive.load(std::memory_order_acquire) ? 1 : 0,
            impl_->youngMarkActive.load(std::memory_order_acquire) ? 1 : 0,
            impl_->relocationActive.load(std::memory_order_acquire) ? 1 : 0,
            impl_->emergencyServiceActive.load(std::memory_order_acquire)
                ? 1 : 0,
            static_cast<unsigned long long>(activeRegions),
            static_cast<unsigned long long>(reusableRegions),
            static_cast<unsigned long long>(impl_->createdRegions.load(
                std::memory_order_relaxed)),
            static_cast<unsigned long long>(impl_->allocatedBytes.load(
                std::memory_order_relaxed)));
        std::fflush(stderr);
        throw;
    }
}

void* Runtime::allocateVariableOnce(const TypeDescriptor& type,
                                    std::size_t payloadBytes) {
    if (!impl_->running)
        throw std::logic_error("hxcpp GC runtime is not initialized");
    if (currentThread == nullptr)
        throw std::logic_error("managed allocation requires an attached thread");
    if (type.fixedPayloadBytes == 0 || type.alignment == 0 ||
        type.alignment > alignof(ObjectHeader) ||
        (type.alignment & (type.alignment - 1U)) != 0)
        throw std::invalid_argument("unsupported managed type layout");
    if (payloadBytes < type.fixedPayloadBytes ||
        (payloadBytes != type.fixedPayloadBytes &&
         (type.flags & TypeVariableSize) == 0))
        throw std::invalid_argument("invalid variable-size managed allocation");
    if (payloadBytes > UINT32_MAX)
        throw std::bad_alloc();
    if ((type.flags & TypeFinalizable) != 0 && type.finalize == nullptr)
        throw std::invalid_argument("finalizable type has no callback");

    const std::size_t total = alignUp(
        sizeof(ObjectHeader) + payloadBytes, alignof(ObjectHeader));
    if (total > UINT32_MAX)
        throw std::bad_alloc();

    auto* state = static_cast<Impl::ThreadState*>(currentThread);
    Impl::Region* region = nullptr;
    ObjectHeader* header = nullptr;
    bool tlabFast = false;
    const bool pinnedType = (type.flags & TypePinned) != 0;

    if (total <= impl_->config.largeObjectThreshold && !pinnedType) {
        if (state->tlabCursor != nullptr &&
            static_cast<std::size_t>(state->tlabLimit - state->tlabCursor) >=
                total) {
            region = state->tlabRegion;
            header = reinterpret_cast<ObjectHeader*>(state->tlabCursor);
            state->tlabCursor += total;
            tlabFast = true;
        } else {
            // A TLAB refill is the bounded allocation slow path. Poll before
            // taking the heap lock even when it is currently uncontended;
            // otherwise a tight native/template allocation loop can outrun a
            // collector that is waiting for this mutator to acknowledge the
            // handshake.
            safepointPoll();
            std::unique_lock heapLock(impl_->heapMutex, std::defer_lock);
            while (!heapLock.try_lock()) {
                safepointPoll();
                std::this_thread::yield();
            }
            for (auto iterator = impl_->regions.rbegin();
                 iterator != impl_->regions.rend(); ++iterator) {
                if ((*iterator)->acceptsTlab() &&
                    (*iterator)->bytes - (*iterator)->cursor >= total) {
                    region = iterator->get();
                    break;
                }
            }
            if (region == nullptr) {
                auto created = impl_->acquireRegion(
                    impl_->config.regionBytes, Impl::RegionKind::Eden);
                region = created.get();
                const std::size_t grant = std::min(
                    region->bytes,
                    std::max(impl_->config.tlabBytes, total));
                // Reserve the creator's first TLAB before publishing the
                // region in the shared region table. Another refilling thread
                // can therefore never receive the same first address range.
                region->ensureCommitted(grant);
                region->cursor = grant;
                state->tlabRegion = region;
                state->tlabCursor = region->memory;
                state->tlabLimit = region->memory + grant;
                impl_->regions.push_back(std::move(created));
            } else {
                const std::size_t start = region->cursor;
                const std::size_t grant = std::min(
                    region->bytes - start,
                    std::max(impl_->config.tlabBytes, total));
                region->ensureCommitted(start + grant);
                region->cursor += grant;
                state->tlabRegion = region;
                state->tlabCursor = region->memory + start;
                state->tlabLimit = state->tlabCursor + grant;
            }
            impl_->tlabRefills.fetch_add(1, std::memory_order_relaxed);
            header = reinterpret_cast<ObjectHeader*>(state->tlabCursor);
            state->tlabCursor += total;
        }
    } else {
        // Large and ABI-pinned allocations do not use TLABs, so they need
        // their own poll and the shared heap lock.
        // In particular StringBuf/Serializer can produce a succession of
        // increasingly large strings without re-entering generated Haxe code.
        safepointPoll();
        std::unique_lock heapLock(impl_->heapMutex, std::defer_lock);
        while (!heapLock.try_lock()) {
            safepointPoll();
            std::this_thread::yield();
        }
        // Small ABI-pinned buffers (notably Lime Bytes backing image/audio
        // data) are address-stable from birth. Pack them into Pinned regions;
        // one dedicated virtual Region per buffer makes real mod loads create
        // thousands of Regions, while late pin isolation would move each
        // buffer and rescan the complete managed heap. Humongous objects keep
        // a right-sized exclusive reservation.
        const bool packPinned = pinnedType &&
            total <= impl_->config.regionBytes / 2U;
        if (packPinned) {
            for (auto iterator = impl_->regions.rbegin();
                 iterator != impl_->regions.rend(); ++iterator) {
                Impl::Region* candidate = iterator->get();
                if (!candidate->retired && !candidate->largeOnly &&
                    candidate->kind.load(std::memory_order_acquire) ==
                        Impl::RegionKind::Pinned &&
                    candidate->bytes - candidate->cursor >= total) {
                    region = candidate;
                    break;
                }
            }
        }

        if (region == nullptr) {
            auto created = impl_->acquireRegion(
                packPinned ? impl_->config.regionBytes : total,
                pinnedType ? Impl::RegionKind::Pinned
                           : Impl::RegionKind::YoungLarge);
            region = created.get();
            region->largeOnly = !packPinned;
            impl_->regions.push_back(std::move(created));
        }
        const std::size_t offset = region->cursor;
        region->ensureCommitted(offset + total);
        region->cursor = offset + total;
        header = reinterpret_cast<ObjectHeader*>(region->memory + offset);
    }

    ::new (static_cast<void*>(header)) ObjectHeader();
    header->type = &type;
    header->totalBytes = static_cast<std::uint32_t>(total);
    header->payloadBytes = static_cast<std::uint32_t>(payloadBytes);
    if (state->identityCursor == state->identityLimit) {
        state->identityCursor = impl_->nextIdentity.fetch_add(
            IdentityBlockSize, std::memory_order_relaxed);
        state->identityLimit = state->identityCursor + IdentityBlockSize;
    }
    header->identity = state->identityCursor++;
    header->regionIndex = region->index;
    header->reserved0 = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(region));
    // Objects allocated after remark are born live in the last completed
    // epoch. This prevents the following relocation selection from retiring
    // an allocation region containing a post-mark object.
    if (impl_->markEpoch != 0)
        header->markEpoch.store(impl_->markEpoch, std::memory_order_release);
    if (total > impl_->config.largeObjectThreshold)
        header->flags.fetch_or(ObjectLarge, std::memory_order_relaxed);
    if (pinnedType)
        header->flags.fetch_or(ObjectPinned, std::memory_order_relaxed);
    if ((type.flags & TypeFinalizable) != 0) {
        header->flags.fetch_or(ObjectFinalizable, std::memory_order_relaxed);
    }
    region->recordObject(header);
    if (impl_->markActive.load(std::memory_order_acquire)) {
        impl_->accountRegionLive(header);
        impl_->concurrentMarkedObjects.fetch_add(
            1, std::memory_order_relaxed);
        impl_->concurrentMarkedBytes.fetch_add(
            header->payloadBytes, std::memory_order_relaxed);
    }
    ++state->pendingAllocatedObjects;
    state->pendingAllocatedBytes += total;
    if (tlabFast)
        ++state->pendingTlabFastAllocations;
    if (state->pendingAllocatedBytes >= AllocationCounterFlushBytes ||
        state->pendingAllocatedObjects >= AllocationCounterFlushObjects) {
        impl_->flushAllocationCounters(*state);
    }

    void* payload = payloadOf(header);
    // Regions are zero-filled once when committed and allocation is strictly
    // bump-only.  This payload has therefore never been exposed before; a
    // second memset here merely writes every ArrayBuffer byte twice.
    if ((type.flags & TypeFinalizable) != 0) {
        auto cell = std::make_unique<Impl::FinalizerCell>();
        cell->object.store(makeRef(payload).bits(), std::memory_order_release);
        std::scoped_lock finalizerLock(impl_->finalizerMutex);
        impl_->finalizers.push_back(std::move(cell));
    }
    return payload;
}

void Runtime::publish(void* object) {
    if (object == nullptr)
        throw std::invalid_argument("cannot publish a null managed object");
    ObjectHeader* header = headerOf(object);
    const std::uint16_t previous = header->flags.fetch_or(
        ObjectPublished, std::memory_order_acq_rel);
    if ((previous & ObjectPublished) != 0 ||
        !impl_->markActive.load(std::memory_order_acquire))
        return;

    // Allocation makes an object born-live for the active epoch. Publish is
    // the first point at which its descriptor-visible fields are initialized,
    // so enqueue it here for concurrent tracing. If the marker has already
    // exited, finishConcurrentMark drains this same tail queue at remark.
    std::scoped_lock markLock(impl_->markMutex);
    impl_->markQueue.push_back(header);
    concurrentMarkWorkerIdle.store(false, std::memory_order_release);
    impl_->markCondition.notify_one();
}

void detail::publishEmpty(void* object) {
    if (object == nullptr)
        throw std::invalid_argument("cannot publish a null managed object");
    // allocateVariable zeroes the complete payload.  Callers may initialize
    // scalar metadata before this point, but every managed reference slot must
    // still be null.  The object is already born-live for an active mark epoch;
    // with no outgoing edges there is no tracing work to enqueue.  Subsequent
    // edges are covered by storeBarrier's insertion/SATB barrier.
    headerOf(object)->flags.fetch_or(ObjectPublished,
                                     std::memory_order_release);
}

void Runtime::finishConstruction(void* object) {
    if (object == nullptr)
        throw std::invalid_argument("cannot finish a null managed object");
    ObjectHeader* header = headerOf(object);
    if ((header->flags.load(std::memory_order_acquire) & ObjectPublished) == 0)
        throw std::logic_error("managed construction finished before publish");
    if (!impl_->markActive.load(std::memory_order_acquire))
        return;

    // publishEmpty intentionally skipped the mark queue while the descriptor
    // still described only zero reference slots.  C++ member initializers are
    // allowed to populate those slots without a generated owner-aware write
    // barrier, so enqueue the completed shape even if the marker worker has
    // already gone idle; remark drains this same tail queue.
    std::scoped_lock markLock(impl_->markMutex);
    impl_->markQueue.push_back(header);
    concurrentMarkWorkerIdle.store(false, std::memory_order_release);
    impl_->markCondition.notify_one();
}

void detail::finishConstruction(void* object) {
    Runtime::instance().finishConstruction(object);
}

Ref Runtime::makeRef(void* object) const noexcept {
    return Ref::encode(object, impl_->view.load(std::memory_order_acquire));
}

void* Runtime::loadBarrier(RefSlot& slot) {
    std::uint64_t observed = slot.load(std::memory_order_acquire);
    Ref initial(observed);
    if (initial.isNull())
        return nullptr;

    // The overwhelmingly common read already carries the collector's current
    // color.  In that state there is nothing to heal, and outside an active
    // relocation phase the address cannot have a forwarding target.  Avoid a
    // cache miss into the object header plus a redundant CAS on every Haxe
    // ObjectPtr read.  Mark/view flips still take the complete slow path below
    // and heal the slot exactly as before.
    const std::uint32_t fastState =
        fastBarrierState.load(std::memory_order_acquire);
    if (currentThread != nullptr && !gcPhaseActive &&
        (fastState & FastBarrierSlowMask) == 0 &&
        static_cast<std::uint32_t>(initial.view()) ==
            (fastState & FastBarrierViewMask)) {
        return initial.address();
    }

    for (;;) {
        Ref reference(observed);
        if (reference.isNull()) return nullptr;

        void* object = reference.address();
        for (;;) {
            auto* header = headerOf(object);
            const std::uint64_t forwarded =
                header->forwarding.load(std::memory_order_acquire);
            if (forwarded == 0)
                break;
            void* next = Ref(forwarded).address();
            if (next == object)
                throw std::logic_error("self-referential forwarding pointer");
            object = next;
        }

        const std::uint64_t healed = Ref::encode(
            object, impl_->view.load(std::memory_order_acquire)).bits();
        if (observed == healed || slot.compare_exchange_weak(
                observed, healed, std::memory_order_acq_rel,
                std::memory_order_acquire))
            return object;
    }
}

void Runtime::storeBarrier(void* owner, RefSlot& slot, void* value) {
    // Most generated stores happen while the collector is completely idle.
    // Resolve the generational relation here, before touching the phase
    // atomics and remembered-set machinery below.  Region promotion and all
    // moving/marking transitions publish a FastStoreBarrierSlowMask bit before
    // mutators can observe the new state, so this path is limited to stable
    // generations.  A young owner never needs an old-to-young card; an old
    // owner only needs one when the new value is young.
    const std::uint32_t fastState =
        fastBarrierState.load(std::memory_order_acquire);
    if (currentThread != nullptr &&
        (fastState & FastStoreBarrierSlowMask) == 0) {
        bool publishDirect = value == nullptr;
        if (!publishDirect && owner != nullptr) {
            auto* ownerRegion = reinterpret_cast<Impl::Region*>(
                static_cast<std::uintptr_t>(headerOf(owner)->reserved0));
            if (ownerRegion != nullptr &&
                ownerRegion->young.load(std::memory_order_acquire)) {
                publishDirect = true;
            } else {
                auto* valueRegion = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(headerOf(value)->reserved0));
                publishDirect = valueRegion == nullptr ||
                    !valueRegion->young.load(std::memory_order_acquire);
            }
        }
        if (publishDirect) {
            slot.store(Ref::encode(value, static_cast<View>(
                fastState & FastBarrierViewMask)).bits(),
                std::memory_order_release);
            return;
        }
    }

    storeBarrierSlow(impl_, owner, slot, value);
}

void Runtime::refreshUntrackedReference(RefSlot& slot, void* value) {
    // ValueCell and similar native caches are deliberately absent from the
    // exact root map.  Their strong/weak Handle is the authoritative GC edge;
    // the embedded RefSlot is only a fast Dynamic representation.  It is
    // therefore incorrect to SATB-enqueue the overwritten cache value: after
    // relocation that value can name a source region already reclaimed by a
    // later cycle.
    if (!impl_->markActive.load(std::memory_order_acquire)) {
        slot.store(makeRef(value).bits(), std::memory_order_release);
        return;
    }

    std::scoped_lock markLock(impl_->markMutex);
    if (value != nullptr) {
        ObjectHeader* header = headerOf(value);
        std::uint32_t observed =
            header->markEpoch.load(std::memory_order_acquire);
        while (observed != impl_->markEpoch) {
            if (header->markEpoch.compare_exchange_weak(
                    observed, impl_->markEpoch,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                impl_->markQueue.push_back(header);
                impl_->accountRegionLive(header);
                concurrentMarkWorkerIdle.store(false,
                                               std::memory_order_release);
                impl_->concurrentMarkedObjects.fetch_add(
                    1, std::memory_order_relaxed);
                impl_->concurrentMarkedBytes.fetch_add(
                    header->payloadBytes, std::memory_order_relaxed);
                break;
            }
        }
    }
    slot.store(makeRef(value).bits(), std::memory_order_release);
    impl_->markCondition.notify_one();
}

void* Runtime::acquireScopedPin(RefSlot& rootedReference) {
    auto pinResolved = [this, &rootedReference]() -> void* {
        void* object = loadBarrier(rootedReference);
        if (object == nullptr)
            return nullptr;
        auto* header = headerOf(object);
        if (header->pinCount.fetch_add(1, std::memory_order_acq_rel) == 0) {
            auto* region = reinterpret_cast<Impl::Region*>(
                static_cast<std::uintptr_t>(header->reserved0));
            if (region != nullptr)
                region->pinnedObjects.fetch_add(1,
                                                std::memory_order_relaxed);
        }
        header->flags.fetch_or(ObjectPinned, std::memory_order_acq_rel);
        return object;
    };

    auto* state = static_cast<Impl::ThreadState*>(currentThread);
    // Super calls and generated RootedValue receivers commonly pin the same
    // object again while its outer method scope is still active. The outer
    // counted pin already makes that address immovable, so another pair of
    // header atomics is redundant. Record the nested lifetime for strict
    // LIFO release, but inherit stability from the outer pin. This is unlike
    // the rejected raw TLS protocol: a nested record is only admitted when a
    // live counted record for the exact resolved address is immediately
    // enclosing it.
    if (state != nullptr && !state->scopedPins.empty()) {
        void* resolved = loadBarrier(rootedReference);
        if (resolved != nullptr &&
            state->scopedPins.back().object == resolved) {
            state->scopedPins.push_back({resolved, false});
            return resolved;
        }
    }
    // Every scoped receiver pin uses the header-counted protocol.  A previous
    // optimization published an uncounted raw address in the thread ledger
    // while relocation was inactive and relied on the next mutator handshake
    // to exclude its region.  Full generated applications exposed a narrow
    // stale-reference race in that transition (normal timing crashed while a
    // debugger's stop changed the schedule).  Rooting the slot and taking the
    // relocation lock when necessary makes the pin linearizable with every
    // moving phase; the small atomic cost is preferable to an unsafe receiver.
    std::unique_lock relocationLock(impl_->relocationMutex,
                                    std::defer_lock);
    if (impl_->relocationActive.load(std::memory_order_acquire)) {
        while (!relocationLock.try_lock()) {
            if (state != nullptr)
                safepointPoll();
            std::this_thread::yield();
        }
    }
    void* object = pinResolved();
    if (state != nullptr && object != nullptr)
        state->scopedPins.push_back({object, true});
    return object;
}

void Runtime::releaseScopedPin(void* object) noexcept {
    if (object == nullptr)
        return;

    bool counted = true;
    auto* state = static_cast<Impl::ThreadState*>(currentThread);
    if (state != nullptr) {
        // Correct generated/native RAII destroys pins in LIFO order.  Retain a
        // reverse-search fallback so an embedding mistake does not turn a
        // cheap transient pin into an underflowing native pin counter.
        auto found = state->scopedPins.end();
        if (!state->scopedPins.empty() &&
            state->scopedPins.back().object == object) {
            found = std::prev(state->scopedPins.end());
        } else {
            const auto reverse = std::find_if(
                state->scopedPins.rbegin(), state->scopedPins.rend(),
                [object](const Impl::ScopedPinRecord& record) {
                    return record.object == object;
                });
            if (reverse != state->scopedPins.rend())
                found = std::prev(reverse.base());
        }
        if (found != state->scopedPins.end()) {
            counted = found->counted;
            if (found == std::prev(state->scopedPins.end()))
                state->scopedPins.pop_back();
            else
                state->scopedPins.erase(found);
            if (!counted)
                return;
        }
    }

    auto unpin = [object]() noexcept {
        auto* header = headerOf(object);
        if (header->pinCount.fetch_sub(1, std::memory_order_acq_rel) != 1)
            return;

        auto* region = reinterpret_cast<Impl::Region*>(
            static_cast<std::uintptr_t>(header->reserved0));
        if (region != nullptr &&
            region->pinnedObjects.fetch_sub(
                1, std::memory_order_acq_rel) == 1 &&
            (header->type->flags & TypePinned) == 0 &&
            region->kind.load(std::memory_order_acquire) ==
                Impl::RegionKind::Pinned) {
            region->setKind(Impl::RegionKind::Old);
        }
        header->flags.fetch_and(
            static_cast<std::uint16_t>(~ObjectPinned),
            std::memory_order_acq_rel);
        // A different mutator may have acquired the first new pin between the
        // decrement and flag clear. Repair the diagnostic flag; pinCount and
        // the region counter already remain correct through their paired
        // atomic transitions.
        if (header->pinCount.load(std::memory_order_acquire) != 0)
            header->flags.fetch_or(ObjectPinned,
                                   std::memory_order_acq_rel);
    };

    if (!impl_->relocationActive.load(std::memory_order_acquire)) {
        unpin();
        return;
    }
    std::scoped_lock relocationLock(impl_->relocationMutex);
    unpin();
}

void* Runtime::pinForNative(void* object) {
    if (object == nullptr)
        return nullptr;
    const Handle transient = createHandle(object, HandleKind::Pinned);
    void* resolved = resolveHandle(transient);
    ObjectHeader* header = headerOf(resolved);
    header->pinCount.fetch_add(1, std::memory_order_acq_rel);
    header->flags.fetch_or(ObjectPinned, std::memory_order_acq_rel);
    releaseHandle(transient);
    return resolved;
}

const TypeDescriptor* Runtime::typeOfSlow(void* object) {
    if (object == nullptr)
        return nullptr;
    RefSlot temporary{makeRef(object).bits()};
    void* resolved = loadBarrier(temporary);
    return resolved == nullptr ? nullptr : headerOf(resolved)->type;
}

std::uint64_t Runtime::identityOf(void* object) {
    if (object == nullptr)
        return 0;
    RefSlot reference{makeRef(object).bits()};
    void* resolved = loadBarrier(reference);
    return resolved == nullptr ? 0 : headerOf(resolved)->identity;
}

void* Runtime::isolateForPinnedHandle(void* object) {
    if (object == nullptr)
        return nullptr;

    // Keep the requested identity in the caller's exact root chain while a
    // concurrent Young cycle owns the transition lock. The source address
    // supplied by createHandle may be forwarded and reclaimed before this
    // function acquires the lock; the rooted slot is healed by that cycle's
    // final remap and is the only safe value to use afterwards.
    RefSlot resolvedSlot{makeRef(object).bits()};
    RefSlot* resolvedRoots[]{&resolvedSlot};
    std::optional<RootScope> rootScope;
    if (currentThreadAttached())
        rootScope.emplace(resolvedRoots, 1, 0x50494e48U);

    object = loadBarrier(resolvedSlot);
    if (gcCycleActive) {
        // Finalizer callbacks run before the owning Young/Full cycle releases
        // its mutators. CFFI val_data/val_gc legitimately acquire a temporary
        // Pinned handle for the object that drainFinalizers has already pinned.
        // Starting a second isolation cycle here both violates cycle ownership
        // and throws through native libraries that commonly hold non-RAII
        // mutexes (Lime/OpenAL's al_gc_mutex is one example). Keep the object
        // in place for this cycle; createHandle will add the counted pin below
        // and later moving phases already exclude its containing region.
        impl_->pinIsolationFallbacks.fetch_add(1, std::memory_order_relaxed);
        return object;
    }

    // Dynamic pin isolation is itself a moving stop-the-world phase. It must
    // participate in the same transition protocol as Young/Full/relocation;
    // otherwise a CFFI pin can issue a second handshake between a young
    // cycle's initial and final pauses and let one epoch resume another.
    GcCycleScope cycleScope(
        acquireGcCycle(*this, impl_->collectionCycleMutex));
    auto phaseLock = acquireGcPhase(*this, impl_->phaseTransitionMutex);
    GcPhaseScope phaseScope;
    object = loadBarrier(resolvedSlot);
    if (impl_->markActive.load(std::memory_order_acquire) ||
        impl_->youngMarkActive.load(std::memory_order_acquire) ||
        impl_->relocationActive.load(std::memory_order_acquire)) {
        impl_->pinIsolationFallbacks.fetch_add(
            1, std::memory_order_relaxed);
        return object;
    }

    ObjectHeader* original = headerOf(object);
    auto* originalRegion = reinterpret_cast<Impl::Region*>(
        static_cast<std::uintptr_t>(original->reserved0));
    if (originalRegion == nullptr ||
        originalRegion->kind.load(std::memory_order_acquire) ==
            Impl::RegionKind::Pinned ||
        (original->type->flags & TypePinned) != 0 ||
        original->pinCount.load(std::memory_order_acquire) != 0) {
        return object;
    }

    impl_->parkMutators(static_cast<Impl::ThreadState*>(currentThread));
    struct ResumePinnedIsolation final {
        Impl* impl = nullptr;
        ~ResumePinnedIsolation() { impl->resumeMutators(); }
    } resumePinnedIsolation{impl_};
    try {
        std::scoped_lock heapLock(impl_->heapMutex);
        std::scoped_lock threadLock(impl_->threadMutex);
        std::scoped_lock publicationLock(impl_->rootPublicationMutex);

        object = loadBarrier(resolvedSlot);
        original = headerOf(object);
        originalRegion = reinterpret_cast<Impl::Region*>(
            static_cast<std::uintptr_t>(original->reserved0));
        if (originalRegion == nullptr ||
            originalRegion->kind.load(std::memory_order_acquire) ==
                Impl::RegionKind::Pinned ||
            original->pinCount.load(std::memory_order_acquire) != 0) {
            return object;
        }

        for (const auto& thread : impl_->threads) {
            const bool dynamicallyPinned = std::ranges::any_of(
                    thread->scopedPins,
                    [object](const Impl::ScopedPinRecord& pin) {
                        return pin.object == object;
                    });
            bool generatedPinned = false;
            for (std::size_t index = 0;
                 index < thread->fastScopedPinCount; ++index) {
                if (thread->fastScopedPins[index] == object) {
                    generatedPinned = true;
                    break;
                }
            }
            if (dynamicallyPinned || generatedPinned) {
                impl_->pinIsolationFallbacks.fetch_add(
                    1, std::memory_order_relaxed);
                return object;
            }
        }

        // CFFI establishes thousands of long-lived native pins while loading
        // textures and audio. Giving each small pinned wrapper its own virtual
        // Region makes every GC/root snapshot walk thousands of Regions and
        // turns the one-second memory counter into a visible frame hitch.
        // Pinned addresses only require a non-moving Region, not an exclusive
        // Region. Pack ordinary pinned objects into the bump tail of an
        // existing Pinned Region and retain dedicated reservations only for
        // objects larger than half a normal Region.
        const std::size_t totalBytes = original->totalBytes;
        const bool dedicated = totalBytes > impl_->config.regionBytes / 2U;
        Impl::Region* destination = nullptr;
        if (!dedicated) {
            for (auto iterator = impl_->regions.rbegin();
                 iterator != impl_->regions.rend(); ++iterator) {
                Impl::Region* candidate = iterator->get();
                if (!candidate->retired && !candidate->largeOnly &&
                    candidate->kind.load(std::memory_order_acquire) ==
                        Impl::RegionKind::Pinned &&
                    candidate->bytes - candidate->cursor >= totalBytes) {
                    destination = candidate;
                    break;
                }
            }
        }

        std::unique_ptr<Impl::Region> created;
        try {
            if (destination == nullptr) {
                created = impl_->acquireRegion(
                    dedicated ? totalBytes : impl_->config.regionBytes,
                    Impl::RegionKind::Pinned);
                created->largeOnly = dedicated;
                destination = created.get();
            }
            destination->ensureCommitted(destination->cursor + totalBytes);
        } catch (const std::bad_alloc&) {
            impl_->pinIsolationFallbacks.fetch_add(
                1, std::memory_order_relaxed);
            return object;
        }

        const std::size_t destinationOffset = destination->cursor;
        auto* isolated = reinterpret_cast<ObjectHeader*>(
            destination->memory + destinationOffset);
        ::new (static_cast<void*>(isolated)) ObjectHeader();
        isolated->type = original->type;
        isolated->totalBytes = original->totalBytes;
        isolated->payloadBytes = original->payloadBytes;
        isolated->markEpoch.store(
            original->markEpoch.load(std::memory_order_acquire),
            std::memory_order_release);
        isolated->identity = original->identity;
        isolated->flags.store(
            original->flags.load(std::memory_order_acquire),
            std::memory_order_release);
        isolated->age = original->age;
        isolated->regionIndex = destination->index;
        isolated->reserved0 = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(destination));
        std::memcpy(payloadOf(isolated), payloadOf(original),
                    original->payloadBytes);
        destination->cursor = destinationOffset + totalBytes;
        destination->recordObject(isolated);
        if (created != nullptr)
            impl_->regions.push_back(std::move(created));

        original->state.store(ObjectRelocating,
                              std::memory_order_release);
        original->forwarding.store(
            Ref::encode(payloadOf(isolated), View::Remapped).bits(),
            std::memory_order_release);
        original->state.store(ObjectRelocated,
                              std::memory_order_release);
        publishFastBarrierView(impl_->view, View::Remapped);

        class PinRemapper final : public TraceVisitor {
        public:
            PinRemapper(Runtime& runtime, void* source, void* destination)
                : runtime_(runtime), source_(source),
                  destination_(destination) {}
            void visit(RefSlot& slot) override {
                const Ref reference(slot.load(std::memory_order_acquire));
                if (!reference.isNull() && reference.address() == source_) {
                    slot.store(runtime_.makeRef(destination_).bits(),
                               std::memory_order_release);
                }
            }
        private:
            Runtime& runtime_;
            void* source_ = nullptr;
            void* destination_ = nullptr;
        } remapper(*this, object, payloadOf(isolated));

        for (const auto& thread : impl_->threads) {
            for (RootFrame* frame = thread->roots; frame != nullptr;
                 frame = frame->previous) {
                for (std::uint32_t index = 0;
                     index < frame->slotCount; ++index) {
                    remapper.visit(*frame->slots[index]);
                }
            }
        }
        {
            std::scoped_lock staticLock(impl_->staticRootMutex);
            for (RefSlot* slot : impl_->staticRoots)
                remapper.visit(*slot);
        }
        {
            std::scoped_lock handleLock(impl_->handleMutex);
            for (const auto& cell : impl_->handles) {
                if (cell->active)
                    remapper.visit(cell->reference);
            }
        }
        {
            std::scoped_lock ephemeronLock(impl_->ephemeronMutex);
            for (const auto& cell : impl_->ephemerons) {
                if (cell->active) {
                    remapper.visit(cell->key);
                    remapper.visit(cell->value);
                }
            }
        }
        {
            std::scoped_lock finalizerLock(impl_->finalizerMutex);
            for (const auto& cell : impl_->finalizers) {
                if (cell->state != Impl::FinalizerState::Cleared)
                    remapper.visit(cell->object);
            }
        }

        for (const auto& region : impl_->regions) {
            // Successfully relocated sources remain mapped as forwarding
            // metadata until the next exact full traversal.  Their payloads
            // are obsolete copies, not members of the live heap graph; a
            // later young cycle may already have reclaimed objects referenced
            // only by those copies.  Tracing them here can therefore follow a
            // deliberately dead edge into reused memory.
            if (region->retired)
                continue;
            for (ObjectHeader* header : region->snapshotObjects()) {
                if ((header->flags.load(std::memory_order_acquire) &
                     ObjectPublished) == 0 ||
                    header->forwarding.load(std::memory_order_acquire) != 0) {
                    continue;
                }
                auto* payload = static_cast<std::byte*>(payloadOf(header));
                for (const TypeDescriptor* type = header->type;
                     type != nullptr; type = type->super) {
                    for (std::uint32_t index = 0;
                         index < type->referenceOffsetCount; ++index) {
                        remapper.visit(*reinterpret_cast<RefSlot*>(
                            payload + type->referenceOffsets[index]));
                    }
                    if (type->trace != nullptr)
                        type->trace(payload, header->payloadBytes, remapper);
                }
            }
        }
        impl_->remapRememberedOwner(original, isolated);
        object = payloadOf(isolated);
        impl_->isolatedPinnedObjects.fetch_add(
            1, std::memory_order_relaxed);
    } catch (...) {
        throw;
    }
    return object;
}

Handle Runtime::createHandle(void* object, HandleKind kind) {
    if (object == nullptr)
        return {};
    if (kind == HandleKind::Pinned)
        object = isolateForPinnedHandle(object);
    std::unique_lock heapLock(impl_->heapMutex, std::defer_lock);
    std::unique_lock relocationLock(impl_->relocationMutex, std::defer_lock);
    if (kind == HandleKind::Pinned) {
        std::lock(heapLock, relocationLock);
        RefSlot temporary{makeRef(object).bits()};
        object = loadBarrier(temporary);
    }
    std::unique_lock handleLock(impl_->handleMutex);
    std::uint32_t index = 0;
    if (!impl_->freeHandleIndices.empty()) {
        index = impl_->freeHandleIndices.back();
        impl_->freeHandleIndices.pop_back();
    } else {
        index = static_cast<std::uint32_t>(impl_->handles.size());
        impl_->handles.push_back(std::make_unique<Impl::HandleCell>());
    }
    Impl::HandleCell& cell = *impl_->handles[index];
    if (cell.active)
        throw std::logic_error("hxcpp GC free handle index is active");
    cell.kind = kind;
    cell.active = true;
    cell.enqueued = false;
    cell.reference.store(makeRef(object).bits(), std::memory_order_release);
    handleLock.unlock();
    if (impl_->markActive.load(std::memory_order_acquire) &&
        Impl::handleIsStrongRoot(
            kind, impl_->clearSoftReferencesThisCycle)) {
        std::scoped_lock markLock(impl_->markMutex);
        ObjectHeader* header = headerOf(object);
        std::uint32_t observed =
            header->markEpoch.load(std::memory_order_acquire);
        while (observed != impl_->markEpoch) {
            if (header->markEpoch.compare_exchange_weak(
                    observed, impl_->markEpoch, std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                impl_->markQueue.push_back(header);
                impl_->accountRegionLive(header);
                impl_->concurrentRoots.fetch_add(
                    1, std::memory_order_relaxed);
                impl_->concurrentMarkedObjects.fetch_add(
                    1, std::memory_order_relaxed);
                impl_->concurrentMarkedBytes.fetch_add(
                    header->payloadBytes, std::memory_order_relaxed);
                concurrentMarkWorkerIdle.store(false,
                                               std::memory_order_release);
                impl_->markCondition.notify_one();
                break;
            }
        }
    }
    if (Impl::handleIsStrongRoot(
            kind, impl_->clearSoftReferencesThisCycle)) {
        impl_->enqueueYoungMark(object);
    }
    if (kind == HandleKind::Pinned) {
        auto* header = headerOf(object);
        if (header->pinCount.fetch_add(1, std::memory_order_acq_rel) == 0) {
            auto* region = reinterpret_cast<Impl::Region*>(
                static_cast<std::uintptr_t>(header->reserved0));
            if (region != nullptr)
                region->pinnedObjects.fetch_add(1,
                                                std::memory_order_relaxed);
        }
        header->flags.fetch_or(ObjectPinned, std::memory_order_acq_rel);
    }
    return Handle{index, cell.generation};
}

void* Runtime::resolveHandle(Handle handle) {
    if (!handle.valid())
        return nullptr;
    std::scoped_lock handleLock(impl_->handleMutex);
    if (handle.index >= impl_->handles.size())
        return nullptr;
    Impl::HandleCell& cell = *impl_->handles[handle.index];
    if (!cell.active || cell.generation != handle.generation)
        return nullptr;
    if (cell.kind == HandleKind::Phantom)
        return nullptr;
    return loadBarrier(cell.reference);
}

void* Runtime::retainNativeCallback(void* object) {
    if (object == nullptr)
        return nullptr;

    // createHandle(Pinned) performs any required late-pin isolation and the
    // resolved address is the only address a native library may retain.
    const Handle candidate = createHandle(object, HandleKind::Pinned);
    void* stable = resolveHandle(candidate);
    if (stable == nullptr) {
        releaseHandle(candidate);
        return nullptr;
    }

    bool keepCandidate = false;
    {
        std::scoped_lock callbackLock(impl_->nativeCallbackMutex);
        const auto [found, inserted] = impl_->nativeCallbackPins.try_emplace(
            stable, Impl::NativeCallbackPin{candidate, 0});
        ++found->second.retainCount;
        keepCandidate = inserted;
    }
    if (!keepCandidate)
        releaseHandle(candidate);
    return stable;
}

void Runtime::releaseNativeCallback(void* object) {
    if (object == nullptr)
        return;

    Handle released{};
    {
        std::scoped_lock callbackLock(impl_->nativeCallbackMutex);
        const auto found = impl_->nativeCallbackPins.find(object);
        if (found == impl_->nativeCallbackPins.end())
            return;
        if (--found->second.retainCount != 0)
            return;
        released = found->second.handle;
        impl_->nativeCallbackPins.erase(found);
    }
    // releaseHandle takes the heap, relocation and handle locks. Keep it
    // outside nativeCallbackMutex so callback registration can never invert
    // the GC lock order.
    releaseHandle(released);
}

void Runtime::releaseHandle(Handle handle) {
    if (!handle.valid())
        return;
    std::scoped_lock heapAndRelocation(impl_->heapMutex,
                                       impl_->relocationMutex);
    std::scoped_lock handleLock(impl_->handleMutex);
    if (handle.index >= impl_->handles.size())
        throw std::invalid_argument("invalid hxcpp GC handle index");
    Impl::HandleCell& cell = *impl_->handles[handle.index];
    if (!cell.active || cell.generation != handle.generation)
        throw std::invalid_argument("stale hxcpp GC handle generation");
    if (cell.kind == HandleKind::Pinned) {
        void* object = loadBarrier(cell.reference);
        if (object != nullptr) {
            auto* header = headerOf(object);
            if (header->pinCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                header->flags.fetch_and(
                    static_cast<std::uint16_t>(~ObjectPinned),
                    std::memory_order_acq_rel);
                auto* region = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(header->reserved0));
                if (region != nullptr &&
                    region->pinnedObjects.fetch_sub(
                        1, std::memory_order_acq_rel) == 1 &&
                    (header->type->flags & TypePinned) == 0 &&
                    region->kind.load(std::memory_order_acquire) ==
                        Impl::RegionKind::Pinned) {
                    region->setKind(Impl::RegionKind::Old);
                }
            }
        }
    }
    cell.reference.store(0, std::memory_order_release);
    cell.active = false;
    cell.enqueued = false;
    ++cell.generation;
    if (cell.generation == 0)
        cell.generation = 1;
    impl_->freeHandleIndices.push_back(handle.index);
}

void Runtime::requestSoftReferenceClearing() noexcept {
    impl_->clearSoftReferencesRequested.store(true,
                                               std::memory_order_release);
}

bool Runtime::referenceEnqueued(Handle handle) const {
    if (!handle.valid())
        return false;
    std::scoped_lock handleLock(impl_->handleMutex);
    if (handle.index >= impl_->handles.size())
        return false;
    const Impl::HandleCell& cell = *impl_->handles[handle.index];
    return cell.active && cell.generation == handle.generation &&
           cell.enqueued;
}

bool Runtime::pollReferenceQueue(Handle& handle) {
    std::scoped_lock handleLock(impl_->handleMutex);
    while (!impl_->referenceQueue.empty()) {
        const Handle candidate = impl_->referenceQueue.front();
        impl_->referenceQueue.pop_front();
        if (candidate.index >= impl_->handles.size())
            continue;
        Impl::HandleCell& cell = *impl_->handles[candidate.index];
        if (!cell.active || cell.generation != candidate.generation ||
            !cell.enqueued) {
            continue;
        }
        cell.enqueued = false;
        handle = candidate;
        return true;
    }
    handle = {};
    return false;
}

EphemeronHandle Runtime::createEphemeron(void* key, void* value) {
    if (key == nullptr)
        throw std::invalid_argument("ephemeron key cannot be null");
    std::scoped_lock heapLock(impl_->heapMutex);
    std::scoped_lock ephemeronLock(impl_->ephemeronMutex);
    RefSlot keySlot{makeRef(key).bits()};
    RefSlot valueSlot{makeRef(value).bits()};
    key = loadBarrier(keySlot);
    value = loadBarrier(valueSlot);
    std::uint32_t index = 0;
    for (; index < impl_->ephemerons.size(); ++index) {
        if (!impl_->ephemerons[index]->active)
            break;
    }
    if (index == impl_->ephemerons.size())
        impl_->ephemerons.push_back(
            std::make_unique<Impl::EphemeronCell>());
    auto& cell = *impl_->ephemerons[index];
    cell.key.store(makeRef(key).bits(), std::memory_order_release);
    cell.value.store(makeRef(value).bits(), std::memory_order_release);
    cell.active = true;
    return EphemeronHandle{index, cell.generation};
}

void* Runtime::resolveEphemeronKey(EphemeronHandle handle) {
    if (!handle.valid())
        return nullptr;
    std::scoped_lock ephemeronLock(impl_->ephemeronMutex);
    if (handle.index >= impl_->ephemerons.size())
        return nullptr;
    auto& cell = *impl_->ephemerons[handle.index];
    if (!cell.active || cell.generation != handle.generation)
        return nullptr;
    return loadBarrier(cell.key);
}

void* Runtime::resolveEphemeronValue(EphemeronHandle handle) {
    if (!handle.valid())
        return nullptr;
    std::scoped_lock ephemeronLock(impl_->ephemeronMutex);
    if (handle.index >= impl_->ephemerons.size())
        return nullptr;
    auto& cell = *impl_->ephemerons[handle.index];
    if (!cell.active || cell.generation != handle.generation)
        return nullptr;
    return loadBarrier(cell.value);
}

void Runtime::releaseEphemeron(EphemeronHandle handle) {
    if (!handle.valid())
        return;
    std::scoped_lock ephemeronLock(impl_->ephemeronMutex);
    if (handle.index >= impl_->ephemerons.size())
        throw std::invalid_argument("invalid ephemeron index");
    auto& cell = *impl_->ephemerons[handle.index];
    if (!cell.active || cell.generation != handle.generation)
        throw std::invalid_argument("stale ephemeron generation");
    cell.key.store(0, std::memory_order_release);
    cell.value.store(0, std::memory_order_release);
    cell.active = false;
    ++cell.generation;
    if (cell.generation == 0)
        cell.generation = 1;
}

FinalizerStats Runtime::drainFinalizers() {
    if (impl_->markActive.load(std::memory_order_acquire) ||
        impl_->youngMarkActive.load(std::memory_order_acquire) ||
        impl_->relocationActive.load(std::memory_order_acquire))
        throw std::logic_error("cannot drain finalizers during a GC phase");
    std::scoped_lock drainLock(impl_->finalizerDrainMutex);
    FinalizerStats stats{};
    struct PendingCall final {
        Impl::FinalizerCell* cell = nullptr;
        void* object = nullptr;
        std::function<void(void*)> callback;
        bool failed = false;
    };
    std::vector<PendingCall> pending;
    {
        // Snapshot once.  The previous implementation restarted a linear scan
        // from index zero after every callback, making a burst of N pending
        // objects O(N^2) (182k Anon finalizers took 88 seconds in the game).
        std::scoped_lock finalizerLock(impl_->finalizerMutex);
        pending.reserve(impl_->finalizers.size());
        for (const auto& cell : impl_->finalizers) {
            if (cell->state != Impl::FinalizerState::Pending)
                continue;
            void* object = loadBarrier(cell->object);
            std::function<void(void*)> callback = cell->callback;
            if (!callback && object != nullptr) {
                const FinalizeFunction descriptorCallback =
                    headerOf(object)->type->finalize;
                if (descriptorCallback != nullptr)
                    callback = descriptorCallback;
            }
            if (object == nullptr || !callback)
                throw std::logic_error(
                    "pending finalizer lost its managed object");
            cell->state = Impl::FinalizerState::Running;
            pending.push_back(PendingCall{cell.get(), object, callback, false});
        }
    }

    for (auto& call : pending) {
        auto* header = headerOf(call.object);
        header->pinCount.fetch_add(1, std::memory_order_acq_rel);
        header->flags.fetch_or(ObjectPinned, std::memory_order_acq_rel);
        try {
            call.callback(call.object);
        } catch (const std::exception& error) {
            call.failed = true;
            std::fprintf(stderr,
                "novagc:finalizer_error type=%s what=%s\n",
                header->type != nullptr && header->type->name != nullptr
                    ? header->type->name : "<unknown>",
                error.what());
        } catch (...) {
            call.failed = true;
            std::fprintf(stderr,
                "novagc:finalizer_error type=%s what=<non-standard>\n",
                header->type != nullptr && header->type->name != nullptr
                    ? header->type->name : "<unknown>");
        }
        if (header->pinCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            header->flags.fetch_and(
                static_cast<std::uint16_t>(~ObjectPinned),
                std::memory_order_acq_rel);

        ++stats.completed;
        if (call.failed)
            ++stats.failed;
    }

    // Keep every cell in Running state until all callbacks finish.  A mark
    // racing a direct runtime caller therefore sees the complete batch as
    // strong roots.  Commit completion under one lock instead of one lock per
    // object.
    {
        std::scoped_lock finalizerLock(impl_->finalizerMutex);
        for (const auto& call : pending) {
            void* current = loadBarrier(call.cell->object);
            if (current != nullptr)
                headerOf(current)->flags.fetch_or(
                    ObjectFinalized, std::memory_order_acq_rel);
            call.cell->state = Impl::FinalizerState::Finalized;
        }
    }
    return stats;
}

void Runtime::addFinalizer(void* object,
                           std::function<void(void*)> callback) {
    if (object == nullptr || !callback)
        throw std::invalid_argument("invalid per-object finalizer");
    RefSlot reference{makeRef(object).bits()};
    object = loadBarrier(reference);
    auto* header = headerOf(object);
    header->flags.fetch_or(ObjectFinalizable, std::memory_order_acq_rel);
    auto cell = std::make_unique<Impl::FinalizerCell>();
    cell->object.store(makeRef(object).bits(), std::memory_order_release);
    cell->callback = std::move(callback);
    std::scoped_lock finalizerLock(impl_->finalizerMutex);
    impl_->finalizers.push_back(std::move(cell));
}

YoungCollectionStats Runtime::collectYoung() {
    GcCycleScope cycleScope(
        acquireGcCycle(*this, impl_->collectionCycleMutex));
    auto phaseLock = acquireGcPhase(*this, impl_->phaseTransitionMutex);
    GcPhaseScope phaseScope;
    if (!impl_->running)
        throw std::logic_error("hxcpp GC runtime is not initialized");
    if (impl_->markActive.load(std::memory_order_acquire))
        throw std::logic_error("cannot collect nursery during concurrent mark");
    if (impl_->youngMarkActive.load(std::memory_order_acquire))
        throw std::logic_error("concurrent young cycle is already active");
    if (impl_->relocationActive.load(std::memory_order_acquire))
        throw std::logic_error("cannot collect nursery during relocation");

    const auto pauseStarted = std::chrono::steady_clock::now();
    YoungCollectionStats stats{};
    const bool clearSoftReferences =
        impl_->clearSoftReferencesRequested.exchange(
            false, std::memory_order_acq_rel);
    std::vector<std::unique_ptr<Impl::Region>> pendingReclamation;
    std::vector<std::pair<RefSlot*, ObjectHeader*>> scannedRememberedSlots;
    std::vector<Impl::Region*> scannedRememberedRegions;
    constexpr std::uint32_t ForceInitialRememberedOverflow = 1U << 0U;
    constexpr std::uint32_t ForceYoungMarkMetadataOverflow = 1U << 1U;
    constexpr std::uint32_t ForceFinalRememberedOverflow = 1U << 2U;
    constexpr std::uint32_t ForceEvacuationReservation = 1U << 3U;
    constexpr std::uint32_t ForcePinnedSource = 1U << 4U;
    constexpr std::uint32_t ForceNoMoveRequested = 1U << 5U;
    bool forceInPlaceYoung = false;
    std::uint8_t scannedRememberedEpoch =
        impl_->activeRememberedEpoch.load(std::memory_order_acquire);
    std::chrono::steady_clock::time_point finalYoungPauseStarted{};
    impl_->parkMutators(static_cast<Impl::ThreadState*>(currentThread));
    // Existing TLS receiver pins are stable now and are included in the
    // transient-pinned Region set below. Any receiver acquired while the
    // concurrent Young marker runs must use a counted pin so the final
    // evacuation cannot move it underneath a resumed generated method.
    scopedPinBarrierActive.store(true, std::memory_order_release);
    stats.handshakeMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - pauseStarted).count());
    auto phaseStarted = std::chrono::steady_clock::now();
    try {
        {
        std::unique_lock heapLock(impl_->heapMutex);
        std::unique_lock threadLock(impl_->threadMutex);
        std::unique_lock publicationLock(impl_->rootPublicationMutex);

        std::unordered_set<Impl::Region*> transientPinnedRegions;
        for (const auto& thread : impl_->threads) {
            for (const auto& pin : thread->scopedPins) {
                if (pin.object == nullptr)
                    continue;
                auto* region = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(
                        headerOf(pin.object)->reserved0));
                if (region != nullptr)
                    transientPinnedRegions.insert(region);
            }
            for (std::size_t index = 0;
                 index < thread->fastScopedPinCount; ++index) {
                void* object = thread->fastScopedPins[index];
                if (object == nullptr)
                    continue;
                auto* region = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(
                        headerOf(object)->reserved0));
                if (region != nullptr)
                    transientPinnedRegions.insert(region);
            }
        }

        // Atomically rotate the exact slot sets. The detached snapshot is the
        // complete old-to-young edge set for the cohort being collected;
        // every survivor is promoted to old before mutators resume and later
        // old-to-young writes will repopulate it through storeBarrier.
        {
            scannedRememberedEpoch =
                impl_->activeRememberedEpoch.load(
                    std::memory_order_acquire);
            const std::uint8_t nextRememberedEpoch =
                static_cast<std::uint8_t>(scannedRememberedEpoch ^ 1U);
            for (const auto& region : impl_->regions) {
                region->clearRememberedBitmap(nextRememberedEpoch);
                stats.rememberedBitmapBytes +=
                    region->rememberedBitmapBytes();
            }
            impl_->activeRememberedEpoch.store(
                nextRememberedEpoch, std::memory_order_release);
            std::scoped_lock rememberedLock(impl_->rememberedMutex);
            impl_->rememberedSnapshot.clear();
            impl_->rememberedSnapshot.swap(impl_->rememberedSlots);
            scannedRememberedSlots.assign(impl_->rememberedSnapshot.begin(),
                                           impl_->rememberedSnapshot.end());
        }

        ++impl_->youngMarkEpoch;
        stats.epoch = impl_->youngMarkEpoch;
        {
            std::scoped_lock youngMarkLock(impl_->youngMarkMutex);
            impl_->youngMarkQueue.clear();
            impl_->youngMarkRegions.clear();
            for (const auto& region : impl_->regions) {
                if (region->isYoung()) {
                    region->clearYoungMarks();
                    region->youngCollectionSource = true;
                    impl_->youngMarkRegions.insert(region.get());
                }
            }
            impl_->youngMarkClosing = false;
            impl_->youngMarkFailure = nullptr;
            impl_->activeYoungMarkWorkers = 0;
        }
        impl_->youngMarkMetadataOverflow.store(
            false, std::memory_order_release);
        impl_->concurrentYoungMarkedObjects.store(
            0, std::memory_order_relaxed);
        impl_->concurrentYoungMarkedBytes.store(
            0, std::memory_order_relaxed);

        // A nursery region must be immutable while it is classified.  Every
        // TLAB backed by this cohort is retired before tracing; mutators will
        // refill from fresh young regions after the handshake.
        for (const auto& state : impl_->threads) {
            if (state->tlabRegion != nullptr &&
                state->tlabRegion->young.load(std::memory_order_acquire)) {
                Impl::invalidateTlab(*state);
            }
        }

        std::vector<ObjectHeader*> work;
        class YoungMarker final : public TraceVisitor {
        public:
            YoungMarker(Runtime& runtime, YoungCollectionStats& stats,
                        std::vector<ObjectHeader*>& work)
                : runtime_(runtime), stats_(stats), work_(work) {}

            void visit(RefSlot& slot) override {
                mark(runtime_.loadBarrier(slot));
            }

            void mark(void* object) {
                if (object == nullptr)
                    return;
                auto* header = headerOf(object);
                auto* region = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(header->reserved0));
                if (region == nullptr ||
                    !runtime_.impl_->youngMarkRegions.contains(region)) {
                    return;
                }
                if ((header->flags.load(std::memory_order_acquire) &
                     ObjectPublished) == 0) {
                    throw std::logic_error(
                        "exact root references an unpublished young object");
                }
                if (region->tryMarkYoung(header)) {
                    work_.push_back(header);
                    ++stats_.markedObjects;
                    stats_.markedBytes += header->payloadBytes;
                }
            }

        private:
            Runtime& runtime_;
            YoungCollectionStats& stats_;
            std::vector<ObjectHeader*>& work_;
        } marker(*this, stats, work);

        const char* verifyReferencesEnvironment =
            std::getenv("HXCPP_NOVAGC_VERIFY_REFERENCES");
        const bool verifyReferences =
            verifyReferencesEnvironment != nullptr &&
            *verifyReferencesEnvironment != '\0' &&
            std::strcmp(verifyReferencesEnvironment, "0") != 0 &&
            std::strcmp(verifyReferencesEnvironment, "false") != 0;
        auto traceObject = [this, verifyReferences](ObjectHeader* header,
                                                    TraceVisitor& visitor) {
            auto* payload = static_cast<std::byte*>(payloadOf(header));
            for (const TypeDescriptor* type = header->type; type != nullptr;
                 type = type->super) {
                for (std::uint32_t index = 0;
                     index < type->referenceOffsetCount; ++index) {
                    const std::uint32_t offset = type->referenceOffsets[index];
                    if (offset + sizeof(RefSlot) > header->payloadBytes)
                        throw std::logic_error(
                            "type descriptor reference offset is invalid");
                    auto& slot = *reinterpret_cast<RefSlot*>(payload + offset);
                    // The Region container can grow while concurrent young
                    // marking is active.  Exact-reference verification walks
                    // that container only in a safepoint pause; concurrent
                    // workers still validate publication and the immutable
                    // source-cohort membership in their visitor.
                    if (verifyReferences &&
                        !impl_->youngMarkActive.load(
                            std::memory_order_acquire)) {
                        const std::uint64_t raw =
                            slot.load(std::memory_order_acquire);
                        void* target = Ref(raw).address();
                        bool exactManagedPayload = target == nullptr;
                        Impl::Region* targetRegion = nullptr;
                        ObjectHeader* containingTarget = nullptr;
                        if (target != nullptr) {
                            auto* address = static_cast<std::byte*>(target);
                            for (const auto& region : impl_->regions) {
                                if (address < region->memory ||
                                    address >= region->memory + region->bytes)
                                    continue;
                                targetRegion = region.get();
                                containingTarget =
                                    targetRegion->objectAtPayload(address);
                                exactManagedPayload =
                                    containingTarget != nullptr &&
                                    payloadOf(containingTarget) == target &&
                                    (containingTarget->flags.load(
                                         std::memory_order_acquire) &
                                     ObjectPublished) != 0;
                                break;
                            }
                        }
                        if (!exactManagedPayload) {
                            auto* ownerRegion = reinterpret_cast<Impl::Region*>(
                                static_cast<std::uintptr_t>(header->reserved0));
                            std::ostringstream message;
                            message << "invalid exact reference"
                                    << " owner_type="
                                    << (header->type != nullptr &&
                                                header->type->name != nullptr
                                            ? header->type->name
                                            : "<null>")
                                    << " owner_identity=" << header->identity
                                     << " descriptor_type="
                                     << (type->name != nullptr ? type->name
                                                              : "<null>")
                                     << " offset=" << offset
                                     << " owner_age=" << std::dec << header->age
                                     << " owner_mark_epoch="
                                     << header->markEpoch.load(
                                            std::memory_order_acquire)
                                     << " current_epoch=" << impl_->markEpoch
                                     << " owner_region=" << header->regionIndex
                                     << " owner_young="
                                     << (ownerRegion != nullptr &&
                                         ownerRegion->young.load(
                                             std::memory_order_acquire))
                                     << " owner_retired="
                                     << (ownerRegion != nullptr &&
                                         ownerRegion->retired)
                                     << " target_region="
                                     << (targetRegion != nullptr
                                             ? std::to_string(targetRegion->index)
                                             : std::string("none"))
                                     << " target_young="
                                     << (targetRegion != nullptr &&
                                         targetRegion->young.load(
                                             std::memory_order_acquire))
                                     << " target_retired="
                                     << (targetRegion != nullptr &&
                                         targetRegion->retired)
                                     << " target_containing_identity="
                                     << (containingTarget != nullptr
                                             ? containingTarget->identity : 0)
                                     << " target_containing_type="
                                     << (containingTarget != nullptr &&
                                                 containingTarget->type != nullptr &&
                                                 containingTarget->type->name != nullptr
                                             ? containingTarget->type->name
                                             : "<none>")
                                     << " target_exact_payload="
                                     << (containingTarget != nullptr &&
                                         payloadOf(containingTarget) == target)
                                     << " target_flags="
                                     << (containingTarget != nullptr
                                             ? containingTarget->flags.load(
                                                   std::memory_order_acquire)
                                             : 0)
                                     << " raw=0x" << std::hex << raw;
                            throw std::logic_error(message.str());
                        }
                    }
                    visitor.visit(slot);
                }
                if (type->trace != nullptr)
                    type->trace(payload, header->payloadBytes, visitor);
            }
        };

        for (const auto& thread : impl_->threads) {
            ++stats.threadRootSnapshots;
            stats.threadRootSlots += thread->rootSnapshot.size();
            for (const std::uint64_t raw : thread->rootSnapshot) {
                RefSlot snapshot{raw};
                ++stats.roots;
                marker.visit(snapshot);
            }
        }
        {
            std::scoped_lock staticLock(impl_->staticRootMutex);
            for (RefSlot* slot : impl_->staticRoots) {
                ++stats.roots;
                marker.visit(*slot);
            }
        }
        {
            std::scoped_lock handleLock(impl_->handleMutex);
            for (const auto& cell : impl_->handles) {
                if (cell->active && Impl::handleIsStrongRoot(
                        cell->kind, clearSoftReferences)) {
                    ++stats.roots;
                    marker.visit(cell->reference);
                }
            }
        }
        {
            std::scoped_lock finalizerLock(impl_->finalizerMutex);
            for (const auto& cell : impl_->finalizers) {
                if (cell->state == Impl::FinalizerState::Pending ||
                    cell->state == Impl::FinalizerState::Running) {
                    ++stats.roots;
                    marker.visit(cell->object);
                }
            }
        }

        // Native pins are address-bearing roots even when the transient
        // handle used to establish the pin has already been released.
        for (const auto& region : impl_->regions) {
            if (!region->young.load(std::memory_order_acquire) ||
                region->pinnedObjects.load(std::memory_order_acquire) == 0) {
                continue;
            }
            for (ObjectHeader* header : region->snapshotObjects()) {
                if (header->pinCount.load(std::memory_order_acquire) != 0) {
                    ++stats.roots;
                    marker.mark(payloadOf(header));
                }
            }
        }

        // Scan only the exact old-generation fields recorded by store
        // barriers. This avoids rescanning every field of a remembered owner
        // and gives the next concurrent-young implementation a stable bitmap-
        // equivalent snapshot.
        const char* verifyRememberedEnvironment =
            std::getenv("HXCPP_NOVAGC_VERIFY_REMEMBERED");
        const bool verifyRemembered = verifyRememberedEnvironment != nullptr &&
            *verifyRememberedEnvironment != '\0' &&
            std::strcmp(verifyRememberedEnvironment, "0") != 0 &&
            std::strcmp(verifyRememberedEnvironment, "false") != 0;
        (void)verifyRemembered;
        std::unordered_set<Impl::Region*> rememberedRegions;
        std::vector<std::pair<RefSlot*, ObjectHeader*>> validRememberedSlots;
        validRememberedSlots.reserve(scannedRememberedSlots.size());
        for (const auto& [slot, header] : scannedRememberedSlots) {
            if (slot == nullptr || header == nullptr)
                continue;
            auto* region = reinterpret_cast<Impl::Region*>(
                static_cast<std::uintptr_t>(header->reserved0));
            if (region == nullptr || region->retired ||
                region->young.load(std::memory_order_acquire)) {
                continue;
            }
            if ((header->flags.load(std::memory_order_acquire) &
                 ObjectPublished) == 0) {
                continue;
            }
            auto* payload = static_cast<std::byte*>(payloadOf(header));
            auto* slotAddress = reinterpret_cast<std::byte*>(slot);
            if (region->objectAtPayload(payload) != header ||
                slotAddress < payload ||
                slotAddress + sizeof(RefSlot) >
                    payload + header->payloadBytes) {
                // A moved owner's exact field must be rebuilt at its new
                // payload address. Retaining an old/out-of-owner slot makes
                // the next Young cycle dereference reclaimed Region storage.
                // Degrade this owner Region to the validated overflow scan
                // and discard the corrupt metadata entry itself.
                region->youngDirty.store(true, std::memory_order_release);
                region->youngOverflow.store(true,
                                            std::memory_order_release);
                continue;
            }
            validRememberedSlots.emplace_back(slot, header);
            if (rememberedRegions.insert(region).second) {
                scannedRememberedRegions.push_back(region);
                region->youngDirty.store(false, std::memory_order_release);
                ++stats.rememberedRegions;
            }
            marker.visit(*slot);
        }
        scannedRememberedSlots.swap(validRememberedSlots);

        auto recoverRememberedOverflow = [&](Impl::Region* ownerRegion) {
            if (ownerRegion == nullptr || impl_->lastFullMarkEpoch == 0)
                return false;

            std::uint64_t skippedStaleReferences = 0;
            class OverflowRememberVisitor final : public TraceVisitor {
            public:
                OverflowRememberVisitor(
                        Runtime& runtime, Impl& impl, TraceVisitor& marker,
                        std::vector<std::pair<RefSlot*, ObjectHeader*>>& slots,
                        std::uint64_t& skippedStaleReferences)
                    : runtime_(runtime), impl_(impl), marker_(marker),
                      slots_(slots),
                      skippedStaleReferences_(skippedStaleReferences) {}

                void setOwner(ObjectHeader* owner) noexcept { owner_ = owner; }

                void visit(RefSlot& slot) override {
                    const Ref reference(
                        slot.load(std::memory_order_acquire));
                    if (reference.isNull())
                        return;
                    void* candidate = reference.address();
                    ObjectHeader* candidateHeader = nullptr;
                    auto* candidateAddress =
                        static_cast<std::byte*>(candidate);
                    for (const auto& region : impl_.regions) {
                        if (candidateAddress < region->memory ||
                            candidateAddress >=
                                region->memory + region->cursor) {
                            continue;
                        }
                        candidateHeader =
                            region->objectAtPayload(candidateAddress);
                        break;
                    }
                    if (candidateHeader == nullptr ||
                        (candidateHeader->flags.load(
                             std::memory_order_acquire) &
                         ObjectPublished) == 0) {
                        // lastFullMarkEpoch proves that the owner was alive at
                        // the previous Full, not that it is alive now. A dead
                        // old owner can therefore retain a Ref into a nursery
                        // Region reclaimed by an intervening Young cycle.
                        // Never feed such an interior/reused address into the
                        // forwarding-pointer load barrier.
                        ++skippedStaleReferences_;
                        return;
                    }
                    void* target = runtime_.loadBarrier(slot);
                    if (target == nullptr)
                        return;
                    auto* targetRegion = reinterpret_cast<Impl::Region*>(
                        static_cast<std::uintptr_t>(
                            headerOf(target)->reserved0));
                    if (targetRegion == nullptr ||
                        !impl_.youngMarkRegions.contains(targetRegion)) {
                        return;
                    }
                    // Preserve the recovered edge for the next survivor
                    // cohort as well as using it for this cycle's marking.
                    slots_.emplace_back(&slot, owner_);
                    marker_.visit(slot);
                }

            private:
                Runtime& runtime_;
                Impl& impl_;
                TraceVisitor& marker_;
                std::vector<std::pair<RefSlot*, ObjectHeader*>>& slots_;
                std::uint64_t& skippedStaleReferences_;
                ObjectHeader* owner_ = nullptr;
            } visitor(*this, *impl_, marker, scannedRememberedSlots,
                      skippedStaleReferences);

            try {
                for (ObjectHeader* header : ownerRegion->snapshotObjects()) {
                    // A completed full mark stamps every then-live object with
                    // lastFullMarkEpoch. Objects born or promoted afterwards
                    // inherit that same epoch. Older headers are dead old-gen
                    // storage whose descriptor callbacks are not safe to run.
                    if ((header->flags.load(std::memory_order_acquire) &
                         ObjectPublished) == 0 ||
                        header->markEpoch.load(std::memory_order_acquire) !=
                            impl_->lastFullMarkEpoch) {
                        continue;
                    }
                    visitor.setOwner(header);
                    traceObject(header, visitor);
                }
                if (skippedStaleReferences != 0) {
                    std::fprintf(
                        stdout,
                        "novagc:young_overflow_stale_references_skipped "
                        "owner_region=%u references=%llu\n",
                        ownerRegion->index,
                        static_cast<unsigned long long>(
                            skippedStaleReferences));
                    std::fflush(stdout);
                }
                return true;
            } catch (const std::bad_alloc&) {
                return false;
            }
        };

        // A thread-local buffer/map allocation failure sets a per-region
        // overflow bit before the managed edge is published. An Old Region
        // may contain unreachable objects whose fields are intentionally no
        // longer valid. Once a full-mark liveness epoch exists, rescan only
        // objects proven live by that epoch (including later born/promoted
        // objects stamped with it) and rebuild exact slots. This avoids the
        // catastrophic whole-nursery promotion that used to follow a Full GC.
        // Before the first full mark, or if recovery metadata itself cannot be
        // allocated, retain the immutable cohort in place for correctness.
        for (const auto& region : impl_->regions) {
            if (region->isYoung() || !region->youngOverflow.exchange(
                    false, std::memory_order_acq_rel)) {
                continue;
            }
            ++stats.initialRememberedOverflowRegions;
            stats.metadataDegraded = true;
            if (rememberedRegions.insert(region.get()).second) {
                scannedRememberedRegions.push_back(region.get());
                ++stats.rememberedRegions;
            }
            if (recoverRememberedOverflow(region.get())) {
                ++stats.recoveredRememberedOverflowRegions;
            } else {
                forceInPlaceYoung = true;
                stats.forceInPlaceReasonMask |=
                    ForceInitialRememberedOverflow;
            }
            region->youngDirty.store(false, std::memory_order_release);
        }
        if (forceInPlaceYoung) {
            for (Impl::Region* region : impl_->youngMarkRegions) {
                for (ObjectHeader* header : region->snapshotObjects()) {
                    if ((header->flags.load(std::memory_order_acquire) &
                         ObjectPublished) == 0) {
                        continue;
                    }
                    if (region->tryMarkYoung(header)) {
                        ++stats.markedObjects;
                        stats.markedBytes += header->payloadBytes;
                    }
                }
            }
        }

        // Publish the initial young frontier to a queue shared with mutator
        // SATB/insertion barriers. The source Region set is immutable for this
        // cycle; TLAB invalidation above guarantees allocations after resume
        // enter fresh Eden Regions and are not accidentally collected.
        {
            std::scoped_lock youngMarkLock(impl_->youngMarkMutex);
            for (ObjectHeader* header : work)
                impl_->youngMarkQueue.push_back(header);
        }
        work.clear();
        impl_->concurrentYoungMarkedObjects.store(
            stats.markedObjects, std::memory_order_relaxed);
        impl_->concurrentYoungMarkedBytes.store(
            stats.markedBytes, std::memory_order_relaxed);
        rootFrameBarrierActive.store(true, std::memory_order_release);
        fastBarrierState.fetch_or(FastBarrierYoungActive,
                                  std::memory_order_acq_rel);
        impl_->youngMarkActive.store(true, std::memory_order_release);
        stats.concurrentMark = true;

        const std::uint32_t concurrentYoungWorkers =
            std::max(1U, impl_->configuredMarkWorkers());
        stats.traceWorkers = std::max(stats.traceWorkers,
                                      concurrentYoungWorkers);
        impl_->youngMarkerThreads.clear();
        impl_->youngMarkerThreads.reserve(concurrentYoungWorkers);
        for (std::uint32_t workerIndex = 0;
             workerIndex < concurrentYoungWorkers; ++workerIndex) {
            (void)workerIndex;
            impl_->youngMarkerThreads.emplace_back([this, &traceObject] {
                class ConcurrentYoungVisitor final : public TraceVisitor {
                public:
                    explicit ConcurrentYoungVisitor(Runtime& runtime)
                        : runtime_(runtime) {}
                    void visit(RefSlot& slot) override {
                        runtime_.impl_->enqueueYoungMark(
                            runtime_.loadBarrier(slot));
                    }
                private:
                    Runtime& runtime_;
                } visitor(*this);

                try {
                    for (;;) {
                        ObjectHeader* header = nullptr;
                        {
                            std::unique_lock markLock(
                                impl_->youngMarkMutex);
                            impl_->youngMarkCondition.wait(markLock, [this] {
                                return impl_->youngMarkFailure != nullptr ||
                                       !impl_->youngMarkQueue.empty() ||
                                       (impl_->youngMarkClosing &&
                                        impl_->activeYoungMarkWorkers == 0);
                            });
                            if (impl_->youngMarkFailure != nullptr)
                                return;
                            if (impl_->youngMarkQueue.empty()) {
                                if (impl_->youngMarkClosing &&
                                    impl_->activeYoungMarkWorkers == 0) {
                                    return;
                                }
                                continue;
                            }
                            header = impl_->youngMarkQueue.front();
                            impl_->youngMarkQueue.pop_front();
                            ++impl_->activeYoungMarkWorkers;
                        }
                        traceObject(header, visitor);
                        {
                            std::scoped_lock markLock(
                                impl_->youngMarkMutex);
                            --impl_->activeYoungMarkWorkers;
                            if (impl_->youngMarkQueue.empty() &&
                                impl_->activeYoungMarkWorkers == 0) {
                                impl_->youngMarkCondition.notify_all();
                            }
                        }
                    }
                } catch (...) {
                    std::scoped_lock markLock(impl_->youngMarkMutex);
                    if (impl_->youngMarkFailure == nullptr)
                        impl_->youngMarkFailure = std::current_exception();
                    impl_->youngMarkCondition.notify_all();
                }
            });
        }
        impl_->youngMarkCondition.notify_all();

        stats.initialPauseMicros = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - pauseStarted).count());
        publicationLock.unlock();
        threadLock.unlock();
        heapLock.unlock();
        impl_->resumeMutators();
        const auto concurrentYoungStarted =
            std::chrono::steady_clock::now();

        {
            std::unique_lock youngMarkLock(impl_->youngMarkMutex);
            impl_->youngMarkCondition.wait(youngMarkLock, [this] {
                return impl_->youngMarkFailure != nullptr ||
                       (impl_->youngMarkQueue.empty() &&
                        impl_->activeYoungMarkWorkers == 0);
            });
        }

        finalYoungPauseStarted = std::chrono::steady_clock::now();
        impl_->parkMutators(static_cast<Impl::ThreadState*>(currentThread));
        publicationLock.lock();
        {
            std::scoped_lock youngMarkLock(impl_->youngMarkMutex);
            impl_->youngMarkClosing = true;
            impl_->youngMarkCondition.notify_all();
        }
        for (std::thread& worker : impl_->youngMarkerThreads) {
            if (worker.joinable())
                worker.join();
        }
        impl_->youngMarkerThreads.clear();
        impl_->youngMarkActive.store(false, std::memory_order_release);
        fastBarrierState.fetch_and(~FastBarrierYoungActive,
                                   std::memory_order_acq_rel);
        rootFrameBarrierActive.store(false, std::memory_order_release);
        stats.concurrentMarkMicros = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                finalYoungPauseStarted - concurrentYoungStarted).count());
        if (impl_->youngMarkFailure != nullptr)
            std::rethrow_exception(impl_->youngMarkFailure);

        heapLock.lock();
        threadLock.lock();
        stats.markedObjects =
            impl_->concurrentYoungMarkedObjects.load(
                std::memory_order_relaxed);
        stats.markedBytes = impl_->concurrentYoungMarkedBytes.load(
            std::memory_order_relaxed);

        // Re-read static roots after every mutator has acknowledged the final
        // pause.  Lazy HX_ literals can be created after the initial snapshot;
        // their registration barrier normally feeds the concurrent queue, but
        // this final remark is the authoritative closure before any source
        // Region is selected for evacuation and reclamation.
        {
            std::scoped_lock staticLock(impl_->staticRootMutex);
            for (RefSlot* slot : impl_->staticRoots)
                marker.visit(*slot);
        }
        if (impl_->youngMarkMetadataOverflow.exchange(
                false, std::memory_order_acq_rel)) {
            // Queue allocation failed after one or more mark bits were set.
            // Retain the complete source cohort without tracing dead objects.
            // All source addresses remain valid, so children of an object
            // omitted from the queue cannot become dangling.
            stats.metadataDegraded = true;
            stats.youngMarkMetadataOverflow = true;
            stats.forceInPlaceReasonMask |=
                ForceYoungMarkMetadataOverflow;
            forceInPlaceYoung = true;
            for (Impl::Region* region : impl_->youngMarkRegions) {
                for (ObjectHeader* header : region->snapshotObjects()) {
                    if ((header->flags.load(std::memory_order_acquire) &
                         ObjectPublished) == 0) {
                        continue;
                    }
                    if (region->tryMarkYoung(header)) {
                        ++stats.markedObjects;
                        stats.markedBytes += header->payloadBytes;
                    }
                }
            }
        }

        // Mutators populated the active remembered epoch while concurrent
        // tracing ran. If that metadata overflowed after the initial pause,
        // apply the same no-move fallback before selecting evacuation sets.
        bool finalRememberedOverflow = false;
        for (const auto& region : impl_->regions) {
            if (region->isYoung() || !region->youngOverflow.exchange(
                    false, std::memory_order_acq_rel)) {
                continue;
            }
            ++stats.finalRememberedOverflowRegions;
            stats.metadataDegraded = true;
            if (rememberedRegions.insert(region.get()).second) {
                scannedRememberedRegions.push_back(region.get());
                ++stats.rememberedRegions;
            }
            if (recoverRememberedOverflow(region.get())) {
                ++stats.recoveredRememberedOverflowRegions;
            } else {
                finalRememberedOverflow = true;
                stats.forceInPlaceReasonMask |=
                    ForceFinalRememberedOverflow;
            }
            region->youngDirty.store(false, std::memory_order_release);
        }
        if (finalRememberedOverflow) {
            forceInPlaceYoung = true;
            for (Impl::Region* region : impl_->youngMarkRegions) {
                for (ObjectHeader* header : region->snapshotObjects()) {
                    if ((header->flags.load(std::memory_order_acquire) &
                         ObjectPublished) == 0) {
                        continue;
                    }
                    if (region->tryMarkYoung(header)) {
                        ++stats.markedObjects;
                        stats.markedBytes += header->payloadBytes;
                    }
                }
            }
        }

        auto drainWork = [&] {
            if (work.empty())
                return;
            const std::uint32_t workerCount =
                std::max(1U, impl_->configuredMarkWorkers());
            stats.traceWorkers = std::max(stats.traceWorkers, workerCount);
            if (workerCount == 1U) {
                while (!work.empty()) {
                    ObjectHeader* header = work.back();
                    work.pop_back();
                    traceObject(header, marker);
                }
                return;
            }

            struct ParallelState final {
                std::mutex mutex;
                std::condition_variable condition;
                std::deque<ObjectHeader*> queue;
                std::size_t outstanding = 0;
                std::exception_ptr failure;
            } parallel;
            for (ObjectHeader* header : work)
                parallel.queue.push_back(header);
            parallel.outstanding = parallel.queue.size();
            work.clear();

            struct WorkerTotals final {
                std::uint64_t objects = 0;
                std::uint64_t bytes = 0;
            };
            std::vector<WorkerTotals> totals(workerCount);
            std::vector<std::thread> workers;
            workers.reserve(workerCount);
            for (std::uint32_t workerIndex = 0;
                 workerIndex < workerCount; ++workerIndex) {
                workers.emplace_back([&, workerIndex] {
                    class ParallelVisitor final : public TraceVisitor {
                    public:
                        ParallelVisitor(Runtime& runtime,
                                        ParallelState& parallel,
                                        WorkerTotals& totals)
                            : runtime_(runtime), parallel_(parallel),
                              totals_(totals) {}

                        void visit(RefSlot& slot) override {
                            void* object = runtime_.loadBarrier(slot);
                            if (object == nullptr)
                                return;
                            auto* header = headerOf(object);
                            auto* region = reinterpret_cast<Impl::Region*>(
                                static_cast<std::uintptr_t>(
                                    header->reserved0));
                            if (region == nullptr ||
                                !runtime_.impl_->youngMarkRegions.contains(
                                    region))
                                return;
                            if ((header->flags.load(
                                     std::memory_order_acquire) &
                                 ObjectPublished) == 0) {
                                throw std::logic_error(
                                    "young worker reached unpublished object");
                            }
                            if (region->tryMarkYoung(header)) {
                                    std::scoped_lock lock(parallel_.mutex);
                                    parallel_.queue.push_back(header);
                                    ++parallel_.outstanding;
                                    ++totals_.objects;
                                    totals_.bytes += header->payloadBytes;
                                    parallel_.condition.notify_one();
                            }
                        }

                    private:
                        Runtime& runtime_;
                        ParallelState& parallel_;
                        WorkerTotals& totals_;
                    } visitor(*this, parallel, totals[workerIndex]);

                    try {
                        for (;;) {
                            ObjectHeader* header = nullptr;
                            {
                                std::unique_lock lock(parallel.mutex);
                                parallel.condition.wait(lock, [&] {
                                    return parallel.failure != nullptr ||
                                           !parallel.queue.empty() ||
                                           parallel.outstanding == 0;
                                });
                                if (parallel.failure != nullptr ||
                                    parallel.outstanding == 0) {
                                    return;
                                }
                                header = parallel.queue.front();
                                parallel.queue.pop_front();
                            }
                            traceObject(header, visitor);
                            {
                                std::scoped_lock lock(parallel.mutex);
                                --parallel.outstanding;
                                if (parallel.outstanding == 0)
                                    parallel.condition.notify_all();
                            }
                        }
                    } catch (...) {
                        std::scoped_lock lock(parallel.mutex);
                        if (parallel.failure == nullptr)
                            parallel.failure = std::current_exception();
                        parallel.condition.notify_all();
                    }
                });
            }
            parallel.condition.notify_all();
            for (std::thread& worker : workers)
                worker.join();
            if (parallel.failure != nullptr)
                std::rethrow_exception(parallel.failure);
            for (const WorkerTotals& total : totals) {
                stats.markedObjects += total.objects;
                stats.markedBytes += total.bytes;
            }
        };
        drainWork();

        const char* verifyAllYoungEnvironment =
            std::getenv("HXCPP_NOVAGC_VERIFY_ALL_YOUNG");
        const bool verifyAllYoung = verifyAllYoungEnvironment != nullptr &&
            *verifyAllYoungEnvironment != '\0' &&
            std::strcmp(verifyAllYoungEnvironment, "0") != 0 &&
            std::strcmp(verifyAllYoungEnvironment, "false") != 0;
        if (verifyAllYoung) {
            for (const auto& region : impl_->regions) {
                if (!impl_->youngMarkRegions.contains(region.get()))
                    continue;
                for (ObjectHeader* header : region->snapshotObjects()) {
                    if ((header->flags.load(std::memory_order_acquire) &
                         ObjectPublished) != 0) {
                        marker.mark(payloadOf(header));
                    }
                }
            }
            drainWork();
        }

        auto processEphemerons = [&] {
            for (;;) {
                const std::uint64_t before = stats.markedObjects;
                {
                    std::scoped_lock ephemeronLock(impl_->ephemeronMutex);
                    for (const auto& cell : impl_->ephemerons) {
                        if (!cell->active)
                            continue;
                        void* key = loadBarrier(cell->key);
                        if (key == nullptr)
                            continue;
                        ObjectHeader* keyHeader = headerOf(key);
                        auto* keyRegion = reinterpret_cast<Impl::Region*>(
                            static_cast<std::uintptr_t>(keyHeader->reserved0));
                        const bool keyIsYoung = keyRegion != nullptr &&
                            impl_->youngMarkRegions.contains(keyRegion);
                        if (!keyIsYoung || keyRegion->isYoungMarked(keyHeader)) {
                            marker.visit(cell->value);
                        }
                    }
                }
                drainWork();
                if (stats.markedObjects == before)
                    break;
            }
        };
        processEphemerons();

        {
            std::scoped_lock finalizerLock(impl_->finalizerMutex);
            for (const auto& cell : impl_->finalizers) {
                if (cell->state != Impl::FinalizerState::Registered)
                    continue;
                void* object = loadBarrier(cell->object);
                if (object == nullptr)
                    continue;
                ObjectHeader* header = headerOf(object);
                auto* region = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(header->reserved0));
                if (region != nullptr &&
                    impl_->youngMarkRegions.contains(region) &&
                    !region->isYoungMarked(header)) {
                    cell->state = Impl::FinalizerState::Pending;
                    marker.visit(cell->object);
                }
            }
        }
        drainWork();
        processEphemerons();

        {
            std::scoped_lock ephemeronLock(impl_->ephemeronMutex);
            for (const auto& cell : impl_->ephemerons) {
                if (!cell->active)
                    continue;
                void* key = loadBarrier(cell->key);
                if (key == nullptr)
                    continue;
                ObjectHeader* header = headerOf(key);
                auto* region = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(header->reserved0));
                if (region != nullptr &&
                    impl_->youngMarkRegions.contains(region) &&
                    !region->isYoungMarked(header)) {
                    cell->key.store(0, std::memory_order_release);
                    cell->value.store(0, std::memory_order_release);
                } else {
                    (void)loadBarrier(cell->value);
                }
            }
        }
        {
            std::scoped_lock handleLock(impl_->handleMutex);
            for (std::uint32_t index = 0;
                 index < impl_->handles.size(); ++index) {
                auto& cell = impl_->handles[index];
                if (!cell->active || !Impl::handleNeedsClearing(
                        cell->kind, clearSoftReferences))
                    continue;
                void* object = loadBarrier(cell->reference);
                if (object == nullptr)
                    continue;
                ObjectHeader* header = headerOf(object);
                auto* region = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(header->reserved0));
                if (region != nullptr &&
                    impl_->youngMarkRegions.contains(region) &&
                    !region->isYoungMarked(header)) {
                    impl_->enqueueReference(index, *cell);
                }
            }
        }
        {
            std::scoped_lock finalizerLock(impl_->finalizerMutex);
            for (const auto& cell : impl_->finalizers) {
                if (cell->state != Impl::FinalizerState::Finalized)
                    continue;
                void* object = loadBarrier(cell->object);
                if (object == nullptr)
                    continue;
                ObjectHeader* header = headerOf(object);
                auto* region = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(header->reserved0));
                if (region != nullptr &&
                    impl_->youngMarkRegions.contains(region) &&
                    !region->isYoungMarked(header)) {
                    cell->object.store(0, std::memory_order_release);
                    cell->state = Impl::FinalizerState::Cleared;
                }
            }
            std::erase_if(impl_->finalizers, [](const auto& cell) {
                return cell->state == Impl::FinalizerState::Cleared;
            });
        }

        stats.traceMicros = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - phaseStarted).count());
        phaseStarted = std::chrono::steady_clock::now();

        struct YoungSource final {
            Impl::Region* region = nullptr;
            std::vector<ObjectHeader*> live;
            std::vector<Impl::Region*> evacuationPlan;
            bool pinned = false;
            bool addressPinned = false;
        };
        const char* noMoveYoungEnvironment =
            std::getenv("HXCPP_NOVAGC_YOUNG_NO_MOVE");
        const bool noMoveYoung = noMoveYoungEnvironment != nullptr &&
            *noMoveYoungEnvironment != '\0' &&
            std::strcmp(noMoveYoungEnvironment, "0") != 0 &&
            std::strcmp(noMoveYoungEnvironment, "false") != 0;
        stats.noMoveRequested = noMoveYoung;
        std::vector<YoungSource> sources;
        for (const auto& region : impl_->regions) {
            // Mutators allocate into fresh Eden regions during concurrent
            // tracing.  Only the immutable cohort captured in the initial
            // pause belongs to this collection.
            if (!impl_->youngMarkRegions.contains(region.get()))
                continue;
            YoungSource source{};
            source.region = region.get();
            source.pinned = transientPinnedRegions.contains(region.get());
            source.addressPinned = source.pinned;
            for (ObjectHeader* header : region->snapshotObjects()) {
                if (region->isYoungMarked(header)) {
                    source.live.push_back(header);
                    const bool objectPinned =
                        header->pinCount.load(std::memory_order_acquire) != 0;
                    source.pinned = source.pinned || objectPinned;
                    source.addressPinned = source.addressPinned || objectPinned;
                } else {
                    ++stats.deadObjects;
                }
            }
            if (source.pinned && !source.live.empty()) {
                ++stats.pinnedSourceRegions;
                stats.forceInPlaceReasonMask |= ForcePinnedSource;
            }
            // Diagnostic counterpart to stock hxcpp's conservative stack and
            // register pinning: retain the address of every region containing
            // a survivor while still reclaiming completely-dead nursery
            // regions.  This separates relocation correctness from tracing
            // correctness without disabling young collections altogether.
            if (forceInPlaceYoung && !source.pinned &&
                !source.live.empty()) {
                ++stats.inPlacePromotedRegions;
            }
            source.pinned = source.pinned ||
                ((forceInPlaceYoung || noMoveYoung) &&
                 !source.live.empty());
            if (noMoveYoung && !source.live.empty())
                stats.forceInPlaceReasonMask |= ForceNoMoveRequested;
            sources.push_back(std::move(source));
        }
        stats.forcedInPlace = forceInPlaceYoung;

        stats.tenuringThreshold = std::clamp<std::uint16_t>(
            impl_->currentTenuringThreshold, 1U,
            impl_->config.maximumTenuringThreshold);
        std::vector<Impl::Region*> survivorDestinations;
        std::vector<Impl::Region*> promotionDestinations;
        std::vector<std::pair<ObjectHeader*, Impl::Region*>> promotedCopies;
        std::unordered_set<Impl::Region*> evacuatedSources;
        auto evacuateObject = [&](ObjectHeader* oldHeader,
                                  Impl::Region* sourceRegion,
                                  Impl::Region* destination) {
            const std::uint16_t newAge = oldHeader->age == UINT16_MAX
                ? UINT16_MAX
                : static_cast<std::uint16_t>(oldHeader->age + 1U);
            const bool promote = newAge >= stats.tenuringThreshold;
            if (destination == nullptr) {
                throw std::logic_error(
                    "young evacuation has no preplanned destination");
            }
            if (destination->committed < destination->cursor +
                                             oldHeader->totalBytes) {
                std::ostringstream message;
                message << "young evacuation exceeded committed high-water"
                        << " committed=" << destination->committed
                        << " cursor=" << destination->cursor
                        << " object=" << oldHeader->totalBytes
                        << " region=" << destination->bytes
                        << " age=" << oldHeader->age
                        << " threshold=" << stats.tenuringThreshold;
                throw std::logic_error(message.str());
            }

            auto* newHeader = reinterpret_cast<ObjectHeader*>(
                destination->memory + destination->cursor);
            destination->cursor += oldHeader->totalBytes;
            ::new (static_cast<void*>(newHeader)) ObjectHeader();
            newHeader->type = oldHeader->type;
            newHeader->totalBytes = oldHeader->totalBytes;
            newHeader->payloadBytes = oldHeader->payloadBytes;
            newHeader->markEpoch.store(
                oldHeader->markEpoch.load(std::memory_order_acquire),
                std::memory_order_release);
            newHeader->identity = oldHeader->identity;
            newHeader->flags.store(
                oldHeader->flags.load(std::memory_order_acquire),
                std::memory_order_release);
            newHeader->age = newAge;
            newHeader->regionIndex = destination->index;
            newHeader->reserved0 = static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(destination));
            newHeader->pinCount.store(0, std::memory_order_release);
            std::memcpy(payloadOf(newHeader), payloadOf(oldHeader),
                        oldHeader->payloadBytes);
            destination->recordObject(newHeader);
            oldHeader->state.store(ObjectRelocating,
                                   std::memory_order_release);
            oldHeader->forwarding.store(
                Ref::encode(payloadOf(newHeader), View::Remapped).bits(),
                std::memory_order_release);
            oldHeader->state.store(ObjectRelocated,
                                   std::memory_order_release);
            if (promote) {
                ++stats.promotedObjects;
                stats.promotedBytes += oldHeader->totalBytes;
                promotedCopies.emplace_back(newHeader, sourceRegion);
            } else {
                ++stats.survivorObjects;
                stats.survivorBytes += oldHeader->totalBytes;
            }
            return newHeader;
        };

        auto reserveEvacuation = [&](YoungSource& source) {
            try {
                // All operations that can reserve bookkeeping storage or
                // commit physical pages run before this source publishes its
                // first forwarding pointer. A failure can therefore promote
                // the complete source in place without rolling back a
                // partially copied object graph.
                impl_->regions.reserve(impl_->regions.size() +
                                       source.live.size());
                survivorDestinations.reserve(
                    survivorDestinations.size() + source.live.size());
                promotionDestinations.reserve(
                    promotionDestinations.size() + source.live.size());
                source.evacuationPlan.assign(source.live.size(), nullptr);

                auto reserveClass = [&](auto& destinations,
                                        Impl::RegionKind kind,
                                        bool promotionClass,
                                        std::uint64_t& regionCounter) {
                    std::vector<std::pair<Impl::Region*, std::size_t>> plan;
                    plan.reserve(destinations.size() + source.live.size());
                    for (Impl::Region* region : destinations)
                        plan.emplace_back(region, region->cursor);

                    for (std::size_t objectIndex = 0;
                         objectIndex < source.live.size(); ++objectIndex) {
                        ObjectHeader* header = source.live[objectIndex];
                        const std::uint16_t newAge = header->age == UINT16_MAX
                            ? UINT16_MAX
                            : static_cast<std::uint16_t>(header->age + 1U);
                        if ((newAge >= stats.tenuringThreshold) !=
                            promotionClass) {
                            continue;
                        }

                        auto selected = plan.rend();
                        for (auto iterator = plan.rbegin();
                             iterator != plan.rend(); ++iterator) {
                            if (iterator->first->bytes - iterator->second >=
                                header->totalBytes) {
                                selected = iterator;
                                break;
                            }
                        }
                        if (selected == plan.rend()) {
                            auto created = impl_->acquireRegion(
                                std::max<std::size_t>(
                                    impl_->config.regionBytes,
                                    header->totalBytes),
                                kind);
                            Impl::Region* destination = created.get();
                            impl_->regions.push_back(std::move(created));
                            destinations.push_back(destination);
                            plan.emplace_back(destination, 0U);
                            selected = plan.rbegin();
                            ++regionCounter;
                        }
                        source.evacuationPlan[objectIndex] =
                            selected->first;
                        selected->second += header->totalBytes;
                    }

                    // Commit only the planned high-water mark. Subsequent
                    // copying performs no OS allocation and is thus atomic at
                    // source-region granularity.
                    for (const auto& [region, highWater] : plan)
                        region->ensureCommitted(highWater);
                };

                reserveClass(survivorDestinations,
                             Impl::RegionKind::Survivor, false,
                             stats.survivorRegions);
                reserveClass(promotionDestinations,
                             Impl::RegionKind::Old, true,
                             stats.promotedRegions);
                if (std::ranges::any_of(source.evacuationPlan,
                        [](Impl::Region* destination) {
                            return destination == nullptr;
                        })) {
                    throw std::logic_error(
                        "young evacuation plan omitted a live object");
                }
                return true;
            } catch (const std::bad_alloc&) {
                return false;
            }
        };

        for (YoungSource& source : sources) {
            if (!source.pinned && !reserveEvacuation(source)) {
                // Promotion failure must not begin a partial copy.  Retain
                // this complete source as old in place, sacrificing
                // compaction but preserving every exact address and edge.
                source.pinned = true;
                ++stats.inPlacePromotedRegions;
                ++stats.evacuationFallbackRegions;
                stats.forceInPlaceReasonMask |=
                    ForceEvacuationReservation;
            }
            if (source.pinned) {
                source.region->setKind(source.addressPinned
                    ? Impl::RegionKind::Pinned
                    : Impl::RegionKind::Old);
                source.region->youngDirty.store(false,
                                                std::memory_order_release);
                ++stats.promotedRegions;
                stats.promotedObjects += source.live.size();
                for (ObjectHeader* header : source.live)
                    stats.promotedBytes += header->totalBytes;
                for (ObjectHeader* header : source.live) {
                    if (header->age != UINT16_MAX)
                        ++header->age;
                }
                continue;
            }
            evacuatedSources.insert(source.region);
            stats.reclaimedBytes += source.region->cursor;
            ++stats.reclaimedRegions;
            for (std::size_t objectIndex = 0;
                 objectIndex < source.live.size(); ++objectIndex) {
                (void)evacuateObject(source.live[objectIndex], source.region,
                                     source.evacuationPlan[objectIndex]);
            }
        }

        if (forceInPlaceYoung) {
            // An exact remembered edge may also be born from a source object
            // into an Eden Region allocated during concurrent marking. Since
            // the overflowing source metadata cannot prove which object owns
            // that edge, promote the fresh cohort in place as well. The whole
            // young generation then becomes non-moving for this exceptional
            // cycle and no unknown slot requires healing or reconstruction.
            for (const auto& region : impl_->regions) {
                if (!region->isYoung() ||
                    impl_->youngMarkRegions.contains(region.get())) {
                    continue;
                }
                region->setKind(Impl::RegionKind::Old);
                region->youngDirty.store(false, std::memory_order_release);
                ++stats.promotedRegions;
                for (ObjectHeader* header : region->snapshotObjects()) {
                    if ((header->flags.load(std::memory_order_acquire) &
                         ObjectPublished) == 0) {
                        continue;
                    }
                    if (header->age != UINT16_MAX)
                        ++header->age;
                    ++stats.promotedObjects;
                    stats.promotedBytes += header->totalBytes;
                }
            }
        }

        std::uint16_t nextThreshold = stats.tenuringThreshold;
        if (impl_->config.adaptiveTenuring) {
            std::uint64_t youngCapacity = 0;
            for (const YoungSource& source : sources)
                youngCapacity += source.region->bytes;
            const std::uint64_t target = std::max<std::uint64_t>(
                impl_->config.regionBytes,
                youngCapacity * impl_->config.survivorTargetPercent / 100U);
            if (stats.survivorBytes > target && nextThreshold > 1U) {
                --nextThreshold;
            } else if (stats.survivorBytes < target / 2U &&
                       nextThreshold <
                           impl_->config.maximumTenuringThreshold) {
                ++nextThreshold;
            }
        }
        impl_->currentTenuringThreshold = nextThreshold;
        stats.nextTenuringThreshold = nextThreshold;

        stats.evacuateMicros = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - phaseStarted).count());
        phaseStarted = std::chrono::steady_clock::now();

        publishFastBarrierView(impl_->view, View::Remapped);
        class YoungRemapper final : public TraceVisitor {
        public:
            explicit YoungRemapper(Runtime& runtime) : runtime_(runtime) {}
            void visit(RefSlot& slot) override {
                (void)runtime_.loadBarrier(slot);
            }
        private:
            Runtime& runtime_;
        } remapper(*this);

        for (const auto& thread : impl_->threads) {
            for (RootFrame* frame = thread->roots; frame != nullptr;
                 frame = frame->previous) {
                for (std::uint32_t index = 0; index < frame->slotCount;
                     ++index) {
                    remapper.visit(*frame->slots[index]);
                }
            }
        }
        {
            std::scoped_lock staticLock(impl_->staticRootMutex);
            for (RefSlot* slot : impl_->staticRoots)
                remapper.visit(*slot);
        }
        {
            std::scoped_lock handleLock(impl_->handleMutex);
            for (const auto& cell : impl_->handles) {
                if (cell->active)
                    remapper.visit(cell->reference);
            }
        }
        {
            std::scoped_lock ephemeronLock(impl_->ephemeronMutex);
            for (const auto& cell : impl_->ephemerons) {
                if (cell->active) {
                    remapper.visit(cell->key);
                    remapper.visit(cell->value);
                }
            }
        }
        {
            std::scoped_lock finalizerLock(impl_->finalizerMutex);
            for (const auto& cell : impl_->finalizers) {
                if (cell->state != Impl::FinalizerState::Cleared)
                    remapper.visit(cell->object);
            }
        }

        // Heal only slots whose owners are proven live for this young cycle.
        // Scanning every object in an Old mixed-live Region is incorrect: an
        // unreachable old object may legally retain a stale field after its
        // target Region was reclaimed.  Old-to-source edges are represented
        // by the detached snapshot or by the active remembered set populated
        // during concurrent marking.  New Eden objects and evacuation copies
        // are also safe to scan because they were allocated by this cycle and
        // have not yet participated in a collection.
        if (!forceInPlaceYoung) {
            for (const auto& [slot, header] : scannedRememberedSlots) {
                auto* ownerRegion = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(header->reserved0));
                if (ownerRegion != nullptr && !ownerRegion->retired)
                    remapper.visit(*slot);
            }
            {
                std::scoped_lock rememberedLock(impl_->rememberedMutex);
                for (const auto& [slot, header] : impl_->rememberedSlots) {
                    auto* ownerRegion = reinterpret_cast<Impl::Region*>(
                        static_cast<std::uintptr_t>(header->reserved0));
                    if (ownerRegion != nullptr && !ownerRegion->retired)
                        remapper.visit(*slot);
                }
            }
            for (Impl::Region* region : survivorDestinations) {
                for (ObjectHeader* header : region->snapshotObjects()) {
                    if ((header->flags.load(std::memory_order_acquire) &
                         ObjectPublished) != 0)
                        traceObject(header, remapper);
                }
            }
            for (const auto& [header, sourceRegion] : promotedCopies) {
                (void)sourceRegion;
                traceObject(header, remapper);
            }
            for (const auto& region : impl_->regions) {
                if (!region->isYoung() ||
                    impl_->youngMarkRegions.contains(region.get()) ||
                    std::ranges::find(survivorDestinations, region.get()) !=
                        survivorDestinations.end()) {
                    continue;
                }
                for (ObjectHeader* header : region->snapshotObjects()) {
                    if ((header->flags.load(std::memory_order_acquire) &
                         ObjectPublished) != 0)
                        traceObject(header, remapper);
                }
            }
        }
        for (const YoungSource& source : sources) {
            if (!source.pinned)
                continue;
            if (!forceInPlaceYoung) {
                for (ObjectHeader* header : source.live)
                    traceObject(header, remapper);
            }
            source.region->youngCollectionSource = false;
        }

        // Copying can create fresh old-to-survivor edges without executing a
        // mutator store.  Rebuild exact remembered slots only after every
        // copied field has been remapped to its final destination.
        if (!forceInPlaceYoung) {
            for (const auto& [header, sourceRegion] : promotedCopies)
                impl_->rememberRelocatedOwner(header, sourceRegion);
            for (const YoungSource& source : sources) {
                if (!source.pinned)
                    continue;
                for (ObjectHeader* header : source.live)
                    impl_->rememberRelocatedOwner(header, source.region);
            }
        }

        // Old-to-survivor edges that existed before this collection remain
        // relevant for the next survivor cohort.  The old implementation
        // could discard the complete snapshot only because it promoted every
        // survivor immediately.
        {
            std::scoped_lock rememberedLock(impl_->rememberedMutex);
            for (const auto& [slot, header] : scannedRememberedSlots) {
                const Ref value(slot->load(std::memory_order_acquire));
                if (value.isNull())
                    continue;
                auto* targetRegion = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(
                        headerOf(value.address())->reserved0));
                if (targetRegion == nullptr || !targetRegion->isYoung())
                    continue;
                auto* ownerRegion = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(header->reserved0));
                if (ownerRegion != nullptr &&
                    ownerRegion->rememberSlot(
                        slot, impl_->activeRememberedEpoch.load(
                                  std::memory_order_acquire)) ==
                        Impl::Region::RememberResult::Overflow) {
                    ownerRegion->youngOverflow.store(
                        true, std::memory_order_release);
                }
                try {
                    impl_->rememberedSlots.insert_or_assign(slot, header);
                } catch (...) {
                    // Forwarding and field remapping have already committed.
                    // A metadata OOM must therefore degrade to the exact
                    // region scan, not abort across the completed copy.
                    if (ownerRegion != nullptr)
                        ownerRegion->youngOverflow.store(
                            true, std::memory_order_release);
                    continue;
                }
                if (ownerRegion != nullptr) {
                    ownerRegion->youngDirty.store(true,
                                                  std::memory_order_release);
                }
            }
        }

        const char* quarantineEnvironment =
            std::getenv("HXCPP_NOVAGC_QUARANTINE_YOUNG");
        const bool quarantineYoung = quarantineEnvironment != nullptr &&
            *quarantineEnvironment != '\0' &&
            std::strcmp(quarantineEnvironment, "0") != 0 &&
            std::strcmp(quarantineEnvironment, "false") != 0;
        for (auto region = impl_->regions.begin();
             region != impl_->regions.end();) {
            if (!evacuatedSources.contains(region->get())) {
                ++region;
                continue;
            }
            if (quarantineYoung) {
                (*region)->setKind(Impl::RegionKind::Old);
                (*region)->youngDirty.store(false,
                                            std::memory_order_release);
                (*region)->retired = true;
                (*region)->retiredEpoch = impl_->markEpoch;
                ++region;
                continue;
            }
            pendingReclamation.push_back(std::move(*region));
            region = impl_->regions.erase(region);
        }
        for (const auto& region : impl_->regions)
            region->clearRememberedBitmap(scannedRememberedEpoch);
        {
            std::scoped_lock youngMarkLock(impl_->youngMarkMutex);
            for (Impl::Region* region : impl_->youngMarkRegions)
                region->youngCollectionSource = false;
            impl_->youngMarkRegions.clear();
            impl_->youngMarkQueue.clear();
            impl_->youngMarkClosing = false;
        }
        stats.remapMicros = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - phaseStarted).count());
        }

        // CFFI native libraries such as Lime keep weak identity tokens in
        // native maps.  Stock hxcpp discovers and invokes their finalizers in
        // one stop-the-world transaction.  Resuming mutators between those
        // steps allows a native call to republish a pending token and then
        // race its already-queued callback, leaving a live wrapper with its
        // native state cleared.  Commit the pending batch before releasing
        // this safepoint handshake.  Heap locks are deliberately released so
        // callbacks may allocate and publish exact roots.
        const auto finalizerStarted = std::chrono::steady_clock::now();
        stats.finalizers = drainFinalizers();
        stats.finalizerMicros = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - finalizerStarted).count());
    } catch (...) {
        impl_->youngMarkActive.store(false, std::memory_order_release);
        fastBarrierState.fetch_and(~FastBarrierYoungActive,
                                   std::memory_order_acq_rel);
        rootFrameBarrierActive.store(false, std::memory_order_release);
        {
            std::scoped_lock youngMarkLock(impl_->youngMarkMutex);
            impl_->youngMarkClosing = true;
            impl_->youngMarkCondition.notify_all();
        }
        for (std::thread& worker : impl_->youngMarkerThreads) {
            if (worker.joinable())
                worker.join();
        }
        impl_->youngMarkerThreads.clear();
        {
            std::scoped_lock youngMarkLock(impl_->youngMarkMutex);
            for (Impl::Region* region : impl_->youngMarkRegions)
                region->youngCollectionSource = false;
            impl_->youngMarkRegions.clear();
            impl_->youngMarkQueue.clear();
            impl_->youngMarkClosing = false;
        }
        for (Impl::Region* region : scannedRememberedRegions) {
            region->youngDirty.store(true, std::memory_order_release);
            // The overflow scan may itself have failed while extending its
            // temporary exact-slot vector. Retain the coarse recovery bit so
            // the next cycle cannot silently forget the unscanned suffix.
            region->youngOverflow.store(true, std::memory_order_release);
        }
        {
            std::scoped_lock rememberedLock(impl_->rememberedMutex);
            const std::uint8_t activeEpoch =
                impl_->activeRememberedEpoch.load(
                    std::memory_order_acquire);
            for (const auto& [slot, header] : scannedRememberedSlots) {
                auto* ownerRegion = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(header->reserved0));
                if (ownerRegion != nullptr &&
                    ownerRegion->rememberSlot(slot, activeEpoch) ==
                        Impl::Region::RememberResult::Overflow) {
                    ownerRegion->youngOverflow.store(
                        true, std::memory_order_release);
                }
                try {
                    impl_->rememberedSlots.insert_or_assign(slot, header);
                } catch (...) {
                    if (ownerRegion != nullptr)
                        ownerRegion->youngOverflow.store(
                            true, std::memory_order_release);
                }
            }
            impl_->rememberedSnapshot.clear();
        }
        {
            std::scoped_lock heapLock(impl_->heapMutex);
            for (const auto& region : impl_->regions)
                region->clearRememberedBitmap(scannedRememberedEpoch);
        }
        scopedPinBarrierActive.store(false, std::memory_order_release);
        impl_->resumeMutators();
        throw;
    }
    scopedPinBarrierActive.store(false, std::memory_order_release);
    impl_->resumeMutators();
    {
        std::scoped_lock rememberedLock(impl_->rememberedMutex);
        impl_->rememberedSnapshot.clear();
    }
    stats.finalPauseMicros = finalYoungPauseStarted ==
            std::chrono::steady_clock::time_point{}
        ? 0
        : static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() -
                finalYoungPauseStarted).count());
    stats.pauseMicros = stats.initialPauseMicros +
                        stats.finalPauseMicros;
    stats.actOnceRememberedStores =
        impl_->actOnceRememberedStores.load(std::memory_order_relaxed) -
        impl_->lastReportedActOnceRememberedStores;
    impl_->lastReportedActOnceRememberedStores +=
        stats.actOnceRememberedStores;
    // Type telemetry used to cover only full collections.  That hid the
    // overwhelmingly common case in interactive Haxe programs: hundreds of
    // thousands of frame-temporary objects die in nursery regions long before
    // a full mark.  Census evacuated source regions after mutators resume so
    // diagnostics never lengthen the stop-the-world pause.
    if (deadTypeCensusEnabled) {
        struct TypeTotals final {
            std::uint64_t objects = 0;
            std::uint64_t bytes = 0;
        };
        std::unordered_map<const TypeDescriptor*, TypeTotals> totals;
        for (const auto& region : pendingReclamation) {
            for (ObjectHeader* header : region->snapshotObjects()) {
                // Live objects have already been copied to promotion space;
                // every object in an evacuated source is now reclaimable.
                auto& total = totals[header->type];
                ++total.objects;
                total.bytes += header->totalBytes;
            }
        }
        std::vector<std::pair<const TypeDescriptor*, TypeTotals>> ordered(
            totals.begin(), totals.end());
        std::sort(ordered.begin(), ordered.end(), [](const auto& left,
                                                      const auto& right) {
            if (left.second.objects != right.second.objects)
                return left.second.objects > right.second.objects;
            return left.second.bytes > right.second.bytes;
        });
        std::ostringstream output;
        output << "novagc:young_dead_types epoch=" << impl_->markEpoch;
        const std::size_t limit = std::min<std::size_t>(ordered.size(), 12);
        for (std::size_t index = 0; index < limit; ++index) {
            const auto& [type, total] = ordered[index];
            output << ' ' << (type != nullptr && type->name != nullptr
                    ? type->name : "<unknown>")
                   << ":objects=" << total.objects
                   << ",kb=" << total.bytes / 1024U
                   << ",avg_bytes="
                   << (total.objects == 0 ? 0 : total.bytes / total.objects)
                   << ",type_id=" << (type == nullptr ? 0 : type->typeId);
        }
        std::scoped_lock censusLock(deadTypeCensusMutex);
        lastDeadTypeCensus = output.str();
    }
    // Region table removal is atomic with the pause; expensive page release
    // happens after mutators resume.
    impl_->recycleRegions(pendingReclamation);
    impl_->adaptWorkerTarget(
        impl_->adaptiveMarkWorkerTarget, impl_->config.markWorkerCount != 0,
        stats.pauseMicros, stats.markedObjects);
    return stats;
}

CollectionStats Runtime::collectBootstrap() {
    GcCycleScope cycleScope(
        acquireGcCycle(*this, impl_->collectionCycleMutex));
    auto phaseLock = acquireGcPhase(*this, impl_->phaseTransitionMutex);
    GcPhaseScope phaseScope;
    if (!impl_->running)
        throw std::logic_error("hxcpp GC runtime is not initialized");
    if (impl_->markActive.load(std::memory_order_acquire) ||
        impl_->youngMarkActive.load(std::memory_order_acquire) ||
        impl_->relocationActive.load(std::memory_order_acquire)) {
        throw std::logic_error("cannot bootstrap-collect during a GC phase");
    }

    // The bootstrap oracle used to lock the thread registry without actually
    // parking mutators. Thread-local remembered buffers could therefore be
    // published only after a dead owner had already been reclaimed. Give this
    // path the same exact handshake/flush guarantee as production cycles.
    impl_->parkMutators(static_cast<Impl::ThreadState*>(currentThread));
    struct ResumeBootstrapMutators final {
        Impl* impl = nullptr;
        ~ResumeBootstrapMutators() { impl->resumeMutators(); }
    } resumeMutators{impl_};

    std::scoped_lock heapLock(impl_->heapMutex);
    std::scoped_lock threadLock(impl_->threadMutex);
    std::scoped_lock publicationLock(impl_->rootPublicationMutex);

    ++impl_->markEpoch;
    for (const auto& region : impl_->regions) {
        region->liveObjects.store(0, std::memory_order_relaxed);
        region->liveBytes.store(0, std::memory_order_relaxed);
    }
    const View previousView = impl_->view.load(std::memory_order_acquire);
    publishFastBarrierView(
        impl_->view,
        previousView == View::Marked0 ? View::Marked1 : View::Marked0);

    CollectionStats stats{};
    const bool clearSoftReferences =
        impl_->clearSoftReferencesRequested.exchange(
            false, std::memory_order_acq_rel);
    stats.epoch = impl_->markEpoch;
    std::vector<ObjectHeader*> work;
    std::unordered_set<ObjectHeader*> discovered;

    class Marker final : public TraceVisitor {
    public:
        Marker(Runtime& runtime, std::uint32_t epoch,
               std::vector<ObjectHeader*>& work,
               std::unordered_set<ObjectHeader*>& discovered,
               CollectionStats& stats)
            : runtime_(runtime), epoch_(epoch), work_(work),
              discovered_(discovered), stats_(stats) {}

        void visit(RefSlot& slot) override {
            void* object = runtime_.loadBarrier(slot);
            if (object == nullptr)
                return;
            ObjectHeader* header = headerOf(object);
            if ((header->flags.load(std::memory_order_acquire) & ObjectPublished) == 0)
                throw std::logic_error("exact root references an unpublished object");
            if (discovered_.insert(header).second) {
                header->markEpoch.store(epoch_, std::memory_order_release);
                work_.push_back(header);
                runtime_.impl_->accountRegionLive(header);
                ++stats_.markedObjects;
                stats_.markedBytes += header->payloadBytes;
            }
        }

    private:
        Runtime& runtime_;
        std::uint32_t epoch_;
        std::vector<ObjectHeader*>& work_;
        std::unordered_set<ObjectHeader*>& discovered_;
        CollectionStats& stats_;
    } marker(*this, impl_->markEpoch, work, discovered, stats);

    for (const auto& thread : impl_->threads) {
        for (RootFrame* frame = thread->roots; frame != nullptr;
             frame = frame->previous) {
            for (std::uint32_t index = 0; index < frame->slotCount; ++index) {
                if (frame->slots[index] == nullptr)
                    throw std::logic_error("exact root map contains a null slot entry");
                ++stats.roots;
                marker.visit(*frame->slots[index]);
            }
        }
    }
    {
        std::scoped_lock staticLock(impl_->staticRootMutex);
        for (RefSlot* slot : impl_->staticRoots) {
            ++stats.roots;
            marker.visit(*slot);
        }
    }
    {
        std::scoped_lock handleLock(impl_->handleMutex);
        for (const auto& cell : impl_->handles) {
            if (cell->active && Impl::handleIsStrongRoot(
                    cell->kind, clearSoftReferences)) {
                ++stats.roots;
                marker.visit(cell->reference);
            }
        }
    }
    for (const auto& region : impl_->regions) {
        if (region->pinnedObjects.load(std::memory_order_acquire) == 0)
            continue;
        for (ObjectHeader* header : region->snapshotObjects()) {
            if (header->pinCount.load(std::memory_order_acquire) == 0)
                continue;
            RefSlot pinned{makeRef(payloadOf(header)).bits()};
            ++stats.roots;
            marker.visit(pinned);
        }
    }
    {
        std::scoped_lock finalizerLock(impl_->finalizerMutex);
        for (const auto& cell : impl_->finalizers) {
            if (cell->state == Impl::FinalizerState::Pending ||
                cell->state == Impl::FinalizerState::Running) {
                ++stats.roots;
                marker.visit(cell->object);
            }
        }
    }

    auto drainMarkWork = [&] {
        while (!work.empty()) {
            ObjectHeader* header = work.back();
            work.pop_back();
            for (const TypeDescriptor* type = header->type; type != nullptr;
                 type = type->super) {
                auto* payload = static_cast<std::byte*>(payloadOf(header));
                for (std::uint32_t index = 0;
                     index < type->referenceOffsetCount; ++index) {
                    const std::uint32_t offset = type->referenceOffsets[index];
                    if (offset + sizeof(RefSlot) > header->payloadBytes)
                        throw std::logic_error(
                            "type descriptor reference offset is invalid");
                    marker.visit(*reinterpret_cast<RefSlot*>(payload + offset));
                }
                if (type->trace != nullptr)
                    type->trace(payload, header->payloadBytes, marker);
            }
        }
    };
    drainMarkWork();

    // Ephemerons require a least fixpoint: a marked key promotes its value,
    // and that value may make another ephemeron key reachable.
    auto processEphemerons = [&] {
        for (;;) {
            bool promoted = false;
            {
                std::scoped_lock ephemeronLock(impl_->ephemeronMutex);
                for (const auto& cell : impl_->ephemerons) {
                    if (!cell->active)
                        continue;
                    const Ref key(cell->key.load(std::memory_order_acquire));
                    if (key.isNull() ||
                        headerOf(key.address())->markEpoch.load(
                            std::memory_order_acquire) != impl_->markEpoch)
                        continue;
                    const std::size_t before = discovered.size();
                    marker.visit(cell->value);
                    promoted = promoted || discovered.size() != before;
                }
            }
            drainMarkWork();
            if (!promoted)
                break;
        }
    };
    processEphemerons();

    {
        std::scoped_lock finalizerLock(impl_->finalizerMutex);
        for (const auto& cell : impl_->finalizers) {
            if (cell->state != Impl::FinalizerState::Registered)
                continue;
            const Ref object(cell->object.load(std::memory_order_acquire));
            if (!object.isNull() &&
                headerOf(object.address())->markEpoch.load(
                    std::memory_order_acquire) != impl_->markEpoch) {
                cell->state = Impl::FinalizerState::Pending;
                marker.visit(cell->object);
            }
        }
    }
    drainMarkWork();
    processEphemerons();

    {
        std::scoped_lock ephemeronLock(impl_->ephemeronMutex);
        for (const auto& cell : impl_->ephemerons) {
            if (!cell->active)
                continue;
            const Ref key(cell->key.load(std::memory_order_acquire));
            if (!key.isNull() &&
                headerOf(key.address())->markEpoch.load(
                    std::memory_order_acquire) == impl_->markEpoch) {
                (void)loadBarrier(cell->key);
                (void)loadBarrier(cell->value);
            } else {
                cell->key.store(0, std::memory_order_release);
                cell->value.store(0, std::memory_order_release);
            }
        }
    }

    {
        std::scoped_lock handleLock(impl_->handleMutex);
        for (std::uint32_t index = 0;
             index < impl_->handles.size(); ++index) {
            auto& cell = impl_->handles[index];
            if (!cell->active || !Impl::handleNeedsClearing(
                    cell->kind, clearSoftReferences))
                continue;
            const Ref weak(cell->reference.load(std::memory_order_acquire));
            if (!weak.isNull() &&
                headerOf(weak.address())->markEpoch.load(std::memory_order_acquire) !=
                    impl_->markEpoch)
                impl_->enqueueReference(index, *cell);
        }
    }
    {
        std::scoped_lock finalizerLock(impl_->finalizerMutex);
        for (const auto& cell : impl_->finalizers) {
            if (cell->state != Impl::FinalizerState::Finalized)
                continue;
            const Ref object(cell->object.load(std::memory_order_acquire));
            if (!object.isNull() &&
                headerOf(object.address())->markEpoch.load(
                    std::memory_order_acquire) != impl_->markEpoch) {
                cell->object.store(0, std::memory_order_release);
                cell->state = Impl::FinalizerState::Cleared;
            }
        }
        std::erase_if(impl_->finalizers, [](const auto& cell) {
            return cell->state == Impl::FinalizerState::Cleared;
        });
    }

    // Remembered owners are weak metadata, not roots.  Prune dead referrers
    // while every header is still addressable and before whole dead regions
    // are released.
    impl_->pruneRememberedOwners(impl_->markEpoch);
    for (auto region = impl_->regions.begin(); region != impl_->regions.end();) {
        if ((*region)->retired) {
            if ((*region)->retiredEpoch < impl_->markEpoch) {
                impl_->invalidateTlabsFor(region->get());
                region = impl_->regions.erase(region);
                ++stats.reclaimedRegions;
            } else {
                ++region;
            }
            continue;
        }
        std::uint64_t live = 0;
        for (ObjectHeader* header : (*region)->snapshotObjects()) {
            if (header->markEpoch.load(std::memory_order_acquire) == impl_->markEpoch)
                ++live;
            else
                ++stats.deadObjects;
        }
        if (live == 0) {
            impl_->invalidateTlabsFor(region->get());
            region = impl_->regions.erase(region);
            ++stats.reclaimedRegions;
        } else {
            ++region;
        }
    }
    impl_->lastFullMarkEpoch = impl_->markEpoch;
    return stats;
}

RelocationStats Runtime::compactBootstrap() {
    GcCycleScope cycleScope(
        acquireGcCycle(*this, impl_->collectionCycleMutex));
    auto phaseLock = acquireGcPhase(*this, impl_->phaseTransitionMutex);
    GcPhaseScope phaseScope;
    if (!impl_->running)
        throw std::logic_error("hxcpp GC runtime is not initialized");
    if (impl_->markActive.load(std::memory_order_acquire))
        throw std::logic_error("cannot bootstrap-compact during concurrent mark");
    if (impl_->youngMarkActive.load(std::memory_order_acquire))
        throw std::logic_error("cannot bootstrap-compact during young mark");
    if (impl_->relocationActive.load(std::memory_order_acquire))
        throw std::logic_error("cannot bootstrap-compact during relocation");
    if (impl_->markEpoch == 0)
        throw std::logic_error("bootstrap compaction requires a completed mark");

    // This implementation deliberately provides the correctness oracle for
    // forwarding and exact slot remapping. Copying is still inside the
    // handshake; the concurrent relocation implementation will retain this
    // verifier but move bulk copying outside the pause.
    impl_->parkMutators(static_cast<Impl::ThreadState*>(currentThread));
    RelocationStats stats{};
    try {
        std::scoped_lock heapLock(impl_->heapMutex);
        std::scoped_lock threadLock(impl_->threadMutex);
        std::scoped_lock publicationLock(impl_->rootPublicationMutex);

        std::unordered_set<Impl::Region*> transientPinnedRegions;
        for (const auto& thread : impl_->threads) {
            for (const auto& pin : thread->scopedPins) {
                if (pin.object == nullptr)
                    continue;
                auto* region = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(
                        headerOf(pin.object)->reserved0));
                if (region != nullptr)
                    transientPinnedRegions.insert(region);
            }
            for (std::size_t index = 0;
                 index < thread->fastScopedPinCount; ++index) {
                void* object = thread->fastScopedPins[index];
                if (object == nullptr)
                    continue;
                auto* region = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(
                        headerOf(object)->reserved0));
                if (region != nullptr)
                    transientPinnedRegions.insert(region);
            }
        }

        struct BootstrapSource final {
            Impl::Region* region = nullptr;
            std::vector<ObjectHeader*> live;
        };
        std::vector<BootstrapSource> sources;
        std::size_t totalLiveObjects = 0;
        for (const auto& region : impl_->regions) {
            const auto objects = region->snapshotObjects();
            BootstrapSource source{};
            source.region = region.get();
            for (ObjectHeader* header : objects) {
                if (header->markEpoch.load(std::memory_order_acquire) ==
                    impl_->markEpoch) {
                    source.live.push_back(header);
                }
            }
            const bool hasLive = !source.live.empty();
            const bool hasPinned = transientPinnedRegions.contains(
                                       region.get()) ||
                region->kind.load(std::memory_order_acquire) ==
                    Impl::RegionKind::Pinned ||
                std::any_of(
                objects.begin(), objects.end(),
                [this](ObjectHeader* header) {
                    return header->markEpoch.load(std::memory_order_acquire) ==
                               impl_->markEpoch &&
                           header->pinCount.load(std::memory_order_acquire) != 0;
                });
            if (hasLive && !hasPinned) {
                region->relocationSource = true;
                impl_->invalidateTlabsFor(region.get());
                totalLiveObjects += source.live.size();
                sources.push_back(std::move(source));
            }
        }
        stats.sourceRegions = sources.size();
        if (sources.empty()) {
            impl_->resumeMutators();
            return stats;
        }

        std::vector<Impl::Region*> destinations;
        std::vector<std::pair<ObjectHeader*, Impl::Region*>> movedHeaders;
        movedHeaders.reserve(totalLiveObjects);
        Impl::ExternalForwardingMap inPlaceForwarding;
        inPlaceForwarding.reserve(totalLiveObjects);

        auto allocateCopy = [&](ObjectHeader* oldHeader,
                                Impl::Region* destination) -> ObjectHeader* {
            auto* newHeader = reinterpret_cast<ObjectHeader*>(
                destination->memory + destination->cursor);
            auto* sourceRegion = reinterpret_cast<Impl::Region*>(
                static_cast<std::uintptr_t>(oldHeader->reserved0));
            if (sourceRegion != nullptr &&
                (sourceRegion->young.load(std::memory_order_acquire) ||
                 sourceRegion->youngDirty.load(std::memory_order_acquire))) {
                destination->youngDirty.store(true,
                                              std::memory_order_release);
            }
            destination->ensureCommitted(destination->cursor +
                                         oldHeader->totalBytes);
            destination->cursor += oldHeader->totalBytes;
            ::new (static_cast<void*>(newHeader)) ObjectHeader();
            newHeader->type = oldHeader->type;
            newHeader->totalBytes = oldHeader->totalBytes;
            newHeader->payloadBytes = oldHeader->payloadBytes;
            newHeader->markEpoch.store(impl_->markEpoch,
                                       std::memory_order_release);
            newHeader->identity = oldHeader->identity;
            newHeader->flags.store(
                oldHeader->flags.load(std::memory_order_acquire),
                std::memory_order_release);
            newHeader->age = oldHeader->age;
            newHeader->regionIndex = destination->index;
            newHeader->reserved0 = static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(destination));
            newHeader->pinCount.store(0, std::memory_order_release);
            std::memcpy(payloadOf(newHeader), payloadOf(oldHeader),
                        oldHeader->payloadBytes);
            destination->recordObject(newHeader);
            impl_->accountRegionLive(newHeader);
            movedHeaders.emplace_back(newHeader, sourceRegion);
            return newHeader;
        };

        auto compactInPlace = [&](BootstrapSource& source) {
            std::size_t writeCursor = 0;
            for (ObjectHeader* oldHeader : source.live) {
                auto* newHeader = reinterpret_cast<ObjectHeader*>(
                    source.region->memory + writeCursor);
                if (newHeader != oldHeader) {
                    inPlaceForwarding.emplace(payloadOf(oldHeader),
                                              payloadOf(newHeader));
                }
                writeCursor += oldHeader->totalBytes;
            }

            for (std::size_t word = 0;
                 word < source.region->objectStartWordCount; ++word) {
                source.region->objectStarts[word].store(
                    0, std::memory_order_relaxed);
            }
            source.region->liveObjects.store(0, std::memory_order_relaxed);
            source.region->liveBytes.store(0, std::memory_order_relaxed);
            writeCursor = 0;
            for (ObjectHeader* oldHeader : source.live) {
                const TypeDescriptor* type = oldHeader->type;
                const std::uint32_t totalBytes = oldHeader->totalBytes;
                const std::uint32_t payloadBytes = oldHeader->payloadBytes;
                const std::uint64_t identity = oldHeader->identity;
                const std::uint16_t flags = oldHeader->flags.load(
                    std::memory_order_acquire);
                const std::uint16_t age = oldHeader->age;
                auto* newHeader = reinterpret_cast<ObjectHeader*>(
                    source.region->memory + writeCursor);
                if (newHeader != oldHeader) {
                    // Move payload first.  For a downward slide memmove
                    // preserves an overlapping source; constructing the new
                    // header afterwards cannot clobber unread payload bytes.
                    std::memmove(payloadOf(newHeader), payloadOf(oldHeader),
                                 payloadBytes);
                    ::new (static_cast<void*>(newHeader)) ObjectHeader();
                }
                newHeader->forwarding.store(0, std::memory_order_relaxed);
                newHeader->type = type;
                newHeader->totalBytes = totalBytes;
                newHeader->payloadBytes = payloadBytes;
                newHeader->markEpoch.store(impl_->markEpoch,
                                           std::memory_order_relaxed);
                newHeader->state.store(ObjectStable,
                                       std::memory_order_relaxed);
                newHeader->identity = identity;
                newHeader->flags.store(flags, std::memory_order_relaxed);
                newHeader->age = age;
                newHeader->regionIndex = source.region->index;
                newHeader->pinCount.store(0, std::memory_order_relaxed);
                newHeader->reserved0 = static_cast<std::uint64_t>(
                    reinterpret_cast<std::uintptr_t>(source.region));
                source.region->recordObject(newHeader);
                impl_->accountRegionLive(newHeader);
                movedHeaders.emplace_back(newHeader, source.region);
                ++stats.relocatedObjects;
                stats.relocatedBytes += payloadBytes;
                writeCursor += totalBytes;
            }
            source.region->cursor = writeCursor;
            source.region->setKind(source.region->largeOnly
                ? Impl::RegionKind::Large : Impl::RegionKind::Old);
            source.region->relocationSource = false;
            ++stats.inPlaceRegions;
        };

        for (BootstrapSource& source : sources) {
            std::vector<Impl::Region*> evacuationPlan;
            evacuationPlan.reserve(source.live.size());
            const std::size_t newDestinationStart = destinations.size();
            try {
                std::unordered_map<Impl::Region*, std::size_t> highWater;
                highWater.reserve(destinations.size() + 1U);
                for (Impl::Region* destination : destinations)
                    highWater.emplace(destination, destination->cursor);
                for (ObjectHeader* header : source.live) {
                    Impl::Region* selected = nullptr;
                    for (auto iterator = destinations.rbegin();
                         iterator != destinations.rend(); ++iterator) {
                        const std::size_t cursor = highWater[*iterator];
                        if ((*iterator)->bytes - cursor >=
                            header->totalBytes) {
                            selected = *iterator;
                            break;
                        }
                    }
                    if (selected == nullptr) {
                        auto created = impl_->acquireRegion(
                            std::max<std::size_t>(
                                impl_->config.regionBytes,
                                source.region->bytes),
                            Impl::RegionKind::Old);
                        selected = created.get();
                        impl_->regions.push_back(std::move(created));
                        destinations.push_back(selected);
                        highWater.emplace(selected, 0U);
                        ++stats.destinationRegions;
                    }
                    evacuationPlan.push_back(selected);
                    highWater[selected] += header->totalBytes;
                }
                for (const auto& [destination, required] : highWater)
                    destination->ensureCommitted(required);
            } catch (const std::bad_alloc&) {
                // Planning happens before the first source forwarding pointer
                // is published. Empty destinations from a failed plan are
                // released, then the source is compacted downward in its own
                // committed pages with an external forwarding table.
                while (destinations.size() > newDestinationStart) {
                    Impl::Region* empty = destinations.back();
                    destinations.pop_back();
                    auto owned = std::ranges::find_if(
                        impl_->regions, [empty](const auto& region) {
                            return region.get() == empty;
                        });
                    if (owned != impl_->regions.end())
                        impl_->regions.erase(owned);
                    --stats.destinationRegions;
                }
                compactInPlace(source);
                continue;
            }

            for (std::size_t index = 0; index < source.live.size(); ++index) {
                ObjectHeader* oldHeader = source.live[index];
                oldHeader->state.store(ObjectRelocating,
                                       std::memory_order_release);
                ObjectHeader* newHeader = allocateCopy(
                    oldHeader, evacuationPlan[index]);
                oldHeader->forwarding.store(
                    Ref::encode(payloadOf(newHeader), View::Remapped).bits(),
                    std::memory_order_release);
                oldHeader->state.store(ObjectRelocated,
                                       std::memory_order_release);
                ++stats.relocatedObjects;
                stats.relocatedBytes += oldHeader->payloadBytes;
            }
        }
        impl_->remapRememberedOwners(&inPlaceForwarding);

        publishFastBarrierView(impl_->view, View::Remapped);

        class RemapVisitor final : public TraceVisitor {
        public:
            RemapVisitor(Runtime& runtime,
                         const Impl::ExternalForwardingMap& forwarding)
                : runtime_(runtime), forwarding_(forwarding) {}
            void visit(RefSlot& slot) override {
                const Ref reference(slot.load(std::memory_order_acquire));
                if (!reference.isNull()) {
                    const auto moved = forwarding_.find(reference.address());
                    if (moved != forwarding_.end()) {
                        slot.store(runtime_.makeRef(moved->second).bits(),
                                   std::memory_order_release);
                        return;
                    }
                }
                (void)runtime_.loadBarrier(slot);
            }
        private:
            Runtime& runtime_;
            const Impl::ExternalForwardingMap& forwarding_;
        } remapper(*this, inPlaceForwarding);

        for (const auto& thread : impl_->threads) {
            for (RootFrame* frame = thread->roots; frame != nullptr;
                 frame = frame->previous) {
                for (std::uint32_t index = 0; index < frame->slotCount; ++index)
                    remapper.visit(*frame->slots[index]);
            }
        }
        {
            std::scoped_lock staticLock(impl_->staticRootMutex);
            for (RefSlot* slot : impl_->staticRoots)
                remapper.visit(*slot);
        }
        {
            std::scoped_lock handleLock(impl_->handleMutex);
            for (const auto& cell : impl_->handles) {
                if (cell->active)
                    remapper.visit(cell->reference);
            }
        }
        {
            std::scoped_lock ephemeronLock(impl_->ephemeronMutex);
            for (const auto& cell : impl_->ephemerons) {
                if (cell->active) {
                    remapper.visit(cell->key);
                    remapper.visit(cell->value);
                }
            }
        }
        {
            std::scoped_lock finalizerLock(impl_->finalizerMutex);
            for (const auto& cell : impl_->finalizers) {
                if (cell->state != Impl::FinalizerState::Cleared)
                    remapper.visit(cell->object);
            }
        }
        // Heal every surviving heap field, not only destination fields. A
        // pinned/non-selected owner can point into a source that is about to
        // be released, and remembered sets cover only old-to-young edges.
        for (const auto& region : impl_->regions) {
            if (region->relocationSource)
                continue;
            for (ObjectHeader* header : region->snapshotObjects()) {
                if (header->markEpoch.load(std::memory_order_acquire) !=
                    impl_->markEpoch) {
                    continue;
                }
                for (const TypeDescriptor* type = header->type; type != nullptr;
                     type = type->super) {
                    auto* payload = static_cast<std::byte*>(payloadOf(header));
                    for (std::uint32_t index = 0;
                         index < type->referenceOffsetCount; ++index) {
                        remapper.visit(*reinterpret_cast<RefSlot*>(
                            payload + type->referenceOffsets[index]));
                    }
                    if (type->trace != nullptr)
                        type->trace(payload, header->payloadBytes, remapper);
                }
            }
        }
        for (const auto& [header, sourceRegion] : movedHeaders)
            impl_->rememberRelocatedOwner(header, sourceRegion);

        impl_->regions.erase(
            std::remove_if(impl_->regions.begin(), impl_->regions.end(),
                [](const std::unique_ptr<Impl::Region>& region) {
                    return region->relocationSource;
                }),
            impl_->regions.end());
    } catch (...) {
        impl_->resumeMutators();
        throw;
    }
    impl_->resumeMutators();
    return stats;
}

void Runtime::beginConcurrentRelocation() {
    auto cycleLock = acquireGcCycle(*this, impl_->collectionCycleMutex);
    auto phaseLock = acquireGcPhase(*this, impl_->phaseTransitionMutex);
    GcPhaseScope phaseScope;
    if (!impl_->running)
        throw std::logic_error("hxcpp GC runtime is not initialized");
    if (impl_->markActive.load(std::memory_order_acquire))
        throw std::logic_error("cannot relocate during concurrent mark");
    if (impl_->youngMarkActive.load(std::memory_order_acquire))
        throw std::logic_error("cannot relocate during concurrent young mark");
    if (impl_->relocationActive.load(std::memory_order_acquire))
        throw std::logic_error("concurrent relocation is already active");
    if (impl_->markEpoch == 0)
        throw std::logic_error("concurrent relocation requires a completed mark");
    if (std::ranges::any_of(impl_->relocationThreads,
                            [](const std::thread& worker) {
                                return worker.joinable();
                            }))
        throw std::logic_error("previous relocation worker was not joined");

    const auto pauseStarted = std::chrono::steady_clock::now();
    impl_->parkMutators(static_cast<Impl::ThreadState*>(currentThread));
    // All existing fast receiver pins are now stable and will be included in
    // source-region selection. New pins after resume must use the counted
    // header protocol until concurrent relocation finishes.
    scopedPinBarrierActive.store(true, std::memory_order_release);
    try {
        std::scoped_lock heapLock(impl_->heapMutex);
        std::scoped_lock threadLock(impl_->threadMutex);
        std::scoped_lock publicationLock(impl_->rootPublicationMutex);
        std::scoped_lock relocationLock(impl_->relocationMutex);
        impl_->relocationStats = {};
        impl_->relocationSources.clear();
        impl_->relocationDestinations.clear();
        impl_->relocationFailure = nullptr;
        impl_->relocationThreads.clear();
        impl_->nextRelocationSource.store(0, std::memory_order_release);

        std::unordered_set<Impl::Region*> transientPinnedRegions;
        for (const auto& thread : impl_->threads) {
            for (const auto& pin : thread->scopedPins) {
                if (pin.object == nullptr)
                    continue;
                auto* region = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(
                        headerOf(pin.object)->reserved0));
                if (region != nullptr)
                    transientPinnedRegions.insert(region);
            }
            for (std::size_t index = 0;
                 index < thread->fastScopedPinCount; ++index) {
                void* object = thread->fastScopedPins[index];
                if (object == nullptr)
                    continue;
                auto* region = reinterpret_cast<Impl::Region*>(
                    static_cast<std::uintptr_t>(
                        headerOf(object)->reserved0));
                if (region != nullptr)
                    transientPinnedRegions.insert(region);
            }
        }

        for (const auto& region : impl_->regions) {
            if (region->retired)
                continue;
            const std::uint64_t liveObjects =
                region->liveObjects.load(std::memory_order_acquire);
            const std::uint64_t liveBytes =
                region->liveBytes.load(std::memory_order_acquire);
            const bool hasPinned = transientPinnedRegions.contains(
                                       region.get()) ||
                region->kind.load(std::memory_order_acquire) ==
                    Impl::RegionKind::Pinned ||
                region->pinnedObjects.load(std::memory_order_acquire) != 0;
            const std::uint64_t reclaimableBytes =
                region->cursor > liveBytes ? region->cursor - liveBytes : 0;
            const std::uint64_t minimumReclaim = std::max<std::uint64_t>(
                256U * 1024U, region->cursor / 5U);
            const bool selective = selectiveAutomaticRelocation.load(
                std::memory_order_acquire);
            const bool worthMoving = !selective ||
                (!region->largeOnly && reclaimableBytes >= minimumReclaim);
            if (liveObjects != 0 && !hasPinned && worthMoving) {
                region->relocationSource = true;
                region->relocationComplete = false;
                region->relocationFailed = false;
                impl_->invalidateTlabsFor(region.get());
                impl_->relocationSources.push_back(region.get());
            }
        }
        impl_->relocationStats.sourceRegions =
            impl_->relocationSources.size();
        fastBarrierState.fetch_or(FastBarrierRelocationActive,
                                  std::memory_order_acq_rel);
        impl_->relocationActive.store(true, std::memory_order_release);
        concurrentRelocationWorkerDone.store(false, std::memory_order_release);

        const std::uint32_t workerCount = std::max<std::uint32_t>(
            1, std::min<std::uint32_t>(impl_->configuredRelocationWorkers(),
                static_cast<std::uint32_t>(std::max<std::size_t>(
                    1, impl_->relocationSources.size()))));
        impl_->liveRelocationWorkers.store(workerCount,
                                            std::memory_order_release);
        impl_->relocationThreads.reserve(workerCount);
        for (std::uint32_t workerIndex = 0; workerIndex < workerCount;
             ++workerIndex) {
        impl_->relocationThreads.emplace_back([this] {
            Impl::Region* currentSource = nullptr;
            ObjectHeader* currentHeader = nullptr;
            try {
                for (;;) {
                    const std::size_t sourceIndex =
                        impl_->nextRelocationSource.fetch_add(
                            1, std::memory_order_acq_rel);
                    if (sourceIndex >= impl_->relocationSources.size())
                        break;
                    Impl::Region* source =
                        impl_->relocationSources[sourceIndex];
                    currentSource = source;
                    if (!source->relocationSource)
                        continue;
                    for (ObjectHeader* oldHeader : source->snapshotObjects()) {
                        if (oldHeader->markEpoch.load(std::memory_order_acquire) !=
                            impl_->markEpoch)
                            continue;

                        if (impl_->config.relocationBeforeCopyHook != nullptr) {
                            impl_->config.relocationBeforeCopyHook(
                                impl_->config.relocationHookContext,
                                payloadOf(oldHeader));
                        }

                        std::scoped_lock heapAndRelocation(
                            impl_->heapMutex, impl_->relocationMutex);
                        currentHeader = oldHeader;
                        if (oldHeader->pinCount.load(
                                std::memory_order_acquire) != 0) {
                            // A native/mutator pin arrived after the source
                            // set snapshot. Keep the whole source region this
                            // cycle so the pinned address remains stable.
                            source->relocationSource = false;
                            currentHeader = nullptr;
                            break;
                        }
                        oldHeader->state.store(ObjectRelocating,
                                               std::memory_order_release);
                        Impl::Region* destination = nullptr;
                        for (Impl::Region* candidate :
                             impl_->relocationDestinations) {
                            if (candidate->bytes - candidate->cursor >=
                                oldHeader->totalBytes) {
                                destination = candidate;
                                break;
                            }
                        }
                        if (destination == nullptr) {
                            auto created = impl_->acquireRegion(
                                std::max<std::size_t>(impl_->config.regionBytes,
                                                      oldHeader->totalBytes),
                                Impl::RegionKind::Old);
                            destination = created.get();
                            impl_->regions.push_back(std::move(created));
                            impl_->relocationDestinations.push_back(destination);
                            ++impl_->relocationStats.destinationRegions;
                        }

                        auto* newHeader = reinterpret_cast<ObjectHeader*>(
                            destination->memory + destination->cursor);
                        auto* sourceRegion = reinterpret_cast<Impl::Region*>(
                            static_cast<std::uintptr_t>(oldHeader->reserved0));
                        if (sourceRegion != nullptr &&
                            (sourceRegion->young.load(
                                 std::memory_order_acquire) ||
                             sourceRegion->youngDirty.load(
                                 std::memory_order_acquire))) {
                            destination->youngDirty.store(
                                true, std::memory_order_release);
                        }
                        destination->ensureCommitted(
                            destination->cursor + oldHeader->totalBytes);
                        destination->cursor += oldHeader->totalBytes;
                        ::new (static_cast<void*>(newHeader)) ObjectHeader();
                        newHeader->type = oldHeader->type;
                        newHeader->totalBytes = oldHeader->totalBytes;
                        newHeader->payloadBytes = oldHeader->payloadBytes;
                        newHeader->markEpoch.store(
                            impl_->markEpoch, std::memory_order_release);
                        newHeader->identity = oldHeader->identity;
                        newHeader->flags.store(
                            oldHeader->flags.load(std::memory_order_acquire),
                            std::memory_order_release);
                        newHeader->age = oldHeader->age;
                        newHeader->regionIndex = destination->index;
                        newHeader->reserved0 = static_cast<std::uint64_t>(
                            reinterpret_cast<std::uintptr_t>(destination));
                        std::memcpy(payloadOf(newHeader), payloadOf(oldHeader),
                                    oldHeader->payloadBytes);
                        destination->recordObject(newHeader);
                        impl_->accountRegionLive(newHeader);
                        impl_->rememberRelocatedOwner(newHeader, sourceRegion);
                        oldHeader->forwarding.store(
                            Ref::encode(payloadOf(newHeader), View::Remapped).bits(),
                            std::memory_order_release);
                        oldHeader->state.store(ObjectRelocated,
                                               std::memory_order_release);
                        currentHeader = nullptr;
                        ++impl_->relocationStats.relocatedObjects;
                        impl_->relocationStats.relocatedBytes +=
                            oldHeader->payloadBytes;
                    }
                    if (source->relocationSource)
                        source->relocationComplete = true;
                    currentSource = nullptr;
                }
            } catch (const std::bad_alloc&) {
                std::scoped_lock relocationLock(impl_->relocationMutex);
                if (currentHeader != nullptr &&
                    currentHeader->forwarding.load(
                        std::memory_order_acquire) == 0) {
                    currentHeader->state.store(ObjectStable,
                                               std::memory_order_release);
                }
                if (currentSource != nullptr &&
                    !currentSource->relocationFailed) {
                    currentSource->relocationFailed = true;
                    ++impl_->relocationStats.failedRegions;
                }
                impl_->relocationStats.degraded = true;
            } catch (...) {
                std::scoped_lock relocationLock(impl_->relocationMutex);
                if (currentHeader != nullptr &&
                    currentHeader->forwarding.load(
                        std::memory_order_acquire) == 0) {
                    currentHeader->state.store(ObjectStable,
                                               std::memory_order_release);
                }
                if (currentSource != nullptr &&
                    !currentSource->relocationFailed) {
                    currentSource->relocationFailed = true;
                    ++impl_->relocationStats.failedRegions;
                }
                if (!impl_->relocationFailure)
                    impl_->relocationFailure = std::current_exception();
            }
            if (impl_->liveRelocationWorkers.fetch_sub(
                    1, std::memory_order_acq_rel) == 1) {
                concurrentRelocationWorkerDone.store(
                    true, std::memory_order_release);
            }
        });
        }
    } catch (...) {
        concurrentRelocationWorkerDone.store(true, std::memory_order_release);
        impl_->relocationActive.store(false, std::memory_order_release);
        fastBarrierState.fetch_and(~FastBarrierRelocationActive,
                                   std::memory_order_acq_rel);
        scopedPinBarrierActive.store(false, std::memory_order_release);
        impl_->resumeMutators();
        recordPauseMicros(lastRelocationSelectPauseMicros, pauseStarted);
        throw;
    }
    impl_->resumeMutators();
    recordPauseMicros(lastRelocationSelectPauseMicros, pauseStarted);
    impl_->relocationCycleLock.emplace(std::move(cycleLock));
    gcCycleActive = true;
}

RelocationStats Runtime::finishConcurrentRelocation() {
    auto phaseLock = acquireGcPhase(*this, impl_->phaseTransitionMutex);
    GcPhaseScope phaseScope;
    if (!impl_->relocationActive.load(std::memory_order_acquire))
        throw std::logic_error("no concurrent relocation is active");
    if (!gcCycleActive || !impl_->relocationCycleLock.has_value())
        throw std::logic_error(
            "concurrent relocation must finish on its owning thread");
    struct ReleaseRelocationCycle final {
        Impl* impl = nullptr;
        ~ReleaseRelocationCycle() {
            impl->relocationCycleLock.reset();
            gcCycleActive = false;
        }
    } releaseRelocationCycle{impl_};
    const auto pauseStarted = std::chrono::steady_clock::now();
    impl_->parkMutators(static_cast<Impl::ThreadState*>(currentThread));
    std::exception_ptr fatalFailure;
    try {
        for (std::thread& worker : impl_->relocationThreads) {
            if (worker.joinable())
                worker.join();
        }
        impl_->relocationThreads.clear();
        concurrentRelocationWorkerDone.store(true, std::memory_order_release);
        std::scoped_lock heapLock(impl_->heapMutex);
        std::scoped_lock threadLock(impl_->threadMutex);
        std::scoped_lock publicationLock(impl_->rootPublicationMutex);
        std::scoped_lock relocationLock(impl_->relocationMutex);
        fatalFailure = impl_->relocationFailure;

        publishFastBarrierView(impl_->view, View::Remapped);
        class RootRemapper final : public TraceVisitor {
        public:
            explicit RootRemapper(Runtime& runtime) : runtime_(runtime) {}
            void visit(RefSlot& slot) override {
                (void)runtime_.loadBarrier(slot);
            }
        private:
            Runtime& runtime_;
        } remapper(*this);
        for (const auto& thread : impl_->threads) {
            for (RootFrame* frame = thread->roots; frame != nullptr;
                 frame = frame->previous) {
                for (std::uint32_t index = 0; index < frame->slotCount; ++index)
                    remapper.visit(*frame->slots[index]);
            }
        }
        {
            std::scoped_lock staticLock(impl_->staticRootMutex);
            for (RefSlot* slot : impl_->staticRoots)
                remapper.visit(*slot);
        }
        {
            std::scoped_lock handleLock(impl_->handleMutex);
            for (const auto& cell : impl_->handles) {
                if (cell->active)
                    remapper.visit(cell->reference);
            }
        }
        {
            std::scoped_lock ephemeronLock(impl_->ephemeronMutex);
            for (const auto& cell : impl_->ephemerons) {
                if (cell->active) {
                    remapper.visit(cell->key);
                    remapper.visit(cell->value);
                }
            }
        }
        {
            std::scoped_lock finalizerLock(impl_->finalizerMutex);
            for (const auto& cell : impl_->finalizers) {
                if (cell->state != Impl::FinalizerState::Cleared)
                    remapper.visit(cell->object);
            }
        }
        impl_->remapRememberedOwners();
        for (Impl::Region* source : impl_->relocationSources) {
            if (source->relocationSource && source->relocationComplete &&
                !source->relocationFailed) {
                source->relocationSource = false;
                source->retired = true;
                source->retiredEpoch = impl_->markEpoch;
            } else {
                // A late pin or an exhausted relocation reserve leaves this
                // source in place.  Objects copied before the failure keep
                // valid forwarding pointers, while uncopied objects remain
                // directly reachable from the retained source region.  The
                // next full mark can reclaim the redundant copies safely.
                source->relocationSource = false;
            }
            source->relocationComplete = false;
            source->relocationFailed = false;
        }
        impl_->relocationActive.store(false, std::memory_order_release);
        fastBarrierState.fetch_and(~FastBarrierRelocationActive,
                                   std::memory_order_acq_rel);
        scopedPinBarrierActive.store(false, std::memory_order_release);
        impl_->relocationFailure = nullptr;
    } catch (...) {
        impl_->resumeMutators();
        recordPauseMicros(lastRelocationRemapPauseMicros, pauseStarted);
        throw;
    }
    impl_->resumeMutators();
    recordPauseMicros(lastRelocationRemapPauseMicros, pauseStarted);
    if (fatalFailure)
        std::rethrow_exception(fatalFailure);
    impl_->adaptWorkerTarget(
        impl_->adaptiveRelocationWorkerTarget,
        impl_->config.relocationWorkerCount != 0,
        lastRelocationRemapPauseMicros.load(std::memory_order_acquire),
        impl_->relocationStats.relocatedObjects);
    return impl_->relocationStats;
}

bool Runtime::concurrentRelocationActive() const noexcept {
    return impl_->relocationActive.load(std::memory_order_acquire);
}

AllocationStats Runtime::allocationStats() const noexcept {
    if (currentThread != nullptr) {
        impl_->flushAllocationCounters(
            *static_cast<Impl::ThreadState*>(currentThread));
    }
    AllocationStats stats{};
    stats.allocatedObjects =
        impl_->allocatedObjects.load(std::memory_order_relaxed);
    stats.allocatedBytes =
        impl_->allocatedBytes.load(std::memory_order_relaxed);
    stats.tlabFastAllocations =
        impl_->tlabFastAllocations.load(std::memory_order_relaxed);
    stats.tlabRefills = impl_->tlabRefills.load(std::memory_order_relaxed);
    stats.createdRegions = impl_->createdRegions.load(std::memory_order_relaxed);
    stats.emergencyCollections =
        impl_->emergencyCollections.load(std::memory_order_relaxed);
    stats.allocationStalls =
        impl_->allocationStalls.load(std::memory_order_relaxed);
    stats.allocationStallMicros =
        impl_->allocationStallMicros.load(std::memory_order_relaxed);
    stats.allocationStallTimeouts =
        impl_->allocationStallTimeouts.load(std::memory_order_relaxed);
    stats.maximumAllocationStallWaiters =
        impl_->maximumAllocationStallWaiters.load(
            std::memory_order_relaxed);
    return stats;
}

HeapStats Runtime::heapStats() const {
    std::scoped_lock heapLock(impl_->heapMutex);
    HeapStats stats{};
    stats.activeRegions = static_cast<std::uint32_t>(impl_->regions.size());
    stats.reusableRegions =
        static_cast<std::uint32_t>(impl_->freeRegions.size());
    stats.tenuringThreshold = impl_->currentTenuringThreshold;
    stats.isolatedPinnedObjects =
        impl_->isolatedPinnedObjects.load(std::memory_order_relaxed);
    stats.pinIsolationFallbacks =
        impl_->pinIsolationFallbacks.load(std::memory_order_relaxed);
    stats.adaptiveMarkWorkers = impl_->config.markWorkerCount != 0
        ? impl_->config.markWorkerCount
        : std::max(1U, impl_->adaptiveMarkWorkerTarget.load(
                           std::memory_order_relaxed));
    stats.adaptiveRelocationWorkers = impl_->config.relocationWorkerCount != 0
        ? impl_->config.relocationWorkerCount
        : std::max(1U, impl_->adaptiveRelocationWorkerTarget.load(
                           std::memory_order_relaxed));
    if (impl_->addressSpace != nullptr) {
        stats.addressSpaceReservedBytes =
            impl_->addressSpace->bytes;
    }
    for (const auto& region : impl_->regions) {
        const std::uint64_t used = region->cursor;
        stats.usedBytes += used;
        stats.reservedBytes += region->bytes;
        stats.committedBytes += region->committed;
        if (region->largePageAdvised)
            stats.largePageAdvisedBytes += region->committed;
        if (region->numaPlaced)
            stats.numaPlacedBytes += region->committed;
        stats.memoryPolicyFallbacks += region->memoryPolicyFallbacks;
        stats.rememberedBitmapBytes +=
            region->rememberedBitmapBytes();
        stats.youngMarkBitmapBytes +=
            region->objectStartWordCount * sizeof(std::uint64_t);
        switch (region->kind.load(std::memory_order_acquire)) {
        case Impl::RegionKind::Eden:
            stats.edenBytes += used;
            break;
        case Impl::RegionKind::Survivor:
            stats.survivorBytes += used;
            break;
        case Impl::RegionKind::YoungLarge:
            stats.edenBytes += used;
            stats.largeBytes += used;
            stats.youngLargeBytes += used;
            break;
        case Impl::RegionKind::Old:
        case Impl::RegionKind::Relocation:
            stats.oldBytes += used;
            break;
        case Impl::RegionKind::Pinned:
            stats.pinnedBytes += used;
            break;
        case Impl::RegionKind::Large:
            stats.largeBytes += used;
            break;
        }
    }
    for (const auto& free : impl_->freeRegions) {
        stats.reservedBytes += free.region->bytes;
        stats.committedBytes += free.region->committed;
        stats.reusableBytes += free.region->bytes;
        if (free.region->largePageAdvised)
            stats.largePageAdvisedBytes += free.region->committed;
        if (free.region->numaPlaced)
            stats.numaPlacedBytes += free.region->committed;
        stats.memoryPolicyFallbacks +=
            free.region->memoryPolicyFallbacks;
        stats.rememberedBitmapBytes +=
            free.region->rememberedBitmapBytes();
        stats.youngMarkBitmapBytes +=
            free.region->objectStartWordCount * sizeof(std::uint64_t);
    }
    return stats;
}

std::uint64_t Runtime::usedBytes() const {
    std::scoped_lock heapLock(impl_->heapMutex);
    std::uint64_t bytes = 0;
    for (const auto& region : impl_->regions)
        bytes += static_cast<std::uint64_t>(region->cursor);
    return bytes;
}

std::uint64_t Runtime::reservedBytes() const {
    std::scoped_lock heapLock(impl_->heapMutex);
    std::uint64_t bytes = 0;
    for (const auto& region : impl_->regions)
        bytes += static_cast<std::uint64_t>(region->bytes);
    for (const auto& free : impl_->freeRegions)
        bytes += static_cast<std::uint64_t>(free.region->bytes);
    return bytes;
}

std::uint64_t Runtime::committedBytes() const {
    std::scoped_lock heapLock(impl_->heapMutex);
    std::uint64_t bytes = 0;
    for (const auto& region : impl_->regions)
        bytes += static_cast<std::uint64_t>(region->committed);
    for (const auto& free : impl_->freeRegions)
        bytes += static_cast<std::uint64_t>(free.region->committed);
    return bytes;
}

std::uint64_t Runtime::reusableBytes() const {
    std::scoped_lock heapLock(impl_->heapMutex);
    std::uint64_t bytes = 0;
    for (const auto& free : impl_->freeRegions)
        bytes += static_cast<std::uint64_t>(free.region->bytes);
    return bytes;
}

void Runtime::uncommitUnusedMemory(bool force) {
    std::scoped_lock heapLock(impl_->heapMutex);
    impl_->trimFreeRegions(force);
}

void Runtime::startConcurrentMark() {
    auto cycleLock = acquireGcCycle(*this, impl_->collectionCycleMutex);
    auto phaseLock = acquireGcPhase(*this, impl_->phaseTransitionMutex);
    GcPhaseScope phaseScope;
    if (!impl_->running)
        throw std::logic_error("hxcpp GC runtime is not initialized");
    if (impl_->markActive.load(std::memory_order_acquire))
        throw std::logic_error("concurrent mark cycle is already active");
    if (impl_->youngMarkActive.load(std::memory_order_acquire))
        throw std::logic_error("cannot start full mark during young mark");
    if (impl_->relocationActive.load(std::memory_order_acquire))
        throw std::logic_error("cannot start full mark during relocation");
    if (std::ranges::any_of(impl_->markerThreads,
                            [](const std::thread& worker) {
                                return worker.joinable();
                            }))
        throw std::logic_error("previous concurrent marker was not joined");

    const auto pauseStarted = std::chrono::steady_clock::now();
    impl_->parkMutators(static_cast<Impl::ThreadState*>(currentThread));
    try {
        std::scoped_lock heapLock(impl_->heapMutex);
        std::scoped_lock threadLock(impl_->threadMutex);
        std::scoped_lock publicationLock(impl_->rootPublicationMutex);
        std::scoped_lock markLock(impl_->markMutex);

    ++impl_->markEpoch;
    for (const auto& region : impl_->regions) {
        region->liveObjects.store(0, std::memory_order_relaxed);
        region->liveBytes.store(0, std::memory_order_relaxed);
    }
    const View previousView = impl_->view.load(std::memory_order_acquire);
    publishFastBarrierView(
        impl_->view,
        previousView == View::Marked0 ? View::Marked1 : View::Marked0);
    impl_->markQueue.clear();
    impl_->markClosing = false;
    impl_->markSealed = false;
    impl_->activeMarkWorkers = 0;
    impl_->markFailure = nullptr;
    impl_->markerThreads.clear();
    impl_->concurrentRoots.store(0, std::memory_order_relaxed);
    impl_->concurrentThreadRootSnapshots.store(0,
                                               std::memory_order_relaxed);
    impl_->concurrentThreadRootSlots.store(0,
                                           std::memory_order_relaxed);
    impl_->concurrentMarkedObjects.store(0, std::memory_order_relaxed);
    impl_->concurrentMarkedBytes.store(0, std::memory_order_relaxed);
    impl_->clearSoftReferencesThisCycle =
        impl_->clearSoftReferencesRequested.exchange(
            false, std::memory_order_acq_rel);
    rootFrameBarrierActive.store(true, std::memory_order_release);
    fastBarrierState.fetch_or(FastBarrierMarkActive,
                              std::memory_order_acq_rel);
    impl_->markActive.store(true, std::memory_order_release);
    concurrentMarkWorkerIdle.store(false, std::memory_order_release);

    auto enqueueLocked = [this](void* object) {
        if (object == nullptr)
            return;
        ObjectHeader* header = headerOf(object);
        if ((header->flags.load(std::memory_order_acquire) & ObjectPublished) == 0)
            throw std::logic_error("exact root references an unpublished object");
        const std::uint32_t previous =
            header->markEpoch.exchange(impl_->markEpoch, std::memory_order_acq_rel);
        if (previous != impl_->markEpoch) {
            impl_->markQueue.push_back(header);
            impl_->accountRegionLive(header);
            concurrentMarkWorkerIdle.store(false,
                                           std::memory_order_release);
            impl_->concurrentMarkedObjects.fetch_add(1, std::memory_order_relaxed);
            impl_->concurrentMarkedBytes.fetch_add(
                header->payloadBytes, std::memory_order_relaxed);
        }
    };

    for (const auto& thread : impl_->threads) {
        impl_->concurrentThreadRootSnapshots.fetch_add(
            1, std::memory_order_relaxed);
        impl_->concurrentThreadRootSlots.fetch_add(
            thread->rootSnapshot.size(), std::memory_order_relaxed);
        for (const std::uint64_t raw : thread->rootSnapshot) {
            RefSlot snapshot{raw};
            impl_->concurrentRoots.fetch_add(1, std::memory_order_relaxed);
            enqueueLocked(loadBarrier(snapshot));
        }
    }
    {
        std::scoped_lock staticLock(impl_->staticRootMutex);
        for (RefSlot* slot : impl_->staticRoots) {
            impl_->concurrentRoots.fetch_add(1, std::memory_order_relaxed);
            enqueueLocked(loadBarrier(*slot));
        }
    }
    {
        std::scoped_lock handleLock(impl_->handleMutex);
        for (const auto& cell : impl_->handles) {
            if (cell->active && Impl::handleIsStrongRoot(
                    cell->kind, impl_->clearSoftReferencesThisCycle)) {
                impl_->concurrentRoots.fetch_add(1, std::memory_order_relaxed);
                enqueueLocked(loadBarrier(cell->reference));
            }
        }
    }
    for (const auto& region : impl_->regions) {
        if (region->pinnedObjects.load(std::memory_order_acquire) == 0)
            continue;
        for (ObjectHeader* header : region->snapshotObjects()) {
            if (header->pinCount.load(std::memory_order_acquire) == 0)
                continue;
            impl_->concurrentRoots.fetch_add(1,
                                             std::memory_order_relaxed);
            enqueueLocked(payloadOf(header));
        }
    }
    {
        std::scoped_lock finalizerLock(impl_->finalizerMutex);
        for (const auto& cell : impl_->finalizers) {
            if (cell->state == Impl::FinalizerState::Pending ||
                cell->state == Impl::FinalizerState::Running) {
                impl_->concurrentRoots.fetch_add(1, std::memory_order_relaxed);
                enqueueLocked(loadBarrier(cell->object));
            }
        }
    }

        const std::uint32_t workerCount = impl_->configuredMarkWorkers();
        impl_->markerThreads.reserve(workerCount);
        for (std::uint32_t workerIndex = 0; workerIndex < workerCount;
             ++workerIndex) {
        impl_->markerThreads.emplace_back([this] {
        try {
        class ConcurrentVisitor final : public TraceVisitor {
        public:
            explicit ConcurrentVisitor(Runtime& runtime) : runtime_(runtime) {}

            void visit(RefSlot& slot) override {
                void* object = runtime_.loadBarrier(slot);
                if (object == nullptr)
                    return;
                auto* impl = runtime_.impl_;
                ObjectHeader* header = headerOf(object);
                std::scoped_lock markLock(impl->markMutex);
                if (impl->markSealed || impl->markFailure)
                    return;
                std::uint32_t observed =
                    header->markEpoch.load(std::memory_order_acquire);
                while (observed != impl->markEpoch) {
                    if (header->markEpoch.compare_exchange_weak(
                            observed, impl->markEpoch, std::memory_order_acq_rel,
                            std::memory_order_acquire)) {
                        impl->markQueue.push_back(header);
                        impl->accountRegionLive(header);
                        concurrentMarkWorkerIdle.store(
                            false, std::memory_order_release);
                        impl->concurrentMarkedObjects.fetch_add(
                            1, std::memory_order_relaxed);
                        impl->concurrentMarkedBytes.fetch_add(
                            header->payloadBytes, std::memory_order_relaxed);
                        impl->markCondition.notify_one();
                        break;
                    }
                }
            }

        private:
            Runtime& runtime_;
        } visitor(*this);

        for (;;) {
            ObjectHeader* header = nullptr;
            {
                std::unique_lock markLock(impl_->markMutex);
                impl_->markCondition.wait(markLock, [this] {
                    return !impl_->markQueue.empty() ||
                           (impl_->markClosing &&
                            impl_->activeMarkWorkers == 0) ||
                           impl_->markFailure != nullptr;
                });
                if (impl_->markQueue.empty()) {
                    if (impl_->markFailure ||
                        (impl_->markClosing &&
                         impl_->activeMarkWorkers == 0)) {
                        break;
                    }
                    continue;
                }
                concurrentMarkWorkerIdle.store(false,
                                               std::memory_order_release);
                header = impl_->markQueue.front();
                impl_->markQueue.pop_front();
                ++impl_->activeMarkWorkers;
            }

            for (const TypeDescriptor* type = header->type; type != nullptr;
                 type = type->super) {
                auto* payload = static_cast<std::byte*>(payloadOf(header));
                for (std::uint32_t index = 0;
                     index < type->referenceOffsetCount; ++index) {
                    const std::uint32_t offset = type->referenceOffsets[index];
                    if (offset + sizeof(RefSlot) > header->payloadBytes)
                        throw std::logic_error(
                            "type descriptor reference offset is invalid");
                    visitor.visit(*reinterpret_cast<RefSlot*>(payload + offset));
                }
                if (type->trace != nullptr)
                    type->trace(payload, header->payloadBytes, visitor);
            }
            {
                std::scoped_lock markLock(impl_->markMutex);
                --impl_->activeMarkWorkers;
                if (impl_->markQueue.empty() &&
                    impl_->activeMarkWorkers == 0) {
                    concurrentMarkWorkerIdle.store(true,
                                                   std::memory_order_release);
                    impl_->markCondition.notify_all();
                }
            }
        }
        } catch (...) {
            std::scoped_lock markLock(impl_->markMutex);
            if (!impl_->markFailure)
                impl_->markFailure = std::current_exception();
            impl_->markClosing = true;
            concurrentMarkWorkerIdle.store(true, std::memory_order_release);
            impl_->markCondition.notify_all();
        }
        });
        }
        impl_->markCondition.notify_all();
    } catch (...) {
        // startConcurrentMark can fail after markActive is published (for
        // example while a worker thread is being created).  Leaving even one
        // marker alive here poisons every later young cycle with
        // "cannot collect nursery during concurrent mark".  Close and join
        // the partially-created worker set before releasing the mutators.
        {
            std::scoped_lock markLock(impl_->markMutex);
            impl_->markClosing = true;
            impl_->markSealed = true;
            impl_->markCondition.notify_all();
        }
        for (std::thread& worker : impl_->markerThreads) {
            if (worker.joinable())
                worker.join();
        }
        impl_->markerThreads.clear();
        impl_->markActive.store(false, std::memory_order_release);
        fastBarrierState.fetch_and(~FastBarrierMarkActive,
                                   std::memory_order_release);
        rootFrameBarrierActive.store(false, std::memory_order_release);
        impl_->clearSoftReferencesThisCycle = false;
        concurrentMarkWorkerIdle.store(true, std::memory_order_release);
        impl_->resumeMutators();
        recordPauseMicros(lastInitialMarkPauseMicros, pauseStarted);
        throw;
    }
    impl_->resumeMutators();
    recordPauseMicros(lastInitialMarkPauseMicros, pauseStarted);
    impl_->concurrentMarkCycleLock.emplace(std::move(cycleLock));
    gcCycleActive = true;
}

CollectionStats Runtime::finishConcurrentMark() {
    auto phaseLock = acquireGcPhase(*this, impl_->phaseTransitionMutex);
    GcPhaseScope phaseScope;
    if (!impl_->markActive.load(std::memory_order_acquire))
        throw std::logic_error("no concurrent mark cycle is active");
    if (!gcCycleActive || !impl_->concurrentMarkCycleLock.has_value())
        throw std::logic_error(
            "concurrent mark must finish on its owning thread");
    struct ReleaseConcurrentMarkCycle final {
        Impl* impl = nullptr;
        ~ReleaseConcurrentMarkCycle() {
            impl->concurrentMarkCycleLock.reset();
            gcCycleActive = false;
        }
    } releaseConcurrentMarkCycle{impl_};
    CollectionStats stats{};
    std::vector<std::unique_ptr<Impl::Region>> pendingReclamation;
    // Let the concurrent marker drain and exit while mutators are still
    // running. Stores and newly-published objects after marker exit continue
    // appending to markQueue; the short remark below drains that tail after
    // the safepoint handshake.
    {
        std::scoped_lock markLock(impl_->markMutex);
        impl_->markClosing = true;
        impl_->markCondition.notify_all();
    }
    for (std::thread& worker : impl_->markerThreads) {
        if (worker.joinable())
            worker.join();
    }
    impl_->markerThreads.clear();
    {
        std::scoped_lock markLock(impl_->markMutex);
        impl_->markSealed = true;
        if (impl_->markFailure) {
            impl_->markActive.store(false, std::memory_order_release);
            fastBarrierState.fetch_and(~FastBarrierMarkActive,
                                       std::memory_order_release);
            rootFrameBarrierActive.store(false, std::memory_order_release);
            impl_->clearSoftReferencesThisCycle = false;
            concurrentMarkWorkerIdle.store(true, std::memory_order_release);
            std::rethrow_exception(impl_->markFailure);
        }
    }
    if (impl_->config.remarkTailHook != nullptr)
        impl_->config.remarkTailHook(impl_->config.remarkTailHookContext);

    const auto pauseStarted = std::chrono::steady_clock::now();
    try {
        // The remark handshake itself can throw.  It must be inside this
        // recovery scope: otherwise markActive/rootFrameBarrierActive remain
        // set and the automatic collector loops forever in a poisoned phase.
        impl_->parkMutators(static_cast<Impl::ThreadState*>(currentThread));
        stats.epoch = impl_->markEpoch;
        stats.roots = impl_->concurrentRoots.load(std::memory_order_relaxed);
        stats.threadRootSnapshots =
            impl_->concurrentThreadRootSnapshots.load(
                std::memory_order_relaxed);
        stats.threadRootSlots = impl_->concurrentThreadRootSlots.load(
            std::memory_order_relaxed);
        stats.markedObjects =
            impl_->concurrentMarkedObjects.load(std::memory_order_relaxed);
        stats.markedBytes =
            impl_->concurrentMarkedBytes.load(std::memory_order_relaxed);

        {
        std::scoped_lock heapLock(impl_->heapMutex);
        std::scoped_lock threadLock(impl_->threadMutex);
        std::scoped_lock publicationLock(impl_->rootPublicationMutex);
        std::vector<ObjectHeader*> remarkWork;
        {
            std::scoped_lock markLock(impl_->markMutex);
            remarkWork.reserve(impl_->markQueue.size());
            while (!impl_->markQueue.empty()) {
                remarkWork.push_back(impl_->markQueue.front());
                impl_->markQueue.pop_front();
            }
        }
        class RemarkVisitor final : public TraceVisitor {
        public:
            RemarkVisitor(Runtime& runtime, CollectionStats& stats,
                          std::vector<ObjectHeader*>& work)
                : runtime_(runtime), stats_(stats), work_(work) {}
            void visit(RefSlot& slot) override {
                void* object = runtime_.loadBarrier(slot);
                if (object == nullptr)
                    return;
                auto* header = headerOf(object);
                auto* impl = runtime_.impl_;
                std::uint32_t observed =
                    header->markEpoch.load(std::memory_order_acquire);
                while (observed != impl->markEpoch) {
                    if (header->markEpoch.compare_exchange_weak(
                            observed, impl->markEpoch,
                            std::memory_order_acq_rel,
                            std::memory_order_acquire)) {
                        work_.push_back(header);
                        impl->accountRegionLive(header);
                        ++stats_.markedObjects;
                        stats_.markedBytes += header->payloadBytes;
                        break;
                    }
                }
            }
        private:
            Runtime& runtime_;
            CollectionStats& stats_;
            std::vector<ObjectHeader*>& work_;
        } remarkVisitor(*this, stats, remarkWork);

        auto drainRemarkWork = [&] {
            if (remarkWork.empty())
                return;

            const std::uint32_t workerCount = std::min<std::uint32_t>(
                impl_->configuredMarkWorkers(),
                std::max<std::uint32_t>(
                    1U, static_cast<std::uint32_t>(remarkWork.size())));
            stats.remarkWorkers = std::max(stats.remarkWorkers, workerCount);
            if (workerCount == 1U) {
                while (!remarkWork.empty()) {
                    ObjectHeader* header = remarkWork.back();
                    remarkWork.pop_back();
                    for (const TypeDescriptor* type = header->type;
                         type != nullptr; type = type->super) {
                        auto* payload =
                            static_cast<std::byte*>(payloadOf(header));
                        for (std::uint32_t index = 0;
                             index < type->referenceOffsetCount; ++index) {
                            remarkVisitor.visit(*reinterpret_cast<RefSlot*>(
                                payload + type->referenceOffsets[index]));
                        }
                        if (type->trace != nullptr) {
                            type->trace(payload, header->payloadBytes,
                                        remarkVisitor);
                        }
                    }
                }
                return;
            }

            struct WorkerQueue final {
                std::mutex mutex;
                std::deque<ObjectHeader*> work;
            };
            struct WorkerTotals final {
                std::uint64_t markedObjects = 0;
                std::uint64_t markedBytes = 0;
            };
            struct ParallelRemark final {
                std::vector<std::unique_ptr<WorkerQueue>> queues;
                std::atomic<std::size_t> available{0};
                std::atomic<std::size_t> outstanding{0};
                std::atomic<std::uint64_t> stolen{0};
                std::atomic<std::uint64_t> signal{0};
                std::atomic<std::uint32_t> readyWorkers{0};
                std::atomic<bool> workersStarted{false};
                std::mutex failureMutex;
                std::exception_ptr failure;
            } parallel;

            parallel.queues.reserve(workerCount);
            for (std::uint32_t index = 0; index < workerCount; ++index)
                parallel.queues.push_back(std::make_unique<WorkerQueue>());
            // The detached global tail belongs to worker zero. Other workers
            // immediately steal from its opposite end, which avoids a second
            // serial distribution pass and exercises the same balancing path
            // used for recursively discovered graph work.
            for (ObjectHeader* header : remarkWork)
                parallel.queues.front()->work.push_back(header);
            parallel.available.store(remarkWork.size(),
                                     std::memory_order_release);
            parallel.outstanding.store(remarkWork.size(),
                                       std::memory_order_release);
            remarkWork.clear();

            std::vector<WorkerTotals> totals(workerCount);
            std::vector<std::thread> workers;
            workers.reserve(workerCount);
            for (std::uint32_t workerIndex = 0;
                 workerIndex < workerCount; ++workerIndex) {
                workers.emplace_back([&, workerIndex] {
                    parallel.readyWorkers.fetch_add(
                        1, std::memory_order_acq_rel);
                    parallel.readyWorkers.notify_one();
                    parallel.workersStarted.wait(
                        false, std::memory_order_acquire);
                    if (workerIndex == 0 && workerCount > 1U) {
                        // Let at least one thief contend for the detached
                        // global tail before its original owner starts popping
                        // from the back. This prevents a tiny tail from being
                        // consumed by the first-created OS thread before the
                        // rest of the dynamic worker set is even scheduled.
                        while (parallel.stolen.load(
                                   std::memory_order_acquire) == 0 &&
                               parallel.available.load(
                                   std::memory_order_acquire) > 1U) {
                            std::this_thread::yield();
                        }
                    }
                    auto pushLocal = [&](ObjectHeader* header) {
                        parallel.outstanding.fetch_add(
                            1, std::memory_order_acq_rel);
                        {
                            std::scoped_lock queueLock(
                                parallel.queues[workerIndex]->mutex);
                            parallel.queues[workerIndex]->work.push_back(
                                header);
                        }
                        parallel.available.fetch_add(
                            1, std::memory_order_release);
                        parallel.signal.fetch_add(
                            1, std::memory_order_release);
                        parallel.signal.notify_one();
                    };

                    class ParallelRemarkVisitor final : public TraceVisitor {
                    public:
                        ParallelRemarkVisitor(
                                Runtime& runtime, WorkerTotals& totals,
                                decltype(pushLocal)& push)
                            : runtime_(runtime), totals_(totals), push_(push) {}

                        void visit(RefSlot& slot) override {
                            void* object = runtime_.loadBarrier(slot);
                            if (object == nullptr)
                                return;
                            ObjectHeader* header = headerOf(object);
                            std::uint32_t observed = header->markEpoch.load(
                                std::memory_order_acquire);
                            while (observed != runtime_.impl_->markEpoch) {
                                if (header->markEpoch.compare_exchange_weak(
                                        observed,
                                        runtime_.impl_->markEpoch,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
                                    runtime_.impl_->accountRegionLive(header);
                                    ++totals_.markedObjects;
                                    totals_.markedBytes +=
                                        header->payloadBytes;
                                    push_(header);
                                    break;
                                }
                            }
                        }

                    private:
                        Runtime& runtime_;
                        WorkerTotals& totals_;
                        decltype(pushLocal)& push_;
                    } visitor(*this, totals[workerIndex], pushLocal);

                    auto take = [&]() -> ObjectHeader* {
                        {
                            std::scoped_lock queueLock(
                                parallel.queues[workerIndex]->mutex);
                            auto& local =
                                parallel.queues[workerIndex]->work;
                            if (!local.empty()) {
                                ObjectHeader* result = local.back();
                                local.pop_back();
                                parallel.available.fetch_sub(
                                    1, std::memory_order_acq_rel);
                                return result;
                            }
                        }
                        for (std::uint32_t offset = 1;
                             offset < workerCount; ++offset) {
                            const std::uint32_t victim =
                                (workerIndex + offset) % workerCount;
                            std::unique_lock queueLock(
                                parallel.queues[victim]->mutex,
                                std::try_to_lock);
                            if (!queueLock.owns_lock() ||
                                parallel.queues[victim]->work.empty()) {
                                continue;
                            }
                            ObjectHeader* result =
                                parallel.queues[victim]->work.front();
                            parallel.queues[victim]->work.pop_front();
                            parallel.available.fetch_sub(
                                1, std::memory_order_acq_rel);
                            parallel.stolen.fetch_add(
                                1, std::memory_order_relaxed);
                            return result;
                        }
                        return nullptr;
                    };

                    try {
                        for (;;) {
                            {
                                std::scoped_lock failureLock(
                                    parallel.failureMutex);
                                if (parallel.failure != nullptr)
                                    return;
                            }
                            ObjectHeader* header = take();
                            if (header == nullptr) {
                                if (parallel.outstanding.load(
                                        std::memory_order_acquire) == 0) {
                                    return;
                                }
                                const std::uint64_t observedSignal =
                                    parallel.signal.load(
                                        std::memory_order_acquire);
                                bool failed = false;
                                {
                                    std::scoped_lock failureLock(
                                        parallel.failureMutex);
                                    failed = parallel.failure != nullptr;
                                }
                                if (failed || parallel.available.load(
                                        std::memory_order_acquire) != 0 ||
                                    parallel.outstanding.load(
                                        std::memory_order_acquire) == 0) {
                                    continue;
                                }
                                // atomic::wait cannot lose a transition that
                                // happens between the predicate check and the
                                // actual sleep: a changed signal returns
                                // immediately instead of requiring a queued
                                // condition-variable notification.
                                parallel.signal.wait(
                                    observedSignal,
                                    std::memory_order_acquire);
                                continue;
                            }

                            for (const TypeDescriptor* type = header->type;
                                 type != nullptr; type = type->super) {
                                auto* payload = static_cast<std::byte*>(
                                    payloadOf(header));
                                for (std::uint32_t index = 0;
                                     index < type->referenceOffsetCount;
                                     ++index) {
                                    const std::uint32_t offset =
                                        type->referenceOffsets[index];
                                    if (offset + sizeof(RefSlot) >
                                        header->payloadBytes) {
                                        throw std::logic_error(
                                            "type descriptor reference "
                                            "offset is invalid");
                                    }
                                    visitor.visit(
                                        *reinterpret_cast<RefSlot*>(
                                            payload + offset));
                                }
                                if (type->trace != nullptr) {
                                    type->trace(payload,
                                                header->payloadBytes,
                                                visitor);
                                }
                            }
                            (void)parallel.outstanding.fetch_sub(
                                1, std::memory_order_acq_rel);
                            parallel.signal.fetch_add(
                                1, std::memory_order_release);
                            parallel.signal.notify_all();
                        }
                    } catch (...) {
                        {
                            std::scoped_lock failureLock(
                                parallel.failureMutex);
                            if (parallel.failure == nullptr)
                                parallel.failure = std::current_exception();
                        }
                        parallel.signal.fetch_add(
                            1, std::memory_order_release);
                        parallel.signal.notify_all();
                    }
                });
            }
            for (;;) {
                const std::uint32_t observed =
                    parallel.readyWorkers.load(std::memory_order_acquire);
                if (observed == workerCount)
                    break;
                parallel.readyWorkers.wait(observed,
                                           std::memory_order_acquire);
            }
            parallel.workersStarted.store(true, std::memory_order_release);
            parallel.workersStarted.notify_all();
            for (std::thread& worker : workers)
                worker.join();
            if (parallel.failure != nullptr)
                std::rethrow_exception(parallel.failure);
            for (const WorkerTotals& total : totals) {
                stats.markedObjects += total.markedObjects;
                stats.markedBytes += total.markedBytes;
            }
            stats.stolenMarkTasks += parallel.stolen.load(
                std::memory_order_relaxed);
        };

        auto processRemarkEphemerons = [&] {
            for (;;) {
                bool promoted = false;
                {
                    std::scoped_lock ephemeronLock(impl_->ephemeronMutex);
                    for (const auto& cell : impl_->ephemerons) {
                        if (!cell->active)
                            continue;
                        const Ref key(cell->key.load(std::memory_order_acquire));
                        if (key.isNull() ||
                            headerOf(key.address())->markEpoch.load(
                                std::memory_order_acquire) != impl_->markEpoch)
                            continue;
                        const std::size_t before = remarkWork.size();
                        remarkVisitor.visit(cell->value);
                        promoted = promoted || remarkWork.size() != before;
                    }
                }
                drainRemarkWork();
                if (!promoted)
                    break;
            }
        };
        drainRemarkWork();
        processRemarkEphemerons();

        {
            std::scoped_lock finalizerLock(impl_->finalizerMutex);
            for (const auto& cell : impl_->finalizers) {
                if (cell->state != Impl::FinalizerState::Registered)
                    continue;
                const Ref object(cell->object.load(std::memory_order_acquire));
                if (!object.isNull() &&
                    headerOf(object.address())->markEpoch.load(
                        std::memory_order_acquire) != impl_->markEpoch) {
                    cell->state = Impl::FinalizerState::Pending;
                    remarkVisitor.visit(cell->object);
                }
            }
        }
        drainRemarkWork();
        processRemarkEphemerons();
        {
            std::scoped_lock ephemeronLock(impl_->ephemeronMutex);
            for (const auto& cell : impl_->ephemerons) {
                if (!cell->active)
                    continue;
                const Ref key(cell->key.load(std::memory_order_acquire));
                if (key.isNull() ||
                    headerOf(key.address())->markEpoch.load(
                        std::memory_order_acquire) != impl_->markEpoch) {
                    cell->key.store(0, std::memory_order_release);
                    cell->value.store(0, std::memory_order_release);
                } else {
                    (void)loadBarrier(cell->key);
                    (void)loadBarrier(cell->value);
                }
            }
        }
        {
            std::scoped_lock handleLock(impl_->handleMutex);
            for (std::uint32_t index = 0;
                 index < impl_->handles.size(); ++index) {
                auto& cell = impl_->handles[index];
                if (!cell->active || !Impl::handleNeedsClearing(
                        cell->kind,
                        impl_->clearSoftReferencesThisCycle))
                    continue;
                const Ref weak(cell->reference.load(std::memory_order_acquire));
                if (!weak.isNull() &&
                    headerOf(weak.address())->markEpoch.load(
                        std::memory_order_acquire) != impl_->markEpoch)
                    impl_->enqueueReference(index, *cell);
            }
        }
        {
            std::scoped_lock finalizerLock(impl_->finalizerMutex);
            for (const auto& cell : impl_->finalizers) {
                if (cell->state != Impl::FinalizerState::Finalized)
                    continue;
                const Ref object(cell->object.load(std::memory_order_acquire));
                if (!object.isNull() &&
                    headerOf(object.address())->markEpoch.load(
                        std::memory_order_acquire) != impl_->markEpoch) {
                    cell->object.store(0, std::memory_order_release);
                    cell->state = Impl::FinalizerState::Cleared;
                }
            }
            std::erase_if(impl_->finalizers, [](const auto& cell) {
                return cell->state == Impl::FinalizerState::Cleared;
            });
        }
        // Exact remembered owners are weak GC metadata.  Remove dead owners
        // before pending reclamation can release the regions containing them.
        impl_->pruneRememberedOwners(impl_->markEpoch);
        for (auto region = impl_->regions.begin(); region != impl_->regions.end();) {
            if ((*region)->retired) {
                if ((*region)->retiredEpoch < impl_->markEpoch) {
                    impl_->invalidateTlabsFor(region->get());
                    pendingReclamation.push_back(std::move(*region));
                    region = impl_->regions.erase(region);
                    ++stats.reclaimedRegions;
                } else {
                    ++region;
                }
                continue;
            }
            const std::uint64_t live =
                (*region)->liveObjects.load(std::memory_order_acquire);
            const std::uint64_t total = (*region)->countObjects();
            if (live > total)
                throw std::logic_error(
                    "concurrent region live count exceeds object count");
            stats.deadObjects += total - live;
            if (live == 0) {
                impl_->invalidateTlabsFor(region->get());
                pendingReclamation.push_back(std::move(*region));
                region = impl_->regions.erase(region);
                ++stats.reclaimedRegions;
            } else {
                ++region;
            }
        }
        impl_->lastFullMarkEpoch = impl_->markEpoch;
        impl_->markActive.store(false, std::memory_order_release);
        fastBarrierState.fetch_and(~FastBarrierMarkActive,
                                   std::memory_order_release);
        rootFrameBarrierActive.store(false, std::memory_order_release);
        impl_->clearSoftReferencesThisCycle = false;
        concurrentMarkWorkerIdle.store(true, std::memory_order_release);
        }

        // Finalizer discovery and callback execution are one atomic phase
        // with respect to mutators.  This preserves hxcpp/CFFI weak-token
        // semantics while callbacks still run outside the heap/thread locks.
        const auto finalizerStarted = std::chrono::steady_clock::now();
        stats.finalizers = drainFinalizers();
        stats.finalizerMicros = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - finalizerStarted).count());
    } catch (...) {
        impl_->markActive.store(false, std::memory_order_release);
        fastBarrierState.fetch_and(~FastBarrierMarkActive,
                                   std::memory_order_release);
        rootFrameBarrierActive.store(false, std::memory_order_release);
        impl_->clearSoftReferencesThisCycle = false;
        concurrentMarkWorkerIdle.store(true, std::memory_order_release);
        impl_->resumeMutators();
        recordPauseMicros(lastRemarkPauseMicros, pauseStarted);
        throw;
    }
    impl_->resumeMutators();
    recordPauseMicros(lastRemarkPauseMicros, pauseStarted);
    if (deadTypeCensusEnabled) {
        struct TypeTotals final {
            std::uint64_t objects = 0;
            std::uint64_t bytes = 0;
        };
        std::unordered_map<const TypeDescriptor*, TypeTotals> totals;
        for (const auto& region : pendingReclamation) {
            for (ObjectHeader* header : region->snapshotObjects()) {
                auto& total = totals[header->type];
                ++total.objects;
                total.bytes += header->totalBytes;
            }
        }
        std::vector<std::pair<const TypeDescriptor*, TypeTotals>> ordered(
            totals.begin(), totals.end());
        std::sort(ordered.begin(), ordered.end(), [](const auto& left,
                                                      const auto& right) {
            if (left.second.objects != right.second.objects)
                return left.second.objects > right.second.objects;
            return left.second.bytes > right.second.bytes;
        });
        std::ostringstream output;
        output << "novagc:dead_types epoch=" << impl_->markEpoch;
        const std::size_t limit = std::min<std::size_t>(ordered.size(), 12);
        for (std::size_t index = 0; index < limit; ++index) {
            const auto& [type, total] = ordered[index];
            output << ' ' << (type != nullptr && type->name != nullptr
                                  ? type->name : "<unknown>")
                   << ":objects=" << total.objects
                   << ",mb=" << total.bytes / (1024U * 1024U)
                   << ",avg_bytes="
                   << (total.objects == 0 ? 0 : total.bytes / total.objects)
                   << ",type_id=" << (type == nullptr ? 0 : type->typeId);
        }
        std::scoped_lock censusLock(deadTypeCensusMutex);
        lastDeadTypeCensus = output.str();
    }
    // Region table removal is part of the atomic remark, but returning pages
    // to the OS is not. Destruct them only after mutators have resumed.
    impl_->recycleRegions(pendingReclamation);
    impl_->adaptWorkerTarget(
        impl_->adaptiveMarkWorkerTarget, impl_->config.markWorkerCount != 0,
        lastRemarkPauseMicros.load(std::memory_order_acquire),
        stats.markedObjects);
    return stats;
}

bool Runtime::concurrentMarkActive() const noexcept {
    return impl_->markActive.load(std::memory_order_acquire);
}

View Runtime::goodView() const noexcept {
    return impl_->view.load(std::memory_order_acquire);
}

extern "C" bool __hxcpp_zgc_concurrent_mark_worker_idle() noexcept {
    return concurrentMarkWorkerIdle.load(std::memory_order_acquire);
}

extern "C" bool __hxcpp_zgc_concurrent_relocation_worker_done() noexcept {
    return concurrentRelocationWorkerDone.load(std::memory_order_acquire);
}

extern "C" std::uint64_t __hxcpp_zgc_last_initial_mark_pause_us() noexcept {
    return lastInitialMarkPauseMicros.load(std::memory_order_acquire);
}

extern "C" std::uint64_t __hxcpp_zgc_last_remark_pause_us() noexcept {
    return lastRemarkPauseMicros.load(std::memory_order_acquire);
}

extern "C" std::uint64_t
__hxcpp_zgc_last_relocation_select_pause_us() noexcept {
    return lastRelocationSelectPauseMicros.load(std::memory_order_acquire);
}

extern "C" std::uint64_t
__hxcpp_zgc_last_relocation_remap_pause_us() noexcept {
    return lastRelocationRemapPauseMicros.load(std::memory_order_acquire);
}

extern "C" const char* __hxcpp_zgc_last_dead_type_census() noexcept {
    thread_local std::string snapshot;
    std::scoped_lock censusLock(deadTypeCensusMutex);
    snapshot = lastDeadTypeCensus;
    return snapshot.c_str();
}

extern "C" const char* __hxcpp_zgc_last_handshake_diagnostic() noexcept {
    thread_local std::string snapshot;
    std::scoped_lock diagnosticLock(handshakeDiagnosticMutex);
    snapshot.swap(lastHandshakeDiagnostic);
    lastHandshakeDiagnostic.clear();
    return snapshot.c_str();
}

extern "C" void __hxcpp_zgc_set_selective_relocation(bool enabled) noexcept {
    selectiveAutomaticRelocation.store(enabled, std::memory_order_release);
}

} // namespace hx::gc

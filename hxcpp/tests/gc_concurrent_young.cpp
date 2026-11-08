#include <hx/gc/runtime.hpp>

#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>

namespace {

struct Node final {
    hx::gc::RefSlot next{0};
    std::uint64_t value = 0;
};

std::atomic<std::uint64_t> traced{0};
std::atomic<bool> traceStarted{false};
std::atomic<bool> rejectDeadOldTrace{false};
constexpr std::uint64_t DeadOldSentinel = 0xd34d0dd0ULL;

void traceNode(void* payload, std::size_t, hx::gc::TraceVisitor& visitor) {
    auto* node = static_cast<Node*>(payload);
    if (rejectDeadOldTrace.load(std::memory_order_acquire) &&
        node->value == DeadOldSentinel) {
        throw std::logic_error(
            "young final remap traced an unreachable old object");
    }
    traceStarted.store(true, std::memory_order_release);
    traced.fetch_add(1, std::memory_order_relaxed);
    visitor.visit(node->next);
    std::this_thread::sleep_for(std::chrono::microseconds(100));
}

const hx::gc::TypeDescriptor NodeType{
    0x434f4e43594f554eULL,
    "test.ConcurrentYoungNode",
    nullptr,
    sizeof(Node),
    alignof(Node),
    0,
    hx::gc::TypeContainsReferences,
    0,
    nullptr,
    &traceNode,
    nullptr,
};

[[nodiscard]] Node* allocateNode(std::uint64_t value) {
    auto& runtime = hx::gc::Runtime::instance();
    void* storage = runtime.allocate(NodeType);
    auto* node = ::new (storage) Node();
    node->value = value;
    runtime.publish(node);
    return node;
}

void disableAutomaticGc() {
#ifdef _WIN32
    _putenv_s("HXCPP_NOVAGC_AUTO_GC", "0");
    _putenv_s("HXCPP_NOVAGC_MARK_WORKERS", "4");
#else
    setenv("HXCPP_NOVAGC_AUTO_GC", "0", 1);
    setenv("HXCPP_NOVAGC_MARK_WORKERS", "4", 1);
#endif
}

} // namespace

int main() {
    disableAutomaticGc();
    auto& runtime = hx::gc::Runtime::instance();
    runtime.initialize();
    runtime.attachCurrentThread();

    constexpr std::uint64_t ChainLength = 2048;
    Node* head = allocateNode(0);
    Node* cursor = head;
    for (std::uint64_t index = 1; index < ChainLength; ++index) {
        Node* next = allocateNode(index);
        runtime.storeBarrier(cursor, cursor->next, next);
        cursor = next;
    }
    hx::gc::RefSlot chainRoot{runtime.makeRef(head).bits()};
    hx::gc::RefSlot* chainRoots[]{&chainRoot};

    // These objects deliberately have no exact root in the initial young
    // snapshot.  The mutator publishes function-local-static-style roots only
    // after concurrent tracing has begun, matching lazy HX_ literal creation
    // in generated Haxe code.
    constexpr std::size_t LateStaticRootCount = 128;
    std::array<Node*, LateStaticRootCount> lateStaticCandidates{};
    std::array<hx::gc::RefSlot, LateStaticRootCount> lateStaticRoots{};
    for (std::size_t index = 0; index < LateStaticRootCount; ++index) {
        lateStaticCandidates[index] = allocateNode(
            0x4c4154450000ULL + index);
    }

    std::atomic<bool> mutatorReady{false};
    std::atomic<bool> collectionFinished{false};
    std::atomic<bool> allocatedDuringMark{false};
    std::atomic<std::uint64_t> mutatorProgress{0};
    std::atomic<std::uint64_t> retainedFreshValue{0};
    std::thread mutator([&] {
        runtime.attachCurrentThread();
        hx::gc::RefSlot freshRoot{0};
        hx::gc::RefSlot sourceRoot{runtime.makeRef(head).bits()};
        hx::gc::RefSlot* freshRoots[]{&freshRoot, &sourceRoot};
        {
            hx::gc::RootScope roots(freshRoots, 2, 0x43594f55U);
            mutatorReady.store(true, std::memory_order_release);
            while (!traceStarted.load(std::memory_order_acquire)) {
                runtime.safepointPoll();
                std::this_thread::yield();
            }
            for (std::size_t index = 0; index < LateStaticRootCount; ++index) {
                lateStaticRoots[index].store(
                    runtime.makeRef(lateStaticCandidates[index]).bits(),
                    std::memory_order_release);
                runtime.registerStaticRoot(lateStaticRoots[index]);
            }
            Node* fresh = allocateNode(0xdecafbadULL);
            Node* source = static_cast<Node*>(
                runtime.loadBarrier(sourceRoot));
            runtime.storeBarrier(fresh, fresh->next, source);
            runtime.storeBarrier(nullptr, freshRoot, fresh);
            allocatedDuringMark.store(true, std::memory_order_release);
            while (!collectionFinished.load(std::memory_order_acquire)) {
                mutatorProgress.fetch_add(1, std::memory_order_relaxed);
                runtime.safepointPoll();
            }
            fresh = static_cast<Node*>(runtime.loadBarrier(freshRoot));
            if (fresh != nullptr) {
                Node* retainedSource = static_cast<Node*>(
                    runtime.loadBarrier(fresh->next));
                if (retainedSource != nullptr &&
                    retainedSource->value == 0) {
                    retainedFreshValue.store(fresh->value,
                                             std::memory_order_release);
                }
            }
        }
        runtime.detachCurrentThread();
    });

    while (!mutatorReady.load(std::memory_order_acquire))
        std::this_thread::yield();

    hx::gc::YoungCollectionStats cycle{};
    {
        hx::gc::RootScope roots(chainRoots, 1, 0x43594f56U);
        cycle = runtime.collectYoung();
        head = static_cast<Node*>(runtime.loadBarrier(chainRoot));
    }
    collectionFinished.store(true, std::memory_order_release);
    mutator.join();

    bool retainedLateStaticRoots = true;
    for (std::size_t index = 0; index < LateStaticRootCount; ++index) {
        auto* retainedLate = static_cast<Node*>(
            runtime.loadBarrier(lateStaticRoots[index]));
        if (retainedLate == nullptr ||
            retainedLate->value != 0x4c4154450000ULL + index) {
            retainedLateStaticRoots = false;
        }
        runtime.unregisterStaticRoot(lateStaticRoots[index]);
        lateStaticRoots[index].store(0, std::memory_order_release);
    }

    std::uint64_t retained = 0;
    for (Node* node = head; node != nullptr;
         node = static_cast<Node*>(runtime.loadBarrier(node->next))) {
        ++retained;
    }

    if (!cycle.concurrentMark || cycle.concurrentMarkMicros == 0 ||
        cycle.initialPauseMicros == 0 || cycle.finalPauseMicros == 0 ||
        cycle.pauseMicros != cycle.initialPauseMicros +
                                 cycle.finalPauseMicros ||
        !allocatedDuringMark.load(std::memory_order_acquire) ||
        !retainedLateStaticRoots ||
        mutatorProgress.load(std::memory_order_acquire) == 0 ||
        retainedFreshValue.load(std::memory_order_acquire) != 0xdecafbadULL ||
        retained != ChainLength || traced.load(std::memory_order_acquire) <
                                      ChainLength) {
        std::cerr << "concurrent young mark failed"
                  << " concurrent=" << cycle.concurrentMark
                  << " concurrent_us=" << cycle.concurrentMarkMicros
                  << " initial_us=" << cycle.initialPauseMicros
                  << " final_us=" << cycle.finalPauseMicros
                  << " progress=" << mutatorProgress.load()
                  << " late_static=" << retainedLateStaticRoots
                  << " fresh=" << retainedFreshValue.load()
                  << " retained=" << retained
                  << " traced=" << traced.load() << '\n';
        return 1;
    }

    // Promote an object to Old, then make it unreachable without running a
    // full collection.  Its bytes remain in a mixed-live Old Region and are
    // not part of a young cycle's live set.  A final remap that scans the
    // entire heap would invoke its trace function (and, in a real program,
    // could follow a field whose target Region has already been reused).
    Node* deadOld = allocateNode(DeadOldSentinel);
    hx::gc::RefSlot deadOldRoot{runtime.makeRef(deadOld).bits()};
    hx::gc::RefSlot* deadOldRoots[]{&deadOldRoot};
    bool deadOldPromoted = false;
    {
        hx::gc::RootScope roots(deadOldRoots, 1, 0x43594f57U);
        for (std::uint32_t cycleIndex = 0;
             cycleIndex < 16 && !deadOldPromoted; ++cycleIndex) {
            const auto promotion = runtime.collectYoung();
            deadOld = static_cast<Node*>(runtime.loadBarrier(deadOldRoot));
            deadOldPromoted = promotion.promotedObjects != 0;
        }
    }
    if (deadOld == nullptr || !deadOldPromoted) {
        std::cerr << "dead-old remap fixture was not promoted\n";
        return 1;
    }
    deadOldRoot.store(0, std::memory_order_release);
    rejectDeadOldTrace.store(true, std::memory_order_release);
    Node* fresh = allocateNode(0x51504f53ULL);
    hx::gc::RefSlot freshRoot{runtime.makeRef(fresh).bits()};
    hx::gc::RefSlot* freshRoots[]{&freshRoot};
    {
        hx::gc::RootScope roots(freshRoots, 1, 0x43594f58U);
        (void)runtime.collectYoung();
        fresh = static_cast<Node*>(runtime.loadBarrier(freshRoot));
    }
    rejectDeadOldTrace.store(false, std::memory_order_release);
    if (fresh == nullptr || fresh->value != 0x51504f53ULL) {
        std::cerr << "selective young remap lost its fresh root\n";
        return 1;
    }

    // Automatic and emergency allocation recovery can request a collection
    // from different attached threads. Runtime-level phase serialization must
    // park the waiter cooperatively and let both calls complete in sequence;
    // relying only on the public facade lock leaves the emergency path out.
    Node* phaseNode = allocateNode(0x50484153ULL);
    hx::gc::RefSlot phaseRoot{runtime.makeRef(phaseNode).bits()};
    hx::gc::RefSlot* phaseRoots[]{&phaseRoot};
    std::atomic<std::uint32_t> phaseReady{0};
    std::atomic<std::uint32_t> phaseDone{0};
    std::atomic<bool> phaseStart{false};
    std::exception_ptr phaseFailures[2];
    auto phaseCollector = [&](std::uint32_t index) {
        runtime.attachCurrentThread();
        phaseReady.fetch_add(1, std::memory_order_release);
        while (!phaseStart.load(std::memory_order_acquire)) {
            runtime.safepointPoll();
            std::this_thread::yield();
        }
        try {
            (void)runtime.collectYoung();
        } catch (...) {
            phaseFailures[index] = std::current_exception();
        }
        phaseDone.fetch_add(1, std::memory_order_release);
        runtime.detachCurrentThread();
    };
    std::thread firstCollector(phaseCollector, 0U);
    std::thread secondCollector(phaseCollector, 1U);
    {
        hx::gc::RootScope roots(phaseRoots, 1, 0x43594f59U);
        while (phaseReady.load(std::memory_order_acquire) != 2U) {
            runtime.safepointPoll();
            std::this_thread::yield();
        }
        phaseStart.store(true, std::memory_order_release);
        while (phaseDone.load(std::memory_order_acquire) != 2U) {
            runtime.safepointPoll();
            std::this_thread::yield();
        }
        phaseNode = static_cast<Node*>(runtime.loadBarrier(phaseRoot));
    }
    firstCollector.join();
    secondCollector.join();
    if (phaseFailures[0] != nullptr || phaseFailures[1] != nullptr ||
        phaseNode == nullptr || phaseNode->value != 0x50484153ULL) {
        std::cerr << "overlapping young requests were not serialized\n";
        return 1;
    }

    // A persistent pinned handle performs its own moving safepoint isolation.
    // Request it while concurrent Young owns the phase lock: the pin request
    // must cooperatively wait, keep its raw argument exactly rooted across the
    // move, then run isolation as a distinct handshake.
    constexpr std::uint64_t PinChainLength = 256;
    Node* pinHead = allocateNode(0x50494e30ULL);
    Node* pinCursor = pinHead;
    for (std::uint64_t index = 1; index < PinChainLength; ++index) {
        Node* next = allocateNode(0x50494e30ULL + index);
        runtime.storeBarrier(pinCursor, pinCursor->next, next);
        pinCursor = next;
    }
    hx::gc::RefSlot pinRoot{runtime.makeRef(pinHead).bits()};
    hx::gc::RefSlot* pinRoots[]{&pinRoot};
    traceStarted.store(false, std::memory_order_release);
    std::exception_ptr pinCollectionFailure;
    std::thread pinCollector([&] {
        runtime.attachCurrentThread();
        try {
            (void)runtime.collectYoung();
        } catch (...) {
            pinCollectionFailure = std::current_exception();
        }
        runtime.detachCurrentThread();
    });
    hx::gc::Handle pinnedHandle{};
    {
        hx::gc::RootScope roots(pinRoots, 1, 0x43594f5aU);
        while (!traceStarted.load(std::memory_order_acquire)) {
            runtime.safepointPoll();
            std::this_thread::yield();
        }
        pinnedHandle = runtime.createHandle(
            pinHead, hx::gc::HandleKind::Pinned);
        pinHead = static_cast<Node*>(runtime.loadBarrier(pinRoot));
    }
    pinCollector.join();
    auto* pinnedNode = static_cast<Node*>(
        runtime.resolveHandle(pinnedHandle));
    if (pinCollectionFailure != nullptr || pinnedNode == nullptr ||
        pinnedNode->value != 0x50494e30ULL || pinHead == nullptr) {
        std::cerr << "young/pin phase transitions overlapped\n";
        return 1;
    }
    runtime.releaseHandle(pinnedHandle);

    runtime.detachCurrentThread();
    runtime.shutdown();
    std::cout << "hxcpp concurrent young mark passed: concurrent_us="
              << cycle.concurrentMarkMicros
              << " mutator_progress=" << mutatorProgress.load() << '\n';
    return 0;
}

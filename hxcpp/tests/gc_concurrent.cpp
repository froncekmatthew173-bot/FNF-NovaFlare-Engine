#include <hx/gc/runtime.hpp>
#include <hx/gc/internal.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <new>
#include <thread>
#include <vector>

namespace {

struct Node final {
    hx::gc::RefSlot next{0};
    int value = 0;
};

constexpr std::uint32_t NodeReferences[]{
    static_cast<std::uint32_t>(offsetof(Node, next)),
};

const hx::gc::TypeDescriptor NodeType{
    0x434f4e4355524e54ULL,
    "test.ConcurrentNode",
    nullptr,
    sizeof(Node),
    alignof(Node),
    0,
    hx::gc::TypeContainsReferences,
    1,
    NodeReferences,
    nullptr,
    nullptr,
};

struct RemarkTimeoutContext final {
    hx::gc::Runtime* runtime = nullptr;
    std::atomic<bool> armed{false};
    std::atomic<bool> ready{false};
    std::atomic<bool> release{false};
    std::thread blocker;
};

void injectRemarkTimeout(void* rawContext) {
    auto& context = *static_cast<RemarkTimeoutContext*>(rawContext);
    if (!context.armed.exchange(false, std::memory_order_acq_rel))
        return;
    context.ready.store(false, std::memory_order_release);
    context.release.store(false, std::memory_order_release);
    context.blocker = std::thread([&context] {
        context.runtime->attachCurrentThread();
        context.ready.store(true, std::memory_order_release);
        // Intentionally do not poll. finishConcurrentMark must time out and
        // recover its phase state instead of poisoning every later cycle.
        while (!context.release.load(std::memory_order_acquire))
            std::this_thread::yield();
        context.runtime->detachCurrentThread();
    });
    while (!context.ready.load(std::memory_order_acquire))
        std::this_thread::yield();
}

[[nodiscard]] Node* allocateNode(int value) {
    auto& runtime = hx::gc::Runtime::instance();
    void* storage = runtime.allocate(NodeType);
    auto* result = ::new (storage) Node();
    result->value = value;
    runtime.publish(result);
    return result;
}

} // namespace

int main() {
    constexpr int MutatorCount = 4;
    constexpr int AllocationsPerMutator = 5000;
    auto& runtime = hx::gc::Runtime::instance();
    RemarkTimeoutContext remarkTimeout;
    remarkTimeout.runtime = &runtime;
    hx::gc::RuntimeConfig config{};
    config.handshakeTimeoutMilliseconds = 100;
    config.remarkTailHook = &injectRemarkTimeout;
    config.remarkTailHookContext = &remarkTimeout;
    runtime.initialize(config);

    std::atomic<int> ready{0};
    std::atomic<int> completed{0};
    std::atomic<bool> begin{false};
    std::atomic<bool> release{false};
    std::vector<std::thread> mutators;
    mutators.reserve(MutatorCount);

    for (int threadIndex = 0; threadIndex < MutatorCount; ++threadIndex) {
        mutators.emplace_back([&, threadIndex] {
            runtime.attachCurrentThread();
            Node* root = allocateNode(threadIndex);
            hx::gc::RefSlot rootSlot{runtime.makeRef(root).bits()};
            hx::gc::RefSlot* rootSlots[]{&rootSlot};
            {
                hx::gc::RootScope roots(rootSlots, 1,
                    static_cast<std::uint32_t>(0x2000 + threadIndex));
                ready.fetch_add(1, std::memory_order_release);
                while (!begin.load(std::memory_order_acquire)) {
                    runtime.safepointPoll();
                    std::this_thread::yield();
                }

                for (int index = 0; index < AllocationsPerMutator; ++index) {
                    if ((index & 63) == 0)
                        runtime.safepointPoll();
                    auto* currentRoot =
                        static_cast<Node*>(runtime.loadBarrier(rootSlot));
                    Node* node = allocateNode(index);
                    runtime.storeBarrier(currentRoot, currentRoot->next, node);
                }
                completed.fetch_add(1, std::memory_order_release);
                while (!release.load(std::memory_order_acquire)) {
                    runtime.safepointPoll();
                    std::this_thread::yield();
                }
            }
            runtime.detachCurrentThread();
        });
    }

    while (ready.load(std::memory_order_acquire) != MutatorCount)
        std::this_thread::yield();
    runtime.startConcurrentMark();
    begin.store(true, std::memory_order_release);
    while (completed.load(std::memory_order_acquire) != MutatorCount)
        std::this_thread::yield();

    const hx::gc::CollectionStats cycle = runtime.finishConcurrentMark();
    if (cycle.roots != MutatorCount ||
        cycle.threadRootSnapshots != MutatorCount ||
        cycle.threadRootSlots != MutatorCount ||
        cycle.markedObjects < static_cast<std::uint64_t>(MutatorCount)) {
        std::cerr << "concurrent mark did not retain independently "
                     "snapshotted mutator roots\n";
        return 1;
    }
    release.store(true, std::memory_order_release);
    for (auto& mutator : mutators)
        mutator.join();

    runtime.attachCurrentThread();
    const hx::gc::CollectionStats cleanup = runtime.collectBootstrap();
    if (cleanup.markedObjects != 0 || cleanup.reclaimedRegions == 0) {
        std::cerr << "post-concurrency cleanup did not reclaim unrooted regions\n";
        return 1;
    }

    Node* lateExistingRoot = allocateNode(7001);
    Node* constructorChild = allocateNode(7002);
    runtime.startConcurrentMark();

    void* parentStorage = runtime.allocate(NodeType);
    auto* constructorParent = ::new (parentStorage) Node();
    constructorParent->value = 7003;
    constructorParent->next.store(runtime.makeRef(constructorChild).bits(),
                                  std::memory_order_release);
    runtime.publish(constructorParent);

    hx::gc::RefSlot lateRootSlot{
        runtime.makeRef(lateExistingRoot).bits()};
    hx::gc::RefSlot parentRootSlot{
        runtime.makeRef(constructorParent).bits()};
    hx::gc::RefSlot* lateSlots[]{&lateRootSlot, &parentRootSlot};
    {
        hx::gc::RootScope lateRoots(lateSlots, 2, 0x2005);
        const auto lateCycle = runtime.finishConcurrentMark();
        auto* retainedExisting = static_cast<Node*>(
            runtime.loadBarrier(lateRootSlot));
        auto* retainedParent = static_cast<Node*>(
            runtime.loadBarrier(parentRootSlot));
        auto* retainedChild = static_cast<Node*>(
            runtime.loadBarrier(retainedParent->next));
        if (lateCycle.markedObjects < 3 || retainedExisting->value != 7001 ||
            retainedParent->value != 7003 || retainedChild == nullptr ||
            retainedChild->value != 7002) {
            std::cerr << "late root frame or publish tail tracing failed\n";
            return 1;
        }
    }

    Node* emptyPublishChild = allocateNode(7101);
    runtime.startConcurrentMark();
    void* emptyStorage = runtime.allocate(NodeType);
    auto* emptyParent = ::new (emptyStorage) Node();
    emptyParent->value = 7102;
    hx::gc::detail::publishEmpty(emptyParent);
    runtime.storeBarrier(emptyParent, emptyParent->next, emptyPublishChild);
    hx::gc::RefSlot emptyRootSlot{
        runtime.makeRef(emptyParent).bits()};
    hx::gc::RefSlot* emptySlots[]{&emptyRootSlot};
    {
        hx::gc::RootScope emptyRoots(emptySlots, 1, 0x2006);
        const auto emptyCycle = runtime.finishConcurrentMark();
        auto* retainedParent = static_cast<Node*>(
            runtime.loadBarrier(emptyRootSlot));
        auto* retainedChild = static_cast<Node*>(
            runtime.loadBarrier(retainedParent->next));
        if (emptyCycle.markedObjects < 2 || retainedChild == nullptr ||
            retainedParent->value != 7102 || retainedChild->value != 7101) {
            std::cerr << "empty publication insertion barrier failed\n";
            return 1;
        }
    }

    const auto lateCleanup = runtime.collectBootstrap();
    if (lateCleanup.markedObjects != 0 || lateCleanup.reclaimedRegions == 0) {
        std::cerr << "late-root test graph was not reclaimed\n";
        return 1;
    }

    // A split concurrent mark owns the complete start/finish interval.  An
    // emergency allocator or second automatic request must wait cooperatively
    // instead of observing markActive between transition-lock acquisitions
    // and throwing "cycle is already active" into the mutator.
    runtime.startConcurrentMark();
    std::atomic<bool> competingReady{false};
    std::atomic<bool> allowCompetingStart{false};
    std::atomic<bool> competingFinished{false};
    std::exception_ptr competingFailure;
    std::thread competingCollector([&] {
        bool attached = false;
        try {
            runtime.attachCurrentThread();
            attached = true;
            competingReady.store(true, std::memory_order_release);
            while (!allowCompetingStart.load(std::memory_order_acquire)) {
                runtime.safepointPoll();
                std::this_thread::yield();
            }
            runtime.startConcurrentMark();
            (void)runtime.finishConcurrentMark();
        } catch (...) {
            competingFailure = std::current_exception();
        }
        if (attached)
            runtime.detachCurrentThread();
        competingFinished.store(true, std::memory_order_release);
    });
    while (!competingReady.load(std::memory_order_acquire))
        std::this_thread::yield();
    allowCompetingStart.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (competingFinished.load(std::memory_order_acquire)) {
        std::cerr << "competing full mark was not serialized\n";
        competingCollector.join();
        return 1;
    }
    (void)runtime.finishConcurrentMark();
    while (!competingFinished.load(std::memory_order_acquire)) {
        runtime.safepointPoll();
        std::this_thread::yield();
    }
    competingCollector.join();
    if (competingFailure != nullptr)
        std::rethrow_exception(competingFailure);

    // Native adapters are layered in real programs (for example TLS over a
    // socket, or a condition wait inside an NDLL).  Leaving an inner adapter
    // must not expose the still-blocked outer scope as an active mutator.
    runtime.enterNativeBlocking();
    runtime.enterNativeBlocking();
    std::atomic<int> nestedCollections{0};
    std::atomic<bool> runSecondNestedCollection{false};
    std::exception_ptr nestedBlockingFailure;
    std::thread nestedBlockingCollector([&] {
        bool attached = false;
        try {
            runtime.attachCurrentThread();
            attached = true;
            (void)runtime.collectBootstrap();
            nestedCollections.store(1, std::memory_order_release);
            while (!runSecondNestedCollection.load(std::memory_order_acquire))
                std::this_thread::yield();
            (void)runtime.collectBootstrap();
            nestedCollections.store(2, std::memory_order_release);
        } catch (...) {
            nestedBlockingFailure = std::current_exception();
            nestedCollections.store(2, std::memory_order_release);
        }
        if (attached)
            runtime.detachCurrentThread();
    });
    while (nestedCollections.load(std::memory_order_acquire) < 1)
        std::this_thread::yield();
    runtime.leaveNativeBlocking();
    runSecondNestedCollection.store(true, std::memory_order_release);
    while (nestedCollections.load(std::memory_order_acquire) < 2)
        std::this_thread::yield();
    nestedBlockingCollector.join();
    runtime.leaveNativeBlocking();
    if (nestedBlockingFailure != nullptr)
        std::rethrow_exception(nestedBlockingFailure);

    // A legacy/native callback may enter generated Haxe code without using
    // val_call*.  Its generated prologue must revoke the native-blocked
    // shortcut before publishing RootFrames or allocating.  Exercise that
    // transition while another attached thread repeatedly handshakes.
    runtime.enterNativeBlocking();
    auto* managedOwner = hx::gc::currentThread;
    if (managedOwner == nullptr ||
        !runtime.beginManagedExecution(*managedOwner)) {
        std::cerr << "native callback did not enter managed execution\n";
        return 1;
    }
    std::atomic<bool> callbackCollectorFinished{false};
    std::exception_ptr callbackCollectorFailure;
    std::thread callbackCollector([&] {
        bool attached = false;
        try {
            runtime.attachCurrentThread();
            attached = true;
            for (int iteration = 0; iteration < 8; ++iteration)
                (void)runtime.collectBootstrap();
        } catch (...) {
            callbackCollectorFailure = std::current_exception();
        }
        if (attached)
            runtime.detachCurrentThread();
        callbackCollectorFinished.store(true, std::memory_order_release);
    });
    std::uint32_t callbackRootsCreated = 0;
    while (!callbackCollectorFinished.load(std::memory_order_acquire)) {
        Node* callbackRoot = allocateNode(9000 + callbackRootsCreated++);
        hx::gc::RefSlot callbackSlot{
            runtime.makeRef(callbackRoot).bits()};
        hx::gc::RefSlot* callbackSlots[]{&callbackSlot};
        {
            hx::gc::RootScope callbackScope(
                callbackSlots, 1, 0x43425254U);
            runtime.safepointPoll();
            callbackRoot = static_cast<Node*>(
                runtime.loadBarrier(callbackSlot));
            if (callbackRoot == nullptr || callbackRoot->value < 9000) {
                std::cerr << "managed callback root was lost\n";
                return 1;
            }
        }
        std::this_thread::yield();
    }
    callbackCollector.join();
    runtime.endManagedExecution(*managedOwner);
    runtime.leaveNativeBlocking();
    if (callbackCollectorFailure != nullptr)
        std::rethrow_exception(callbackCollectorFailure);

    // Race the native-blocked -> managed-callback transition itself against
    // exact-root handshakes. A collector can accept depth zero and, while it
    // still owns the handshake mutex, observe beginManagedExecution increment
    // the depth before that callback reaches safepointPoll. The callback has
    // not entered generated code yet and must be treated as a stable native
    // boundary, not as an escaped mutator.
    runtime.enterNativeBlocking();
    auto* transitionOwner = hx::gc::currentThread;
    std::atomic<bool> transitionCollectorFinished{false};
    std::exception_ptr transitionCollectorFailure;
    std::thread transitionCollector([&] {
        bool attached = false;
        try {
            runtime.attachCurrentThread();
            attached = true;
            for (int iteration = 0; iteration < 128; ++iteration)
                (void)runtime.collectBootstrap();
        } catch (...) {
            transitionCollectorFailure = std::current_exception();
        }
        if (attached)
            runtime.detachCurrentThread();
        transitionCollectorFinished.store(true, std::memory_order_release);
    });
    std::size_t transitionCount = 0;
    while (!transitionCollectorFinished.load(std::memory_order_acquire)) {
        if (!runtime.beginManagedExecution(*transitionOwner)) {
            std::cerr << "managed transition lost its native boundary\n";
            return 1;
        }
        ++transitionCount;
        runtime.endManagedExecution(*transitionOwner);
        std::this_thread::yield();
    }
    transitionCollector.join();
    runtime.leaveNativeBlocking();
    if (transitionCollectorFailure != nullptr)
        std::rethrow_exception(transitionCollectorFailure);
    if (transitionCount == 0) {
        std::cerr << "managed transition race was not exercised\n";
        return 1;
    }

    // Regression for a production failure seen during a full-GC remark: the
    // handshake threw after markActive had been published, leaving the heap in
    // a permanent concurrent-mark phase. The failed cycle must release that
    // ownership so an ordinary nursery cycle can immediately make progress.
    remarkTimeout.armed.store(true, std::memory_order_release);
    runtime.startConcurrentMark();
    bool remarkFailed = false;
    try {
        (void)runtime.finishConcurrentMark();
    } catch (const std::exception&) {
        remarkFailed = true;
    }
    remarkTimeout.release.store(true, std::memory_order_release);
    if (remarkTimeout.blocker.joinable())
        remarkTimeout.blocker.join();
    if (!remarkFailed) {
        std::cerr << "remark timeout injection did not fail\n";
        return 1;
    }
    try {
        (void)runtime.collectYoung();
    } catch (const std::exception& error) {
        std::cerr << "failed remark poisoned the next young cycle: "
                  << error.what() << '\n';
        return 1;
    }

    runtime.detachCurrentThread();

    runtime.shutdown();
    std::cout << "hxcpp concurrent exact mark passed: marked="
              << cycle.markedObjects << " dead=" << cycle.deadObjects << '\n';
    return 0;
}

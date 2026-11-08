#include <hx/gc/runtime.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <new>
#include <thread>

namespace {

struct Node final {
    hx::gc::RefSlot next{0};
    int value = 0;
};

constexpr std::uint32_t NodeReferences[]{
    static_cast<std::uint32_t>(offsetof(Node, next)),
};

const hx::gc::TypeDescriptor NodeType{
    0x4352454c4f434e56ULL, "test.ConcurrentRelocationNode", nullptr,
    sizeof(Node), alignof(Node), 0, hx::gc::TypeContainsReferences,
    1, NodeReferences, nullptr, nullptr,
};

struct CopyGate final {
    std::atomic<void*> target{nullptr};
    std::atomic<bool> blocked{false};
    std::atomic<bool> release{false};
};

void blockBeforeTargetCopy(void* context, void* object) {
    auto& gate = *static_cast<CopyGate*>(context);
    if (object != gate.target.load(std::memory_order_acquire))
        return;
    gate.blocked.store(true, std::memory_order_release);
    while (!gate.release.load(std::memory_order_acquire))
        std::this_thread::yield();
}

Node* allocateNode(int value) {
    auto& runtime = hx::gc::Runtime::instance();
    void* storage = runtime.allocate(NodeType);
    auto* node = ::new (storage) Node();
    node->value = value;
    runtime.publish(node);
    return node;
}

} // namespace

int main() {
    constexpr int ChainLength = 20000;
    constexpr int Mutations = 5000;
    auto& runtime = hx::gc::Runtime::instance();
    CopyGate copyGate{};
    hx::gc::RuntimeConfig config{};
    config.relocationBeforeCopyHook = &blockBeforeTargetCopy;
    config.relocationHookContext = &copyGate;
    runtime.initialize(config);

    std::atomic<bool> ready{false};
    std::atomic<bool> mutate{false};
    std::atomic<bool> preCopyStored{false};
    std::atomic<bool> preCopyValid{false};
    std::atomic<bool> rootForwarded{false};
    std::atomic<bool> done{false};
    std::atomic<bool> release{false};
    std::atomic<bool> graphValid{false};
    std::atomic<int> operations{0};

    std::thread mutator([&] {
        runtime.attachCurrentThread();
        Node* root = allocateNode(1);
        Node* tail = root;
        for (int index = 1; index < ChainLength; ++index) {
            Node* node = allocateNode(index + 1);
            runtime.storeBarrier(tail, tail->next, node);
            tail = node;
        }
        hx::gc::RefSlot rootSlot{runtime.makeRef(root).bits()};
        hx::gc::RefSlot* slots[]{&rootSlot};
        {
            hx::gc::RootScope roots(slots, 1, 0x4001);
            copyGate.target.store(root, std::memory_order_release);
            ready.store(true, std::memory_order_release);
            while (!mutate.load(std::memory_order_acquire)) {
                runtime.safepointPoll();
                std::this_thread::yield();
            }
            // The copy hook has selected this source object but has not taken
            // relocationMutex yet. An owner-aware store must complete before
            // the worker's memcpy and therefore appear in the new copy.
            Node* preCopyReplacement = allocateNode(90000);
            runtime.storeBarrier(root, root->next, preCopyReplacement);
            preCopyStored.store(true, std::memory_order_release);
            while (static_cast<Node*>(runtime.loadBarrier(rootSlot)) == root) {
                std::this_thread::yield();
            }
            rootForwarded.store(true, std::memory_order_release);
            auto* forwardedRoot =
                static_cast<Node*>(runtime.loadBarrier(rootSlot));
            auto* forwardedChild = static_cast<Node*>(
                runtime.loadBarrier(forwardedRoot->next));
            preCopyValid.store(forwardedChild != nullptr &&
                                   forwardedChild->value == 90000,
                               std::memory_order_release);
            // `root` is the owner acquired before the concurrent copy. No
            // safepoint occurs between observing forwarding and this store,
            // so the barrier must redirect the old owner-relative slot.
            Node* firstReplacement = allocateNode(100000);
            runtime.storeBarrier(root, root->next, firstReplacement);
            operations.fetch_add(1, std::memory_order_release);

            for (int index = 1; index < Mutations; ++index) {
                auto* currentRoot =
                    static_cast<Node*>(runtime.loadBarrier(rootSlot));
                Node* replacement = allocateNode(100000 + index);
                runtime.storeBarrier(currentRoot, currentRoot->next, replacement);
                operations.fetch_add(1, std::memory_order_release);
                if ((index & 31) == 0)
                    runtime.safepointPoll();
            }
            // ObjectPtr/Dynamic field assignment reaches the runtime without
            // an explicit owner.  Use the stale pre-relocation owner address
            // deliberately: the barrier must infer its containing object and
            // redirect this final write to the forwarded slot.
            Node* ownerlessReplacement = allocateNode(100000 + Mutations);
            runtime.storeBarrier(nullptr, root->next, ownerlessReplacement);
            operations.fetch_add(1, std::memory_order_release);
            done.store(true, std::memory_order_release);
            while (!release.load(std::memory_order_acquire)) {
                runtime.safepointPoll();
                std::this_thread::yield();
            }
            auto* currentRoot =
                static_cast<Node*>(runtime.loadBarrier(rootSlot));
            auto* currentChild =
                static_cast<Node*>(runtime.loadBarrier(currentRoot->next));
            graphValid.store(currentRoot->value == 1 &&
                                 currentChild != nullptr &&
                                 currentChild->value == 100000 + Mutations,
                             std::memory_order_release);
        }
        runtime.detachCurrentThread();
    });

    while (!ready.load(std::memory_order_acquire))
        std::this_thread::yield();
    const auto phaseStart = std::chrono::steady_clock::now();
    runtime.startConcurrentMark();
    const auto mark = runtime.finishConcurrentMark();
    const auto markDone = std::chrono::steady_clock::now();
    if (mark.markedObjects != ChainLength) {
        std::cerr << "initial concurrent mark missed live chain objects\n";
        return 1;
    }

    runtime.beginConcurrentRelocation();
    const auto relocationBegan = std::chrono::steady_clock::now();
    while (!copyGate.blocked.load(std::memory_order_acquire))
        std::this_thread::yield();
    mutate.store(true, std::memory_order_release);
    while (!preCopyStored.load(std::memory_order_acquire))
        std::this_thread::yield();
    copyGate.release.store(true, std::memory_order_release);
    while (!rootForwarded.load(std::memory_order_acquire))
        std::this_thread::yield();
    // Keep the relocation cycle open through the deterministic owner-less
    // stale-source write at the end of the mutator phase.
    while (!done.load(std::memory_order_acquire))
        std::this_thread::yield();
    const auto forwardingObserved = std::chrono::steady_clock::now();
    const auto relocation = runtime.finishConcurrentRelocation();
    const auto relocationDone = std::chrono::steady_clock::now();
    release.store(true, std::memory_order_release);
    mutator.join();
    const auto mutatorDone = std::chrono::steady_clock::now();

    if (relocation.relocatedObjects != ChainLength ||
        relocation.forwardedStores == 0) {
        std::cerr << "concurrent relocation did not copy the marked graph or "
                     "forward mutator stores: copied="
                  << relocation.relocatedObjects << " forwarded="
                  << relocation.forwardedStores << '\n';
        return 1;
    }
    if (!graphValid.load(std::memory_order_acquire)) {
        std::cerr << "forwarded mutator write was lost during relocation\n";
        return 1;
    }
    if (!preCopyValid.load(std::memory_order_acquire)) {
        std::cerr << "pre-copy mutator write was lost during relocation\n";
        return 1;
    }

    runtime.attachCurrentThread();
    const auto cleanup = runtime.collectBootstrap();
    runtime.detachCurrentThread();
    if (cleanup.markedObjects != 0 || cleanup.reclaimedRegions == 0) {
        std::cerr << "retired relocation regions were not reclaimed after "
                     "the next exact traversal\n";
        return 1;
    }
    runtime.shutdown();
    const auto millis = [](auto from, auto to) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(to - from)
            .count();
    };
    std::cout << "hxcpp concurrent relocation passed: copied="
              << relocation.relocatedObjects << " forwardedStores="
              << relocation.forwardedStores
              << " markMs=" << millis(phaseStart, markDone)
              << " beginMs=" << millis(markDone, relocationBegan)
              << " forwardWaitMs=" << millis(relocationBegan, forwardingObserved)
              << " finishMs=" << millis(forwardingObserved, relocationDone)
              << " joinMs=" << millis(relocationDone, mutatorDone) << '\n';
    return 0;
}

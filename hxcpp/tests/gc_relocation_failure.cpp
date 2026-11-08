#include <hx/gc/runtime.hpp>

#include <cstddef>
#include <iostream>
#include <new>

namespace {

struct Node final {
    hx::gc::RefSlot next{0};
    int value = 0;
};

constexpr std::uint32_t NodeReferences[]{
    static_cast<std::uint32_t>(offsetof(Node, next)),
};

const hx::gc::TypeDescriptor NodeType{
    0x52454c4f43464149ULL, "test.RelocationFailureNode", nullptr,
    sizeof(Node), alignof(Node), 0, hx::gc::TypeContainsReferences,
    1, NodeReferences, nullptr, nullptr,
};

Node* allocateNode(int value) {
    auto& runtime = hx::gc::Runtime::instance();
    void* storage = runtime.allocate(NodeType);
    auto* node = ::new (storage) Node();
    node->value = value;
    runtime.publish(node);
    return node;
}

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main() {
    constexpr int NodeCount = 1200;
    auto& runtime = hx::gc::Runtime::instance();
    hx::gc::RuntimeConfig config{};
    config.regionBytes = 64U * 1024U;
    config.tlabBytes = 16U * 1024U;
    // Two live source regions plus one permitted destination force a partial
    // evacuation: one source completes, then the destination fills while the
    // second source is being copied.
    config.maximumRegions = 3;
    config.markWorkerCount = 2;
    config.relocationWorkerCount = 1;
    runtime.initialize(config);
    runtime.attachCurrentThread();

    Node* root = allocateNode(0);
    Node* tail = root;
    for (int index = 1; index < NodeCount; ++index) {
        Node* child = allocateNode(index);
        runtime.storeBarrier(tail, tail->next, child);
        tail = child;
    }
    hx::gc::RefSlot rootSlot{runtime.makeRef(root).bits()};
    hx::gc::RefSlot* roots[]{&rootSlot};
    {
        hx::gc::RootScope scope(roots, 1, 0x4f4f4d31);
        runtime.startConcurrentMark();
        const auto marked = runtime.finishConcurrentMark();
        if (marked.markedObjects != NodeCount)
            return fail("relocation-failure fixture was not fully marked");

        runtime.beginConcurrentRelocation();
        const auto relocation = runtime.finishConcurrentRelocation();
        if (!relocation.degraded || relocation.failedRegions == 0 ||
            relocation.relocatedObjects == 0 ||
            runtime.concurrentRelocationActive()) {
            return fail("relocation OOM did not degrade and close the cycle");
        }

        root = static_cast<Node*>(runtime.loadBarrier(rootSlot));
        Node* cursor = root;
        for (int index = 0; index < NodeCount; ++index) {
            if (cursor == nullptr || cursor->value != index)
                return fail(
                    "relocation OOM corrupted the retained source graph");
            cursor = static_cast<Node*>(runtime.loadBarrier(cursor->next));
        }
        if (cursor != nullptr)
            return fail("relocation OOM changed the source graph length");

        // A recovered runtime must accept another complete tracing cycle;
        // merely clearing relocationActive is not sufficient.
        runtime.startConcurrentMark();
        const auto recovered = runtime.finishConcurrentMark();
        if (recovered.markedObjects != NodeCount)
            return fail("runtime could not collect after relocation OOM");
    }

    runtime.detachCurrentThread();
    runtime.shutdown();
    std::cout << "hxcpp relocation allocation-failure recovery passed\n";
    return 0;
}

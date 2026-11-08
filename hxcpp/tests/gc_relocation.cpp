#include <hx/gc/runtime.hpp>

#include <cstddef>
#include <cstdint>
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
    0x52454c4f434e4f56ULL,
    "test.RelocationNode",
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

Node* allocateNode(int value) {
    auto& runtime = hx::gc::Runtime::instance();
    void* storage = runtime.allocate(NodeType);
    auto* node = ::new (storage) Node();
    node->value = value;
    runtime.publish(node);
    return node;
}

int fail(const char* message) {
    std::cerr << "relocation failure: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    auto& runtime = hx::gc::Runtime::instance();
    runtime.initialize();
    runtime.attachCurrentThread();

    Node* root = allocateNode(7);
    Node* child = allocateNode(9);
    (void)allocateNode(99);
    runtime.storeBarrier(root, root->next, child);
    hx::gc::RefSlot rootSlot{runtime.makeRef(root).bits()};
    hx::gc::RefSlot* slots[]{&rootSlot};
    {
        hx::gc::RootScope roots(slots, 1, 0x3001);
        for (int round = 0; round < 4; ++round) {
            auto* beforeRoot = static_cast<Node*>(runtime.loadBarrier(rootSlot));
            auto* beforeChild =
                static_cast<Node*>(runtime.loadBarrier(beforeRoot->next));
            const auto mark = runtime.collectBootstrap();
            if (mark.markedObjects != 2)
                return fail("mark did not find the two-object live graph");
            const auto moved = runtime.compactBootstrap();
            if (moved.relocatedObjects != 2 || moved.sourceRegions == 0)
                return fail("live objects were not relocated");

            auto* afterRoot = static_cast<Node*>(runtime.loadBarrier(rootSlot));
            auto* afterChild =
                static_cast<Node*>(runtime.loadBarrier(afterRoot->next));
            if (afterRoot == beforeRoot || afterChild == beforeChild)
                return fail("relocation did not change object addresses");
            if (afterRoot->value != 7 || afterChild->value != 9)
                return fail("relocation corrupted object payload");
            if (hx::gc::Ref(rootSlot.load()).view() != hx::gc::View::Remapped ||
                hx::gc::Ref(afterRoot->next.load()).view() !=
                    hx::gc::View::Remapped)
                return fail("exact slots were not healed to Remapped view");
        }
    }

    const auto cleanup = runtime.collectBootstrap();
    if (cleanup.markedObjects != 0 || cleanup.reclaimedRegions == 0)
        return fail("relocated graph was not reclaimed after dropping roots");
    runtime.detachCurrentThread();
    runtime.shutdown();
    std::cout << "hxcpp forwarding/remap color-cycle test passed\n";
    return 0;
}

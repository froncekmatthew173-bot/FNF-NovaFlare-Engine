#include <hx/gc/runtime.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>

namespace {

struct Node final {
    hx::gc::RefSlot next{0};
    std::uint64_t value = 0;
};

constexpr std::uint32_t NodeReferences[]{
    static_cast<std::uint32_t>(offsetof(Node, next)),
};

const hx::gc::TypeDescriptor NodeType{
    0x494e504c41434552ULL,
    "test.InPlaceRelocationNode",
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

[[nodiscard]] Node* allocateNode(std::uint64_t value) {
    auto& runtime = hx::gc::Runtime::instance();
    auto* node = ::new (runtime.allocate(NodeType)) Node();
    node->value = value;
    runtime.publish(node);
    return node;
}

} // namespace

int main() {
    auto& runtime = hx::gc::Runtime::instance();
    hx::gc::RuntimeConfig config{};
    config.regionBytes = 64U * 1024U;
    config.tlabBytes = 16U * 1024U;
    config.maximumRegions = 1;
    config.maximumAddressSpaceBytes = config.regionBytes;
    runtime.initialize(config);
    runtime.attachCurrentThread();

    // Dead prefix forces both retained objects to slide downward. With the
    // only Region occupied, a separate relocation destination is impossible.
    for (std::uint64_t index = 0; index < 300; ++index)
        (void)allocateNode(index);
    Node* root = allocateNode(0x1111);
    Node* child = allocateNode(0x2222);
    runtime.storeBarrier(root, root->next, child);
    void* originalRoot = root;
    void* originalChild = child;
    hx::gc::RefSlot rootSlot{runtime.makeRef(root).bits()};
    hx::gc::RefSlot* roots[]{&rootSlot};
    {
        hx::gc::RootScope scope(roots, 1, 0x49504c43U);
        const auto mark = runtime.collectBootstrap();
        const auto moved = runtime.compactBootstrap();
        root = static_cast<Node*>(runtime.loadBarrier(rootSlot));
        child = root == nullptr ? nullptr : static_cast<Node*>(
            runtime.loadBarrier(root->next));
        if (mark.markedObjects != 2 || moved.sourceRegions != 1 ||
            moved.destinationRegions != 0 || moved.inPlaceRegions != 1 ||
            moved.relocatedObjects != 2 || root == originalRoot ||
            child == originalChild || root == nullptr || child == nullptr ||
            root->value != 0x1111 || child->value != 0x2222 ||
            runtime.allocationStats().createdRegions != 1) {
            std::cerr << "no-extra-space relocation failed"
                      << " marked=" << mark.markedObjects
                      << " source=" << moved.sourceRegions
                      << " destination=" << moved.destinationRegions
                      << " inplace=" << moved.inPlaceRegions
                      << " moved=" << moved.relocatedObjects << '\n';
            return 1;
        }
    }

    const auto cleanup = runtime.collectBootstrap();
    if (cleanup.markedObjects != 0 || cleanup.reclaimedRegions != 1) {
        std::cerr << "in-place relocation cleanup failed\n";
        return 1;
    }
    runtime.detachCurrentThread();
    runtime.shutdown();
    std::cout << "hxcpp no-extra-space in-place relocation passed\n";
    return 0;
}

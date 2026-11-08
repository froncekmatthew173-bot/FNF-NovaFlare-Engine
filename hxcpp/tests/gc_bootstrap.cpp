#include <hx/gc/runtime.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>

namespace {

struct Node final {
    hx::gc::RefSlot next{0};
    std::uint64_t nonReferenceBits = 0;
    int value = 0;
};

constexpr std::uint32_t NodeReferences[] = {
    static_cast<std::uint32_t>(offsetof(Node, next)),
};

const hx::gc::TypeDescriptor NodeType{
    0x4e4f44455f4e4f56ULL,
    "test.Node",
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

[[nodiscard]] Node* allocateNode(int value) {
    auto& runtime = hx::gc::Runtime::instance();
    void* storage = runtime.allocate(NodeType);
    auto* node = ::new (storage) Node();
    node->value = value;
    runtime.publish(node);
    return node;
}

[[nodiscard]] int fail(const char* message) {
    std::cerr << "bootstrap failure: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    auto& runtime = hx::gc::Runtime::instance();
    runtime.initialize();
    runtime.attachCurrentThread();

    {
        Node* root = allocateNode(1);
        Node* child = allocateNode(2);
        Node* unreachable = allocateNode(3);
        runtime.storeBarrier(root, root->next, child);

        // The exact collector must not scan this integer even though it is
        // bit-identical to a managed object address.
        root->nonReferenceBits = reinterpret_cast<std::uintptr_t>(unreachable);
        volatile std::uintptr_t fakeStackPointer =
            reinterpret_cast<std::uintptr_t>(unreachable);
        (void)fakeStackPointer;

        hx::gc::RefSlot rootSlot{runtime.makeRef(root).bits()};
        hx::gc::RefSlot* slots[]{&rootSlot};
        hx::gc::RootScope roots(slots, 1, 0x1001);

        const hx::gc::CollectionStats first = runtime.collectBootstrap();
        if (first.roots != 1 || first.markedObjects != 2 ||
            first.deadObjects != 1)
            return fail("exact root or descriptor tracing count is wrong");

        auto* healedRoot = static_cast<Node*>(runtime.loadBarrier(rootSlot));
        auto* healedChild = static_cast<Node*>(runtime.loadBarrier(healedRoot->next));
        if (healedRoot->value != 1 || healedChild->value != 2)
            return fail("live graph was corrupted during exact collection");
        if (hx::gc::Ref(rootSlot.load()).view() != runtime.goodView())
            return fail("root slot was not recolored to the current good view");
    }

    const hx::gc::CollectionStats second = runtime.collectBootstrap();
    if (second.markedObjects != 0 || second.reclaimedRegions == 0)
        return fail("unrooted region was not reclaimed");

    runtime.detachCurrentThread();
    runtime.shutdown();
    std::cout << "hxcpp single-heap precise bootstrap passed\n";
    return 0;
}

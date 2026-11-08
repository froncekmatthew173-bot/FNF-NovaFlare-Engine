#include <hx/gc/runtime.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>

namespace {

struct Node final {
    ::hx::gc::RefSlot next{0};
    std::uint64_t value = 0;
};

constexpr std::uint32_t NodeReferences[]{
    static_cast<std::uint32_t>(offsetof(Node, next)),
};

const ::hx::gc::TypeDescriptor NodeType{
    0x454d4552474f4f4dULL,
    "test.EmergencyNode",
    nullptr,
    sizeof(Node),
    alignof(Node),
    0,
    ::hx::gc::TypeContainsReferences,
    1,
    NodeReferences,
    nullptr,
    nullptr,
};

[[nodiscard]] Node* allocateNode(std::uint64_t value) {
    auto& runtime = ::hx::gc::Runtime::instance();
    auto* result = ::new (runtime.allocate(NodeType)) Node();
    result->value = value;
    runtime.publish(result);
    return result;
}

} // namespace

int main() {
    auto& runtime = ::hx::gc::Runtime::instance();
    try {
        ::hx::gc::RuntimeConfig config{};
        config.regionBytes = 64U * 1024U;
        config.tlabBytes = 16U * 1024U;
        config.maximumRegions = 1;
        config.initialTenuringThreshold = 1;
        config.maximumTenuringThreshold = 1;
        config.adaptiveTenuring = false;
        config.uncommitDelayMilliseconds = 0;
        runtime.initialize(config);
        runtime.attachCurrentThread();

        const auto soft = runtime.createHandle(
            allocateNode(7), ::hx::gc::HandleKind::Soft);
        // Fill the only reservation. The next refill must enter the emergency
        // state machine, clear Soft roots, collect, and reuse that reservation.
        Node* last = nullptr;
        for (std::uint64_t index = 0; index < 1200; ++index)
            last = allocateNode(index + 100);
        if (last == nullptr || runtime.resolveHandle(soft) != nullptr ||
            !runtime.referenceEnqueued(soft) ||
            runtime.allocationStats().emergencyCollections == 0 ||
            runtime.allocationStats().createdRegions != 1) {
            std::cerr << "emergency collection did not clear Soft/reuse heap\n";
            return 1;
        }
        ::hx::gc::Handle queued{};
        if (!runtime.pollReferenceQueue(queued) ||
            queued.index != soft.index ||
            queued.generation != soft.generation) {
            std::cerr << "emergency-cleared Soft handle was not queued\n";
            return 1;
        }
        runtime.releaseHandle(soft);
        runtime.detachCurrentThread();
        runtime.shutdown();

        runtime.initialize(config);
        runtime.attachCurrentThread();
        Node* head = nullptr;
        for (std::uint64_t index = 0; index < 300; ++index) {
            Node* node = allocateNode(index);
            runtime.storeBarrier(node, node->next, head);
            head = node;
        }
        ::hx::gc::RefSlot root{runtime.makeRef(head).bits()};
        ::hx::gc::RefSlot* roots[]{&root};
        {
            ::hx::gc::RootScope scope(roots, 1, 0x4f4f4d31U);
            void* original = head;
            const auto collection = runtime.collectYoung();
            head = static_cast<Node*>(runtime.loadBarrier(root));
            if (head != original || collection.inPlacePromotedRegions != 1 ||
                collection.promotedObjects != 300) {
                std::cerr << "promotion reserve exhaustion was not recovered\n";
                return 1;
            }
            std::uint64_t expected = 299;
            for (Node* node = head; node != nullptr;
                 node = static_cast<Node*>(runtime.loadBarrier(node->next))) {
                if (node->value != expected--) {
                    std::cerr << "in-place promotion corrupted live graph\n";
                    return 1;
                }
            }
        }
        runtime.detachCurrentThread();
        runtime.shutdown();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "hxcpp emergency OOM and promotion failure test passed\n";
    return 0;
}

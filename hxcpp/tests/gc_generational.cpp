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
    0x47454e455241544eULL,
    "test.GenerationalNode",
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
        ::hx::gc::RuntimeConfig fixed{};
        fixed.regionBytes = 64U * 1024U;
        fixed.tlabBytes = 16U * 1024U;
        fixed.maximumRegions = 128;
        fixed.initialTenuringThreshold = 3;
        fixed.maximumTenuringThreshold = 3;
        fixed.adaptiveTenuring = false;
        runtime.initialize(fixed);
        runtime.attachCurrentThread();

        Node* owner = allocateNode(1);
        ::hx::gc::RefSlot ownerRoot{runtime.makeRef(owner).bits()};
        ::hx::gc::RefSlot* ownerRoots[]{&ownerRoot};
        {
            ::hx::gc::RootScope scope(ownerRoots, 1, 0x47454e31U);
            const auto first = runtime.collectYoung();
            owner = static_cast<Node*>(runtime.loadBarrier(ownerRoot));
            const auto firstHeap = runtime.heapStats();
            if (owner == nullptr || first.survivorObjects != 1 ||
                first.promotedObjects != 0 || first.tenuringThreshold != 3) {
                std::cerr << "first survivor cohort was not retained\n";
                return 1;
            }
            if (first.epoch != 1) {
                std::cerr << "young generation epoch did not start independently\n";
                return 1;
            }
            if (firstHeap.survivorBytes == 0 ||
                firstHeap.tenuringThreshold != 3) {
                std::cerr << "heap telemetry omitted survivor generation\n";
                return 1;
            }

            const auto second = runtime.collectYoung();
            owner = static_cast<Node*>(runtime.loadBarrier(ownerRoot));
            if (owner == nullptr || second.survivorObjects != 1 ||
                second.promotedObjects != 0 || second.epoch != 2) {
                std::cerr << "second survivor cohort was not retained\n";
                return 1;
            }

            const auto third = runtime.collectYoung();
            owner = static_cast<Node*>(runtime.loadBarrier(ownerRoot));
            if (owner == nullptr || third.promotedObjects != 1 ||
                third.survivorObjects != 0) {
                std::cerr << "tenuring threshold did not promote owner\n";
                return 1;
            }

            Node* child = allocateNode(2);
            runtime.storeBarrier(owner, owner->next, child);
            const auto childFirst = runtime.collectYoung();
            owner = static_cast<Node*>(runtime.loadBarrier(ownerRoot));
            child = static_cast<Node*>(runtime.loadBarrier(owner->next));
            if (child == nullptr || childFirst.rememberedRegions == 0 ||
                childFirst.survivorObjects == 0) {
                std::cerr << "old-to-survivor edge was not discovered\n";
                return 1;
            }

            // No store occurs between these cycles.  The precise remembered
            // slot must survive cohort rotation until the child is tenured.
            const auto childSecond = runtime.collectYoung();
            owner = static_cast<Node*>(runtime.loadBarrier(ownerRoot));
            child = static_cast<Node*>(runtime.loadBarrier(owner->next));
            if (child == nullptr || child->value != 2 ||
                childSecond.rememberedRegions == 0 ||
                childSecond.survivorObjects == 0) {
                std::cerr << "remembered slot did not persist across survivors\n";
                return 1;
            }
        }

        ownerRoot.store(0, std::memory_order_release);
        const auto independentFull = runtime.collectBootstrap();
        if (independentFull.epoch != 1) {
            std::cerr << "young cycles advanced old-generation mark metadata\n";
            return 1;
        }
        runtime.detachCurrentThread();
        runtime.shutdown();

        ::hx::gc::RuntimeConfig adaptive = fixed;
        adaptive.initialTenuringThreshold = 4;
        adaptive.maximumTenuringThreshold = 6;
        adaptive.survivorTargetPercent = 1;
        adaptive.adaptiveTenuring = true;
        adaptive.markWorkerCount = 4;
        runtime.initialize(adaptive);
        runtime.attachCurrentThread();

        Node* head = nullptr;
        for (std::uint64_t index = 0; index < 3000; ++index) {
            Node* node = allocateNode(index);
            runtime.storeBarrier(node, node->next, head);
            head = node;
        }
        ::hx::gc::RefSlot headRoot{runtime.makeRef(head).bits()};
        ::hx::gc::RefSlot* headRoots[]{&headRoot};
        {
            ::hx::gc::RootScope scope(headRoots, 1, 0x47454e32U);
            const auto pressure = runtime.collectYoung();
            head = static_cast<Node*>(runtime.loadBarrier(headRoot));
            if (head == nullptr || pressure.survivorObjects != 3000 ||
                pressure.tenuringThreshold != 4 ||
                pressure.nextTenuringThreshold != 3 ||
                pressure.traceWorkers != 4) {
                std::cerr << "adaptive tenuring did not react to survivor pressure\n";
                return 1;
            }
        }
        headRoot.store(0, std::memory_order_release);
        runtime.detachCurrentThread();
        runtime.shutdown();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "hxcpp survivor and adaptive tenuring test passed\n";
    return 0;
}

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
    0x554e434f4d4d4954ULL,
    "test.UncommitNode",
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
        config.maximumRegions = 4;
        config.initialTenuringThreshold = 1;
        config.maximumTenuringThreshold = 1;
        config.adaptiveTenuring = false;
        config.uncommitDelayMilliseconds = 60'000;
        runtime.initialize(config);
        runtime.attachCurrentThread();

        for (std::uint64_t index = 0; index < 300; ++index)
            (void)allocateNode(index);
        const std::uint64_t committedBefore = runtime.committedBytes();
        const std::uint64_t createdBefore =
            runtime.allocationStats().createdRegions;
        (void)runtime.collectYoung();
        if (runtime.usedBytes() != 0 || runtime.reusableBytes() == 0 ||
            runtime.reservedBytes() == 0 ||
            runtime.committedBytes() != committedBefore) {
            std::cerr << "empty region was not retained for delayed uncommit\n";
            return 1;
        }

        runtime.uncommitUnusedMemory(true);
        if (runtime.committedBytes() != 0 || runtime.reusableBytes() == 0 ||
            runtime.reservedBytes() == 0) {
            std::cerr << "forced uncommit did not preserve virtual reservation\n";
            return 1;
        }

        Node* reused = allocateNode(99);
        if (reused->next.load(std::memory_order_acquire) != 0 ||
            runtime.allocationStats().createdRegions != createdBefore ||
            runtime.reusableBytes() != 0 || runtime.committedBytes() == 0) {
            std::cerr << "decommitted reservation was not zero-recommitted/reused\n";
            return 1;
        }

        runtime.detachCurrentThread();
        runtime.shutdown();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "hxcpp delayed uncommit and reservation reuse test passed\n";
    return 0;
}

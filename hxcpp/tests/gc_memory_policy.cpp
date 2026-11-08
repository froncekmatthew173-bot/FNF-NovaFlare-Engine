#include <hx/gc/runtime.hpp>

#include <cstdint>
#include <iostream>
#include <new>

namespace {

struct Value final {
    std::uint64_t payload = 0;
};

const hx::gc::TypeDescriptor ValueType{
    0x4d454d504f4c4943ULL,
    "test.MemoryPolicyValue",
    nullptr,
    sizeof(Value),
    alignof(Value),
    0,
    hx::gc::TypeNone,
    0,
    nullptr,
    nullptr,
    nullptr,
};

struct Blob final {
    std::byte first{};
};

const hx::gc::TypeDescriptor BlobType{
    0x4d454d504f4c424cULL,
    "test.MemoryPolicyBlob",
    nullptr,
    sizeof(Blob),
    alignof(Blob),
    0,
    hx::gc::TypeVariableSize | hx::gc::TypeByteArray,
    0,
    nullptr,
    nullptr,
    nullptr,
};

Value* allocateValue(std::uint64_t payload) {
    auto& runtime = hx::gc::Runtime::instance();
    void* storage = runtime.allocate(ValueType);
    auto* result = ::new (storage) Value();
    result->payload = payload;
    runtime.publish(result);
    return result;
}

} // namespace

int main() {
    constexpr std::size_t RegionBytes = 64U * 1024U;
    constexpr std::size_t AddressSpaceBytes = 2U * 1024U * 1024U;
    auto& runtime = hx::gc::Runtime::instance();
    hx::gc::RuntimeConfig config{};
    config.regionBytes = RegionBytes;
    config.tlabBytes = 8U * 1024U;
    config.largeObjectThreshold = 1024U;
    config.maximumRegions = 16;
    config.maximumAddressSpaceBytes = AddressSpaceBytes;
    config.numaPolicy = hx::gc::NumaPolicy::Interleave;
    config.largePagePolicy = hx::gc::LargePagePolicy::Explicit;
    runtime.initialize(config);
    runtime.attachCurrentThread();

    Value* first = allocateValue(11);
    const auto active = runtime.heapStats();
    if (active.addressSpaceReservedBytes != AddressSpaceBytes ||
        active.committedBytes == 0 ||
        (active.numaPlacedBytes == 0 &&
         active.memoryPolicyFallbacks == 0) ||
        (active.largePageAdvisedBytes == 0 &&
         active.memoryPolicyFallbacks == 0)) {
        std::cerr << "global VA or platform memory policy was not reported: "
                  << "va=" << active.addressSpaceReservedBytes
                  << " committed=" << active.committedBytes
                  << " numa=" << active.numaPlacedBytes
                  << " huge=" << active.largePageAdvisedBytes
                  << " fallbacks=" << active.memoryPolicyFallbacks << '\n';
        return 1;
    }

    const auto reclaimed = runtime.collectBootstrap();
    if (reclaimed.reclaimedRegions != 1 ||
        runtime.heapStats().addressSpaceReservedBytes != AddressSpaceBytes) {
        std::cerr << "logical reclaim released the process VA arena\n";
        return 1;
    }

    Value* second = allocateValue(22);
    if (second != first || second->payload != 22) {
        std::cerr << "best-fit global VA subspan was not safely reused\n";
        return 1;
    }

    // Dedicated large objects are logical subregions, not full regionBytes
    // capacity units.  More than maximumRegions descriptors must be legal
    // while their aligned reservations still fit in the explicit byte arena.
    for (int index = 0; index < 20; ++index) {
        auto* blob = static_cast<Blob*>(
            runtime.allocateVariable(BlobType, 8U * 1024U));
        blob->first = static_cast<std::byte>(index);
        runtime.publish(blob);
    }
    const auto subregions = runtime.heapStats();
    if (subregions.activeRegions <= config.maximumRegions ||
        subregions.reservedBytes > AddressSpaceBytes) {
        std::cerr << "logical large subregions were count-capped: active="
                  << subregions.activeRegions << " configured_units="
                  << config.maximumRegions << " reserved="
                  << subregions.reservedBytes << '\n';
        return 1;
    }

    runtime.detachCurrentThread();
    runtime.shutdown();
    std::cout << "hxcpp global VA/NUMA/large-page policy passed\n";
    return 0;
}

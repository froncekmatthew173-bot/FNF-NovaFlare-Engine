#include <hx/gc/runtime.hpp>

#include <cstdint>
#include <iostream>
#include <new>

namespace {

struct Tiny final {
    std::uint32_t value = 0;
};

const hx::gc::TypeDescriptor TinyType{
    0x564952544d454d31ULL, "test.VirtualMemoryTiny", nullptr,
    sizeof(Tiny), alignof(Tiny), 0, hx::gc::TypeNone,
    0, nullptr, nullptr, nullptr,
};

} // namespace

int main() {
    constexpr std::uint64_t RegionBytes = 2U * 1024U * 1024U;
    auto& runtime = hx::gc::Runtime::instance();
    hx::gc::RuntimeConfig config{};
    config.regionBytes = RegionBytes;
    config.tlabBytes = 16U * 1024U;
    config.maximumRegions = 4;
    runtime.initialize(config);
    runtime.attachCurrentThread();

    void* storage = runtime.allocate(TinyType);
    auto* object = ::new (storage) Tiny();
    object->value = 42;
    runtime.publish(object);

    const std::uint64_t reserved = runtime.reservedBytes();
    const std::uint64_t committed = runtime.committedBytes();
    if (reserved != RegionBytes || committed == 0 || committed >= reserved) {
        std::cerr << "region was not reserved with bounded on-demand commit: "
                  << "reserved=" << reserved
                  << " committed=" << committed << '\n';
        return 1;
    }

    const auto collection = runtime.collectBootstrap();
    if (collection.deadObjects != 1 || collection.reclaimedRegions != 1 ||
        runtime.reservedBytes() != 0 || runtime.committedBytes() != 0) {
        std::cerr << "reclaimed region did not release virtual/physical memory\n";
        return 1;
    }

    runtime.detachCurrentThread();
    runtime.shutdown();
    std::cout << "hxcpp virtual reserve/commit/release test passed: reserved="
              << reserved << " committed=" << committed << '\n';
    return 0;
}

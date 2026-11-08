#include <hx/gc/runtime.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

struct Blob final {
    std::uint8_t first = 0;
};

const ::hx::gc::TypeDescriptor BlobType{
    0x4c41524745535043ULL,
    "test.LargeBlob",
    nullptr,
    sizeof(Blob),
    alignof(Blob),
    0,
    ::hx::gc::TypeVariableSize | ::hx::gc::TypeByteArray,
    0,
    nullptr,
    nullptr,
    nullptr,
};

} // namespace

int main() {
    auto& runtime = ::hx::gc::Runtime::instance();
    try {
        ::hx::gc::RuntimeConfig config{};
        config.regionBytes = 64U * 1024U;
        config.tlabBytes = 16U * 1024U;
        config.largeObjectThreshold = 1024;
        config.maximumRegions = 8;
        config.uncommitDelayMilliseconds = 60'000;
        runtime.initialize(config);
        runtime.attachCurrentThread();

        constexpr std::size_t PayloadBytes = 96U * 1024U;
        auto* first = static_cast<Blob*>(
            runtime.allocateVariable(BlobType, PayloadBytes));
        first->first = 0x5a;
        runtime.publish(first);
        const std::uint64_t created =
            runtime.allocationStats().createdRegions;
        const std::uint64_t reservation = runtime.reservedBytes();
        if (runtime.heapStats().largeBytes < PayloadBytes ||
            runtime.heapStats().youngLargeBytes < PayloadBytes) {
            std::cerr << "young large-space telemetry omitted active blob\n";
            return 1;
        }

        const auto dead = runtime.collectYoung();
        if (dead.deadObjects != 1 || runtime.reusableBytes() != reservation ||
            runtime.reservedBytes() != reservation) {
            std::cerr << "dead young-large reservation was not cached\n";
            return 1;
        }
        runtime.uncommitUnusedMemory(true);
        if (runtime.committedBytes() != 0) {
            std::cerr << "large cached pages were not decommitted\n";
            return 1;
        }

        auto* second = static_cast<Blob*>(
            runtime.allocateVariable(BlobType, PayloadBytes));
        if (second->first != 0 ||
            runtime.allocationStats().createdRegions != created ||
            runtime.reusableBytes() != 0) {
            std::cerr << "large best-fit reservation was not zero-reused\n";
            return 1;
        }
        second->first = 0xa5;
        runtime.publish(second);
        hx::gc::RefSlot root{runtime.makeRef(second).bits()};
        hx::gc::RefSlot* rootSlots[]{&root};
        {
            hx::gc::RootScope roots(rootSlots, 1, 0x4c47);
            const auto survivor = runtime.collectYoung();
            second = static_cast<Blob*>(runtime.loadBarrier(root));
            if (survivor.survivorObjects != 1 || second == nullptr ||
                second->first != 0xa5 ||
                runtime.heapStats().survivorBytes < PayloadBytes) {
                std::cerr << "live young-large object did not enter survivor "
                             "space\n";
                return 1;
            }
        }

        runtime.detachCurrentThread();
        runtime.shutdown();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "hxcpp large-space reuse and uncommit test passed\n";
    return 0;
}

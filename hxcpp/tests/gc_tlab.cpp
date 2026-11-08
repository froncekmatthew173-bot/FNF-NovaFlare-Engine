#include <hx/gc/runtime.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

namespace {

struct Tiny final { std::uint32_t value = 0; };

const hx::gc::TypeDescriptor TinyType{
    0x544c414254494e59ULL, "test.TlabTiny", nullptr,
    sizeof(Tiny), alignof(Tiny), 0, hx::gc::TypeNone,
    0, nullptr, nullptr, nullptr,
};

} // namespace

int main() {
    constexpr int ThreadCount = 8;
    constexpr int AllocationsPerThread = 20000;
    constexpr std::size_t TotalAllocations =
        ThreadCount * AllocationsPerThread;

    auto& runtime = hx::gc::Runtime::instance();
    hx::gc::RuntimeConfig config{};
    config.regionBytes = 64U * 1024U;
    config.tlabBytes = 4U * 1024U;
    config.maximumRegions = 1024;
    runtime.initialize(config);

    std::mutex resultsMutex;
    std::vector<std::uintptr_t> addresses;
    addresses.reserve(TotalAllocations);
    std::vector<std::uint64_t> identities;
    identities.reserve(TotalAllocations);
    std::vector<std::thread> threads;
    for (int threadIndex = 0; threadIndex < ThreadCount; ++threadIndex) {
        threads.emplace_back([&, threadIndex] {
            runtime.attachCurrentThread();
            std::vector<std::uintptr_t> local;
            local.reserve(AllocationsPerThread);
            std::vector<std::uint64_t> localIdentities;
            localIdentities.reserve(AllocationsPerThread);
            for (int index = 0; index < AllocationsPerThread; ++index) {
                void* storage = runtime.allocate(TinyType);
                auto* value = ::new (storage) Tiny();
                value->value = static_cast<std::uint32_t>(
                    threadIndex * AllocationsPerThread + index);
                runtime.publish(value);
                local.push_back(reinterpret_cast<std::uintptr_t>(value));
                localIdentities.push_back(runtime.identityOf(value));
            }
            runtime.detachCurrentThread();
            std::scoped_lock resultLock(resultsMutex);
            addresses.insert(addresses.end(), local.begin(), local.end());
            identities.insert(identities.end(), localIdentities.begin(),
                              localIdentities.end());
        });
    }
    for (auto& thread : threads)
        thread.join();

    std::sort(addresses.begin(), addresses.end());
    if (addresses.size() != TotalAllocations ||
        std::adjacent_find(addresses.begin(), addresses.end()) != addresses.end()) {
        std::cerr << "TLAB ranges overlapped across mutators\n";
        return 1;
    }
    std::sort(identities.begin(), identities.end());
    if (identities.size() != TotalAllocations || identities.front() == 0 ||
        std::adjacent_find(identities.begin(), identities.end()) !=
            identities.end()) {
        std::cerr << "thread-local identity ranges overlapped across mutators\n";
        return 1;
    }

    const auto allocations = runtime.allocationStats();
    if (allocations.allocatedObjects != TotalAllocations ||
        allocations.tlabFastAllocations < TotalAllocations * 95U / 100U ||
        allocations.tlabRefills == 0 || allocations.createdRegions == 0) {
        std::cerr << "allocation counters do not prove a TLAB fast path\n";
        return 1;
    }

    runtime.attachCurrentThread();
    const auto cleanup = runtime.collectBootstrap();
    runtime.detachCurrentThread();
    if (cleanup.deadObjects != TotalAllocations ||
        cleanup.reclaimedRegions != allocations.createdRegions) {
        std::cerr << "object-start bitmap missed objects or regions during reclaim: "
                  << "dead=" << cleanup.deadObjects
                  << " regions=" << cleanup.reclaimedRegions
                  << " created=" << allocations.createdRegions << '\n';
        return 1;
    }
    runtime.shutdown();
    std::cout << "hxcpp TLAB/object-start test passed: fast="
              << allocations.tlabFastAllocations
              << " refills=" << allocations.tlabRefills
              << " regions=" << allocations.createdRegions << '\n';
    return 0;
}

#include <hx/gc/runtime.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

namespace {

struct Blob final {
    std::uint8_t first = 0;
};

const hx::gc::TypeDescriptor BlobType{
    0x5354414c4c424c4fULL,
    "test.AllocationStallBlob",
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

void delayEmergencyRemark(void*) {
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
}

} // namespace

int main() {
    constexpr int ThreadCount = 6;
    constexpr std::size_t PayloadBytes = 32U * 1024U;
    auto& runtime = hx::gc::Runtime::instance();
    hx::gc::RuntimeConfig config{};
    config.regionBytes = 64U * 1024U;
    config.tlabBytes = 8U * 1024U;
    config.largeObjectThreshold = 1024;
    config.maximumRegions = 1;
    config.allocationServiceLevel =
        hx::gc::AllocationServiceLevel::Throughput;
    config.allocationStallTimeoutMilliseconds = 5000;
    config.remarkTailHook = delayEmergencyRemark;
    runtime.initialize(config);

    runtime.attachCurrentThread();
    auto* seed = static_cast<Blob*>(
        runtime.allocateVariable(BlobType, PayloadBytes));
    seed->first = 1;
    runtime.publish(seed);
    runtime.detachCurrentThread();

    std::atomic<int> ready{0};
    std::atomic<bool> begin{false};
    std::mutex failureMutex;
    std::exception_ptr failure;
    std::vector<std::thread> threads;
    threads.reserve(ThreadCount);
    for (int threadIndex = 0; threadIndex < ThreadCount; ++threadIndex) {
        threads.emplace_back([&, threadIndex] {
            try {
                runtime.attachCurrentThread();
                ready.fetch_add(1, std::memory_order_release);
                while (!begin.load(std::memory_order_acquire))
                    std::this_thread::yield();
                auto* blob = static_cast<Blob*>(
                    runtime.allocateVariable(BlobType, PayloadBytes));
                blob->first = static_cast<std::uint8_t>(threadIndex + 2);
                runtime.publish(blob);
                runtime.safepointPoll();
                runtime.detachCurrentThread();
            } catch (...) {
                std::scoped_lock failureLock(failureMutex);
                if (failure == nullptr)
                    failure = std::current_exception();
            }
        });
    }
    while (ready.load(std::memory_order_acquire) != ThreadCount)
        std::this_thread::yield();
    begin.store(true, std::memory_order_release);
    for (std::thread& thread : threads)
        thread.join();
    if (failure != nullptr)
        std::rethrow_exception(failure);

    const auto stats = runtime.allocationStats();
    if (stats.allocationStalls == 0 ||
        stats.maximumAllocationStallWaiters == 0 ||
        stats.allocationStallMicros == 0 ||
        stats.allocationStallTimeouts != 0 ||
        stats.emergencyCollections == 0) {
        std::cerr << "allocation-stall service was not exercised: stalls="
                  << stats.allocationStalls << " max_waiters="
                  << stats.maximumAllocationStallWaiters << " wait_us="
                  << stats.allocationStallMicros << " timeouts="
                  << stats.allocationStallTimeouts << " emergency="
                  << stats.emergencyCollections << '\n';
        return 1;
    }

    runtime.shutdown();
    std::cout << "hxcpp allocation-stall service passed: stalls="
              << stats.allocationStalls << " max_waiters="
              << stats.maximumAllocationStallWaiters << '\n';
    return 0;
}

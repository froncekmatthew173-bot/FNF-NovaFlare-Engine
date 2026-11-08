#include <hx/gc/runtime.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

struct CallbackValue final {
    int sequence = 0;
};

const hx::gc::TypeDescriptor CallbackValueType{
    0x464f524549474e41ULL,
    "test.ForeignCallbackValue",
    nullptr,
    sizeof(CallbackValue),
    alignof(CallbackValue),
    0,
    0,
    0,
    nullptr,
    nullptr,
    nullptr,
};

[[nodiscard]] CallbackValue* allocateValue(int sequence) {
    auto& runtime = hx::gc::Runtime::instance();
    void* storage = runtime.allocate(CallbackValueType);
    auto* value = ::new (storage) CallbackValue();
    value->sequence = sequence;
    runtime.publish(value);
    return value;
}

} // namespace

int main() {
    constexpr int CycleCount = 32;
    constexpr int CallbackCount = 8;
    auto& runtime = hx::gc::Runtime::instance();
    hx::gc::RuntimeConfig config{};
    config.handshakeTimeoutMilliseconds = 3000;
    runtime.initialize(config);

    for (int cycleIndex = 0; cycleIndex < CycleCount; ++cycleIndex) {
        runtime.startConcurrentMark();

        std::atomic<int> ready{0};
        std::atomic<bool> enterManaged{false};
        std::atomic<int> completed{0};
        std::mutex failureMutex;
        std::exception_ptr failure;
        std::vector<std::thread> callbacks;
        callbacks.reserve(CallbackCount);

        for (int callbackIndex = 0; callbackIndex < CallbackCount;
             ++callbackIndex) {
            callbacks.emplace_back([&, callbackIndex] {
                try {
                    ready.fetch_add(1, std::memory_order_release);
                    while (!enterManaged.load(std::memory_order_acquire))
                        std::this_thread::yield();

                    // This is the NDLL transition pattern: attachment, managed
                    // allocation, and an owner-less exact stack root.  The
                    // transition intentionally races the remark handshake.
                    runtime.attachCurrentThread();
                    auto* value = allocateValue(
                        cycleIndex * CallbackCount + callbackIndex);
                    hx::gc::RefSlot rootSlot{runtime.makeRef(value).bits()};
                    hx::gc::RefSlot* slots[]{&rootSlot};
                    {
                        hx::gc::RootScope roots(
                            slots, 1,
                            static_cast<std::uint32_t>(0x4600 + callbackIndex));
                        auto* retained = static_cast<CallbackValue*>(
                            runtime.loadBarrier(rootSlot));
                        if (retained == nullptr ||
                            retained->sequence !=
                                cycleIndex * CallbackCount + callbackIndex) {
                            throw std::runtime_error(
                                "foreign callback root was not retained");
                        }
                    }
                    runtime.detachCurrentThread();
                    completed.fetch_add(1, std::memory_order_release);
                } catch (...) {
                    std::scoped_lock failureLock(failureMutex);
                    if (failure == nullptr)
                        failure = std::current_exception();
                }
            });
        }

        while (ready.load(std::memory_order_acquire) != CallbackCount)
            std::this_thread::yield();
        enterManaged.store(true, std::memory_order_release);
        (void)runtime.finishConcurrentMark();

        for (auto& callback : callbacks)
            callback.join();
        if (failure != nullptr)
            std::rethrow_exception(failure);
        if (completed.load(std::memory_order_acquire) != CallbackCount) {
            std::cerr << "foreign callback transition did not complete\n";
            return 1;
        }

        (void)runtime.collectBootstrap();
    }

    runtime.shutdown();
    std::cout << "hxcpp foreign callback/remark handshake passed\n";
    return 0;
}

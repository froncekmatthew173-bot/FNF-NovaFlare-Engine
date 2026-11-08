#include <hxcpp.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace {

std::atomic<bool> nativeWaitEntered{false};

Dynamic persistentNativeWait() {
    // Model TraceServer's Socket.accept(): the Haxe thread remains attached,
    // but tells the precise collector it is blocked outside managed code.
    auto& runtime = ::hx::gc::Runtime::instance();
    runtime.enterNativeBlocking();
    nativeWaitEntered.store(true, std::memory_order_release);
    for (;;)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

} // namespace

int main() {
    __hxcpp_nova_initialize();
    {
        HX_NOVAGC_VARI(::Dynamic, job) = ::hx::CreateStaticFunction0(
            "persistentNativeWait", &persistentNativeWait);
        (void)__hxcpp_thread_create(job);
    }

    const auto startDeadline = std::chrono::steady_clock::now() +
                               std::chrono::seconds(2);
    while (!nativeWaitEntered.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= startDeadline) {
            std::cerr << "persistent Haxe thread did not enter native wait\n";
            return 1;
        }
        std::this_thread::yield();
    }

    const auto shutdownStart = std::chrono::steady_clock::now();
    __hxcpp_nova_shutdown();
    const auto shutdownElapsed = std::chrono::steady_clock::now() -
                                 shutdownStart;
    if (shutdownElapsed > std::chrono::seconds(2)) {
        std::cerr << "shutdown waited for a detached application thread\n";
        return 2;
    }
    std::cout << "detached application thread shutdown test passed\n";
    return 0;
}

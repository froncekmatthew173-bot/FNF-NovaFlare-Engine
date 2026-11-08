#include <hxcpp.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>

namespace {

std::atomic<int> automaticFinalizers{0};

struct Node final {
    ::hx::gc::RefSlot next{0};
    std::uint64_t value = 0;
};

constexpr std::uint32_t NodeReferences[]{
    static_cast<std::uint32_t>(offsetof(Node, next)),
};

const ::hx::gc::TypeDescriptor NodeType{
    0x4155544f47435445ULL,
    "test.AutomaticGcNode",
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

struct AutomaticFinalizable final {
    std::uint64_t value = 0;
};

void finalizeAutomatic(void*) {
    automaticFinalizers.fetch_add(1, std::memory_order_relaxed);
}

const ::hx::gc::TypeDescriptor AutomaticFinalizableType{
    0x4155544f46494e31ULL,
    "test.AutomaticFinalizable",
    nullptr,
    sizeof(AutomaticFinalizable),
    alignof(AutomaticFinalizable),
    0,
    ::hx::gc::TypeFinalizable,
    0,
    nullptr,
    nullptr,
    &finalizeAutomatic,
};

[[nodiscard]] Node* allocateNode(std::uint64_t value) {
    auto& runtime = ::hx::gc::Runtime::instance();
    void* storage = runtime.allocate(NodeType);
    auto* node = ::new (storage) Node();
    node->value = value;
    runtime.publish(node);
    return node;
}

void allocateAutomaticFinalizable(std::uint64_t value) {
    auto& runtime = ::hx::gc::Runtime::instance();
    void* storage = runtime.allocate(AutomaticFinalizableType);
    auto* object = ::new (storage) AutomaticFinalizable();
    object->value = value;
    runtime.publish(object);
}

void configureEnvironment() {
#ifdef _WIN32
    _putenv_s("HXCPP_NOVAGC_INITIAL_TRIGGER_MB", "16");
    _putenv_s("HXCPP_NOVAGC_YOUNG_TRIGGER_MB", "4");
    _putenv_s("HXCPP_NOVAGC_MIN_YOUNG_MB", "4");
    _putenv_s("HXCPP_NOVAGC_MAX_YOUNG_MB", "4");
    _putenv_s("HXCPP_NOVAGC_MIN_HEADROOM_MB", "16");
    _putenv_s("HXCPP_NOVAGC_MAX_HEADROOM_MB", "64");
    _putenv_s("HXCPP_NOVAGC_RESUME_GRACE_MS", "25");
#else
    setenv("HXCPP_NOVAGC_INITIAL_TRIGGER_MB", "16", 1);
    setenv("HXCPP_NOVAGC_YOUNG_TRIGGER_MB", "4", 1);
    setenv("HXCPP_NOVAGC_MIN_YOUNG_MB", "4", 1);
    setenv("HXCPP_NOVAGC_MAX_YOUNG_MB", "4", 1);
    setenv("HXCPP_NOVAGC_MIN_HEADROOM_MB", "16", 1);
    setenv("HXCPP_NOVAGC_MAX_HEADROOM_MB", "64", 1);
    setenv("HXCPP_NOVAGC_RESUME_GRACE_MS", "25", 1);
#endif
}

} // namespace

int main() {
    configureEnvironment();
    __hxcpp_nova_initialize();
    auto& runtime = ::hx::gc::Runtime::instance();
    bool reclaimed = false;
    bool requestedFullReclaimed = false;
    bool suppressionHeld = false;
    bool resumedCollection = false;
    bool smallDebtResumedCollection = false;
    std::uint64_t requestLatencyMicros = 0;
    std::uint64_t allocated = 0;
    std::uint64_t used = 0;

    {
        Node* root = allocateNode(1);
        ::hx::gc::RefSlot rootSlot{runtime.makeRef(root).bits()};
        ::hx::gc::RefSlot* roots[]{&rootSlot};
        ::hx::gc::RootScope rootScope(roots, 1, 0x4155544fU);

        constexpr int AllocationCount = 1'200'000;
        for (int index = 0; index < AllocationCount; ++index) {
            (void)allocateNode(static_cast<std::uint64_t>(index));
            if ((index & 1023) == 0)
                allocateAutomaticFinalizable(
                    static_cast<std::uint64_t>(index));
            if ((index & 255) == 0)
                runtime.safepointPoll();
        }

        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(8);
        while (std::chrono::steady_clock::now() < deadline) {
            runtime.safepointPoll();
            allocated = runtime.allocationStats().allocatedBytes;
            used = runtime.usedBytes();
            if (allocated > used + 16U * 1024U * 1024U &&
                automaticFinalizers.load(std::memory_order_acquire) != 0) {
                reclaimed = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        auto* retained = static_cast<Node*>(runtime.loadBarrier(rootSlot));
        if (retained == nullptr || retained->value != 1) {
            std::cerr << "automatic GC corrupted its exact root\n";
            return 1;
        }

        // cpp.vm.Gc.enable(false) is used by NovaFlare's loading screen. It
        // must suppress ordinary automatic cycles while decoding/uploading
        // assets, then resume without synchronously collecting on the caller.
        __hxcpp_enable(false);
        const auto settleDeadline = std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(50);
        while (std::chrono::steady_clock::now() < settleDeadline) {
            runtime.safepointPoll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const int finalizersBeforeSuppression =
            automaticFinalizers.load(std::memory_order_acquire);
        allocateAutomaticFinalizable(0xD15AB1EDU);
        for (int index = 0; index < 400'000; ++index) {
            (void)allocateNode(static_cast<std::uint64_t>(index));
            if ((index & 255) == 0)
                runtime.safepointPoll();
        }
        __hxcpp_gc_request_full();
        const auto suppressedDeadline = std::chrono::steady_clock::now() +
                                        std::chrono::milliseconds(150);
        while (std::chrono::steady_clock::now() < suppressedDeadline) {
            runtime.safepointPoll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        suppressionHeld = automaticFinalizers.load(std::memory_order_acquire) ==
                          finalizersBeforeSuppression;
        __hxcpp_enable(true);
        const auto resumedDeadline = std::chrono::steady_clock::now() +
                                     std::chrono::seconds(4);
        while (std::chrono::steady_clock::now() < resumedDeadline) {
            runtime.safepointPoll();
            if (automaticFinalizers.load(std::memory_order_acquire) >
                finalizersBeforeSuppression) {
                resumedCollection = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // A UI/page transition must only enqueue the full collection.  Keep
        // allocation below the next automatic threshold, then verify that the
        // explicit request returns immediately and the collector performs it.
        const int finalizersBeforeRequest =
            automaticFinalizers.load(std::memory_order_acquire);
        allocateAutomaticFinalizable(0xF011U);
        const auto requestStart = std::chrono::steady_clock::now();
        __hxcpp_gc_request_full();
        requestLatencyMicros = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - requestStart).count());
        const auto requestDeadline = std::chrono::steady_clock::now() +
                                     std::chrono::seconds(4);
        while (std::chrono::steady_clock::now() < requestDeadline) {
            runtime.safepointPoll();
            if (automaticFinalizers.load(std::memory_order_acquire) >
                finalizersBeforeRequest) {
                requestedFullReclaimed = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // A short loading window with less than one nursery of allocation
        // must resume at the adaptive Young threshold. The old implementation
        // installed MAX_HEADROOM (64 MiB here, 256 MiB in production), so the
        // finalizable below survived indefinitely even after another nursery
        // had been allocated.
        __hxcpp_enable(false);
        const int finalizersBeforeSmallDebt =
            automaticFinalizers.load(std::memory_order_acquire);
        allocateAutomaticFinalizable(0x5A11DEB7U);
        for (int index = 0; index < 1'000; ++index)
            (void)allocateNode(static_cast<std::uint64_t>(index));
        __hxcpp_enable(true);
        for (int index = 0; index < 200'000; ++index) {
            (void)allocateNode(static_cast<std::uint64_t>(index));
            if ((index & 255) == 0)
                runtime.safepointPoll();
        }
        const auto smallDebtDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (std::chrono::steady_clock::now() < smallDebtDeadline) {
            runtime.safepointPoll();
            if (automaticFinalizers.load(std::memory_order_acquire) >
                finalizersBeforeSmallDebt) {
                smallDebtResumedCollection = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    __hxcpp_nova_shutdown();
    if (!reclaimed) {
        std::cerr << "automatic GC did not reclaim allocation debt: allocated="
                  << allocated << " used=" << used
                  << " finalizers=" << automaticFinalizers.load() << '\n';
        return 1;
    }
    if (!requestedFullReclaimed || requestLatencyMicros > 50'000U) {
        std::cerr << "asynchronous full GC request failed: reclaimed="
                  << requestedFullReclaimed
                  << " request_us=" << requestLatencyMicros << '\n';
        return 1;
    }
    if (!suppressionHeld || !resumedCollection) {
        std::cerr << "automatic GC suppression failed: held="
                  << suppressionHeld << " resumed=" << resumedCollection
                  << '\n';
        return 1;
    }
    if (!smallDebtResumedCollection) {
        std::cerr << "automatic GC small-debt resume skipped the adaptive "
                     "Young threshold\n";
        return 1;
    }
    std::cout << "hxcpp automatic concurrent GC passed: allocated="
              << allocated << " used=" << used
              << " request_us=" << requestLatencyMicros << '\n';
    return 0;
}

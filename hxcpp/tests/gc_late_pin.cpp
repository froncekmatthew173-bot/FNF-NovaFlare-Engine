#include <hx/gc/runtime.hpp>

#include <atomic>
#include <cstddef>
#include <iostream>
#include <new>
#include <thread>

namespace {

struct Node final {
    int value = 0;
};

const hx::gc::TypeDescriptor NodeType{
    0x4c41544550494e31ULL, "test.LatePinNode", nullptr,
    sizeof(Node), alignof(Node), 0, hx::gc::TypeNone,
    0, nullptr, nullptr, nullptr,
};

struct HookState final {
    void* target = nullptr;
    std::atomic<bool> arrived{false};
    std::atomic<bool> resume{false};
};

void beforeCopy(void* context, void* object) {
    auto& state = *static_cast<HookState*>(context);
    if (object != state.target)
        return;
    state.arrived.store(true, std::memory_order_release);
    while (!state.resume.load(std::memory_order_acquire))
        std::this_thread::yield();
}

} // namespace

int main() {
    HookState hook;
    hx::gc::RuntimeConfig config;
    config.relocationBeforeCopyHook = &beforeCopy;
    config.relocationHookContext = &hook;

    auto& runtime = hx::gc::Runtime::instance();
    runtime.initialize(config);
    runtime.attachCurrentThread();
    void* storage = runtime.allocate(NodeType);
    auto* original = ::new (storage) Node{91};
    runtime.publish(original);
    hook.target = original;

    hx::gc::RefSlot root{runtime.makeRef(original).bits()};
    hx::gc::RefSlot* slots[]{&root};
    {
        hx::gc::RootScope roots(slots, 1);
        runtime.startConcurrentMark();
        (void)runtime.finishConcurrentMark();
        runtime.beginConcurrentRelocation();
        while (!hook.arrived.load(std::memory_order_acquire))
            std::this_thread::yield();

        void* pinned = runtime.acquireScopedPin(root);
        void* nestedPinned = runtime.acquireScopedPin(root);
        if (nestedPinned != pinned) {
            std::cerr << "nested scoped pin changed a counted address\n";
            return 1;
        }
        runtime.releaseScopedPin(nestedPinned);
        hook.resume.store(true, std::memory_order_release);
        const auto relocation = runtime.finishConcurrentRelocation();
        auto* rooted = static_cast<Node*>(runtime.loadBarrier(root));
        auto* resolved = static_cast<Node*>(pinned);
        if (relocation.relocatedObjects != 0 || rooted != original ||
            resolved != original || resolved->value != 91) {
            std::cerr << "late pin did not cancel source retirement safely\n";
            return 1;
        }
        runtime.releaseScopedPin(pinned);
    }

    runtime.detachCurrentThread();
    runtime.shutdown();
    std::cout << "late relocation pin cancellation test passed\n";
    return 0;
}

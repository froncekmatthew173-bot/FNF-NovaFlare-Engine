#include <hx/gc/runtime.hpp>

#include <atomic>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {

struct Resource final { int id = 0; };
hx::gc::RefSlot resurrected{0};
std::atomic<int> callbacks{0};

void finalizeResource(void* object) {
    auto* resource = static_cast<Resource*>(object);
    callbacks.fetch_add(1, std::memory_order_relaxed);
    if (resource->id == 1)
        hx::gc::Runtime::instance().storeBarrier(nullptr, resurrected, resource);
    if (resource->id == 3)
        throw std::runtime_error("injected finalizer failure");
    if (resource->id == 4) {
        // CFFI finalizers call val_data/val_gc while the owning Full cycle is
        // still active. Their temporary pinned handle must reuse the
        // finalizer's existing pin instead of recursively starting a pin-
        // isolation GC cycle.
        auto& runtime = hx::gc::Runtime::instance();
        const auto pin = runtime.createHandle(
            resource, hx::gc::HandleKind::Pinned);
        auto* resolved = static_cast<Resource*>(runtime.resolveHandle(pin));
        if (resolved == nullptr || resolved->id != 4)
            throw std::runtime_error("finalizer pinned handle was not exact");
        runtime.releaseHandle(pin);
    }
}

const hx::gc::TypeDescriptor ResourceType{
    0x46494e414c4e4f56ULL, "test.FinalizableResource", nullptr,
    sizeof(Resource), alignof(Resource), 0, hx::gc::TypeFinalizable,
    0, nullptr, nullptr, &finalizeResource,
};

Resource* allocateResource(int id) {
    auto& runtime = hx::gc::Runtime::instance();
    void* storage = runtime.allocate(ResourceType);
    auto* resource = ::new (storage) Resource();
    resource->id = id;
    runtime.publish(resource);
    return resource;
}

int fail(const char* message) {
    std::cerr << "finalizer failure: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    auto& runtime = hx::gc::Runtime::instance();
    runtime.initialize();
    runtime.attachCurrentThread();
    runtime.registerStaticRoot(resurrected);

    (void)allocateResource(1);
    (void)allocateResource(2);
    (void)allocateResource(3);
    const auto discovered = runtime.collectBootstrap();
    if (discovered.markedObjects != 3)
        return fail("unreachable finalizable objects were not kept pending");
    const auto moved = runtime.compactBootstrap();
    if (moved.relocatedObjects != 3)
        return fail("pending finalizers were not safely relocated");

    const auto drained = runtime.drainFinalizers();
    if (drained.completed != 3 || drained.failed != 1 ||
        callbacks.load(std::memory_order_relaxed) != 3)
        return fail("finalizer completion/failure counts are wrong");
    if (runtime.drainFinalizers().completed != 0)
        return fail("finalizer callback executed more than once");
    auto* survivor = static_cast<Resource*>(runtime.loadBarrier(resurrected));
    if (survivor == nullptr || survivor->id != 1)
        return fail("finalizer resurrection did not publish the object");

    const auto afterResurrection = runtime.collectBootstrap();
    if (afterResurrection.markedObjects != 1)
        return fail("only the resurrected finalized object should remain live");
    resurrected.store(0, std::memory_order_release);
    const auto afterDrop = runtime.collectBootstrap();
    if (afterDrop.markedObjects != 0 || afterDrop.reclaimedRegions == 0)
        return fail("finalized object was not reclaimed after dropping resurrection");

    (void)allocateResource(4);
    runtime.detachCurrentThread();
    runtime.startConcurrentMark();
    const auto concurrentDiscovery = runtime.finishConcurrentMark();
    runtime.attachCurrentThread();
    if (concurrentDiscovery.markedObjects != 1)
        return fail("concurrent remark did not retain pending finalizer");
    if (concurrentDiscovery.finalizers.completed != 1 ||
        concurrentDiscovery.finalizers.failed != 0 ||
        callbacks.load(std::memory_order_relaxed) != 4)
        return fail("concurrent finalizer was not committed before resume");
    if (runtime.drainFinalizers().completed != 0)
        return fail("concurrent remark left a deferred finalizer race");
    const auto concurrentCleanup = runtime.collectBootstrap();
    if (concurrentCleanup.markedObjects != 0 ||
        concurrentCleanup.reclaimedRegions == 0)
        return fail("concurrently discovered finalizer was not reclaimed");

    runtime.unregisterStaticRoot(resurrected);
    runtime.detachCurrentThread();
    runtime.shutdown();
    std::cout << "hxcpp finalizer/resurrection test passed\n";
    return 0;
}

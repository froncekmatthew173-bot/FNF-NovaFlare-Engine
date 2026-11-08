#include <hx/gc/runtime.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>

namespace {

struct Value final { int number = 0; };

const hx::gc::TypeDescriptor ValueType{
    0x48414e444c454e56ULL, "test.HandleValue", nullptr,
    sizeof(Value), alignof(Value), 0, hx::gc::TypeNone,
    0, nullptr, nullptr, nullptr,
};

Value* allocateValue(int number) {
    auto& runtime = hx::gc::Runtime::instance();
    void* storage = runtime.allocate(ValueType);
    auto* value = ::new (storage) Value();
    value->number = number;
    runtime.publish(value);
    return value;
}

int fail(const char* message) {
    std::cerr << "handle failure: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    auto& runtime = hx::gc::Runtime::instance();
    runtime.initialize();
    runtime.attachCurrentThread();

    const auto strong = runtime.createHandle(allocateValue(11),
                                              hx::gc::HandleKind::Strong);
    const auto weak = runtime.createHandle(allocateValue(22),
                                            hx::gc::HandleKind::Weak);
    const auto pinned = runtime.createHandle(allocateValue(33),
                                              hx::gc::HandleKind::Pinned);
    void* pinnedBefore = runtime.resolveHandle(pinned);
    // Generated instance methods hold a scoped receiver pin while registering
    // an opaque native callback.  The persistent pin must adopt that exact
    // address so the userdata already handed to the C library stays valid.
    hx::gc::RefSlot callbackRoot{
        runtime.makeRef(allocateValue(44)).bits()};
    void* callbackReceiver = runtime.acquireScopedPin(callbackRoot);
    void* callbackBefore = runtime.retainNativeCallback(callbackReceiver);
    void* callbackRetained = runtime.retainNativeCallback(callbackBefore);
    if (callbackBefore == nullptr || callbackBefore != callbackReceiver ||
        callbackRetained != callbackBefore)
        return fail("native callback retain did not return one stable address");
    runtime.releaseScopedPin(callbackReceiver);
    const auto pinStats = runtime.heapStats();
    if (pinStats.isolatedPinnedObjects != 1 ||
        pinStats.pinIsolationFallbacks != 0)
        return fail("pinned handle target was not isolated");

    const auto mark = runtime.collectBootstrap();
    if (mark.markedObjects != 3)
        return fail("weak handle incorrectly retained its target");
    if (runtime.resolveHandle(weak) != nullptr)
        return fail("dead weak handle was not cleared at remark");
    if (static_cast<Value*>(runtime.resolveHandle(strong))->number != 11)
        return fail("strong handle did not retain its target");

    const auto moved = runtime.compactBootstrap();
    if (runtime.resolveHandle(pinned) != pinnedBefore)
        return fail("pinned handle target changed address");
    if (static_cast<Value*>(callbackBefore)->number != 44)
        return fail("native callback target changed address or contents");

    runtime.releaseNativeCallback(callbackBefore);
    const auto retainedOnce = runtime.collectBootstrap();
    if (retainedOnce.markedObjects != 3 ||
        static_cast<Value*>(callbackBefore)->number != 44)
        return fail("one native callback release dropped a retained target");
    runtime.releaseNativeCallback(callbackBefore);

    const auto movedAfterCallback = runtime.compactBootstrap();
    if (movedAfterCallback.relocatedObjects == 0)
        return fail("released native callback pin still blocked compaction");
    if (runtime.resolveHandle(pinned) != pinnedBefore)
        return fail("isolated pinned handle moved after callback release");

    runtime.releaseHandle(weak);
    runtime.releaseHandle(strong);
    runtime.releaseHandle(pinned);
    if (runtime.resolveHandle(strong) != nullptr)
        return fail("stale handle generation resolved after release");
    const auto cleanup = runtime.collectBootstrap();
    if (cleanup.markedObjects != 0 || cleanup.reclaimedRegions == 0)
        return fail("released handles retained objects");

    runtime.detachCurrentThread();
    runtime.shutdown();
    std::cout << "hxcpp strong/weak/pinned/native-callback handle test passed\n";
    return 0;
}

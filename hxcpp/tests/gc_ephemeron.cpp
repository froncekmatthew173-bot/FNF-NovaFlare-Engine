#include <hx/gc/runtime.hpp>

#include <iostream>
#include <new>

namespace {

struct Value final { int number = 0; };

const hx::gc::TypeDescriptor ValueType{
    0x455048454d45524fULL, "test.EphemeronValue", nullptr,
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
    std::cerr << "ephemeron failure: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    auto& runtime = hx::gc::Runtime::instance();
    runtime.initialize();
    runtime.attachCurrentThread();

    Value* key1 = allocateValue(1);
    Value* key2 = allocateValue(2);
    Value* value2 = allocateValue(3);
    Value* deadKey = allocateValue(4);
    Value* deadValue = allocateValue(5);
    const auto first = runtime.createEphemeron(key1, key2);
    const auto second = runtime.createEphemeron(key2, value2);
    const auto dead = runtime.createEphemeron(deadKey, deadValue);

    hx::gc::RefSlot rootSlot{runtime.makeRef(key1).bits()};
    hx::gc::RefSlot* slots[]{&rootSlot};
    {
        hx::gc::RootScope roots(slots, 1, 0x5001);
        const auto mark = runtime.collectBootstrap();
        if (mark.markedObjects != 3)
            return fail("ephemeron fixpoint did not retain the two-level chain");
        if (static_cast<Value*>(runtime.resolveEphemeronValue(second))->number != 3)
            return fail("live ephemeron value was cleared");
        if (runtime.resolveEphemeronKey(dead) != nullptr ||
            runtime.resolveEphemeronValue(dead) != nullptr)
            return fail("dead ephemeron key/value were not cleared");

        const auto moved = runtime.compactBootstrap();
        if (moved.relocatedObjects != 3 ||
            static_cast<Value*>(runtime.resolveEphemeronValue(second))->number != 3)
            return fail("ephemeron cells were not remapped after relocation");
    }

    runtime.releaseEphemeron(first);
    runtime.releaseEphemeron(second);
    runtime.releaseEphemeron(dead);
    const auto cleanup = runtime.collectBootstrap();
    if (cleanup.markedObjects != 0 || cleanup.reclaimedRegions == 0)
        return fail("released ephemerons retained objects");

    Value* concurrentKey1 = allocateValue(11);
    Value* concurrentKey2 = allocateValue(12);
    Value* concurrentValue = allocateValue(13);
    Value* concurrentDeadKey = allocateValue(14);
    Value* concurrentDeadValue = allocateValue(15);
    const auto concurrentFirst =
        runtime.createEphemeron(concurrentKey1, concurrentKey2);
    const auto concurrentSecond =
        runtime.createEphemeron(concurrentKey2, concurrentValue);
    const auto concurrentDead =
        runtime.createEphemeron(concurrentDeadKey, concurrentDeadValue);
    hx::gc::RefSlot staticKey{runtime.makeRef(concurrentKey1).bits()};
    runtime.registerStaticRoot(staticKey);

    runtime.detachCurrentThread();
    runtime.startConcurrentMark();
    const auto concurrentMark = runtime.finishConcurrentMark();
    runtime.attachCurrentThread();
    if (concurrentMark.markedObjects != 3 ||
        static_cast<Value*>(runtime.resolveEphemeronValue(concurrentSecond))
                ->number != 13)
        return fail("concurrent remark did not compute ephemeron fixpoint");
    if (runtime.resolveEphemeronKey(concurrentDead) != nullptr ||
        runtime.resolveEphemeronValue(concurrentDead) != nullptr)
        return fail("concurrent remark did not clear dead ephemeron");

    runtime.unregisterStaticRoot(staticKey);
    staticKey.store(0, std::memory_order_release);
    runtime.releaseEphemeron(concurrentFirst);
    runtime.releaseEphemeron(concurrentSecond);
    runtime.releaseEphemeron(concurrentDead);
    const auto finalCleanup = runtime.collectBootstrap();
    if (finalCleanup.markedObjects != 0 || finalCleanup.reclaimedRegions == 0)
        return fail("concurrent ephemeron graph survived release");

    runtime.detachCurrentThread();
    runtime.shutdown();
    std::cout << "hxcpp ephemeron fixpoint/remap test passed\n";
    return 0;
}

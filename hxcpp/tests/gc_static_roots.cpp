#include <hx/gc/runtime.hpp>

#include <iostream>
#include <new>

namespace {

struct Value final { int number = 0; };
hx::gc::RefSlot staticValue{0};
hx::gc::RefSlot lateStaticValue{0};

const hx::gc::TypeDescriptor ValueType{
    0x5354415449434e56ULL, "test.StaticValue", nullptr,
    sizeof(Value), alignof(Value), 0, hx::gc::TypeNone,
    0, nullptr, nullptr, nullptr,
};

} // namespace

int main() {
    // Keep this translation unit in the strict RuntimeConfig ABI rebuild gate.
    auto& runtime = hx::gc::Runtime::instance();
    runtime.initialize();
    runtime.attachCurrentThread();
    void* storage = runtime.allocate(ValueType);
    auto* value = ::new (storage) Value();
    value->number = 73;
    runtime.publish(value);
    runtime.storeBarrier(nullptr, staticValue, value);
    runtime.registerStaticRoot(staticValue);

    const auto first = runtime.collectBootstrap();
    if (first.roots != 1 || first.markedObjects != 1 ||
        static_cast<Value*>(runtime.loadBarrier(staticValue))->number != 73) {
        std::cerr << "registered static root did not retain its object\n";
        return 1;
    }
    const auto moved = runtime.compactBootstrap();
    if (moved.relocatedObjects != 1 ||
        hx::gc::Ref(staticValue.load()).view() != hx::gc::View::Remapped) {
        std::cerr << "static root was not remapped after relocation\n";
        return 1;
    }

    storage = runtime.allocate(ValueType);
    auto* lateValue = ::new (storage) Value();
    lateValue->number = 91;
    runtime.publish(lateValue);
    lateStaticValue.store(runtime.makeRef(lateValue).bits(),
                          std::memory_order_release);
    runtime.startConcurrentMark();
    runtime.registerStaticRoot(lateStaticValue);
    const auto concurrent = runtime.finishConcurrentMark();
    if (concurrent.markedObjects < 2 ||
        static_cast<Value*>(runtime.loadBarrier(lateStaticValue))->number != 91) {
        std::cerr << "late static root was not shaded during concurrent mark\n";
        return 1;
    }
    runtime.unregisterStaticRoot(lateStaticValue);
    lateStaticValue.store(0, std::memory_order_release);

    runtime.unregisterStaticRoot(staticValue);
    staticValue.store(0, std::memory_order_release);
    const auto cleanup = runtime.collectBootstrap();
    if (cleanup.markedObjects != 0 || cleanup.reclaimedRegions == 0) {
        std::cerr << "unregistered static root retained its object\n";
        return 1;
    }
    runtime.detachCurrentThread();
    runtime.shutdown();
    std::cout << "hxcpp exact static-root test passed\n";
    return 0;
}

#include <hx/gc/runtime.hpp>

#include <cstdint>
#include <iostream>
#include <new>

namespace {

struct Value final {
    std::uint64_t number = 0;
};

const ::hx::gc::TypeDescriptor ValueType{
    0x5245464552454e43ULL,
    "test.ReferenceValue",
    nullptr,
    sizeof(Value),
    alignof(Value),
    0,
    ::hx::gc::TypeNone,
    0,
    nullptr,
    nullptr,
    nullptr,
};

[[nodiscard]] Value* allocateValue(std::uint64_t number) {
    auto& runtime = ::hx::gc::Runtime::instance();
    auto* value = ::new (runtime.allocate(ValueType)) Value();
    value->number = number;
    runtime.publish(value);
    return value;
}

[[nodiscard]] bool sameHandle(::hx::gc::Handle left,
                              ::hx::gc::Handle right) noexcept {
    return left.index == right.index && left.generation == right.generation;
}

bool expectQueued(::hx::gc::Runtime& runtime,
                  ::hx::gc::Handle expected) {
    if (!runtime.referenceEnqueued(expected))
        return false;
    ::hx::gc::Handle actual{};
    if (!runtime.pollReferenceQueue(actual) ||
        !sameHandle(actual, expected) ||
        runtime.referenceEnqueued(expected)) {
        return false;
    }
    return true;
}

} // namespace

int main() {
    auto& runtime = ::hx::gc::Runtime::instance();
    try {
        ::hx::gc::RuntimeConfig config{};
        config.regionBytes = 64U * 1024U;
        config.tlabBytes = 16U * 1024U;
        config.maximumRegions = 32;
        config.initialTenuringThreshold = 3;
        config.maximumTenuringThreshold = 3;
        config.adaptiveTenuring = false;
        runtime.initialize(config);
        runtime.attachCurrentThread();

        const auto weak = runtime.createHandle(
            allocateValue(1), ::hx::gc::HandleKind::Weak);
        (void)runtime.collectYoung();
        if (runtime.resolveHandle(weak) != nullptr ||
            !expectQueued(runtime, weak)) {
            std::cerr << "weak reference was not cleared and enqueued\n";
            return 1;
        }
        runtime.releaseHandle(weak);

        const auto soft = runtime.createHandle(
            allocateValue(2), ::hx::gc::HandleKind::Soft);
        (void)runtime.collectYoung();
        auto* softValue = static_cast<Value*>(runtime.resolveHandle(soft));
        if (softValue == nullptr || softValue->number != 2 ||
            runtime.referenceEnqueued(soft)) {
            std::cerr << "soft reference was cleared without pressure\n";
            return 1;
        }
        runtime.requestSoftReferenceClearing();
        (void)runtime.collectYoung();
        if (runtime.resolveHandle(soft) != nullptr ||
            !expectQueued(runtime, soft)) {
            std::cerr << "soft reference ignored critical pressure\n";
            return 1;
        }
        runtime.releaseHandle(soft);

        Value* phantomValue = allocateValue(3);
        ::hx::gc::RefSlot root{runtime.makeRef(phantomValue).bits()};
        ::hx::gc::RefSlot* roots[]{&root};
        const auto phantom = runtime.createHandle(
            phantomValue, ::hx::gc::HandleKind::Phantom);
        {
            ::hx::gc::RootScope scope(roots, 1, 0x52454631U);
            (void)runtime.collectYoung();
            if (runtime.resolveHandle(phantom) != nullptr ||
                runtime.referenceEnqueued(phantom)) {
                std::cerr << "live phantom reference was exposed or queued\n";
                return 1;
            }
            root.store(0, std::memory_order_release);
            (void)runtime.collectYoung();
        }
        if (!expectQueued(runtime, phantom)) {
            std::cerr << "dead phantom reference was not enqueued\n";
            return 1;
        }
        runtime.releaseHandle(phantom);

        ::hx::gc::Handle empty{};
        if (runtime.pollReferenceQueue(empty)) {
            std::cerr << "reference queue retained a stale entry\n";
            return 1;
        }

        runtime.detachCurrentThread();
        runtime.shutdown();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "hxcpp soft/weak/phantom reference queue test passed\n";
    return 0;
}

#include <hxcpp.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <new>

namespace {

struct TestObject final : ::hx::Object {
    int value = 0;
};

struct TestNativeStruct final {
    int value = 0;
    bool operator==(const TestNativeStruct&) const = default;
};

const ::hx::gc::TypeDescriptor TestDescriptor{
    0x434f4d5041543031ULL, "test.CompatibilityObject", nullptr,
    sizeof(TestObject), alignof(TestObject), 0, ::hx::gc::TypeNone,
    0, nullptr, nullptr, nullptr,
};

std::atomic<int> finalizerCalls{0};

void dynamicFinalizer(Dynamic value) {
    if (value.tag() == Dynamic::Tag::Object)
        finalizerCalls.fetch_add(1, std::memory_order_relaxed);
}

int fail(const char* message) {
    std::cerr << "compatibility failure: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    auto& runtime = ::hx::gc::Runtime::instance();
    runtime.initialize();
    runtime.attachCurrentThread();

    const auto castNull =
        ::hx::TCast<::hx::ObjectPtr<TestObject>>::cast(null());
    if (castNull != nullptr)
        return fail("TCast(null()) did not produce a null object pointer");

    const int field = __hxcpp_field_to_id("textureScale");
    if (field <= 0 || __hxcpp_field_from_id(field) != "textureScale")
        return fail("field atom did not round-trip");

    if (__hxcpp_parse_substr_int("xx0x2aYY", 2, 4) != 42 ||
        std::abs(__hxcpp_parse_substr_float("aa12.5zz", 2, 4) - 12.5) >
            0.000001)
        return fail("numeric substring conversion is incompatible");

    alignas(double) unsigned char bytes[16]{};
    __hxcpp_align_set_float32(bytes, 1, 3.25F);
    __hxcpp_align_set_float64(bytes, 7, -9.5);
    if (__hxcpp_align_get_float32(bytes, 1) != 3.25F ||
        __hxcpp_align_get_float64(bytes, 7) != -9.5)
        return fail("unaligned floating access lost data");

    int endian = 0x01020304;
    __hxcpp_reverse_endian(endian);
    if (static_cast<std::uint32_t>(endian) != 0x04030201U)
        return fail("endian reversal is wrong");

    {
        ::cpp::Struct<TestNativeStruct> native(TestNativeStruct{17});
        if (::hx::IsNull(native) || !::hx::IsNotNull(native) ||
            native == null() || !(native != null()))
            return fail("inline native struct was misclassified as null");
        native = null();
        if (native->value != 0 || ::hx::IsNull(native))
            return fail("native struct null assignment did not reset value");
    }

    {
        auto object = ::hx::allocate<TestObject>(TestDescriptor);
        object->value = 17;
        Dynamic dynamic(object.get());
        const int firstId = __hxcpp_obj_id(dynamic);
        const int secondId = __hxcpp_obj_id(dynamic);
        if (firstId <= 0 || firstId != secondId ||
            __hxcpp_id_obj(firstId) != object.get())
            return fail("stable object identity is broken");

        ::hx::Object* weakObject = __hxcpp_weak_ref_create(dynamic);
        ::hx::gc::RefSlot weakRoot{
            runtime.makeRef(weakObject).bits()
        };
        runtime.registerStaticRoot(weakRoot);
        dynamic = Dynamic();
        object = nullptr;
        runtime.collectBootstrap();
        if (__hxcpp_weak_ref_get(Dynamic(static_cast<::hx::Object*>(
                runtime.loadBarrier(weakRoot)))) != nullptr)
            return fail("weak reference retained its referent");
        if (__hxcpp_id_obj(firstId) != nullptr)
            return fail("object ID retained a dead object");
        weakRoot.store(0, std::memory_order_release);
        runtime.unregisterStaticRoot(weakRoot);
        runtime.collectBootstrap();
        runtime.drainFinalizers();
        runtime.collectBootstrap();
    }

    {
        auto object = ::hx::allocate<TestObject>(TestDescriptor);
        _hx_set_finalizer(Dynamic(object.get()), &dynamicFinalizer);
        object = nullptr;
        runtime.collectBootstrap();
        const auto drained = runtime.drainFinalizers();
        if (drained.completed != 1 || drained.failed != 0 ||
            finalizerCalls.load(std::memory_order_relaxed) != 1)
            return fail("per-object finalizer did not execute exactly once");
        runtime.collectBootstrap();
        if (runtime.drainFinalizers().completed != 0)
            return fail("per-object finalizer executed twice");
    }

    __hxcpp_set_minor_base_delta_bytes(12345);
    __hxcpp_set_minor_gate_ms(7);
    __hxcpp_set_minor_start_bytes(67890);
    __hxcpp_gc_set_threads(3, 2);
    __hxcpp_gc_set_max_pause_ms(2);
    __hxcpp_gc_aggressive_safepoint(1);
    __hxcpp_gc_enable_parallel_ref_proc(0);
    if (__hxcpp_get_minor_base_delta_bytes() != 12345 ||
        __hxcpp_get_minor_gate_ms() != 7 ||
        __hxcpp_get_minor_start_bytes() != 67890 ||
        __hxcpp_gc_get_parallel_threads() != 3 ||
        __hxcpp_gc_get_refine_threads() != 2 ||
        __hxcpp_gc_get_max_pause_ms() != 2 ||
        __hxcpp_gc_get_aggressive_safepoint() != 1 ||
        __hxcpp_gc_get_parallel_ref_proc_enabled() != 0)
        return fail("GC compatibility controls did not round-trip");

    runtime.detachCurrentThread();
    runtime.shutdown();
    std::cout << "hxcpp public compatibility test passed\n";
    return 0;
}

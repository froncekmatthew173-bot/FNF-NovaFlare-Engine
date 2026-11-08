#include <hxcpp.h>

#include <iostream>

namespace {

struct Key final : ::hx::Object {
    int number = 0;
};

const ::hx::gc::TypeDescriptor KeyDescriptor{
    0x5745414b4d41504bULL,
    "test.WeakMapKey",
    nullptr,
    sizeof(Key),
    alignof(Key),
    0,
    ::hx::gc::TypeNone,
    0,
    nullptr,
    nullptr,
    nullptr,
};

int fail(const char* message) {
    std::cerr << "weak-map failure: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    auto& runtime = ::hx::gc::Runtime::instance();
    runtime.initialize();
    runtime.attachCurrentThread();

    ::Dynamic map = __hxcpp_weak_map_create();
    auto keyObject = ::hx::allocate<Key>(KeyDescriptor);
    keyObject->number = 7;
    ::Dynamic key(keyObject);
    ::hx::gc::RefSlot* roots[]{&map.mPtr, &key.mPtr};
    {
        ::hx::gc::RootScope rootScope(roots, 2, 0x574d);
        __hxcpp_weak_map_set(map, key, ::Dynamic(42));
        if (!__hxcpp_weak_map_exists(map, key) ||
            __hxcpp_weak_map_get(map, key).Cast<int>() != 42)
            return fail("live key did not resolve its ephemeron value");

        (void)runtime.collectBootstrap();
        if (!__hxcpp_weak_map_exists(map, key) ||
            __hxcpp_weak_map_keys(map)->length != 1)
            return fail("rooted key was lost during collection");

        key = nullptr;
        (void)runtime.collectBootstrap();
        if (__hxcpp_weak_map_keys(map)->length != 0 ||
            __hxcpp_weak_map_values(map)->length != 0)
            return fail("dead weak key retained its entry");
    }

    map = nullptr;
    (void)runtime.collectBootstrap();
    const auto finalized = runtime.drainFinalizers();
    if (finalized.completed != 1 || finalized.failed != 0)
        return fail("weak map storage finalizer did not run exactly once");
    (void)runtime.collectBootstrap();

    runtime.detachCurrentThread();
    runtime.shutdown();
    std::cout << "hxcpp exact ephemeron WeakMap test passed\n";
    return 0;
}

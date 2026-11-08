#include <hxcpp.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace {

struct WeakValue final : ::hx::Object {
    ::Dynamic value;
};

void traceWeakValue(void* object, std::size_t,
                    ::hx::gc::TraceVisitor& visitor) {
    visitor.visit(static_cast<WeakValue*>(object)->value.mPtr);
}

const ::hx::gc::TypeDescriptor WeakValueDescriptor{
    0x5745414b56414c55ULL,
    "NovaGC.WeakValue",
    nullptr,
    sizeof(WeakValue),
    alignof(WeakValue),
    0,
    ::hx::gc::TypeContainsReferences,
    0,
    nullptr,
    &traceWeakValue,
    nullptr,
};

struct NativeWeakMap final {
    std::mutex mutex;
    std::vector<::hx::gc::EphemeronHandle> entries;
};

struct WeakMapShell final : ::hx::Object {
    NativeWeakMap* native = nullptr;
};

void releaseAll(NativeWeakMap& map) noexcept {
    auto& runtime = ::hx::gc::Runtime::instance();
    for (const auto handle : map.entries) {
        try {
            runtime.releaseEphemeron(handle);
        } catch (...) {
        }
    }
    map.entries.clear();
}

void finalizeWeakMap(void* object) {
    auto* shell = static_cast<WeakMapShell*>(object);
    if (shell->native == nullptr)
        return;
    {
        std::scoped_lock guard(shell->native->mutex);
        releaseAll(*shell->native);
    }
    delete shell->native;
    shell->native = nullptr;
}

const ::hx::gc::TypeDescriptor WeakMapShellDescriptor{
    0x5745414b4d415053ULL,
    "NovaGC.WeakMapStorage",
    nullptr,
    sizeof(WeakMapShell),
    alignof(WeakMapShell),
    0,
    ::hx::gc::TypeFinalizable,
    0,
    nullptr,
    nullptr,
    &finalizeWeakMap,
};

WeakMapShell* shellFrom(const ::Dynamic& map) {
    if (map.tag() != ::Dynamic::Tag::Object)
        throw std::runtime_error("invalid NovaGC weak map storage");
    auto* shell = static_cast<WeakMapShell*>(map.managedPointer());
    if (shell == nullptr ||
        ::hx::gc::Runtime::instance().typeOf(shell) !=
            &WeakMapShellDescriptor || shell->native == nullptr)
        throw std::runtime_error("closed NovaGC weak map storage");
    return shell;
}

void* objectKey(const ::Dynamic& key) {
    if (key.tag() != ::Dynamic::Tag::Object || key.managedPointer() == nullptr)
        throw std::invalid_argument("WeakMap keys must be non-null objects");
    return key.managedPointer();
}

void storeValue(WeakValue* owner, const ::Dynamic& value) {
    ::hx::gc::Runtime::instance().storeBarrier(
        owner, owner->value.mPtr, value.managedPointer());
    owner->value.copyNonReferenceFrom(value);
}

void pruneDead(NativeWeakMap& map) {
    auto& runtime = ::hx::gc::Runtime::instance();
    auto iterator = map.entries.begin();
    while (iterator != map.entries.end()) {
        if (runtime.resolveEphemeronKey(*iterator) == nullptr) {
            runtime.releaseEphemeron(*iterator);
            iterator = map.entries.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

auto findKey(NativeWeakMap& map, void* key) {
    auto& runtime = ::hx::gc::Runtime::instance();
    const std::uint64_t identity = runtime.identityOf(key);
    return std::find_if(map.entries.begin(), map.entries.end(),
        [&](const ::hx::gc::EphemeronHandle handle) {
            void* candidate = runtime.resolveEphemeronKey(handle);
            return candidate != nullptr &&
                   runtime.identityOf(candidate) == identity;
        });
}

} // namespace

::Dynamic __hxcpp_weak_map_create() {
    auto shell = ::hx::allocate<WeakMapShell>(WeakMapShellDescriptor);
    shell->native = new NativeWeakMap();
    return ::Dynamic(shell);
}

void __hxcpp_weak_map_set(::Dynamic map, ::Dynamic key, ::Dynamic value) {
    ::hx::ScopedPin shellPin(shellFrom(map));
    auto* shell = static_cast<WeakMapShell*>(shellPin.get());
    void* keyObject = objectKey(key);
    std::scoped_lock guard(shell->native->mutex);
    pruneDead(*shell->native);
    auto existing = findKey(*shell->native, keyObject);
    auto& runtime = ::hx::gc::Runtime::instance();
    if (existing != shell->native->entries.end()) {
        auto* entry = static_cast<WeakValue*>(
            runtime.resolveEphemeronValue(*existing));
        if (entry != nullptr)
            storeValue(entry, value);
        return;
    }

    auto entry = ::hx::allocate<WeakValue>(WeakValueDescriptor);
    ::hx::gc::RefSlot* roots[]{&entry.mPtr};
    ::hx::gc::RootScope rootScope(roots, 1);
    storeValue(entry.get(), value);
    shell->native->entries.push_back(
        runtime.createEphemeron(keyObject, entry.get()));
}

::Dynamic __hxcpp_weak_map_get(::Dynamic map, ::Dynamic key) {
    ::hx::ScopedPin shellPin(shellFrom(map));
    auto* shell = static_cast<WeakMapShell*>(shellPin.get());
    void* keyObject = objectKey(key);
    std::scoped_lock guard(shell->native->mutex);
    pruneDead(*shell->native);
    auto found = findKey(*shell->native, keyObject);
    if (found == shell->native->entries.end())
        return nullptr;
    auto* entry = static_cast<WeakValue*>(
        ::hx::gc::Runtime::instance().resolveEphemeronValue(*found));
    if (entry == nullptr)
        return nullptr;
    ::hx::ScopedPin pin(entry);
    return static_cast<WeakValue*>(pin.get())->value;
}

bool __hxcpp_weak_map_exists(::Dynamic map, ::Dynamic key) {
    ::hx::ScopedPin shellPin(shellFrom(map));
    auto* shell = static_cast<WeakMapShell*>(shellPin.get());
    void* keyObject = objectKey(key);
    std::scoped_lock guard(shell->native->mutex);
    pruneDead(*shell->native);
    return findKey(*shell->native, keyObject) != shell->native->entries.end();
}

bool __hxcpp_weak_map_remove(::Dynamic map, ::Dynamic key) {
    ::hx::ScopedPin shellPin(shellFrom(map));
    auto* shell = static_cast<WeakMapShell*>(shellPin.get());
    void* keyObject = objectKey(key);
    std::scoped_lock guard(shell->native->mutex);
    pruneDead(*shell->native);
    auto found = findKey(*shell->native, keyObject);
    if (found == shell->native->entries.end())
        return false;
    ::hx::gc::Runtime::instance().releaseEphemeron(*found);
    shell->native->entries.erase(found);
    return true;
}

::Array<::Dynamic> __hxcpp_weak_map_keys(::Dynamic map) {
    ::hx::ScopedPin shellPin(shellFrom(map));
    auto* shell = static_cast<WeakMapShell*>(shellPin.get());
    auto result = ::Array_obj<::Dynamic>::__new();
    ::hx::gc::RefSlot* roots[]{&result.mPtr};
    ::hx::gc::RootScope rootScope(roots, 1);
    std::scoped_lock guard(shell->native->mutex);
    pruneDead(*shell->native);
    auto& runtime = ::hx::gc::Runtime::instance();
    for (const auto handle : shell->native->entries) {
        auto* key = static_cast<::hx::Object*>(runtime.resolveEphemeronKey(handle));
        if (key != nullptr)
            result->push(::Dynamic(key));
    }
    return result;
}

::Array<::Dynamic> __hxcpp_weak_map_values(::Dynamic map) {
    ::hx::ScopedPin shellPin(shellFrom(map));
    auto* shell = static_cast<WeakMapShell*>(shellPin.get());
    auto result = ::Array_obj<::Dynamic>::__new();
    ::hx::gc::RefSlot* roots[]{&result.mPtr};
    ::hx::gc::RootScope rootScope(roots, 1);
    std::scoped_lock guard(shell->native->mutex);
    pruneDead(*shell->native);
    auto& runtime = ::hx::gc::Runtime::instance();
    for (const auto handle : shell->native->entries) {
        auto* entry = static_cast<WeakValue*>(
            runtime.resolveEphemeronValue(handle));
        if (entry != nullptr) {
            ::hx::ScopedPin pin(entry);
            result->push(static_cast<WeakValue*>(pin.get())->value);
        }
    }
    return result;
}

::String __hxcpp_weak_map_to_string(::Dynamic map) {
    ::hx::ScopedPin shellPin(shellFrom(map));
    auto* shell = static_cast<WeakMapShell*>(shellPin.get());
    std::scoped_lock guard(shell->native->mutex);
    pruneDead(*shell->native);
    return ::String("[WeakMap size=") +
        static_cast<int>(shell->native->entries.size()) + ::String("]");
}

void __hxcpp_weak_map_clear(::Dynamic map) {
    ::hx::ScopedPin shellPin(shellFrom(map));
    auto* shell = static_cast<WeakMapShell*>(shellPin.get());
    std::scoped_lock guard(shell->native->mutex);
    releaseAll(*shell->native);
}

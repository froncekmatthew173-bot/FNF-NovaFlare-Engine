#include <hxcpp.h>

#include <cstring>
#include <new>
#include <stdexcept>

namespace hx {
namespace {

class DynamicArrayIterator final : public ::hx::Object {
public:
    explicit DynamicArrayIterator(::hx::Object* input,
                                  bool inputKeyValue = false) noexcept
        : array(input), keyValue(inputKeyValue) {}

    ::hx::ObjectPtr<::hx::Object> array;
    int index = 0;
    bool keyValue = false;

    static const std::uint32_t referenceOffsets[1];
    static const char* const dynamicFieldNames[2];
    static const ::hx::gc::TypeDescriptor descriptor;
};

const std::uint32_t DynamicArrayIterator::referenceOffsets[1]{
    ::hx::fieldOffset(&DynamicArrayIterator::array),
};

const char* const DynamicArrayIterator::dynamicFieldNames[2]{
    "hasNext", "next",
};

[[nodiscard]] Dynamic iteratorHasNext(::hx::Object* object) {
    auto* self = static_cast<DynamicArrayIterator*>(object);
    ::hx::Object* array = self->array.get();
    if (array == nullptr)
        return Dynamic(false);
    return Dynamic(self->index < Dynamic(array).__length());
}

[[nodiscard]] Dynamic iteratorNext(::hx::Object* object) {
    auto* self = static_cast<DynamicArrayIterator*>(object);
    ::hx::Object* array = self->array.get();
    if (array == nullptr)
        throw std::runtime_error("dynamic Array iterator owner is null");
    const int length = Dynamic(array).__length();
    if (self->index >= length)
        return Dynamic();
    const int index = self->index++;
    ::hx::RootedValue<::Dynamic> value(Dynamic(array).__GetItem(index));
    if (!self->keyValue)
        return value.get();

    auto result = ::hx::Anon_obj::Create(2);
    ::hx::gc::RefSlot* roots[]{&result.mPtr};
    ::hx::gc::RootScope rootScope(roots, 1);
    result->setFixed(0, ::String("key"), ::Dynamic(index));
    result->setFixed(1, ::String("value"), value.get());
    return ::Dynamic(result);
}

bool iteratorFieldGet(void* object, const char* name, int access,
                      void* output) {
    (void)access;
    auto* self = static_cast<DynamicArrayIterator*>(object);
    auto& result = *static_cast<Dynamic*>(output);
    if (std::strcmp(name, "hasNext") == 0) {
        result = ::hx::CreateMemberFunction0(
            "hasNext", self, &iteratorHasNext);
        return true;
    }
    if (std::strcmp(name, "next") == 0) {
        result = ::hx::CreateMemberFunction0("next", self, &iteratorNext);
        return true;
    }
    return false;
}

const ::hx::gc::TypeDescriptor DynamicArrayIterator::descriptor{
    0x44594e4152524954ULL,
    "DynamicArrayIterator",
    nullptr,
    sizeof(DynamicArrayIterator),
    alignof(DynamicArrayIterator),
    0,
    ::hx::gc::TypeContainsReferences,
    1,
    DynamicArrayIterator::referenceOffsets,
    nullptr,
    nullptr,
    &iteratorFieldGet,
    nullptr,
    nullptr,
    nullptr,
    2,
    DynamicArrayIterator::dynamicFieldNames,
};

[[nodiscard]] Dynamic createArrayIterator(::hx::Object* array,
                                          bool keyValue) {
    auto& runtime = ::hx::gc::Runtime::instance();
    void* memory = runtime.allocate(DynamicArrayIterator::descriptor);
    auto* iterator = ::new (memory) DynamicArrayIterator(array, keyValue);
    runtime.publish(iterator);
    return Dynamic(static_cast<::hx::Object*>(iterator));
}

[[nodiscard]] Dynamic createArrayValueIterator(::hx::Object* array) {
    return createArrayIterator(array, false);
}

[[nodiscard]] Dynamic createArrayKeyValueIterator(::hx::Object* array) {
    return createArrayIterator(array, true);
}

} // namespace

Dynamic makeArrayIteratorFactory(::hx::Object* array) {
    if (array == nullptr)
        return Dynamic();
    return ::hx::CreateMemberFunction0("iterator", array,
                                       &createArrayValueIterator);
}

Dynamic makeArrayKeyValueIteratorFactory(::hx::Object* array) {
    if (array == nullptr)
        return Dynamic();
    return ::hx::CreateMemberFunction0("keyValueIterator", array,
                                       &createArrayKeyValueIterator);
}

Dynamic makeArrayIterator(::hx::Object* array) {
    if (array == nullptr)
        return Dynamic();
    return createArrayValueIterator(array);
}

Dynamic makeArrayKeyValueIterator(::hx::Object* array) {
    if (array == nullptr)
        return Dynamic();
    return createArrayKeyValueIterator(array);
}

Dynamic virtualArrayGet(::hx::Object* array, int index) {
    if (array == nullptr)
        return Dynamic();
    ::hx::ScopedPin pin(array);
    array = static_cast<::hx::Object*>(pin.get());
    const auto* type = ::hx::gc::Runtime::instance().typeOf(array);
    if (type == nullptr || type->dynamicIndexGet == nullptr)
        throw std::runtime_error("cpp.VirtualArray target is not indexable");
    Dynamic output;
    if (!type->dynamicIndexGet(array, index, &output))
        return Dynamic();
    return output;
}

Dynamic virtualArraySet(::hx::Object* array, int index,
                        const Dynamic& value) {
    if (array == nullptr)
        throw std::runtime_error("write through null cpp.VirtualArray");
    ::hx::ScopedPin pin(array);
    array = static_cast<::hx::Object*>(pin.get());
    const auto* type = ::hx::gc::Runtime::instance().typeOf(array);
    if (type == nullptr || type->dynamicIndexSet == nullptr ||
        !type->dynamicIndexSet(array, index, &value))
        throw std::runtime_error("cpp.VirtualArray target is not writable");
    return value;
}

int virtualArrayPush(::hx::Object* array, const Dynamic& value) {
    if (array == nullptr)
        throw std::runtime_error("push through null cpp.VirtualArray");
    ::hx::ScopedPin pin(array);
    array = static_cast<::hx::Object*>(pin.get());
    const auto* type = ::hx::gc::Runtime::instance().typeOf(array);
    if (type == nullptr || type->dynamicFieldGet == nullptr ||
        type->dynamicIndexSet == nullptr)
        throw std::runtime_error("cpp.VirtualArray target is not writable");
    Dynamic length;
    if (!type->dynamicFieldGet(array, "length", ::hx::paccDynamic,
                               &length))
        throw std::runtime_error("cpp.VirtualArray has no length");
    const int index = length.Cast<int>();
    if (!type->dynamicIndexSet(array, index, &value))
        throw std::runtime_error("cpp.VirtualArray push failed");
    return index + 1;
}

} // namespace hx

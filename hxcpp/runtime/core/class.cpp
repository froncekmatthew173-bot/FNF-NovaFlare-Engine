#include <hxcpp.h>

#include <mutex>
#include <string>
#include <vector>

namespace hx {
namespace {

struct RegistryEntry final {
    const ::hx::gc::TypeDescriptor* represented;
    std::string name;
    ::hx::gc::Handle handle;
};

using GeneratedEnumCreate = Dynamic (*)(String, ::cpp::VirtualArray);

struct GeneratedEnumEntry final {
    const ::hx::gc::TypeDescriptor* represented;
    GeneratedEnumCreate create;
    std::vector<std::string> constructs;
};

std::mutex registryMutex;
std::vector<RegistryEntry> registry;
std::mutex generatedEnumMutex;
std::vector<GeneratedEnumEntry> generatedEnums;

void appendUnique(std::vector<const char*>& output, const char* name) {
    if (name == nullptr)
        return;
    for (const char* existing : output) {
        if (std::string_view(existing) == name)
            return;
    }
    output.push_back(name);
}

void collectInstanceMembers(const ::hx::gc::TypeDescriptor* type,
                            std::vector<const char*>& output) {
    if (type == nullptr)
        return;
    collectInstanceMembers(type->super, output);
    const char* const* names = type->instanceMemberNames;
    std::uint32_t count = type->instanceMemberCount;
    // Hand-written runtime descriptors predate the generated member list.
    // Their dynamic data-field list is still a useful conservative fallback.
    if (names == nullptr) {
        names = type->dynamicFieldNames;
        count = type->dynamicFieldCount;
    }
    for (std::uint32_t index = 0; index < count; ++index)
        appendUnique(output, names[index]);
}

::Array<String> makeFieldArray(const std::vector<const char*>& names) {
    auto result = ::Array_obj<String>::__new();
    ::hx::gc::RefSlot* roots[]{&result.mPtr};
    ::hx::gc::RootScope rootScope(roots, 1);
    for (const char* name : names) {
        ::hx::RootedValue<::String> rootedName{::String(name)};
        result->push(rootedName.get());
    }
    return result;
}

::Array<String> makeOwnedFieldArray(const std::vector<std::string>& names) {
    auto result = ::Array_obj<String>::__new();
    ::hx::gc::RefSlot* roots[]{&result.mPtr};
    ::hx::gc::RootScope rootScope(roots, 1);
    for (const auto& name : names) {
        ::hx::RootedValue<::String> rootedName{::String(name)};
        result->push(rootedName.get());
    }
    return result;
}

} // namespace

namespace {

bool classDynamicGet(void* object, const char* name, int access,
                     void* output) {
    (void)access;
    auto* self = static_cast<Class_obj*>(object);
    auto& result = *static_cast<Dynamic*>(output);
    const std::string_view requested(name == nullptr ? "" : name);
    if (requested == "__meta__") {
        result = self->__meta__;
        return true;
    }
    if (requested == "__rtti") {
        result = Dynamic(self->__rtti__);
        return true;
    }
    const auto* represented = self->represented;
    if (represented != nullptr && represented->staticFieldGet != nullptr)
        return represented->staticFieldGet(name, access, output);
    return false;
}

bool classDynamicSet(void* object, const char* name, int access,
                     const void* input) {
    auto* self = static_cast<Class_obj*>(object);
    const auto* represented = self->represented;
    return represented != nullptr && represented->staticFieldSet != nullptr &&
           represented->staticFieldSet(name, access, input);
}

} // namespace

const std::uint32_t Class_obj::referenceOffsets[3]{
    ::hx::fieldOffset(&Class_obj::mName),
    ::hx::fieldOffset(&Class_obj::__meta__) +
        static_cast<std::uint32_t>(offsetof(Dynamic, mPtr)),
    ::hx::fieldOffset(&Class_obj::__rtti__) +
        static_cast<std::uint32_t>(offsetof(String, mPtr)),
};

const ::hx::gc::TypeDescriptor Class_obj::descriptor{
    0x434c4153534e4f56ULL,
    "Class",
    nullptr,
    sizeof(Class_obj),
    alignof(Class_obj),
    0,
    ::hx::gc::TypeContainsReferences | ::hx::gc::TypeDynamicObject,
    3,
    Class_obj::referenceOffsets,
    nullptr,
    nullptr,
    &classDynamicGet,
    &classDynamicSet,
};

Class Class_obj::Create(const ::hx::gc::TypeDescriptor* represented,
                        CreateEmptyFunction createEmpty,
                        ScalarClassKind scalarKind,
                        const char* name, int interfaceHash,
                        CreateArgsFunction createArgs,
                        const char* const* interfaceMemberNames,
                        std::uint32_t interfaceMemberCount) {
    auto& runtime = ::hx::gc::Runtime::instance();
    auto result = ::hx::allocate<Class_obj>(descriptor);
    ::hx::gc::RefSlot* roots[]{&result.mPtr};
    ::hx::gc::RootScope rootScope(roots, 1);
    {
        ::hx::ScopedPin pin(result.get());
        auto* self = static_cast<Class_obj*>(pin.get());
        self->represented = represented;
        self->createEmpty = createEmpty;
        self->createArgs = createArgs;
        self->scalarKind = scalarKind;
        self->interfaceHash = interfaceHash;
        self->interfaceMemberNames = interfaceMemberNames;
        self->interfaceMemberCount = interfaceMemberCount;
        self->mName = String(name == nullptr ? "Object" : name);
    }
    const auto handle = runtime.createHandle(result.get(),
                                              ::hx::gc::HandleKind::Strong);
    std::scoped_lock lock(registryMutex);
    registry.push_back({represented, name == nullptr ? "Object" : name,
                        handle});
    return result;
}

Class Class_obj::ForDescriptor(
        const ::hx::gc::TypeDescriptor* represented) {
    if (represented == nullptr)
        return nullptr;
    {
        std::scoped_lock lock(registryMutex);
        for (const auto& entry : registry) {
            if (entry.represented == represented)
                return Class(static_cast<Class_obj*>(
                    ::hx::gc::Runtime::instance().resolveHandle(entry.handle)));
        }
    }
    // Original hxcpp exposes all anonymous/dynamic records through the
    // synthetic "__Anon" class.  The generated Type.getClass implementation
    // detects that name and returns null, as required by Haxe's TObject
    // semantics.  Class_obj itself remains the real "Class" class.
    const bool anonymous = represented != &Class_obj::descriptor &&
        (represented->flags & ::hx::gc::TypeDynamicObject) != 0;
    return Create(represented, nullptr, ScalarClassKind::None,
                  anonymous ? "__Anon" : represented->name);
}

Class Class_obj::Resolve(const String& name) {
    const std::string requested(name.view());
    std::scoped_lock lock(registryMutex);
    for (const auto& entry : registry) {
        if (entry.name == requested)
            return Class(static_cast<Class_obj*>(
                ::hx::gc::Runtime::instance().resolveHandle(entry.handle)));
    }
    return nullptr;
}

Dynamic Class_obj::ConstructEmpty() {
    // String is the one scalar class that original hxcpp exposes as a real
    // reflected constructor.  Type.createEmptyInstance(String) returns the
    // empty string even though scalar values do not have a managed-object
    // allocation factory.
    if (scalarKind == ScalarClassKind::String)
        return Dynamic(String(""));
    if (createEmpty == nullptr)
        throw std::runtime_error("class cannot create an empty instance");
    return createEmpty();
}

Dynamic Class_obj::ConstructArgs(::cpp::VirtualArray arguments) {
    // Match hxcpp's CreateString: the first argument is converted through
    // Haxe toString(), while an omitted argument produces the null/default
    // String value.  This is used directly by Type.createInstance(String,...).
    if (scalarKind == ScalarClassKind::String) {
        if (arguments && arguments->length > 0)
            return Dynamic(arguments->__get(0).toString());
        return Dynamic(String());
    }
    if (createArgs != nullptr) {
        if (!arguments)
            arguments = ::cpp::VirtualArray_obj::__new();
        return createArgs(arguments);
    }
    if (!arguments || arguments->length == 0)
        return ConstructEmpty();
    throw std::runtime_error("dynamic constructor arguments are not registered");
}

Dynamic Class_obj::ConstructEnum(const String& name,
                                 ::cpp::VirtualArray arguments) {
    GeneratedEnumCreate create = nullptr;
    {
        std::scoped_lock lock(generatedEnumMutex);
        for (const auto& entry : generatedEnums) {
            if (entry.represented == represented) {
                create = entry.create;
                break;
            }
        }
    }
    if (create == nullptr)
        throw std::runtime_error("class is not a dynamically registered enum: " +
                                 std::string(mName.view()));
    return create(name, arguments);
}

Class Class_obj::GetSuper() {
    return represented == nullptr ? nullptr : ForDescriptor(represented->super);
}

::Array<String> Class_obj::GetInstanceFields() {
    ::hx::ScopedPin pin(this);
    const auto* self = static_cast<Class_obj*>(pin.get());
    std::vector<const char*> names;
    if (self->represented != nullptr) {
        collectInstanceMembers(self->represented, names);
    } else {
        for (std::uint32_t index = 0;
             index < self->interfaceMemberCount; ++index)
            appendUnique(names, self->interfaceMemberNames[index]);
    }
    return makeFieldArray(names);
}

::Array<String> Class_obj::GetClassFields() {
    ::hx::ScopedPin pin(this);
    const auto* self = static_cast<Class_obj*>(pin.get());
    if (self->represented != nullptr &&
        (self->represented->flags & ::hx::gc::TypeEnum) != 0) {
        std::vector<std::string> constructs;
        {
            std::scoped_lock lock(generatedEnumMutex);
            for (const auto& entry : generatedEnums) {
                if (entry.represented == self->represented) {
                    constructs = entry.constructs;
                    break;
                }
            }
        }
        return makeOwnedFieldArray(constructs);
    }
    std::vector<const char*> names;
    if (self->represented != nullptr) {
        for (std::uint32_t index = 0;
             index < self->represented->staticFieldCount; ++index) {
            appendUnique(names, self->represented->staticFieldNames[index]);
        }
    }
    if (!self->__rtti__.isNull())
        appendUnique(names, "__rtti");
    return makeFieldArray(names);
}

bool Class_obj::__IsEnum() const noexcept {
    return represented != nullptr &&
           (represented->flags & ::hx::gc::TypeEnum) != 0;
}

void RegisterGeneratedEnum(
        const ::hx::gc::TypeDescriptor* represented,
        const char* name,
        Dynamic (*create)(String, ::cpp::VirtualArray),
        const char* const* constructs,
        std::size_t constructCount) {
    if (represented == nullptr || create == nullptr)
        throw std::invalid_argument("invalid generated enum registration");
    {
        std::scoped_lock lock(generatedEnumMutex);
        for (const auto& entry : generatedEnums) {
            if (entry.represented == represented)
                return;
        }
        GeneratedEnumEntry entry{represented, create, {}};
        entry.constructs.reserve(constructCount);
        for (std::size_t index = 0; index < constructCount; ++index)
            entry.constructs.emplace_back(constructs[index]);
        generatedEnums.push_back(std::move(entry));
    }
    Class_obj::Create(represented, nullptr, ScalarClassKind::None,
                      name == nullptr ? "HaxeEnum" : name);
}

} // namespace hx

::Array<::String> __hxcpp_get_class_list() {
    auto result = ::Array_obj<::String>::__new();
    ::hx::gc::RefSlot* roots[]{&result.mPtr};
    ::hx::gc::RootScope rootScope(roots, 1);
    std::scoped_lock lock(::hx::registryMutex);
    for (const auto& entry : ::hx::registry)
        result->push(::String(entry.name));
    return result;
}

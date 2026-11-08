#pragma once

#include <cstdint>
#include <type_traits>

namespace hx {

enum class ScalarClassKind : std::uint8_t {
    None,
    Integer,
    Floating,
    Boolean,
    String,
    Dynamic,
    Int64,
    Array,
    EnumClass,
};

class Class_obj final : public Object {
public:
    using CreateEmptyFunction = Dynamic (*)();
    using CreateArgsFunction = Dynamic (*)(::cpp::VirtualArray);

    const ::hx::gc::TypeDescriptor* represented = nullptr;
    CreateEmptyFunction createEmpty = nullptr;
    CreateArgsFunction createArgs = nullptr;
    ScalarClassKind scalarKind = ScalarClassKind::None;
    int interfaceHash = 0;
    const char* const* interfaceMemberNames = nullptr;
    std::uint32_t interfaceMemberCount = 0;
    String mName;
    Dynamic __meta__;
    String __rtti__;

    static const std::uint32_t referenceOffsets[3];
    static const ::hx::gc::TypeDescriptor descriptor;

    static Class Create(const ::hx::gc::TypeDescriptor* represented,
                        CreateEmptyFunction createEmpty,
                        ScalarClassKind scalarKind,
                        const char* name, int interfaceHash = 0,
                        CreateArgsFunction createArgs = nullptr,
                        const char* const* interfaceMemberNames = nullptr,
                        std::uint32_t interfaceMemberCount = 0);
    static Class ForDescriptor(const ::hx::gc::TypeDescriptor* represented);
    static Class Resolve(const String& name);

    Dynamic ConstructEmpty();
    Dynamic ConstructArgs(::cpp::VirtualArray arguments);
    Dynamic ConstructEnum(const String& name,
                          ::cpp::VirtualArray arguments);
    Class GetSuper();
    ::Array<String> GetInstanceFields();
    ::Array<String> GetClassFields();
    bool __IsEnum() const noexcept;
    String __ToString() const { return mName; }
};

// All Array<T> descriptors share Haxe's single Array class identity.
// Declared here so Dynamic metadata can canonicalize a concrete typed-array
// descriptor before the inline definition near the end of this header.
Class ArrayClass();

namespace detail {

template<class Element>
Dynamic createEmptyManaged() {
    void* memory = ::hx::gc::Runtime::instance().allocate(Element::__novaType);
    auto* object = ::new (memory) Element();
    ::hx::gc::Runtime::instance().publish(object);
    return Dynamic(object);
}

template<class T>
Class makeClass() {
    if constexpr (std::is_same_v<T, int>)
        return Class_obj::Create(nullptr, nullptr, ScalarClassKind::Integer,
                                 "Int");
    else if constexpr (std::is_same_v<T, ::cpp::Int64>)
        return Class_obj::Create(nullptr, nullptr, ScalarClassKind::Int64,
                                 "cpp.Int64");
    else if constexpr (std::is_same_v<T, double> ||
                       std::is_same_v<T, float>)
        return Class_obj::Create(nullptr, nullptr, ScalarClassKind::Floating,
                                 "Float");
    else if constexpr (std::is_same_v<T, bool>)
        return Class_obj::Create(nullptr, nullptr, ScalarClassKind::Boolean,
                                 "Bool");
    else if constexpr (std::is_same_v<T, String>)
        return Class_obj::Create(nullptr, nullptr, ScalarClassKind::String,
                                 "String");
    else if constexpr (std::is_same_v<T, Dynamic>)
        return Class_obj::Create(nullptr, nullptr, ScalarClassKind::Dynamic,
                                 "Dynamic");
    else if constexpr (requires { typename T::element_type; }) {
        using Element = typename T::element_type;
        // Generated enum objects inherit EnumBase_obj::descriptor, so testing
        // for a generic descriptor first (or later) silently gives every enum
        // the reflected Class identity "EnumBase".  EnumDescriptor<Element>
        // is the unique generated identity registered by Enum_obj::__register.
        if constexpr (std::is_same_v<Element, ::hx::EnumBase_obj>)
            return Class_obj::Create(nullptr, nullptr,
                                     ScalarClassKind::EnumClass, "Enum");
        else if constexpr (std::is_base_of_v<::hx::EnumBase_obj, Element>)
            return Class_obj::ForDescriptor(&::hx::EnumDescriptor<Element>);
        else if constexpr (requires { Element::__novaType; }) {
            if constexpr (requires(::cpp::VirtualArray arguments) {
                              Element::__Create(arguments);
                          })
                return Class_obj::Create(&Element::__novaType,
                                         &createEmptyManaged<Element>,
                                         ScalarClassKind::None,
                                         Element::__novaType.name, 0,
                                         &Element::__Create);
            else
                return Class_obj::Create(&Element::__novaType,
                                         &createEmptyManaged<Element>,
                                         ScalarClassKind::None,
                                         Element::__novaType.name);
        }
        else if constexpr (requires { Element::descriptor; })
            return Class_obj::Create(&Element::descriptor, nullptr,
                                     ScalarClassKind::None,
                                     Element::descriptor.name);
        else if constexpr (requires { Element::__novaInterfaceHash; }) {
            const char* interfaceName = "Interface";
            if constexpr (requires { Element::__novaInterfaceName; })
                interfaceName = Element::__novaInterfaceName;
            const char* const* interfaceMembers = nullptr;
            std::uint32_t interfaceMemberCount = 0;
            if constexpr (requires {
                              Element::__novaInterfaceMemberNames;
                              Element::__novaInterfaceMemberCount;
                          }) {
                interfaceMembers = Element::__novaInterfaceMemberNames;
                interfaceMemberCount =
                    Element::__novaInterfaceMemberCount;
            }
            return Class_obj::Create(nullptr, nullptr,
                                     ScalarClassKind::None, interfaceName,
                                     Element::__novaInterfaceHash, nullptr,
                                     interfaceMembers, interfaceMemberCount);
        }
        else
            return Class_obj::Create(nullptr, nullptr,
                                     ScalarClassKind::None, "Native");
    } else {
        return Class_obj::Create(nullptr, nullptr, ScalarClassKind::None,
                                 "Native");
    }
}

} // namespace detail

template<class T>
Class ClassOf() {
    static Class value = detail::makeClass<T>();
    static const bool rooted = [] {
        ::hx::gc::Runtime::instance().registerStaticRoot(value.mPtr);
        return true;
    }();
    (void)rooted;
    if constexpr (requires { typename T::element_type; }) {
        using Element = typename T::element_type;
        if constexpr (requires { &Element::__meta__; }) {
            if constexpr (!std::is_member_object_pointer_v<
                              decltype(&Element::__meta__)>)
                value->__meta__ = Element::__meta__;
        }
        if constexpr (requires { &Element::__rtti; }) {
            if constexpr (!std::is_member_object_pointer_v<
                              decltype(&Element::__rtti)>)
                value->__rtti__ = Element::__rtti;
        }
    }
    return value;
}

template<class T>
struct BaseType { using type = T; };

template<class T>
struct BaseType<ObjectPtr<T>> { using type = T; };

template<class T>
constexpr int ClassSizeOf() {
    return static_cast<int>(sizeof(typename BaseType<T>::type));
}

} // namespace hx

inline ::hx::Class Dynamic::__GetClass() const {
    if (isNull())
        return nullptr;
    if (isMemberFunction())
        return nullptr;
    switch (tag_) {
        case Tag::Integer: return ::hx::ClassOf<int>();
        case Tag::Floating: return ::hx::ClassOf<double>();
        case Tag::Boolean: return ::hx::ClassOf<bool>();
        case Tag::String: return ::hx::ClassOf<String>();
        case Tag::Object: {
            const auto* type = ::hx::gc::Runtime::instance().typeOf(
                managedPointer());
            if (type != nullptr && (type->flags & ::hx::gc::TypeArray) != 0)
                return ::hx::ArrayClass();
            return ::hx::Class_obj::ForDescriptor(type);
        }
        default: return nullptr;
    }
}

inline int Dynamic::__GetType() const noexcept {
    if (isNull())
        return 0;
    if (isMemberFunction())
        return 6;
    switch (tag_) {
        case Tag::Integer: return 0xff;
        case Tag::Floating: return 1;
        case Tag::Boolean: return 2;
        case Tag::String: return 3;
        case Tag::Null: return 0;
        case Tag::Object: {
            auto* type = ::hx::gc::Runtime::instance().typeOf(managedPointer());
            if (type != nullptr && (type->flags & ::hx::gc::TypeFunction) != 0)
                return 6;
            if (type != nullptr && (type->flags & ::hx::gc::TypeEnum) != 0)
                return 7;
            if (type != nullptr && (type->flags & ::hx::gc::TypeArray) != 0)
                return 5;
            if (type != nullptr &&
                (type->flags & ::hx::gc::TypeDynamicObject) != 0)
                return 4;
            return 8;
        }
        default: return -1;
    }
}

inline bool Dynamic::__HasField(const String& name) const {
    if (tag_ != Tag::Object)
        return false;
    const std::string requested(name.view());
    Dynamic ignored;
    void* object = managedPointer();
    ::hx::ScopedPin pin(static_cast<::hx::Object*>(object));
    object = pin.get();
    for (const auto* type = ::hx::gc::Runtime::instance().typeOf(object);
         type != nullptr; type = type->super) {
        if (type->dynamicFieldGet != nullptr &&
            type->dynamicFieldGet(object, requested.c_str(),
                                  ::hx::paccNever, &ignored))
            return true;
    }
    return false;
}

inline void Dynamic::__GetFields(const ::Array<String>& output) const {
    if (tag_ != Tag::Object || !output)
        return;
    void* object = managedPointer();
    ::hx::ScopedPin pin(static_cast<::hx::Object*>(object));
    object = pin.get();
    for (const auto* type = ::hx::gc::Runtime::instance().typeOf(object);
         type != nullptr; type = type->super) {
        if (type->dynamicFieldEnumerate != nullptr) {
            type->dynamicFieldEnumerate(
                object, const_cast<::Array<String>*>(&output),
                [](void* context, const char* name, std::size_t length) {
                    auto* fields = static_cast<::Array<String>*>(context);
                    String field(std::string_view(name, length));
                    ::hx::gc::RefSlot* roots[]{&field.mPtr};
                    ::hx::gc::RootScope rootScope(roots, 1);
                    (*fields)->push(field);
                });
        }
        for (std::uint32_t index = 0; index < type->dynamicFieldCount; ++index)
            output->push(String(type->dynamicFieldNames[index]));
    }
}

inline bool __instanceof(const Dynamic& value, const Dynamic& type) {
    auto* classObject = dynamic_cast<::hx::Class_obj*>(
        static_cast<::hx::Object*>(type.managedPointer()));
    if (classObject == nullptr)
        return false;
    switch (classObject->scalarKind) {
        case ::hx::ScalarClassKind::Integer:
            if (value.tag() == Dynamic::Tag::Integer)
                return true;
            if (value.tag() != Dynamic::Tag::Floating)
                return false;
            {
                const double number = value.Cast<double>();
                return std::isfinite(number) &&
                       number >= static_cast<double>(
                           std::numeric_limits<int>::min()) &&
                       number <= static_cast<double>(
                           std::numeric_limits<int>::max()) &&
                       std::trunc(number) == number;
            }
        case ::hx::ScalarClassKind::Floating:
            return value.tag() == Dynamic::Tag::Floating ||
                   value.tag() == Dynamic::Tag::Integer;
        case ::hx::ScalarClassKind::Int64:
            return value.tag() == Dynamic::Tag::Integer;
        case ::hx::ScalarClassKind::Boolean:
            return value.tag() == Dynamic::Tag::Boolean;
        case ::hx::ScalarClassKind::String:
            return value.tag() == Dynamic::Tag::String;
        case ::hx::ScalarClassKind::Dynamic:
            return !value.isNull();
        case ::hx::ScalarClassKind::Array: {
            auto* descriptor = value.tag() == Dynamic::Tag::Object ?
                ::hx::gc::Runtime::instance().typeOf(value.managedPointer()) :
                nullptr;
            return descriptor != nullptr &&
                   std::string_view(descriptor->name) == "Array";
        }
        case ::hx::ScalarClassKind::EnumClass: {
            if (value.tag() != Dynamic::Tag::Object)
                return false;
            ::hx::ScopedPin pin(
                static_cast<::hx::Object*>(value.managedPointer()));
            auto* enumClass = dynamic_cast<::hx::Class_obj*>(pin.get());
            return enumClass != nullptr && enumClass->__IsEnum();
        }
        case ::hx::ScalarClassKind::None:
            if (classObject->interfaceHash != 0 &&
                value.tag() == Dynamic::Tag::Object) {
                auto* object = static_cast<::hx::Object*>(value.managedPointer());
                ::hx::ScopedPin pin(object);
                return pin.get()->_hx_getInterface(classObject->interfaceHash) !=
                       nullptr;
            }
            if (classObject->represented == &::hx::Class_obj::descriptor &&
                value.tag() == Dynamic::Tag::Object) {
                // Haxe has distinct metatypes for classes and enums.  Both are
                // represented by Class_obj internally, but `MyEnum is Class`
                // is false while `MyEnum is Enum` is true.
                ::hx::ScopedPin valuePin(
                    static_cast<::hx::Object*>(value.managedPointer()));
                auto* valueClass = dynamic_cast<::hx::Class_obj*>(
                    valuePin.get());
                return valueClass != nullptr && !valueClass->__IsEnum();
            }
            return value.tag() == Dynamic::Tag::Object &&
                classObject->represented != nullptr &&
                ::hx::gc::Runtime::instance().isInstanceOf(
                    value.managedPointer(), *classObject->represented);
    }
    return false;
}

namespace hx {

inline Class ArrayClass() {
    static Class value = Class_obj::Create(nullptr, nullptr,
        ScalarClassKind::Array, "Array");
    static const bool rooted = [] {
        ::hx::gc::Runtime::instance().registerStaticRoot(value.mPtr);
        return true;
    }();
    (void)rooted;
    return value;
}

struct ArrayBase final {
    struct ClassProxy final {
        operator ::hx::Class() const { return ArrayClass(); }
        operator ::Dynamic() const { return ::Dynamic(ArrayClass()); }
        friend bool operator==(const ::hx::Class& left, ClassProxy) {
            return left == ArrayClass();
        }
        friend bool operator==(ClassProxy, const ::hx::Class& right) {
            return ArrayClass() == right;
        }
        friend bool operator!=(const ::hx::Class& left, ClassProxy right) {
            return !(left == right);
        }
        friend bool operator!=(ClassProxy left, const ::hx::Class& right) {
            return !(left == right);
        }
    };
    static inline ClassProxy __mClass{};
};

} // namespace hx

inline int __int__(double value) noexcept { return static_cast<int>(value); }

inline Dynamic __hxcpp_parse_int(const String& input) {
    std::string text(input.view());
    const char* begin = text.c_str();
    while (*begin != '\0' &&
           std::isspace(static_cast<unsigned char>(*begin)) != 0)
        ++begin;
    const char* digits = begin;
    if (*digits == '+' || *digits == '-')
        ++digits;
    const int base = digits[0] == '0' &&
                     (digits[1] == 'x' || digits[1] == 'X')
        ? 16 : 10;
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(begin, &end, base);
    if (end == begin || errno == ERANGE)
        return Dynamic();
    return Dynamic(static_cast<int>(value));
}

inline double __hxcpp_parse_float(const String& input) {
    std::string text(input.view());
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str())
        return std::numeric_limits<double>::quiet_NaN();
    return value;
}

inline int __hxcpp_irand(int limit) {
    if (limit <= 0)
        return 0;
    thread_local std::mt19937 generator(std::random_device{}());
    return std::uniform_int_distribution<int>(0, limit - 1)(generator);
}

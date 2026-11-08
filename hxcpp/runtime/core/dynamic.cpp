#include <hxcpp.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace {

thread_local unsigned anonymousStringDepth = 0;

class AnonymousStringDepthScope final {
public:
    AnonymousStringDepthScope() noexcept { ++anonymousStringDepth; }
    ~AnonymousStringDepthScope() { --anonymousStringDepth; }
};

enum class StringMethod : std::uint8_t {
    CharAt,
    CharCodeAt,
    IndexOf,
    LastIndexOf,
    Split,
    Substr,
    Substring,
    ToLowerCase,
    ToString,
    ToUpperCase,
};

class StringMethodFunction final : public ::hx::LocalFunc {
public:
    StringMethodFunction(const ::String& receiver, StringMethod method)
        : receiver_(receiver), method_(method) {
        ::hx::gc::Runtime::instance().publish(this);
    }

    int argCount() const noexcept override {
        switch (method_) {
            case StringMethod::ToLowerCase:
            case StringMethod::ToString:
            case StringMethod::ToUpperCase:
                return 0;
            case StringMethod::CharAt:
            case StringMethod::CharCodeAt:
            case StringMethod::Split:
                return 1;
            case StringMethod::IndexOf:
            case StringMethod::LastIndexOf:
            case StringMethod::Substr:
            case StringMethod::Substring:
                return 2;
        }
        return -1;
    }

    bool sameClosure(const ::hx::LocalFunc& other) const override {
        const auto* method = dynamic_cast<const StringMethodFunction*>(&other);
        return method != nullptr && method_ == method->method_ &&
               receiver_ == method->receiver_;
    }

    ::Dynamic invokeMany(const ::Dynamic* arguments, int count) override {
        const auto requireCount = [count](int minimum, int maximum) {
            if (count < minimum || count > maximum)
                throw std::runtime_error("Haxe closure arity mismatch");
        };
        switch (method_) {
            case StringMethod::CharAt:
                requireCount(1, 1);
                return receiver_.charAt(arguments[0].Cast<int>());
            case StringMethod::CharCodeAt: {
                requireCount(1, 1);
                const int index = arguments[0].Cast<int>();
                if (index < 0 || index >= receiver_.length)
                    return ::Dynamic();
                return receiver_.charCodeAt(index);
            }
            case StringMethod::IndexOf: {
                requireCount(1, 2);
                ::hx::Null<int> start = nullptr;
                if (count == 2 && !arguments[1].isNull())
                    start = arguments[1].Cast<int>();
                return receiver_.indexOf(arguments[0].Cast<::String>(), start);
            }
            case StringMethod::LastIndexOf: {
                requireCount(1, 2);
                ::hx::Null<int> start = nullptr;
                if (count == 2 && !arguments[1].isNull())
                    start = arguments[1].Cast<int>();
                return receiver_.lastIndexOf(arguments[0].Cast<::String>(),
                                             start);
            }
            case StringMethod::Split:
                requireCount(1, 1);
                return receiver_.split(arguments[0].Cast<::String>());
            case StringMethod::Substr: {
                requireCount(1, 2);
                ::hx::Null<int> length = nullptr;
                if (count == 2 && !arguments[1].isNull())
                    length = arguments[1].Cast<int>();
                return receiver_.substr(arguments[0].Cast<int>(), length);
            }
            case StringMethod::Substring: {
                requireCount(1, 2);
                ::hx::Null<int> end = nullptr;
                if (count == 2 && !arguments[1].isNull())
                    end = arguments[1].Cast<int>();
                return receiver_.substring(arguments[0].Cast<int>(), end);
            }
            case StringMethod::ToLowerCase:
                requireCount(0, 0);
                return receiver_.toLowerCase();
            case StringMethod::ToString:
                requireCount(0, 0);
                return receiver_;
            case StringMethod::ToUpperCase:
                requireCount(0, 0);
                return receiver_.toUpperCase();
        }
        throw std::logic_error("unknown String dynamic method");
    }

    static void trace(void* object, std::size_t,
                      ::hx::gc::TraceVisitor& visitor) {
        auto* self = static_cast<StringMethodFunction*>(object);
        visitor.visit(self->receiver_.mPtr);
    }

    static const ::hx::gc::TypeDescriptor descriptor;

    static void* operator new(std::size_t size) {
        if (size != sizeof(StringMethodFunction))
            throw std::bad_alloc();
        return ::hx::gc::Runtime::instance().allocate(descriptor);
    }
    static void operator delete(void*) noexcept {}
    static void operator delete(void*, std::size_t) noexcept {}

private:
    ::String receiver_;
    StringMethod method_;
};

const ::hx::gc::TypeDescriptor StringMethodFunction::descriptor{
    0x5354524d45544844ULL,
    "StringMethodFunction",
    nullptr,
    sizeof(StringMethodFunction),
    alignof(StringMethodFunction),
    0,
    ::hx::gc::TypeContainsReferences | ::hx::gc::TypeFunction,
    0,
    nullptr,
    &StringMethodFunction::trace,
    nullptr,
};

[[nodiscard]] ::Dynamic stringDynamicField(const ::Dynamic& value,
                                           std::string_view name) {
    const ::String receiver = value.Cast<::String>();
    if (name == "length")
        return receiver.length;

    StringMethod method;
    if (name == "charAt") method = StringMethod::CharAt;
    else if (name == "charCodeAt") method = StringMethod::CharCodeAt;
    else if (name == "indexOf") method = StringMethod::IndexOf;
    else if (name == "lastIndexOf") method = StringMethod::LastIndexOf;
    else if (name == "split") method = StringMethod::Split;
    else if (name == "substr") method = StringMethod::Substr;
    else if (name == "substring") method = StringMethod::Substring;
    else if (name == "toLowerCase") method = StringMethod::ToLowerCase;
    else if (name == "toString") method = StringMethod::ToString;
    else if (name == "toUpperCase") method = StringMethod::ToUpperCase;
    else return ::Dynamic();
    return ::Dynamic(new StringMethodFunction(receiver, method));
}

} // namespace

namespace {
::Dynamic stringFromCharCode(const ::Dynamic& code) {
    return ::String::fromCharCode(code.Cast<int>());
}
} // namespace

::Dynamic String::fromCharCode_dyn() {
    return ::hx::CreateStaticFunction1("fromCharCode", &stringFromCharCode);
}

::Dynamic String::charAt_dyn() const {
    return stringDynamicField(::Dynamic(*this), "charAt");
}
::Dynamic String::charCodeAt_dyn() const {
    return stringDynamicField(::Dynamic(*this), "charCodeAt");
}
::Dynamic String::indexOf_dyn() const {
    return stringDynamicField(::Dynamic(*this), "indexOf");
}
::Dynamic String::lastIndexOf_dyn() const {
    return stringDynamicField(::Dynamic(*this), "lastIndexOf");
}
::Dynamic String::split_dyn() const {
    return stringDynamicField(::Dynamic(*this), "split");
}
::Dynamic String::substr_dyn() const {
    return stringDynamicField(::Dynamic(*this), "substr");
}
::Dynamic String::substring_dyn() const {
    return stringDynamicField(::Dynamic(*this), "substring");
}
::Dynamic String::toLowerCase_dyn() const {
    return stringDynamicField(::Dynamic(*this), "toLowerCase");
}
::Dynamic String::toString_dyn() const {
    return stringDynamicField(::Dynamic(*this), "toString");
}
::Dynamic String::toUpperCase_dyn() const {
    return stringDynamicField(::Dynamic(*this), "toUpperCase");
}

::Array<::Dynamic> hx::emptyEnumArguments() {
    static ::Array<::Dynamic> value = ::Array_obj<::Dynamic>::__new();
    static const bool rooted = [] {
        ::hx::gc::Runtime::instance().registerStaticRoot(value.mPtr);
        return true;
    }();
    (void)rooted;
    return value;
}

[[noreturn]] void hx::throwNullObjectReference() {
    // Route synthesized runtime faults through the same eager stack snapshot
    // as generated HX_STACK_DO_THROW.  Throwing Dynamic directly leaves the
    // previous caught exception's stack behind, which can make an unrelated
    // null dereference look as if it came from the last Haxe throw site.
    ::hx::Throw(::Dynamic(::String("Null Object Reference")));
}

[[noreturn]] void hx::throwBadCast() {
    // Generated Haxe catch blocks handle Dynamic values.  A native C++
    // std::bad_cast escapes those handlers and aborts the application.
    ::hx::Throw(::Dynamic(::String("Invalid Cast")));
}

[[noreturn]] void hx::throwDynamicCast(const char* source,
                                       const char* target) {
    // A checked Dynamic conversion is a Haxe language boundary.  Route its
    // failure through hx::Throw so catch(e:Dynamic) sees it and the exception
    // stack is replaced at the actual cast site instead of retaining an older
    // caught exception's snapshot.
    ::hx::Throw(::Dynamic(::String(
        std::string("invalid Haxe Dynamic cast: source=") +
        (source != nullptr ? source : "unknown") + " target=" +
        (target != nullptr ? target : "unknown"))));
}

String hx::Object::toString() {
    // Keep the receiver at a stable address while formatting it.  This also
    // makes direct generated calls to Object::toString safe, rather than only
    // calls made through Dynamic (which already pins its receiver).
    ::hx::ScopedPin selfPin(this);
    auto* object = static_cast<::hx::Object*>(selfPin.get());
    const auto* type = ::hx::gc::Runtime::instance().typeOf(object);
    if (type != nullptr &&
        (type->flags & ::hx::gc::TypeEnum) != 0) {
        auto* value = static_cast<::hx::EnumBase_obj*>(object);
        ::hx::RootedValue<::String> tag(value->_hx_getTag());
        ::hx::RootedValue<::Array<::Dynamic>> arguments(
            value->_hx_getParameters());

        std::string result(tag.get().view());
        const int count = arguments.get() ? arguments.get()->length : 0;
        if (count == 0)
            return ::String(result);

        result.push_back('(');
        for (int index = 0; index < count; ++index) {
            if (index != 0)
                result.push_back(',');
            ::hx::RootedValue<::Dynamic> argument(
                arguments.get()->__get(index));
            ::hx::RootedValue<::String> text(argument.get().toString());
            result.append(text.get().view());
        }
        result.push_back(')');
        return ::String(result);
    }
    return String(type == nullptr || type->name == nullptr
                      ? "Object" : type->name);
}

String Dynamic::toString() const {
    if (isNull())
        return String("null");
    if (isMemberFunction())
        return String("<function>");
    switch (tag_) {
        case Tag::Null: return String("null");
        case Tag::Integer: return String(std::to_string(integer_));
        case Tag::Floating: return String(floating_);
        case Tag::Boolean: return String(integer_ != 0 ? "true" : "false");
        case Tag::String: return Cast<String>();
        case Tag::Object: {
            void* object = managedPointer();
            const auto* type = ::hx::gc::Runtime::instance().typeOf(object);
            // Class values stringify as the represented Haxe class name.  A
            // single out-of-line definition is intentional: keeping this in
            // a public header let incremental links select an older COMDAT
            // body from an unrelated generated object, silently restoring
            // the incorrect constant "Class" result.
            if (type == &::hx::Class_obj::descriptor) {
                ::hx::ScopedPin pin(static_cast<::hx::Object*>(object));
                return static_cast<::hx::Class_obj*>(pin.get())->mName;
            }

            // Haxe anonymous records (and generated classes with an explicit
            // zero-argument toString field) dispatch that field before the
            // native Object fallback.  This lookup must finish while the raw
            // receiver is pinned, but the user closure itself is invoked only
            // after releasing the pin so arbitrary Haxe code does not retain
            // an evacuation-excluded region.
            ::Dynamic customToString;
            {
                ::hx::ScopedPin pin(static_cast<::hx::Object*>(object));
                object = pin.get();
                for (const auto* cursor =
                         ::hx::gc::Runtime::instance().typeOf(object);
                     cursor != nullptr; cursor = cursor->super) {
                    if (cursor->dynamicFieldGet != nullptr &&
                        cursor->dynamicFieldGet(
                            object, "toString", ::hx::paccNever,
                            &customToString))
                        break;
                }
            }
            if (!customToString.isNull()) {
                ::hx::RootedValue<::Dynamic> rootedFunction(customToString);
                ::hx::RootedValue<::Dynamic> result(rootedFunction.get()());
                return result.get().Cast<::String>();
            }
            if (type != nullptr &&
                (type->flags & ::hx::gc::TypeDynamicObject) != 0) {
                if (anonymousStringDepth >= 5)
                    return ::String("...");
                AnonymousStringDepthScope depthScope;
                ::hx::RootedValue<::Dynamic> receiver(*this);
                ::hx::RootedValue<::Array<::String>> fields(
                    ::Array_obj<::String>::__new());
                receiver.get().__GetFields(fields.get());
                if (!fields.get() || fields.get()->length == 0)
                    return ::String("{ }");

                std::string text("{ ");
                const int count = fields.get()->length;
                for (int index = 0; index < count; ++index) {
                    ::hx::RootedValue<::String> name(
                        fields.get()->__get(index));
                    ::hx::RootedValue<::Dynamic> fieldValue(
                        receiver.get().__Field(name.get(), ::hx::paccNever));
                    ::hx::RootedValue<::String> fieldText(
                        fieldValue.get().toString());
                    if (index != 0)
                        text.append(", ");
                    text.append(name.get().view());
                    text.append(" => ");
                    text.append(fieldText.get().view());
                }
                text.append(" }");
                return ::String(text);
            }
            ::hx::ScopedPin pin(static_cast<::hx::Object*>(object));
            return static_cast<::hx::Object*>(pin.get())->toString();
        }
        case Tag::NativePointer: return String("NativePointer");
        default: return String("<function>");
    }
    return String();
}

Dynamic Dynamic::invoke(const Dynamic* arguments, int count) const {
    if (count < 0 || (count != 0 && arguments == nullptr))
        throw ::Dynamic(::String("Invalid Haxe closure argument vector"));

    // hxcpp's reflective call ABI is deliberately permissive.  Generated
    // wrappers index the arguments they declare: absent entries read as null
    // (which activates Haxe optional-argument defaults), while surplus entries
    // are ignored.  Normalize here so all closure implementations see exactly
    // their declared arity without weakening their internal checks.
    std::vector<::Dynamic> normalized;
    const ::Dynamic* callArguments = arguments;
    auto normalize = [&](int arity) {
        if (arity < 0 || count == arity)
            return count;
        normalized.resize(static_cast<std::size_t>(arity));
        const int copyCount = std::min(count, arity);
        for (int index = 0; index < copyCount; ++index)
            normalized[static_cast<std::size_t>(index)] = arguments[index];
        callArguments = arity == 0 ? nullptr : normalized.data();
        return arity;
    };

    if (isMemberFunction()) {
        const int arity = memberFunctionArity();
        const int callCount = normalize(arity);
        (void)callCount;
        auto* owner = static_cast<::hx::Object*>(referencePointer());
        if (owner == nullptr)
            throw ::Dynamic(::String("Null Object Reference"));
        ::hx::ScopedPin ownerPin(owner);
        owner = ownerPin.get();
        switch (arity) {
            case 0:
                return ::hx::invokeBoundMethod(
                    *this, owner, callArguments, std::index_sequence<>{});
            case 1:
                return ::hx::invokeBoundMethod(
                    *this, owner, callArguments, std::make_index_sequence<1>{});
            case 2:
                return ::hx::invokeBoundMethod(
                    *this, owner, callArguments, std::make_index_sequence<2>{});
            case 3:
                return ::hx::invokeBoundMethod(
                    *this, owner, callArguments, std::make_index_sequence<3>{});
            case 4:
                return ::hx::invokeBoundMethod(
                    *this, owner, callArguments, std::make_index_sequence<4>{});
            case 5:
                return ::hx::invokeBoundMethod(
                    *this, owner, callArguments, std::make_index_sequence<5>{});
            case 6:
                return ::hx::invokeBoundMethod(
                    *this, owner, callArguments, std::make_index_sequence<6>{});
            case 7:
                return ::hx::invokeBoundMethod(
                    *this, owner, callArguments, std::make_index_sequence<7>{});
            case 8:
                return ::hx::invokeBoundMethod(
                    *this, owner, callArguments, std::make_index_sequence<8>{});
            case 9:
                return ::hx::invokeBoundMethod(
                    *this, owner, callArguments, std::make_index_sequence<9>{});
            case 10:
                return ::hx::invokeBoundMethod(
                    *this, owner, callArguments,
                    std::make_index_sequence<10>{});
            case 11:
                return ::hx::invokeBoundMethod(
                    *this, owner, callArguments,
                    std::make_index_sequence<11>{});
            case 12:
                return ::hx::invokeBoundMethod(
                    *this, owner, callArguments,
                    std::make_index_sequence<12>{});
            case 13:
                return ::hx::invokeBoundMethod(
                    *this, owner, callArguments,
                    std::make_index_sequence<13>{});
            case 14:
                return ::hx::invokeBoundMethod(
                    *this, owner, callArguments,
                    std::make_index_sequence<14>{});
            case 15:
                return ::hx::invokeBoundMethod(
                    *this, owner, callArguments,
                    std::make_index_sequence<15>{});
            default:
                throw ::Dynamic(::String("Invalid Haxe closure arity"));
        }
    }

    ::hx::LocalFunc* function = ::hx::resolveFunction(*this);
    ::hx::ScopedPin pin(function);
    auto* target = static_cast<::hx::LocalFunc*>(pin.get());
    const int callCount = normalize(target->argCount());
    return target->invokeMany(callArguments, callCount);
}

Dynamic Dynamic::__GetItem(int index) const {
    void* object = managedPointer();
    if (object == nullptr) {
        // Dynamic indexing is also used as an existence probe by the Lua
        // reflection bridge.  A script may run before a group field has been
        // initialised (PlayState's global scripts are one real example).  Do
        // not let that probe manufacture a C++ exception which can escape
        // through Lua/Lime C ABI frames; the Haxe-visible missing item is null.
        return Dynamic();
    }
    ::hx::ScopedPin pin(static_cast<::hx::Object*>(object));
    object = pin.get();
    const auto* type = ::hx::gc::Runtime::instance().typeOf(object);
    // hx::Object::__GetItem in the original runtime is the fallback for every
    // managed value that is not an Array/VirtualArray.  Its observable Haxe
    // result is null, not a native C++ exception.  Lua reflection deliberately
    // relies on this while probing whether a dynamic group value is indexable.
    if (type == nullptr || type->dynamicIndexGet == nullptr)
        return Dynamic();
    Dynamic output;
    if (!type->dynamicIndexGet(object, index, &output))
        return Dynamic();
    return output;
}

Dynamic Dynamic::__SetItem(int index, const Dynamic& value) const {
    void* object = managedPointer();
    if (object == nullptr) {
        // Keep the defensive null-index contract symmetric with __GetItem.
        // A null receiver has no writable slot, so the operation is a no-op
        // with the same null result as hx::Object's fallback setter.
        return Dynamic();
    }
    ::hx::ScopedPin pin(static_cast<::hx::Object*>(object));
    object = pin.get();
    const auto* type = ::hx::gc::Runtime::instance().typeOf(object);
    // Match hx::Object::__SetItem: a write to a non-indexable object is a
    // no-op whose expression result is null.  Concrete Array callbacks still
    // return the assigned value below.
    if (type == nullptr || type->dynamicIndexSet == nullptr ||
        !type->dynamicIndexSet(object, index, &value))
        return Dynamic();
    return value;
}

Dynamic Dynamic::__Field(const String& name, int access) const {
    (void)access;
    if (tag() == Tag::String)
        return stringDynamicField(*this, name.view());
    // Legacy hxcpp boxes scalar Dynamic values as hx::Object subclasses. A
    // field read on those boxes reaches Object::__Field, whose missing-field
    // result is null. NovaGC keeps scalars unboxed, so perform that dispatch
    // explicitly instead of mistaking a non-reference tag for a null object.
    // Inline bound-method values expose the same empty reflection surface as
    // the boxed function object they replace.
    if (tag() != Tag::Object && tag() != Tag::Null)
        return Dynamic();
    void* object = managedPointer();
    if (object == nullptr)
        ::hx::throwNullObjectReference();
    // Generated HX_ names are immortal process-lifetime literals.  Avoid a
    // temporary std::string on that hot path; retain an owned copy only for a
    // movable, dynamically-created field name.
    std::string ownedFieldName;
    const char* fieldName = name.literal_;
    if (fieldName == nullptr) {
        ownedFieldName.assign(name.view());
        fieldName = ownedFieldName.c_str();
    }
    Dynamic output;
    auto& runtime = ::hx::gc::Runtime::instance();
    const auto* directType = runtime.typeOf(object);
    if (directType == &::hx::Anon_obj::descriptor) {
        // Managed fixed anonymous records have a bounded, allocation-free read
        // callback.  Its implementation snapshots the value before any
        // possible safepoint, so the relocation handshake itself protects the
        // raw address and per-access pinCount traffic is unnecessary.
        if (directType->dynamicFieldGet(object, fieldName, access, &output))
            return output;
        return Dynamic();
    }

    ::hx::ScopedPin pin(static_cast<::hx::Object*>(object));
    object = pin.get();
    for (const auto* type = runtime.typeOf(object);
         type != nullptr; type = type->super) {
        if (type->dynamicFieldGet != nullptr &&
            type->dynamicFieldGet(object, fieldName, access, &output))
            return output;
    }
    return Dynamic();
}

Dynamic Dynamic::__SetField(const String& name, const Dynamic& value,
                            int access) const {
    (void)access;
    void* object = managedPointer();
    if (object == nullptr)
        throw ::Dynamic(::String("dynamic field write on non-object"));
    std::string ownedFieldName;
    const char* fieldName = name.literal_;
    if (fieldName == nullptr) {
        ownedFieldName.assign(name.view());
        fieldName = ownedFieldName.c_str();
    }
    ::hx::ScopedPin pin(static_cast<::hx::Object*>(object));
    object = pin.get();
    for (const auto* type = ::hx::gc::Runtime::instance().typeOf(object);
         type != nullptr; type = type->super) {
        if (type->dynamicFieldSet != nullptr &&
            type->dynamicFieldSet(object, fieldName, access, &value))
            return value;
    }
    // This is a Haxe-visible language error, not an internal runtime failure.
    // Throwing std::runtime_error here bypassed generated catch(e:Dynamic)
    // blocks.  In particular, FlxRuntimeShader intentionally probes runtime
    // uniform names on the shader instance inside try/catch; original hxcpp's
    // Object::__SetField uses hx::Throw for the same reason.
    throw ::Dynamic(::String("unknown dynamic field: " +
                             std::string(fieldName)));
}

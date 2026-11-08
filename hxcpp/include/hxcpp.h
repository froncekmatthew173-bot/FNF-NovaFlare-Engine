#pragma once

#ifndef NOVA_HXCPP_H_INCLUDED
#define NOVA_HXCPP_H_INCLUDED

#include <hx/gc/runtime.hpp>
#include <hx/gc/internal.hpp>

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <concepts>
#include <bit>
#include <cstring>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <new>
#include <random>
#include <cmath>
#include <exception>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>
#include <stdexcept>

#ifndef HXCPP_H
#define HXCPP_H
#endif

struct HxNullValue final {
    template<class T>
    [[nodiscard]] operator T() const { return T{}; }

    template<class T>
    [[nodiscard]] T StaticCast() const { return T{}; }
};

class String;
class Dynamic;

#define HXCPP_NOVA_ABI 1
#define HXCPP_API_LEVEL 500
#define HXCPP_CLASS_ATTRIBUTES

namespace hx {

using Val = ::Dynamic;

using GcRefSlot = ::hx::gc::RefSlot;
using GcTypeDescriptor = ::hx::gc::TypeDescriptor;

class EnumBase_obj;
template<class T>
[[nodiscard]] const ::hx::gc::TypeDescriptor& enumDescriptor() noexcept;

class Object {
public:
    Object() = default;
    virtual ~Object() = default;
    virtual void* _hx_getInterface(int) { return nullptr; }
    virtual ::Dynamic __Field(const ::String& name, int access);
    virtual ::Dynamic __SetField(const ::String& name,
                                 const ::Dynamic& value, int access);
    // Preserve hxcpp's scalar coercion contract.  Dynamic values are tagged in
    // this runtime instead of heap-boxing every scalar, but Haxe-visible
    // conversions must still behave as if they went through Object's virtual
    // conversion surface.
    virtual int __ToInt() const { return 0; }
    virtual double __ToDouble() const {
        return static_cast<double>(__ToInt());
    }
    virtual ::String toString();
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
};

template<class T, class = void>
struct IsCompleteManagedObject : std::false_type {};

template<class T>
struct IsCompleteManagedObject<T, std::void_t<decltype(sizeof(T))>>
    : std::bool_constant<std::is_base_of_v<Object, T>> {};

struct Ctx final {};
using StackContext = Ctx;

// Haxe stack metadata is immutable process-lifetime data emitted next to each
// generated function.  The active frames themselves live on the native stack;
// the implementation links them through thread-local native state and never
// asks the collector to conservatively scan a C++ stack.
struct StackPosition final {
    const char* className = nullptr;
    const char* functionName = nullptr;
    const char* fullName = nullptr;
    const char* fileName = nullptr;
    int firstLineNumber = 0;
    int classFunctionHash = 0;
    int fileHash = 0;

    constexpr StackPosition() noexcept = default;
    constexpr StackPosition(const char* inputClassName,
                            const char* inputFunctionName,
                            const char* inputFullName,
                            const char* inputFileName,
                            int inputFirstLineNumber,
                            int inputClassFunctionHash = 0,
                            int inputFileHash = 0) noexcept
        : className(inputClassName), functionName(inputFunctionName),
          fullName(inputFullName), fileName(inputFileName),
          firstLineNumber(inputFirstLineNumber),
          classFunctionHash(inputClassFunctionHash),
          fileHash(inputFileHash) {}
};

class StackFrame;
extern thread_local StackFrame* currentStackTop;
void leaveStackFrame(StackFrame* frame, int currentUncaught) noexcept;

class StackFrame final {
public:
    explicit StackFrame(const StackPosition* inputPosition) noexcept
        : position(inputPosition),
          lineNumber(inputPosition == nullptr
              ? 0 : inputPosition->firstLineNumber),
          previous_(currentStackTop),
          uncaughtOnEntry_(std::uncaught_exceptions()) {
        currentStackTop = this;
    }
    ~StackFrame() noexcept {
        const int currentUncaught = std::uncaught_exceptions();
        if (currentStackTop == this &&
            currentUncaught <= uncaughtOnEntry_) {
            currentStackTop = previous_;
            return;
        }
        leaveStackFrame(this, currentUncaught);
    }
    StackFrame(const StackFrame&) = delete;
    StackFrame& operator=(const StackFrame&) = delete;

    const StackPosition* position = nullptr;
    int lineNumber = 0;
    [[nodiscard]] StackFrame* previous() const noexcept { return previous_; }

private:
    friend void leaveStackFrame(StackFrame*, int) noexcept;
    StackFrame* previous_ = nullptr;
    int uncaughtOnEntry_ = 0;
};

class JustGcStackFrame final {
public:
    JustGcStackFrame() noexcept = default;
};

void captureCurrentExceptionStack(bool preserveExisting = false) noexcept;
void beginHaxeCatch() noexcept;

[[noreturn]] void throwNullObjectReference();
[[noreturn]] void throwBadCast();
[[noreturn]] void throwDynamicCast(const char* source, const char* target);
    // Pre-runtime HX_/HX_W values use immortal literal storage instead of a
    // managed StringPayload.  Recover their Haxe-visible UTF-16 code-unit
    // length when such a value is boxed into Dynamic before main().
    [[nodiscard]] int literalStringLength(const char* utf8) noexcept;

template<class T>
class Null final {
public:
    Null() noexcept = default;
    template<class U>
        requires std::is_same_v<std::remove_cvref_t<U>, std::nullptr_t>
    Null(U&&) noexcept {}
    Null(::HxNullValue) noexcept {}
    Null(const T& input) : value_(input), hasValue_(true) {}

    [[nodiscard]] T Default(const T& fallback) const {
        return hasValue_ ? value_ : fallback;
    }
    [[nodiscard]] operator T() const { return Default(T{}); }
    [[nodiscard]] bool isNull() const noexcept { return !hasValue_; }

private:
    T value_{};
    bool hasValue_ = false;
};

template<class T>
class ObjectPtr final {
public:
    using element_type = T;
    ::hx::gc::RefSlot mPtr{0};

    ObjectPtr() noexcept = default;
    ObjectPtr(std::nullptr_t) noexcept {}
    ObjectPtr(::HxNullValue) noexcept {}
    explicit ObjectPtr(int value) {
        if (value != 0)
            throw std::invalid_argument("only zero converts to a null ObjectPtr");
    }
    ObjectPtr(T* value) noexcept
        : mPtr(::hx::gc::makeRefFast(value).bits()) {}
    ObjectPtr(const ObjectPtr& other) noexcept
        : mPtr(other.mPtr.load(std::memory_order_acquire)) {}
    ObjectPtr(ObjectPtr&& other) noexcept
        : mPtr(other.mPtr.exchange(0, std::memory_order_acq_rel)) {}
    template<class U>
        requires (std::is_convertible_v<U*, T*> || std::is_base_of_v<U, T>)
    ObjectPtr(const ObjectPtr<U>& other)
        : mPtr(other.mPtr.load(std::memory_order_acquire)) {
        if constexpr (!std::is_convertible_v<U*, T*> &&
                      requires { T::__novaType; }) {
            U* raw = other.get();
            if (raw != nullptr && !::hx::gc::Runtime::instance().isInstanceOf(
                    raw, T::__novaType))
                ::hx::throwBadCast();
        }
    }
    template<class U>
        requires (!std::is_convertible_v<U*, T*> &&
                  !std::is_base_of_v<U, T> &&
                  !(requires { typename T::nova_array_element;
                               typename U::nova_array_element; }))
    explicit ObjectPtr(const ObjectPtr<U>& other) {
        U* raw = other.get();
        if (raw == nullptr)
            return;
        if constexpr (requires { T::__novaType; }) {
            if (!::hx::gc::Runtime::instance().isInstanceOf(raw, T::__novaType))
                ::hx::throwBadCast();
            mPtr.store(::hx::gc::makeRefFast(raw).bits(),
                       std::memory_order_release);
        } else {
            ::hx::throwBadCast();
        }
    }

    ObjectPtr& operator=(const ObjectPtr& other) {
        if (this != &other)
            ::hx::gc::copyBarrierFast(nullptr, mPtr, other.mPtr);
        return *this;
    }
    ObjectPtr& operator=(ObjectPtr&& other) {
        if (this != &other) {
            ::hx::gc::copyBarrierFast(nullptr, mPtr, other.mPtr);
            other.mPtr.store(0, std::memory_order_release);
        }
        return *this;
    }
    template<class U>
        requires (std::is_convertible_v<U*, T*> || std::is_base_of_v<U, T>)
    ObjectPtr& operator=(const ObjectPtr<U>& other) {
        if constexpr (!std::is_convertible_v<U*, T*> &&
                      requires { T::__novaType; }) {
            U* raw = other.get();
            if (raw != nullptr && !::hx::gc::Runtime::instance().isInstanceOf(
                    raw, T::__novaType))
                ::hx::throwBadCast();
            ::hx::gc::storeBarrierFast(nullptr, mPtr, raw);
        } else {
            ::hx::gc::copyBarrierFast(nullptr, mPtr, other.mPtr);
        }
        return *this;
    }
    template<class U>
        requires (!std::is_convertible_v<U*, T*> &&
                  !std::is_base_of_v<U, T> &&
                  !(requires { typename T::nova_array_element;
                               typename U::nova_array_element; }))
    ObjectPtr& operator=(const ObjectPtr<U>& other) {
        U* raw = other.get();
        if (raw == nullptr)
            return (*this = nullptr);
        if constexpr (requires { T::__novaType; }) {
            if (!::hx::gc::Runtime::instance().isInstanceOf(raw, T::__novaType))
                ::hx::throwBadCast();
            ::hx::gc::storeBarrierFast(nullptr, mPtr, raw);
            return *this;
        } else {
            ::hx::throwBadCast();
        }
    }
    ObjectPtr& operator=(T* value) {
        ::hx::gc::storeBarrierFast(nullptr, mPtr, value);
        return *this;
    }
    ObjectPtr& operator=(std::nullptr_t) {
        ::hx::gc::storeBarrierFast(nullptr, mPtr, nullptr);
        return *this;
    }
    ObjectPtr& operator=(::HxNullValue) {
        return (*this = nullptr);
    }

    [[nodiscard]] T* get() const {
        return static_cast<T*>(::hx::gc::loadBarrierFast(
            const_cast<::hx::gc::RefSlot&>(mPtr)));
    }
    [[nodiscard]] T* operator->() const {
        T* result = get();
        if (result == nullptr) [[unlikely]]
            ::hx::throwNullObjectReference();
        return result;
    }
    template<class Index>
        requires requires(T* value, Index index) { (*value)[index]; }
    [[nodiscard]] decltype(auto) operator[](Index index) const {
        return (*operator->())[index];
    }
    [[nodiscard]] explicit operator bool() const { return get() != nullptr; }
    [[nodiscard]] operator T*() const { return get(); }

    template<class U>
    [[nodiscard]] bool operator==(const ObjectPtr<U>& other) const {
        return static_cast<const void*>(get()) ==
               static_cast<const void*>(other.get());
    }
    template<class U>
    [[nodiscard]] bool operator!=(const ObjectPtr<U>& other) const {
        return !(*this == other);
    }

    template<class U>
    [[nodiscard]] ObjectPtr<U> as() const {
        return ObjectPtr<U>(static_cast<U*>(get()));
    }

    template<class U>
        requires (requires { typename T::nova_array_element; } &&
                  requires { typename U::nova_array_element; })
    [[nodiscard]] operator ObjectPtr<U>() const;
};

class Class_obj;
using Class = ObjectPtr<Class_obj>;

class ManagedMutatorScope final {
public:
    ManagedMutatorScope() {
        auto* state = ::hx::gc::currentThread;
        if (state != nullptr && state->nativeBlockedLocal &&
            ::hx::gc::Runtime::instance().beginManagedExecution(*state)) {
            state_ = state;
        }
    }

    ~ManagedMutatorScope() noexcept {
        if (state_ != nullptr)
            ::hx::gc::Runtime::instance().endManagedExecution(*state_);
    }

    ManagedMutatorScope(const ManagedMutatorScope&) = delete;
    ManagedMutatorScope& operator=(const ManagedMutatorScope&) = delete;

private:
    ::hx::gc::RootThreadState* state_ = nullptr;
};

class ScopedPin final {
public:
    explicit ScopedPin(Object* object)
        : reference_(::hx::gc::makeRefFast(object).bits()),
          rootSlots_{&reference_},
          rootScope_(rootSlots_, 1), object_(object) {
        auto* state = ::hx::gc::currentThread;
        if (object_ != nullptr && state != nullptr &&
            (!state->nativeBlockedLocal ||
             state->managedExecutionDepth.load(std::memory_order_acquire) != 0) &&
            state->fastScopedPinCount <
                ::hx::gc::RootThreadState::FastScopedPinCapacity &&
            !::hx::gc::scopedPinBarrierActive.load(
                std::memory_order_acquire)) {
            state->fastScopedPins[state->fastScopedPinCount++] = object_;
            fastState_ = state;
        } else {
            object_ = static_cast<Object*>(
                ::hx::gc::Runtime::instance().acquireScopedPin(reference_));
        }
    }
    ~ScopedPin() {
        if (fastState_ != nullptr) {
            if (fastState_ != ::hx::gc::currentThread) {
                ::hx::gc::Runtime::instance().releaseForeignFastScopedPin(
                    *fastState_, object_);
                fastState_ = nullptr;
                return;
            }
            const std::size_t count = fastState_->fastScopedPinCount;
            if (count != 0 &&
                fastState_->fastScopedPins[count - 1U] == object_) {
                fastState_->fastScopedPins[
                    --fastState_->fastScopedPinCount] = nullptr;
                return;
            }
            // Preserve safety for incorrectly nested embedding scopes without
            // decrementing an object-header pin that the fast path never took.
            for (std::size_t index = count; index != 0; --index) {
                if (fastState_->fastScopedPins[index - 1U] != object_)
                    continue;
                for (std::size_t cursor = index; cursor < count; ++cursor) {
                    fastState_->fastScopedPins[cursor - 1U] =
                        fastState_->fastScopedPins[cursor];
                }
                fastState_->fastScopedPins[count - 1U] = nullptr;
                --fastState_->fastScopedPinCount;
                return;
            }
            return;
        }
        ::hx::gc::Runtime::instance().releaseScopedPin(object_);
    }

    ScopedPin(const ScopedPin&) = delete;
    ScopedPin& operator=(const ScopedPin&) = delete;

    [[nodiscard]] Object* get() const noexcept { return object_; }

private:
    ::hx::gc::RefSlot reference_{0};
    ::hx::gc::RefSlot* rootSlots_[1];
    ::hx::gc::RootScope rootScope_;
    Object* object_ = nullptr;
    ::hx::gc::RootThreadState* fastState_ = nullptr;
};

template<class T>
[[nodiscard]] T* load(::hx::gc::RefSlot& slot) {
    static_assert(std::is_base_of_v<Object, T>);
    return static_cast<T*>(::hx::gc::loadBarrierFast(slot));
}

template<class T>
void store(Object* owner, ::hx::gc::RefSlot& slot, T* value) {
    static_assert(std::is_base_of_v<Object, T>);
    ::hx::gc::storeBarrierFast(owner, slot, value);
}

template<class T, class... Args>
[[nodiscard]] ObjectPtr<T> allocate(const GcTypeDescriptor& descriptor,
                                    Args&&... args) {
    static_assert(std::is_base_of_v<Object, T>);
    void* memory = ::hx::gc::Runtime::instance().allocate(descriptor);
    // C++ constructors for generated Haxe classes may allocate default
    // dynamic-method closures which capture `this`.  Publishing/rooting only
    // after placement-new leaves that owner collectible during the nested
    // closure allocation.  Expose an all-null exact shell first, then keep it
    // rooted and non-moving throughout native construction.
    ::hx::gc::detail::publishEmpty(memory);
    ObjectPtr<T> result(static_cast<T*>(memory));
    ::hx::gc::RefSlot* roots[]{&result.mPtr};
    ::hx::gc::RootScope rootScope(roots, 1);
    ::hx::ScopedPin constructionPin(static_cast<Object*>(
        static_cast<T*>(memory)));
    T* object = ::new (constructionPin.get())
        T(std::forward<Args>(args)...);
    ::hx::gc::detail::finishConstruction(object);
    return result;
}

inline void safepoint() {
    if (::hx::gc::safepointRequested.load(std::memory_order_acquire))
        ::hx::gc::Runtime::instance().safepointPoll();
}
inline void Boot() noexcept {}

template<class Owner, class Field>
[[nodiscard]] std::uint32_t fieldOffset(Field Owner::*member) {
    // Descriptor tables are initialized before main(), so computing an
    // offset must never run an Owner constructor (constructors may allocate
    // managed closures). Use a non-null synthetic base solely for the
    // pointer-to-member address calculation; no storage is dereferenced.
    constexpr std::uintptr_t SyntheticBase = 0x10000;
    auto* owner = reinterpret_cast<Owner*>(SyntheticBase);
    const auto* base = reinterpret_cast<const std::byte*>(owner);
    const auto* field = reinterpret_cast<const std::byte*>(&(owner->*member));
    const std::size_t offset = static_cast<std::size_t>(field - base);
    if (offset > UINT32_MAX)
        throw std::overflow_error("managed field offset exceeds 32 bits");
    return static_cast<std::uint32_t>(offset);
}

} // namespace hx

template<class T>
class Array_obj;

template<class T>
using Array = ::hx::ObjectPtr<Array_obj<T>>;

class String final {
public:
    ::hx::gc::RefSlot mPtr{0};
    int length = 0;
    const char* literal_ = nullptr;

    String() noexcept = default;
    String(std::nullptr_t) noexcept {}
    String(::HxNullValue) noexcept {}
    String(const char* value);
    String(const char* value, int inputLength)
        : String(value == nullptr ? std::string_view() :
                 std::string_view(value, static_cast<std::size_t>(
                     std::max(inputLength, 0)))) {}
    String(const char16_t* value);
    String(const char16_t* value, int inputLength);
    String(std::string_view value);
    String(const std::string& value) : String(std::string_view(value)) {}
    String(int value) : String(std::to_string(value)) {}
    String(unsigned int value) : String(std::to_string(value)) {}
    String(double value);
    String(float value) : String(static_cast<double>(value)) {}
    explicit String(long long value) : String(std::to_string(value)) {}
    String(const String& other) noexcept;
    String(String&& other) noexcept;

    String& operator=(const String& other);
    String& operator=(String&& other);
    String& operator=(std::nullptr_t);
    String& operator=(::HxNullValue) { return (*this = nullptr); }
    String& operator=(const char* value);

    [[nodiscard]] const char* c_str() const;
    [[nodiscard]] const char* utf8_str() const { return c_str(); }
    [[nodiscard]] std::string_view view() const;
    [[nodiscard]] bool isNull() const noexcept {
        return mPtr.load(std::memory_order_acquire) == 0 && literal_ == nullptr;
    }
    [[nodiscard]] explicit operator bool() const noexcept { return !isNull(); }
    [[nodiscard]] ::hx::gc::RefSlot& raw_ref() noexcept { return mPtr; }
    [[nodiscard]] const ::hx::gc::RefSlot& raw_ref() const noexcept {
        return mPtr;
    }

    [[nodiscard]] bool operator==(const String& other) const;
    [[nodiscard]] bool operator!=(const String& other) const {
        return !(*this == other);
    }
    [[nodiscard]] bool operator==(const char* other) const;
    [[nodiscard]] bool operator!=(const char* other) const {
        return !(*this == other);
    }
    [[nodiscard]] bool operator==(::HxNullValue) const noexcept {
        return isNull();
    }
    [[nodiscard]] bool operator!=(::HxNullValue) const noexcept {
        return !isNull();
    }
    [[nodiscard]] int cca(int index) const;
    [[nodiscard]] int compare(const String& other) const;
    [[nodiscard]] Dynamic charCodeAt(int index) const;
    [[nodiscard]] int hashCode() const;
    [[nodiscard]] String toUpperCase() const;
    [[nodiscard]] String toLowerCase() const;
    [[nodiscard]] String __URLEncode() const;
    [[nodiscard]] String __URLDecode() const;
    [[nodiscard]] String charAt(int index) const;
    [[nodiscard]] int indexOf(const String& needle,
                              ::hx::Null<int> startIndex = nullptr) const;
    [[nodiscard]] int lastIndexOf(const String& needle,
                                  ::hx::Null<int> startIndex = nullptr) const;
    [[nodiscard]] ::Array<String> split(const String& delimiter) const;
    [[nodiscard]] String substr(int position,
                                ::hx::Null<int> requestedLength = nullptr) const;
    [[nodiscard]] String substring(int startIndex,
                                   ::hx::Null<int> endIndex = nullptr) const;
    [[nodiscard]] String toString() const { return *this; }
    [[nodiscard]] const char* raw_ptr() const { return c_str(); }
    [[nodiscard]] static String fromCharCode(int code);
    [[nodiscard]] static Dynamic fromCharCode_dyn();
    [[nodiscard]] Dynamic charAt_dyn() const;
    [[nodiscard]] Dynamic charCodeAt_dyn() const;
    [[nodiscard]] Dynamic indexOf_dyn() const;
    [[nodiscard]] Dynamic lastIndexOf_dyn() const;
    [[nodiscard]] Dynamic split_dyn() const;
    [[nodiscard]] Dynamic substr_dyn() const;
    [[nodiscard]] Dynamic substring_dyn() const;
    [[nodiscard]] Dynamic toLowerCase_dyn() const;
    [[nodiscard]] Dynamic toString_dyn() const;
    [[nodiscard]] Dynamic toUpperCase_dyn() const;
    [[nodiscard]] bool operator<(const String& other) const {
        return compare(other) < 0;
    }
    [[nodiscard]] bool operator>(const String& other) const {
        return compare(other) > 0;
    }
    [[nodiscard]] bool operator<=(const String& other) const {
        return compare(other) <= 0;
    }
    [[nodiscard]] bool operator>=(const String& other) const {
        return compare(other) >= 0;
    }
};

namespace hx {
class RootedStringLiteral final {
public:
    RootedStringLiteral(const char* text, std::size_t length);
    String value;
};
class RootedWideStringLiteral final {
public:
    explicit RootedWideStringLiteral(const char16_t* text);
    String value;
};
} // namespace hx

struct StringOffset final {
    static constexpr std::size_t Ptr = offsetof(::String, mPtr);
};

[[nodiscard]] String operator+(const String& left, const String& right);
[[nodiscard]] String operator+(const String& left, const char* right);
[[nodiscard]] String operator+(const char* left, const String& right);
[[nodiscard]] String operator+(const String& left, int right);
[[nodiscard]] String operator+(const String& left, double right);
[[nodiscard]] String operator+(const String& left, bool right);
[[nodiscard]] String operator+(int left, const String& right);
[[nodiscard]] String operator+(double left, const String& right);
[[nodiscard]] String operator+(bool left, const String& right);
[[nodiscard]] inline String operator+(const String& left,
                                      const Dynamic& right);
[[nodiscard]] inline String operator+(const Dynamic& left,
                                      const String& right);

namespace hx {
template<class ArrayPtr>
[[nodiscard]] ArrayPtr castDynamicArray(const ::Dynamic& input);
}

class Dynamic final {
public:
    enum class Tag : std::uint8_t {
        Null,
        Integer,
        Floating,
        Boolean,
        Object,
        String,
        NativePointer,
        // A generated bound method does not need a separately allocated GC
        // object.  Its receiver already fits in Dynamic's precise RefSlot and
        // its wrapper entry fits in the scalar payload.  Encoding the arity in
        // the tag keeps the full native function address intact and lets a
        // method safely escape, cross a safepoint, or be stored in a traced
        // Dynamic field without adding a second managed allocation.
        MemberFunction0,
        MemberFunction1,
        MemberFunction2,
        MemberFunction3,
        MemberFunction4,
        MemberFunction5,
        MemberFunction6,
        MemberFunction7,
        MemberFunction8,
        MemberFunction9,
        MemberFunction10,
        MemberFunction11,
        MemberFunction12,
        MemberFunction13,
        MemberFunction14,
        MemberFunction15,
    };

    [[nodiscard]] static constexpr const char* tagName(Tag tag) noexcept {
        switch (tag) {
            case Tag::Null: return "Null";
            case Tag::Integer: return "Int";
            case Tag::Floating: return "Float";
            case Tag::Boolean: return "Bool";
            case Tag::Object: return "Object";
            case Tag::String: return "String";
            case Tag::NativePointer: return "NativePointer";
            default: return "Function";
        }
    }

    ::hx::gc::RefSlot mPtr{0};

    Dynamic() noexcept = default;
    Dynamic(std::nullptr_t) noexcept {}
    Dynamic(::HxNullValue) noexcept {}
    Dynamic(int value) noexcept
        : integer_(value), tag_(Tag::Integer) {}
    Dynamic(bool value) noexcept
        : integer_(value ? 1 : 0), tag_(Tag::Boolean) {}
    Dynamic(double value) noexcept
        : floating_(value), tag_(Tag::Floating) {}
    // Generated native bindings frequently feed C strings through
    // hx::TCast (Lua's lua_tostring/lua_typename, Discord structures, and
    // other CFFI APIs).  Without an exact constructor C++ prefers the
    // standard pointer-to-bool conversion, boxing every non-null C string as
    // `true`.  Original hxcpp has an explicit native-string Dynamic
    // constructor for precisely this reason.
    explicit Dynamic(const char* value)
        : Dynamic(::String(value)) {}
    explicit Dynamic(const char16_t* value)
        : Dynamic(::String(value)) {}
    template<class Integer>
        requires (std::is_integral_v<Integer> &&
                  !std::is_same_v<std::remove_cv_t<Integer>, int> &&
                  !std::is_same_v<std::remove_cv_t<Integer>, bool>)
    Dynamic(Integer value) noexcept
        : integer_(static_cast<std::int64_t>(value)), tag_(Tag::Integer) {}
    Dynamic(const String& value) noexcept
        : mPtr(value.mPtr.load(std::memory_order_acquire)),
          // Global generated Array<String> constants are constructed before
          // the GC starts.  Such Strings intentionally use immortal literal
          // storage and have no managed ref.  Preserve that process-lifetime
          // pointer in the scalar payload when boxing instead of silently
          // converting a valid literal to null.
          integer_(mPtr.load(std::memory_order_relaxed) == 0
                       ? static_cast<std::int64_t>(reinterpret_cast<
                             std::intptr_t>(value.literal_))
                       : value.length),
          tag_(value.isNull() ? Tag::Null : Tag::String) {}
    template<class T>
    Dynamic(const ::hx::ObjectPtr<T>& value) noexcept
        : mPtr(value.mPtr.load(std::memory_order_acquire)),
          tag_(mPtr.load(std::memory_order_relaxed) == 0
                   ? Tag::Null : Tag::Object) {}

    template<class T>
        requires ::hx::IsCompleteManagedObject<T>::value
    Dynamic(T* value) noexcept
        : mPtr(::hx::gc::makeRefFast(value).bits()),
          tag_(value == nullptr ? Tag::Null : Tag::Object) {}

    Dynamic(const Dynamic& other) noexcept
        : mPtr(other.mPtr.load(std::memory_order_acquire)),
          integer_(other.integer_),
          tag_(other.isNull() ? Tag::Null : other.tag_) {}
    Dynamic(Dynamic&& other) noexcept
        : mPtr(other.mPtr.exchange(0, std::memory_order_acq_rel)),
          integer_(other.integer_),
          tag_((other.tag_ == Tag::Object &&
                mPtr.load(std::memory_order_relaxed) == 0) ||
                       (other.tag_ == Tag::String &&
                        mPtr.load(std::memory_order_relaxed) == 0 &&
                        integer_ == 0)
                   ? Tag::Null : other.tag_) {
        other.integer_ = 0;
        other.tag_ = Tag::Null;
    }

    Dynamic& operator=(const Dynamic& other) {
        if (this != &other) {
            void* object = nullptr;
            if (isReferenceTag(other.tag_)) {
                auto& source = const_cast<::hx::gc::RefSlot&>(other.mPtr);
                object = ::hx::gc::loadBarrierFast(source);
            }
            ::hx::gc::storeBarrierFast(nullptr, mPtr, object);
            copyNonReferenceFrom(other);
        }
        return *this;
    }
    Dynamic& operator=(Dynamic&& other) {
        if (this != &other) {
            void* object = nullptr;
            if (isReferenceTag(other.tag_))
                object = ::hx::gc::loadBarrierFast(other.mPtr);
            ::hx::gc::storeBarrierFast(nullptr, mPtr, object);
            copyNonReferenceFrom(other);
            other.mPtr.store(0, std::memory_order_release);
            other.integer_ = 0;
            other.tag_ = Tag::Null;
        }
        return *this;
    }
    Dynamic& operator=(int value) {
        ::hx::gc::storeBarrierFast(nullptr, mPtr, nullptr);
        integer_ = value;
        tag_ = Tag::Integer;
        return *this;
    }
    Dynamic& operator=(bool value) {
        ::hx::gc::storeBarrierFast(nullptr, mPtr, nullptr);
        integer_ = value ? 1 : 0;
        tag_ = Tag::Boolean;
        return *this;
    }
    Dynamic& operator=(double value) {
        ::hx::gc::storeBarrierFast(nullptr, mPtr, nullptr);
        floating_ = value;
        tag_ = Tag::Floating;
        return *this;
    }
    template<class Integer>
        requires (std::is_integral_v<Integer> &&
                  !std::is_same_v<std::remove_cv_t<Integer>, int> &&
                  !std::is_same_v<std::remove_cv_t<Integer>, bool>)
    Dynamic& operator=(Integer value) {
        ::hx::gc::storeBarrierFast(nullptr, mPtr, nullptr);
        integer_ = static_cast<std::int64_t>(value);
        tag_ = Tag::Integer;
        return *this;
    }
    Dynamic& operator=(std::nullptr_t) {
        ::hx::gc::storeBarrierFast(nullptr, mPtr, nullptr);
        integer_ = 0;
        tag_ = Tag::Null;
        return *this;
    }
    Dynamic& operator=(::HxNullValue) { return (*this = nullptr); }

    template<class T>
    Dynamic& operator=(const ::hx::ObjectPtr<T>& value) {
        void* object = value.get();
        ::hx::gc::storeBarrierFast(nullptr, mPtr, object);
        integer_ = 0;
        tag_ = object == nullptr ? Tag::Null : Tag::Object;
        return *this;
    }

    template<class T>
        requires ::hx::IsCompleteManagedObject<T>::value
    Dynamic& operator=(T* value) {
        ::hx::gc::storeBarrierFast(nullptr, mPtr, value);
        integer_ = 0;
        tag_ = value == nullptr ? Tag::Null : Tag::Object;
        return *this;
    }

    Dynamic& operator=(const String& value) {
        auto& source = const_cast<::hx::gc::RefSlot&>(value.mPtr);
        void* object = ::hx::gc::loadBarrierFast(source);
        ::hx::gc::storeBarrierFast(nullptr, mPtr, object);
        integer_ = object == nullptr
            ? static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(
                  value.literal_))
            : value.length;
        tag_ = value.isNull() ? Tag::Null : Tag::String;
        return *this;
    }

    [[nodiscard]] static Dynamic fromNativePointer(void* pointer) noexcept {
        Dynamic result;
        result.integer_ = static_cast<std::int64_t>(
            reinterpret_cast<std::intptr_t>(pointer));
        result.tag_ = Tag::NativePointer;
        return result;
    }

    [[nodiscard]] static Dynamic fromMemberFunction(
            ::hx::Object* owner, std::uintptr_t function, int arity) {
        if (arity < 0 || arity > 15 || function == 0)
            throw std::invalid_argument("invalid Haxe bound method");
        Dynamic result;
        result.mPtr.store(
            owner == nullptr ? 0 :
                ::hx::gc::makeRefFast(owner).bits(),
            std::memory_order_release);
        result.integer_ = static_cast<std::int64_t>(function);
        result.tag_ = static_cast<Tag>(
            static_cast<unsigned>(Tag::MemberFunction0) +
            static_cast<unsigned>(arity));
        return result;
    }

    [[nodiscard]] static constexpr bool isMemberFunctionTag(
            Tag tag) noexcept {
        return tag >= Tag::MemberFunction0 && tag <= Tag::MemberFunction15;
    }

    [[nodiscard]] static constexpr bool isReferenceTag(Tag tag) noexcept {
        return tag == Tag::Object || tag == Tag::String ||
               isMemberFunctionTag(tag);
    }

    [[nodiscard]] bool isMemberFunction() const noexcept {
        return isMemberFunctionTag(tag_);
    }

    [[nodiscard]] int memberFunctionArity() const noexcept {
        return isMemberFunction() ?
            static_cast<int>(tag_) - static_cast<int>(Tag::MemberFunction0) :
            -1;
    }

    [[nodiscard]] std::uintptr_t memberFunctionAddress() const noexcept {
        return isMemberFunction() ?
            static_cast<std::uintptr_t>(integer_) : 0;
    }

    // Resolves any precise reference stored by Dynamic, including the owner
    // of an inline bound method.  managedPointer() intentionally remains an
    // object/string-only API so code asking for a Haxe object cannot mistake
    // a method receiver for the method value itself.
    [[nodiscard]] void* referencePointer() const {
        if (!isReferenceTag(tag_))
            return nullptr;
        auto& slot = const_cast<::hx::gc::RefSlot&>(mPtr);
        return ::hx::gc::loadBarrierFast(slot);
    }

    [[nodiscard]] void* nativePointer() const {
        if (tag_ != Tag::NativePointer)
            throw std::runtime_error("Dynamic value is not a native pointer");
        return reinterpret_cast<void*>(static_cast<std::intptr_t>(integer_));
    }

    [[nodiscard]] void* managedPointer() const {
        if (tag_ != Tag::Object && tag_ != Tag::String)
            return nullptr;
        return referencePointer();
    }

    void copyNonReferenceFrom(const Dynamic& other) noexcept {
        integer_ = other.integer_;
        tag_ = (other.tag_ == Tag::Object &&
                mPtr.load(std::memory_order_acquire) == 0) ||
                       (other.tag_ == Tag::String &&
                        mPtr.load(std::memory_order_acquire) == 0 &&
                        integer_ == 0)
                   ? Tag::Null : other.tag_;
    }

    [[nodiscard]] Tag tag() const noexcept { return tag_; }
    [[nodiscard]] bool isNull() const noexcept {
        return tag_ == Tag::Null ||
            (tag_ == Tag::Object &&
             mPtr.load(std::memory_order_acquire) == 0) ||
            (tag_ == Tag::String &&
             mPtr.load(std::memory_order_acquire) == 0 && integer_ == 0);
    }
    template<class T>
    [[nodiscard]] T Default(const T& fallback) const {
        return isNull() ? fallback : Cast<T>();
    }
    [[nodiscard]] operator bool() const { return Cast<bool>(); }
    [[nodiscard]] operator int() const { return Cast<int>(); }
    [[nodiscard]] operator std::int64_t() const {
        return Cast<std::int64_t>();
    }
    [[nodiscard]] operator std::uint64_t() const {
        return Cast<std::uint64_t>();
    }
    template<class Integer>
        requires (std::is_integral_v<Integer> &&
                  !std::is_same_v<std::remove_cv_t<Integer>, bool> &&
                  !std::is_same_v<std::remove_cv_t<Integer>, int> &&
                  !std::is_same_v<std::remove_cv_t<Integer>, std::int64_t> &&
                  !std::is_same_v<std::remove_cv_t<Integer>, std::uint64_t>)
    [[nodiscard]] operator Integer() const { return Cast<Integer>(); }
    [[nodiscard]] operator double() const { return Cast<double>(); }
    [[nodiscard]] operator float() const { return Cast<float>(); }
    [[nodiscard]] operator String() const { return Cast<String>(); }

    Dynamic& operator++() {
        if (tag_ == Tag::Integer || tag_ == Tag::Boolean)
            return (*this = Cast<std::int64_t>() + 1);
        return (*this = Cast<double>() + 1.0);
    }
    Dynamic operator++(int) {
        Dynamic previous(*this);
        ++(*this);
        return previous;
    }
    Dynamic& operator--() {
        if (tag_ == Tag::Integer || tag_ == Tag::Boolean)
            return (*this = Cast<std::int64_t>() - 1);
        return (*this = Cast<double>() - 1.0);
    }
    Dynamic operator--(int) {
        Dynamic previous(*this);
        --(*this);
        return previous;
    }

    Dynamic operator()() const;
    Dynamic operator()(const Dynamic& argument0) const;
    Dynamic operator()(const Dynamic& argument0,
                       const Dynamic& argument1) const;
    Dynamic operator()(const Dynamic& argument0,
                       const Dynamic& argument1,
                       const Dynamic& argument2) const;
    [[nodiscard]] Dynamic invoke(const Dynamic* arguments, int count) const;
    [[nodiscard]] Dynamic __GetItem(int index) const;
    Dynamic __SetItem(int index, const Dynamic& value) const;
    [[nodiscard]] int __length() const;
    [[nodiscard]] int __ArgCount() const;
    void __SetThis(const Dynamic& owner) const;
    [[nodiscard]] Dynamic __Run(const ::Array<Dynamic>& arguments) const;
    [[nodiscard]] Dynamic __Field(const String& name, int access) const;
    Dynamic __SetField(const String& name,
                       const Dynamic& value, int access) const;
    [[nodiscard]] ::hx::Class __GetClass() const;
    [[nodiscard]] int __GetType() const noexcept;
    [[nodiscard]] int _hx_getIndex() const;
    [[nodiscard]] String toString() const;
    [[nodiscard]] bool __HasField(const String& name) const;
    [[nodiscard]] bool __IsArray() const noexcept;
    void __GetFields(const ::Array<String>& output) const;
    [[nodiscard]] Dynamic* operator->() noexcept { return this; }
    [[nodiscard]] const Dynamic* operator->() const noexcept { return this; }

    template<class... Arguments>
        requires (sizeof...(Arguments) >= 4 && sizeof...(Arguments) <= 12)
    Dynamic operator()(Arguments&&... arguments) const {
        Dynamic packed[]{Dynamic(std::forward<Arguments>(arguments))...};
        return invoke(packed, static_cast<int>(sizeof...(Arguments)));
    }

    template<class T>
    [[nodiscard]] T Cast() const {
        if constexpr (std::is_same_v<T, Dynamic>) {
            return *this;
        }
        if (isNull()) {
            if constexpr (std::is_arithmetic_v<T>) {
                return T{};
            } else if constexpr (std::is_same_v<T, String>) {
                return String();
            } else if constexpr (std::is_pointer_v<T>) {
                return nullptr;
            } else if constexpr (requires { T(nullptr); }) {
                return T(nullptr);
            }
        }
        if constexpr (std::is_integral_v<T> &&
                      !std::is_same_v<T, bool>) {
            if (tag_ == Tag::Integer || tag_ == Tag::Boolean)
                return static_cast<T>(integer_);
            if (tag_ == Tag::Floating)
                return static_cast<T>(floating_);
            if (tag_ == Tag::String)
                return static_cast<T>(std::strtoll(Cast<String>().c_str(),
                                                   nullptr, 10));
            if (tag_ == Tag::Object) {
                auto* object = static_cast<::hx::Object*>(managedPointer());
                if (object != nullptr) {
                    ::hx::ScopedPin pin(object);
                    return static_cast<T>(
                        static_cast<::hx::Object*>(pin.get())->__ToInt());
                }
            }
            // Original hxcpp's base Object::__ToInt returns zero.  Functions,
            // native handles, and ordinary objects therefore coerce to zero.
            return T{};
        } else if constexpr (std::is_same_v<T, bool>) {
            // hxcpp implements Bool conversion through __ToInt, including the
            // truncation of Float and atoi-style String conversion.
            return Cast<int>() != 0;
        } else if constexpr (std::is_floating_point_v<T>) {
            if (tag_ == Tag::Floating)
                return static_cast<T>(floating_);
            if (tag_ == Tag::Integer || tag_ == Tag::Boolean)
                return static_cast<T>(integer_);
            if (tag_ == Tag::String)
                return static_cast<T>(std::strtod(Cast<String>().c_str(),
                                                  nullptr));
            if (tag_ == Tag::Object) {
                auto* object = static_cast<::hx::Object*>(managedPointer());
                if (object != nullptr) {
                    ::hx::ScopedPin pin(object);
                    return static_cast<T>(
                        static_cast<::hx::Object*>(pin.get())->__ToDouble());
                }
            }
            return T{};
        } else if constexpr (std::is_same_v<T, String>) {
            if (tag_ == Tag::String) {
                String result;
                const auto reference = mPtr.load(std::memory_order_acquire);
                result.mPtr.store(reference, std::memory_order_release);
                if (reference == 0) {
                    result.literal_ = reinterpret_cast<const char*>(
                        static_cast<std::intptr_t>(integer_));
                    result.length = ::hx::literalStringLength(result.literal_);
                } else {
                    result.length = static_cast<int>(integer_);
                }
                return result;
            }
            // This is deliberately a coercion, not a checked cast.  Generated
            // JsonPrinter and ordinary Haxe string assignments rely on Int,
            // Float, Bool, arrays, and user objects being stringified here.
            return toString();
        } else if constexpr (std::is_pointer_v<T> &&
                             ::hx::IsCompleteManagedObject<
                                 std::remove_pointer_t<T>>::value) {
            if (tag_ == Tag::Object)
                return static_cast<T>(managedPointer());
        } else {
            if (tag_ == Tag::Object) {
                if constexpr (requires {
                                  typename T::element_type;
                                  typename T::element_type::nova_array_element;
                              }) {
                    return ::hx::castDynamicArray<T>(*this);
                }
                using Element = typename T::element_type;
                auto& slot = const_cast<::hx::gc::RefSlot&>(mPtr);
                auto* object = static_cast<::hx::Object*>(
                    ::hx::gc::loadBarrierFast(slot));
                if (object == nullptr)
                    return T(nullptr);

                // A Dynamic-to-class conversion is Haxe's checked cast
                // boundary.  Merely reinterpreting the payload let unrelated
                // generated classes through and later turned ordinary field
                // reads into memory corruption.  Validate the exact runtime
                // descriptor (or interface table) before constructing the
                // typed, colored reference.
                if constexpr (std::is_same_v<Element, ::hx::Object>) {
                    return T(object);
                } else if constexpr (
                    std::is_base_of_v<::hx::EnumBase_obj, Element>) {
                    const auto* actual =
                        ::hx::gc::Runtime::instance().typeOf(object);
                    if constexpr (std::is_same_v<Element,
                                                  ::hx::EnumBase_obj>) {
                        if (actual == nullptr ||
                            (actual->flags & ::hx::gc::TypeEnum) == 0)
                            ::hx::throwBadCast();
                    } else if (actual != &::hx::enumDescriptor<Element>()) {
                        ::hx::throwBadCast();
                    }
                    return T(static_cast<Element*>(object));
                } else if constexpr (requires { Element::__novaType; }) {
                    if (!::hx::gc::Runtime::instance().isInstanceOf(
                            object, Element::__novaType))
                        ::hx::throwBadCast();
                    return T(static_cast<Element*>(object));
                } else if constexpr (
                    requires { Element::__novaInterfaceHash; }) {
                    ::hx::ScopedPin pin(object);
                    if (pin.get()->_hx_getInterface(
                            Element::__novaInterfaceHash) == nullptr)
                        ::hx::throwBadCast();
                    // Interface ObjectPtr aliases are ABI tokens rather than
                    // C++ base subobjects; generated interface dispatch goes
                    // through _hx_getInterface after recovering Object.
                    return T(reinterpret_cast<Element*>(pin.get()));
                } else if constexpr (requires { Element::descriptor; }) {
                    if (!::hx::gc::Runtime::instance().isInstanceOf(
                            object, Element::descriptor))
                        ::hx::throwBadCast();
                    return T(static_cast<Element*>(object));
                } else {
                    ::hx::throwBadCast();
                }
            }
        }
        ::hx::throwDynamicCast(tagName(tag_), typeid(T).name());
    }

    template<class T>
    [[nodiscard]] T StaticCast() const { return Cast<T>(); }

    template<class T>
    [[nodiscard]] operator ::hx::Null<T>() const {
        return isNull() ? ::hx::Null<T>() : ::hx::Null<T>(Cast<T>());
    }

    template<class T>
    [[nodiscard]] bool IsClass() const {
        // Generated catch dispatch uses IsClass<T>() for both scalar Haxe
        // values and managed references.  Treating every non-ObjectPtr as a
        // native/non-Haxe type made catch(Int), catch(Bool), catch(Float) and
        // catch(String) silently reject the value that had just been thrown.
        if constexpr (std::is_same_v<T, Dynamic>)
            return true;
        else if constexpr (std::is_same_v<T, String>)
            return tag_ == Tag::String;
        else if constexpr (std::is_same_v<T, bool>)
            return tag_ == Tag::Boolean;
        else if constexpr (std::is_integral_v<T>)
            return tag_ == Tag::Integer;
        else if constexpr (std::is_floating_point_v<T>)
            return tag_ == Tag::Floating || tag_ == Tag::Integer;
        else if constexpr (requires { typename T::element_type; }) {
            if (tag_ != Tag::Object)
                return false;
            using Element = typename T::element_type;
            auto& slot = const_cast<::hx::gc::RefSlot&>(mPtr);
            void* object = ::hx::gc::loadBarrierFast(slot);
            if constexpr (std::is_base_of_v<::hx::EnumBase_obj, Element>) {
                const auto* descriptor =
                    ::hx::gc::Runtime::instance().typeOf(object);
                if constexpr (std::is_same_v<Element,
                                              ::hx::EnumBase_obj>)
                    return descriptor != nullptr &&
                           (descriptor->flags & ::hx::gc::TypeEnum) != 0;
                else
                    return descriptor == &::hx::enumDescriptor<Element>();
            } else if constexpr (requires { Element::__novaType; }) {
                return ::hx::gc::Runtime::instance().isInstanceOf(
                    object, Element::__novaType);
            } else if constexpr (requires { Element::__novaInterfaceHash; }) {
                ::hx::ScopedPin pin(static_cast<::hx::Object*>(object));
                return pin.get() != nullptr &&
                       pin.get()->_hx_getInterface(
                           Element::__novaInterfaceHash) != nullptr;
            } else {
                return false;
            }
        } else
            return false;
    }

    template<class T>
        requires requires { typename T::element_type; }
    operator T() const { return Cast<T>(); }

    [[nodiscard]] bool operator==(std::nullptr_t) const noexcept {
        return isNull();
    }
    [[nodiscard]] bool operator!=(std::nullptr_t) const noexcept {
        return !isNull();
    }

    union {
        std::int64_t integer_ = 0;
        double floating_;
    };
    Tag tag_ = Tag::Null;
};

namespace hx {

// Owns one managed call-expression value and exposes its precise reference
// slot to the collector for the remainder of the enclosing C++ full
// expression.  Generated code uses this for completed call arguments: C++ is
// free to evaluate another argument (and therefore allocate/safepoint) before
// entering the callee, so rooting only the callee's formal parameters is too
// late.
template<class T>
class RootedValue final {
public:
    explicit RootedValue(T input)
        : value_(std::move(input)),
          rootSlots_{&value_.mPtr},
          rootScope_(rootSlots_, 1) {}

    RootedValue(const RootedValue&) = delete;
    RootedValue& operator=(const RootedValue&) = delete;
    RootedValue(RootedValue&&) = delete;
    RootedValue& operator=(RootedValue&&) = delete;

    [[nodiscard]] T& get() noexcept { return value_; }
    [[nodiscard]] const T& get() const noexcept { return value_; }

private:
    T value_;
    ::hx::gc::RefSlot* rootSlots_[1];
    ::hx::gc::RootScope rootScope_;
};

// An ObjectPtr receiver is different from an ordinary call argument.  C++ may
// resolve operator->() before it evaluates the remaining arguments.  Those
// arguments can allocate and collect, leaving the already-resolved machine
// pointer stale even though the ObjectPtr's precise root slot was healed.  Pin
// ObjectPtr values for the lifetime of the full-expression so both reachability
// and the raw receiver address remain valid until the callee has been entered.
template<class T>
class RootedValue<::hx::ObjectPtr<T>> final {
public:
    explicit RootedValue(::hx::ObjectPtr<T> input)
        : value_(std::move(input)),
          rootSlots_{&value_.mPtr},
          rootScope_(rootSlots_, 1),
          pin_(reinterpret_cast<::hx::Object*>(value_.get())) {}

    RootedValue(const RootedValue&) = delete;
    RootedValue& operator=(const RootedValue&) = delete;
    RootedValue(RootedValue&&) = delete;
    RootedValue& operator=(RootedValue&&) = delete;

    [[nodiscard]] ::hx::ObjectPtr<T>& get() noexcept { return value_; }
    [[nodiscard]] const ::hx::ObjectPtr<T>& get() const noexcept {
        return value_;
    }

private:
    ::hx::ObjectPtr<T> value_;
    ::hx::gc::RefSlot* rootSlots_[1];
    ::hx::gc::RootScope rootScope_;
    ::hx::ScopedPin pin_;
};

class Anon_obj final : public Object {
public:
    explicit Anon_obj(void* inputState = nullptr) noexcept
        : state(inputState) {}

    static const ::hx::gc::TypeDescriptor descriptor;
    static ::hx::ObjectPtr<Anon_obj> Create(int capacity);
    ::hx::ObjectPtr<Anon_obj> setFixed(int index, const String& name,
                                       const Dynamic& value);

    void* state = nullptr;
};

// Haxe compiler-injected PosInfos are ordinary anonymous managed objects.
// Keeping this a function, as upstream hxcpp does, makes every generated
// SourceInfo(...) expression participate in the same precise tracing and
// relocation contract as a user-created anonymous record.
[[nodiscard]] ::hx::ObjectPtr<Anon_obj> SourceInfo(
    const ::String& fileName, int lineNumber,
    const ::String& className, const ::String& methodName);

template<class T>
[[nodiscard]] Dynamic boxDynamic(const T& input);

template<class T0, class T1, class T2>
struct AnonStruct3_obj final {
    static ::hx::ObjectPtr<Anon_obj> Create(
            const ::String& name0, const T0& value0,
            const ::String& name1, const T1& value1,
            const ::String& name2, const T2& value2) {
        auto result = Anon_obj::Create(3);
        ::hx::gc::RefSlot* roots[]{&result.mPtr};
        ::hx::gc::RootScope rootScope(roots, 1);
        result->setFixed(0, name0, ::hx::boxDynamic(value0));
        result->setFixed(1, name1, ::hx::boxDynamic(value1));
        result->setFixed(2, name2, ::hx::boxDynamic(value2));
        return result;
    }
};

inline Object* DynamicPtr(const Dynamic& value) {
    return static_cast<Object*>(value.managedPointer());
}

template<class T>
inline Object* DynamicPtr(const ObjectPtr<T>& value) {
    return static_cast<Object*>(value.get());
}

inline Object* DynamicPtr(std::nullptr_t) { return nullptr; }

[[nodiscard]] inline int ObjectHash(const Dynamic& value) {
    if (value.isMemberFunction()) {
        void* owner = value.referencePointer();
        std::uint64_t identity = owner == nullptr ? 0 :
            ::hx::gc::Runtime::instance().identityOf(owner);
        identity ^= static_cast<std::uint64_t>(
            value.memberFunctionAddress()) + 0x9e3779b97f4a7c15ULL +
            (identity << 6U) + (identity >> 2U);
        identity ^= static_cast<std::uint64_t>(
            value.memberFunctionArity()) * 0x100000001b3ULL;
        return static_cast<int>((identity ^ (identity >> 32U)) &
                                0x7fffffffU);
    }
    void* object = value.managedPointer();
    if (object == nullptr)
        return 0;
    const std::uint64_t identity =
        ::hx::gc::Runtime::instance().identityOf(object);
    return static_cast<int>((identity ^ (identity >> 32U)) & 0x7fffffffU);
}

template<class T>
[[nodiscard]] inline int ObjectHash(const ObjectPtr<T>& value) {
    T* object = value.get();
    if (object == nullptr)
        return 0;
    const std::uint64_t identity =
        ::hx::gc::Runtime::instance().identityOf(object);
    return static_cast<int>((identity ^ (identity >> 32U)) & 0x7fffffffU);
}

} // namespace hx

namespace hx {

enum PropertyAccess {
    paccNever,
    paccDynamic,
    paccAlways,
};

template<class T>
[[nodiscard]] Dynamic boxDynamic(const T& input) {
    using Value = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<Value, Dynamic>)
        return input;
    else if constexpr (std::is_same_v<Value, bool>)
        return Dynamic(input);
    else if constexpr (std::is_integral_v<Value>)
        return Dynamic(input);
    else if constexpr (std::is_floating_point_v<Value>)
        return Dynamic(static_cast<double>(input));
    else
        return Dynamic(input);
}

template<class T>
[[nodiscard]] T unboxDynamic(const Dynamic& input) {
    using Value = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<Value, Dynamic>)
        return input;
    else if constexpr (std::is_same_v<Value, bool>)
        return input.Cast<bool>();
    else if constexpr (std::is_integral_v<Value>)
        return input.Cast<Value>();
    else if constexpr (std::is_floating_point_v<Value>)
        return static_cast<Value>(input.Cast<double>());
    else
        return input.Cast<Value>();
}

} // namespace hx

using Float = double;

static_assert(std::is_standard_layout_v<String>);
static_assert(offsetof(String, mPtr) == 0);
static_assert(std::is_standard_layout_v<Dynamic>);
static_assert(offsetof(Dynamic, mPtr) == 0);

namespace hx {
bool IsEq(const ::Dynamic& left, const ::Dynamic& right);
int DynamicCompare(const ::Dynamic& left, const ::Dynamic& right);

struct AnyCastProxy final {
    void (*pointer)() = nullptr;
    template<class T>
    [[nodiscard]] operator T() const noexcept {
        if constexpr (std::is_pointer_v<T>)
            return reinterpret_cast<T>(pointer);
        else
            return T(pointer);
    }
};

template<class FunctionPointer>
[[nodiscard]] AnyCastProxy AnyCast(FunctionPointer pointer) noexcept {
    return {reinterpret_cast<void (*)()>(pointer)};
}

// Implemented by the runtime so every Array<T> can expose the same dynamic
// iterator protocol without depending on a generated Haxe iterator class.
[[nodiscard]] ::Dynamic makeArrayIteratorFactory(::hx::Object* array);
[[nodiscard]] ::Dynamic makeArrayKeyValueIteratorFactory(
    ::hx::Object* array);
[[nodiscard]] ::Dynamic makeArrayIterator(::hx::Object* array);
[[nodiscard]] ::Dynamic makeArrayKeyValueIterator(::hx::Object* array);

// Array<T> is defined before hx/function.hpp because LocalFunc's public ABI
// itself mentions Array<Dynamic>.  Array's descriptor still has to create the
// same receiver-rooting member closures as generated classes, so make the
// factories visible at the Array template definition point.  Their complete
// definitions remain in hx/function.hpp.
#define HX_NOVAGC_FORWARD_MEMBER_FUNCTION_FACTORY(N) \
    template<class Owner, class Function> \
    ::Dynamic CreateMemberFunction##N(const char*, Owner*, Function);
HX_NOVAGC_FORWARD_MEMBER_FUNCTION_FACTORY(0)
HX_NOVAGC_FORWARD_MEMBER_FUNCTION_FACTORY(1)
HX_NOVAGC_FORWARD_MEMBER_FUNCTION_FACTORY(2)
HX_NOVAGC_FORWARD_MEMBER_FUNCTION_FACTORY(3)
HX_NOVAGC_FORWARD_MEMBER_FUNCTION_FACTORY(4)
#undef HX_NOVAGC_FORWARD_MEMBER_FUNCTION_FACTORY
// cpp.VirtualArray may refer to any concrete Array<T>. These helpers honor
// the concrete descriptor instead of assuming Dynamic-sized elements.
[[nodiscard]] ::Dynamic virtualArrayGet(::hx::Object* array, int index);
[[nodiscard]] ::Dynamic virtualArraySet(::hx::Object* array, int index,
                                        const ::Dynamic& value);
[[nodiscard]] int virtualArrayPush(::hx::Object* array,
                                   const ::Dynamic& value);

template<class T>
struct TCast final {
    [[nodiscard]] static T cast(::HxNullValue value) {
        return value.template StaticCast<T>();
    }

    [[nodiscard]] static T cast(const ::Dynamic& value) {
        return ::hx::unboxDynamic<T>(value);
    }

    // Do not let a native character pointer reach the Dynamic overload via
    // pointer-to-bool conversion.  Constructing a String directly also
    // preserves null and UTF-16 semantics without an intermediate scalar box.
    [[nodiscard]] static T cast(const char* value) {
        if constexpr (std::is_same_v<T, ::String>)
            return ::String(value);
        else
            return ::hx::unboxDynamic<T>(::Dynamic(::String(value)));
    }
    [[nodiscard]] static T cast(char* value) {
        return cast(static_cast<const char*>(value));
    }
    [[nodiscard]] static T cast(const char16_t* value) {
        if constexpr (std::is_same_v<T, ::String>)
            return ::String(value);
        else
            return ::hx::unboxDynamic<T>(::Dynamic(::String(value)));
    }
    [[nodiscard]] static T cast(char16_t* value) {
        return cast(static_cast<const char16_t*>(value));
    }
};
}

#include <hx/array.hpp>
#include <hx/enum.hpp>
#include <hx/function.hpp>

inline int Dynamic::_hx_getIndex() const {
    if (tag() != Tag::Object)
        throw std::runtime_error("enum index requested from non-enum Dynamic");
    auto* object = static_cast<::hx::Object*>(managedPointer());
    const auto* type = ::hx::gc::Runtime::instance().typeOf(object);
    if (object == nullptr || type == nullptr ||
        (type->flags & ::hx::gc::TypeEnum) == 0)
        throw std::runtime_error("enum index requested from non-enum Dynamic");
    ::hx::ScopedPin pin(object);
    return static_cast<::hx::EnumBase_obj*>(pin.get())->_hx_getIndex();
}

namespace hx {
template<int Hash>
[[nodiscard]] inline bool TIsInterface(::hx::Object* object) {
    if (object == nullptr)
        return false;
    ::hx::ScopedPin pin(object);
    return pin.get()->_hx_getInterface(Hash) != nullptr;
}

template<int Hash>
[[nodiscard]] inline bool TIsInterface(const ::hx::gc::RefSlot& slot) {
    auto& mutableSlot = const_cast<::hx::gc::RefSlot&>(slot);
    return TIsInterface<Hash>(static_cast<::hx::Object*>(
        ::hx::gc::loadBarrierFast(mutableSlot)));
}
} // namespace hx

inline int Dynamic::__length() const {
    return __Field(::String("length"), ::hx::paccDynamic).Cast<int>();
}

namespace hx {

// Original hxcpp never reinterprets Array<Dynamic> element storage as a
// differently typed Array<T>.  Its Array(const Dynamic&) constructor detects a
// non-identical backing array and converts each element through Dynamic.  The
// distinction is essential here because NovaGC String and Dynamic deliberately
// have different third metadata fields (literal_ versus Tag).  Treating one
// layout as the other turns Tag::String (5) into the invalid pointer 0x5.
template<class ArrayPtr>
[[nodiscard]] ArrayPtr castDynamicArray(const ::Dynamic& input) {
    using ArrayClass = typename ArrayPtr::element_type;
    using Destination = typename ArrayClass::nova_array_element;

    if (input.tag() != ::Dynamic::Tag::Object)
        throw std::runtime_error("invalid Haxe Dynamic array cast");

    ::hx::gc::RefSlot sourceRoot{
        input.mPtr.load(std::memory_order_acquire)};
    ::hx::gc::RefSlot* sourceRoots[]{&sourceRoot};
    ::hx::gc::RootScope sourceScope(sourceRoots, 1);
    auto& runtime = ::hx::gc::Runtime::instance();
    auto* source = static_cast<::hx::Object*>(
        ::hx::gc::loadBarrierFast(sourceRoot));
    const auto* sourceType = source == nullptr ? nullptr : runtime.typeOf(source);
    if (sourceType == nullptr ||
        (sourceType->flags & ::hx::gc::TypeArray) == 0)
        throw std::runtime_error("Haxe Dynamic object is not an Array");

    // cpp::VirtualArray is currently represented by Array<Dynamic>.  It is a
    // dynamic facade, not a request to change the backing element type: its
    // Array_obj<Dynamic> accessors inspect the concrete descriptor and route
    // reads/writes through virtualArrayGet/Set/Push.  Preserve the underlying
    // object so writes remain visible to the original typed array.
    if constexpr (std::is_same_v<Destination, ::Dynamic>)
        return ArrayPtr(static_cast<ArrayClass*>(source));

    if (sourceType == &ArrayClass::descriptor)
        return ArrayPtr(static_cast<ArrayClass*>(source));

    ::Dynamic dynamicSource(source);
    ::hx::gc::RefSlot* dynamicSourceRoots[]{&dynamicSource.mPtr};
    ::hx::gc::RootScope dynamicSourceScope(dynamicSourceRoots, 1);
    const int length = dynamicSource.__length();

    ArrayPtr result = ArrayClass::__new();
    ::hx::gc::RefSlot* resultRoots[]{&result.mPtr};
    ::hx::gc::RootScope resultScope(resultRoots, 1);
    for (int index = 0; index < length; ++index) {
        ::Dynamic boxed = dynamicSource.__GetItem(index);
        ::hx::gc::RefSlot* boxedRoots[]{&boxed.mPtr};
        ::hx::gc::RootScope boxedScope(boxedRoots, 1);
        Destination converted = ::hx::unboxDynamic<Destination>(boxed);
        result->push(converted);
    }
    return result;
}

} // namespace hx

inline int Dynamic::__ArgCount() const {
    if (isMemberFunction())
        return memberFunctionArity();
    return ::hx::resolveFunction(*this)->argCount();
}

inline void Dynamic::__SetThis(const Dynamic& owner) const {
    (void)owner;
}

inline Dynamic Dynamic::__Run(const ::Array<Dynamic>& arguments) const {
    if (!arguments)
        return invoke(nullptr, 0);
    std::vector<Dynamic> packed;
    packed.reserve(static_cast<std::size_t>(arguments->length));
    for (int index = 0; index < arguments->length; ++index)
        packed.push_back(arguments->__get(index));
    return invoke(packed.data(), static_cast<int>(packed.size()));
}

inline ::Dynamic _hx_reslove_virtual_array(
        const ::Array<::Dynamic>& array) {
    return ::Dynamic(array);
}

inline bool __hxcpp_enum_eq(const ::Dynamic& left, const ::Dynamic& right) {
    if (left.tag() != ::Dynamic::Tag::Object ||
        right.tag() != ::Dynamic::Tag::Object)
        return ::hx::IsEq(left, right);
    if (left.managedPointer() == right.managedPointer())
        return true;
    const auto* leftType = ::hx::gc::Runtime::instance().typeOf(
        left.managedPointer());
    const auto* rightType = ::hx::gc::Runtime::instance().typeOf(
        right.managedPointer());
    if (leftType == nullptr || rightType == nullptr ||
        (leftType->flags & ::hx::gc::TypeEnum) == 0 ||
        (rightType->flags & ::hx::gc::TypeEnum) == 0)
        return false;
    ::hx::ScopedPin leftPin(static_cast<::hx::Object*>(left.managedPointer()));
    ::hx::ScopedPin rightPin(static_cast<::hx::Object*>(right.managedPointer()));
    auto* leftEnum = static_cast<::hx::EnumBase_obj*>(leftPin.get());
    auto* rightEnum = static_cast<::hx::EnumBase_obj*>(rightPin.get());
    if (leftEnum->_hx_index != rightEnum->_hx_index ||
        leftEnum->_hx_tag != rightEnum->_hx_tag)
        return false;
    const int leftCount = leftEnum->_hx_args ? leftEnum->_hx_args->length : 0;
    const int rightCount = rightEnum->_hx_args ? rightEnum->_hx_args->length : 0;
    if (leftCount != rightCount)
        return false;
    for (int index = 0; index < leftCount; ++index) {
        const ::Dynamic leftArg = leftEnum->_hx_args->__get(index);
        const ::Dynamic rightArg = rightEnum->_hx_args->__get(index);
        const auto* leftArgType = leftArg.tag() == ::Dynamic::Tag::Object
            ? ::hx::gc::Runtime::instance().typeOf(leftArg.managedPointer())
            : nullptr;
        const auto* rightArgType = rightArg.tag() == ::Dynamic::Tag::Object
            ? ::hx::gc::Runtime::instance().typeOf(rightArg.managedPointer())
            : nullptr;
        const bool nestedEnums = leftArgType != nullptr && rightArgType != nullptr &&
            (leftArgType->flags & ::hx::gc::TypeEnum) != 0 &&
            (rightArgType->flags & ::hx::gc::TypeEnum) != 0;
        if (nestedEnums ? !__hxcpp_enum_eq(leftArg, rightArg)
                        : !::hx::IsEq(leftArg, rightArg))
            return false;
    }
    return true;
}

using Enum = ::hx::ObjectPtr<::hx::EnumBase_obj>;
namespace hx { using EnumBase = ::hx::ObjectPtr<::hx::EnumBase_obj>; }

inline int _hx_getEnumValueIndex(const ::hx::EnumBase& value) {
    if (!value)
        throw std::runtime_error("enum index requested from null");
    return value->_hx_getIndex();
}

namespace hx {
struct Resource final {
    const char* mName = nullptr;
    int mDataLength = 0;
    unsigned char* mData = nullptr;
};
Resource* GetResources();
}

::Array<::String> __hxcpp_resource_names();
::String __hxcpp_resource_string(::String name);
::Array<unsigned char> __hxcpp_resource_bytes(::String name);
void __trace(::Dynamic value, ::Dynamic infos);
void __hxcpp_bytes_of_string(::Array<unsigned char>& output,
                             const ::String& input);
void __hxcpp_string_of_bytes(const ::Array<unsigned char>& input,
                             ::String& output, int position, int length,
                             bool reference = false);

double __hxcpp_gc_mem_info(int kind);
int __hxcpp_gc_trace(::hx::Class sought, bool printInstances);
void __hxcpp_gc_do_not_kill(::Dynamic object);
::Dynamic __hxcpp_get_next_zombie();
void __hxcpp_enter_gc_free_zone();
void __hxcpp_exit_gc_free_zone();
void __hxcpp_set_minimum_free_space(int bytes);
void __hxcpp_set_target_free_space_percentage(int percentage);
void __hxcpp_set_minimum_working_memory(int bytes);
::Dynamic __hxcpp_semaphore_create(int initial);
void __hxcpp_semaphore_acquire(::Dynamic semaphore);
bool __hxcpp_semaphore_try_acquire(::Dynamic semaphore, double timeout);
void __hxcpp_semaphore_release(::Dynamic semaphore);
::Dynamic __hxcpp_condition_create();
void __hxcpp_condition_acquire(::Dynamic condition);
bool __hxcpp_condition_try_acquire(::Dynamic condition);
void __hxcpp_condition_release(::Dynamic condition);
void __hxcpp_condition_wait(::Dynamic condition);
bool __hxcpp_condition_timed_wait(::Dynamic condition, double timeout);
void __hxcpp_condition_signal(::Dynamic condition);
void __hxcpp_condition_broadcast(::Dynamic condition);
bool __hxcpp_is_current_thread(::hx::Object* thread);

::Dynamic __loadprim(::String library, ::String primitive, int arity);
int __hxcpp_unload_all_libraries();
void __hxcpp_push_dll_path(::String path);
::String __hxcpp_get_dll_extension();
::String __hxcpp_get_bin_dir();
void __hxcpp_print(::Dynamic value);
void __hxcpp_println(::Dynamic value);
void __hxcpp_set_float_format(::String format);
::Array<::String> __get_args();
[[noreturn]] void __hxcpp_exit(int code);
bool __hxcpp_same_closure(::Dynamic left, ::Dynamic right);
bool __hxcpp_anon_remove(::Dynamic object, ::String field);
::Dynamic __hxcpp_create_var_args(::Dynamic function);
void* __hxcpp_load_native_symbol(::String library, ::String symbol,
                                 bool quietFail);
void* __hxcpp_get_proc_address(::String library, ::String primitive,
                               bool ndllPrimitive, bool quietFail = false);
inline ::hx::AnyCastProxy __hxcpp_cast_get_proc_address(
        ::String library, ::String primitive, bool quietFail = false) {
    return ::hx::AnyCast(__hxcpp_get_proc_address(
        library, primitive, false, quietFail));
}
int __hxcpp_register_prim(const char* name, void* function);
void __hxcpp_run_dll(::String library, ::String primitive);

::Dynamic _hx_regexp_new_options(::String pattern, ::String options);
bool _hx_regexp_match(::Dynamic regexp, ::String subject,
                      int position, int length);
::String _hx_regexp_matched(::Dynamic regexp, int group);
::Dynamic _hx_regexp_matched_pos(::Dynamic regexp, int group);
int _hx_regexp_matched_num(::Dynamic regexp);

double __hxcpp_new_date(int year, int month, int day,
                        int hour, int minute, int second);
double __hxcpp_utc_date(int year, int month, int day,
                        int hour, int minute, int second);
int __hxcpp_get_hours(double seconds);
int __hxcpp_get_minutes(double seconds);
int __hxcpp_get_seconds(double seconds);
int __hxcpp_get_year(double seconds);
int __hxcpp_get_month(double seconds);
int __hxcpp_get_date(double seconds);
int __hxcpp_get_day(double seconds);
int __hxcpp_get_utc_hours(double seconds);
int __hxcpp_get_utc_minutes(double seconds);
int __hxcpp_get_utc_seconds(double seconds);
int __hxcpp_get_utc_year(double seconds);
int __hxcpp_get_utc_month(double seconds);
int __hxcpp_get_utc_date(double seconds);
int __hxcpp_get_utc_day(double seconds);
double __hxcpp_timezone_offset(double seconds);
int __hxcpp_is_dst(double seconds);
::String __hxcpp_to_string(double seconds);
::String __hxcpp_to_utc_string(double seconds);
double __hxcpp_from_utc(int year, int month, int day, int hour, int minute,
                        int second, int milliseconds);
double __hxcpp_date_now();

::Dynamic __hxcpp_weak_map_create();
void __hxcpp_weak_map_set(::Dynamic map, ::Dynamic key, ::Dynamic value);
::Dynamic __hxcpp_weak_map_get(::Dynamic map, ::Dynamic key);
bool __hxcpp_weak_map_exists(::Dynamic map, ::Dynamic key);
bool __hxcpp_weak_map_remove(::Dynamic map, ::Dynamic key);
::Array<::Dynamic> __hxcpp_weak_map_keys(::Dynamic map);
::Array<::Dynamic> __hxcpp_weak_map_values(::Dynamic map);
::String __hxcpp_weak_map_to_string(::Dynamic map);
void __hxcpp_weak_map_clear(::Dynamic map);

::String __hxcpp_char_array_to_utf8_string(const ::Array<int>& characters,
                                            int first = 0, int length = -1);
::String __hxcpp_char_bytes_to_utf8_string(::String bytes);
::String __hxcpp_utf8_string_to_char_bytes(::String utf8);
int _hx_utf8_decode_advance(char*& pointer);
int _hx_string_compare(::String left, ::String right);
int _hx_utf8_char_code_at(::String input, int index);
int _hx_utf8_length(::String input);
bool _hx_utf8_is_valid(::String input);
::String _hx_utf8_sub(::String input, int start, int length);
::Array<int> __hxcpp_utf8_string_to_char_array(::String& input);

using _hx_member_finalizer = void (::hx::Object::*)();
using _hx_alloc_finalizer = void (*)(void*);
void __hxcpp_add_member_finalizer(::hx::Object* object,
                                  _hx_member_finalizer callback, bool pin);
void __hxcpp_add_alloc_finalizer(void* allocation,
                                 _hx_alloc_finalizer callback, bool pin);
void _hx_set_finalizer(::Dynamic object, void (*callback)(::Dynamic));
void __hxcpp_set_finalizer(::Dynamic object, void* callback);
::hx::Object* __hxcpp_weak_ref_create(::Dynamic object);
::hx::Object* __hxcpp_weak_ref_get(::Dynamic reference);
unsigned int __hxcpp_obj_hash(::Dynamic object);
int __hxcpp_obj_id(::Dynamic object);
::hx::Object* __hxcpp_id_obj(int id);
bool __hxcpp_is_const_string(const ::String& string);
void __hxcpp_reachable(::hx::Object* object);
int __hxcpp_field_to_id(const char* name);
const ::String& __hxcpp_field_from_id(int id);
::Array<::String> __hxcpp_get_class_list();
::String __hxcpp_get_kind(::Dynamic value);
void __hxcpp_enum_force(::hx::EnumBase value, ::String name, int index);
double __hxcpp_drand();
double __hxcpp_time_stamp();
void __hxcpp_print_string(const ::String& value);
void __hxcpp_println_string(const ::String& value);
double __hxcpp_parse_substr_float(const ::String& value, int start,
                                  int length);
int __hxcpp_parse_substr_int(const ::String& value, int start = 0,
                             int length = -1);
void __hxcpp_align_set_float32(unsigned char* base, int address, float value);
float __hxcpp_align_get_float32(unsigned char* base, int address);
void __hxcpp_align_set_float64(unsigned char* base, int address, double value);
double __hxcpp_align_get_float64(unsigned char* base, int address);
void __hxcpp_reverse_endian(int& value);
void __hxcpp_check_overflow(int value);

extern unsigned char* __hxcpp_memory;
void __hxcpp_memory_clear();
void __hxcpp_memory_select(const ::Array<unsigned char>& bytes);
int __hxcpp_memory_get_byte(int address);
int __hxcpp_memory_get_i16(int address);
int __hxcpp_memory_get_i32(int address);
int __hxcpp_memory_get_ui16(int address);
int __hxcpp_memory_get_ui32(int address);
float __hxcpp_memory_get_float(int address);
double __hxcpp_memory_get_double(int address);
float __hxcpp_memory_get_f32(int address);
void __hxcpp_memory_set_byte(int address, int value);
void __hxcpp_memory_set_i16(int address, int value);
void __hxcpp_memory_set_i32(int address, int value);
void __hxcpp_memory_set_ui16(int address, int value);
void __hxcpp_memory_set_ui32(int address, int value);
void __hxcpp_memory_set_float(int address, float value);
void __hxcpp_memory_set_double(int address, double value);
void __hxcpp_memory_set_f32(int address, float value);

void __hxcpp_gc_minor();
void __hxcpp_gc_update();
int __hxcpp_get_minor_base_delta_bytes();
void __hxcpp_set_minor_base_delta_bytes(int value);
int __hxcpp_get_minor_gate_ms();
void __hxcpp_set_minor_gate_ms(int value);
int __hxcpp_get_minor_start_bytes();
void __hxcpp_set_minor_start_bytes(int value);
void __hxcpp_gc_large_refresh_enable(int value);
int __hxcpp_gc_get_large_refresh_enabled();
void __hxcpp_gc_set_threads(int parallel, int refine);
int __hxcpp_gc_get_parallel_threads();
int __hxcpp_gc_get_refine_threads();
void __hxcpp_gc_set_max_pause_ms(int value);
int __hxcpp_gc_get_max_pause_ms();
void __hxcpp_gc_aggressive_safepoint(int value);
int __hxcpp_gc_get_aggressive_safepoint();
void __hxcpp_gc_enable_parallel_ref_proc(int value);
int __hxcpp_gc_get_parallel_ref_proc_enabled();
void __hxcpp_execution_trace(int level);
void __hxcpp_on_line_changed(::hx::StackContext* context);
void __hxcpp_stdlibs_boot();
void __hxcpp_boot_std_classes();
int __hxcpp_gc_reserved_bytes();

::Array<::String> __hxcpp_get_call_stack(bool skipLast);
::Array<::String> __hxcpp_get_exception_stack();
void __hxcpp_stack_begin_catch() noexcept;
void __hx_stack_set_last_exception() noexcept;
void __hx_stack_push_last_exception() noexcept;
void __hxcpp_set_stack_frame_line(int line) noexcept;
int __hxcpp_GetCurrentThreadNumber();

// Default-build debugger/profiler/telemetry surface.  These declarations
// deliberately match hxcpp's non-HXCPP_DEBUGGER/non-HXCPP_PROFILER behavior;
// feature backends can replace them without changing generated Haxe ABI.
void __hxcpp_dbg_getScriptableFiles(::Array<::String> output);
void __hxcpp_dbg_getScriptableFilesFullPath(::Array<::String> output);
void __hxcpp_dbg_getScriptableClasses(::Array<::String> output);
void __hxcpp_dbg_getScriptableVariables(void* frame,
                                        ::Array<::Dynamic> output);
bool __hxcpp_dbg_getScriptableValue(void* frame, ::String name,
                                    ::Dynamic& output);
bool __hxcpp_dbg_setScriptableValue(void* frame, ::String name,
                                    ::Dynamic value);
void __hxcpp_dbg_setEventNotificationHandler(::Dynamic handler);
void __hxcpp_dbg_enableCurrentThreadDebugging(bool enabled);
int __hxcpp_dbg_getCurrentThreadNumber();
::Array<::String> __hxcpp_dbg_getFiles();
::Array<::String> __hxcpp_dbg_getFilesFullPath();
::Array<::String> __hxcpp_dbg_getClasses();
::Array<::Dynamic> __hxcpp_dbg_getThreadInfos();
::Dynamic __hxcpp_dbg_getThreadInfo(int threadNumber, bool unsafe);
int __hxcpp_dbg_addFileLineBreakpoint(::String fileName, int lineNumber);
int __hxcpp_dbg_addClassFunctionBreakpoint(::String className,
                                            ::String functionName);
void __hxcpp_dbg_deleteAllBreakpoints();
void __hxcpp_dbg_deleteBreakpoint(int number);
void __hxcpp_dbg_breakNow(bool wait);
void __hxcpp_dbg_continueThreads(int threadNumber, int count);
void __hxcpp_dbg_stepThread(int threadNumber, int stepType, int stepCount);
::Array<::Dynamic> __hxcpp_dbg_getStackVariables(
    int threadNumber, int stackFrameNumber, bool unsafe,
    ::Dynamic markThreadNotStopped);
::Dynamic __hxcpp_dbg_getStackVariableValue(
    int threadNumber, int stackFrameNumber, ::String name, bool unsafe,
    ::Dynamic markNonexistent, ::Dynamic markThreadNotStopped);
::Dynamic __hxcpp_dbg_setStackVariableValue(
    int threadNumber, int stackFrameNumber, ::String name, ::Dynamic value,
    bool unsafe, ::Dynamic markNonexistent,
    ::Dynamic markThreadNotStopped);
void __hxcpp_dbg_setNewParameterFunction(::Dynamic function);
void __hxcpp_dbg_setNewStackFrameFunction(::Dynamic function);
void __hxcpp_dbg_setNewThreadInfoFunction(::Dynamic function);
void __hxcpp_dbg_setAddParameterToStackFrameFunction(::Dynamic function);
void __hxcpp_dbg_setAddStackFrameToThreadInfoFunction(::Dynamic function);
void __hxcpp_dbg_setOnScriptLoadedFunction(::Dynamic function);
bool __hxcpp_dbg_fix_critical_error(::String error);
void __hxcpp_dbg_threadCreatedOrTerminated(int threadNumber, bool created);
::Dynamic __hxcpp_dbg_checkedThrow(::Dynamic value);
::Dynamic __hxcpp_dbg_checkedRethrow(::Dynamic value);

struct TelemetryFrame final {
    int gctime = 0;
    int gcoverhead = 0;
    std::vector<int>* samples = nullptr;
    std::vector<const char*>* names = nullptr;
    std::vector<int>* allocation_data = nullptr;
    std::vector<int>* stacks = nullptr;
};

int __hxcpp_hxt_start_telemetry(bool profiler, bool allocations);
void __hxcpp_hxt_stash_telemetry();
TelemetryFrame* __hxcpp_hxt_dump_telemetry(int threadNumber);
void __hxcpp_hxt_ignore_allocs(int delta);
void __hxcpp_set_hxt_finalizer(void* object, void* callback);
void __hxcpp_start_profiler(::String dumpFile);
void __hxcpp_stop_profiler();
void __hxcpp_spam_collects(int everyNCalls);
void __hxcpp_tracy_framemark();
void __hxcpp_tracy_plot(::String name, ::Float value);
void __hxcpp_tracy_plot_config(::String name, std::uint8_t format,
                               bool step, bool fill, int color);
void __hxcpp_tracy_message(::String message, int color);
void __hxcpp_tracy_message_app_info(::String information);
void __hxcpp_tracy_set_thread_name_and_group(::String name, int groupHint);
int __hxcpp_tracy_get_zone_count();

namespace hx {
[[noreturn]] ::Dynamic Throw(::Dynamic value);
[[noreturn]] ::Dynamic Rethrow(::Dynamic value);
}

namespace haxe {
class NativeStackTrace_obj final {
public:
    static ::Array<::String> callStack() {
        return ::__hxcpp_get_call_stack(true);
    }
    static ::Array<::String> exceptionStack() {
        return ::__hxcpp_get_exception_stack();
    }
    static ::Array<::Dynamic> toHaxe(const ::Array<::String>&,
                                     ::Dynamic = nullptr) {
        return ::Array_obj<::Dynamic>::__new();
    }
};
}

namespace hx::cffi {

// External hxcpp NDLLs were compiled with the Prime HxString ABI rather than
// the managed String wrapper used by this runtime.  Keep this layout exact:
// changing the field order or width would silently corrupt every native
// primitive that accepts or returns a String.
struct LegacyPrimeString final {
    int length = 0;
    const char* data = nullptr;
};

// Defined in cffi.cpp.  The opaque implementation owns a CFFI CallScope so
// every temporary value token and pin remains alive until the native call and
// its result conversion have both completed.
class ExternalPrimeCallScope final {
public:
    ExternalPrimeCallScope();
    ~ExternalPrimeCallScope() noexcept(false);
    ExternalPrimeCallScope(const ExternalPrimeCallScope&) = delete;
    ExternalPrimeCallScope& operator=(const ExternalPrimeCallScope&) = delete;

    // ABI adapters run while the current thread is still an active mutator so
    // managed arguments can be published and scoped-pinned without repeatedly
    // leaving/re-entering native-blocking state. Only the third-party call is
    // native-blocked; result conversion resumes managed participation first.
    template<class Function, class... NativeArguments>
    decltype(auto) invoke(Function&& function,
                          NativeArguments&&... arguments) {
        using Result = std::invoke_result_t<Function, NativeArguments...>;
        enterNativeBlocking();
        try {
            if constexpr (std::is_void_v<Result>) {
                std::forward<Function>(function)(
                    std::forward<NativeArguments>(arguments)...);
                leaveNativeBlocking();
                return;
            } else {
                Result result = std::forward<Function>(function)(
                    std::forward<NativeArguments>(arguments)...);
                leaveNativeBlocking();
                return result;
            }
        } catch (...) {
            leaveNativeBlocking();
            throw;
        }
    }

private:
    void enterNativeBlocking();
    void leaveNativeBlocking();
    void* state_ = nullptr;
};

void registerExternalPrime(void* address);
void clearExternalPrimes();
[[nodiscard]] bool isExternalPrime(void* address);
[[nodiscard]] void* primeDynamicArgument(const ::Dynamic& input);
[[nodiscard]] void* primeObjectArgument(::hx::Object* input);
[[nodiscard]] LegacyPrimeString primeStringArgument(const ::String& input);
[[nodiscard]] ::Dynamic primeDynamicResult(void* input);
[[nodiscard]] ::hx::Object* primeObjectResult(void* input);
[[nodiscard]] ::String primeStringResult(LegacyPrimeString input);

} // namespace hx::cffi

// Haxe's haxe.atomic abstracts are represented as pointers to aligned Int
// storage.  atomic_ref keeps that public representation while providing real
// sequentially-consistent C++ atomics on every supported compiler.
[[nodiscard]] inline std::atomic_ref<int> _hx_atomic_ref(
        volatile int* address) {
    if (address == nullptr)
        throw std::invalid_argument("null haxe.atomic address");
    return std::atomic_ref<int>(*const_cast<int*>(address));
}
inline int _hx_atomic_add(volatile int* address, int value) {
    return _hx_atomic_ref(address).fetch_add(value);
}
inline int _hx_atomic_sub(volatile int* address, int value) {
    return _hx_atomic_ref(address).fetch_sub(value);
}
inline int _hx_atomic_and(volatile int* address, int value) {
    return _hx_atomic_ref(address).fetch_and(value);
}
inline int _hx_atomic_or(volatile int* address, int value) {
    return _hx_atomic_ref(address).fetch_or(value);
}
inline int _hx_atomic_xor(volatile int* address, int value) {
    return _hx_atomic_ref(address).fetch_xor(value);
}
inline int _hx_atomic_compare_exchange(volatile int* address, int expected,
                                       int replacement) {
    _hx_atomic_ref(address).compare_exchange_strong(expected, replacement);
    return expected;
}
inline int _hx_atomic_exchange(volatile int* address, int replacement) {
    return _hx_atomic_ref(address).exchange(replacement);
}
inline int _hx_atomic_load(volatile int* address) {
    return _hx_atomic_ref(address).load();
}
inline int _hx_atomic_store(volatile int* address, int value) {
    _hx_atomic_ref(address).store(value);
    return value;
}
inline void* _hx_atomic_compare_exchange_ptr(volatile void** address,
                                              void* expected,
                                              void* replacement) {
    if (address == nullptr)
        throw std::invalid_argument("null atomic pointer address");
    auto& storage = *const_cast<void**>(address);
    std::atomic_ref<void*> reference(storage);
    reference.compare_exchange_strong(expected, replacement);
    return expected;
}
inline void* _hx_atomic_compare_exchange_cast_ptr(void* address,
                                                   void* expected,
                                                   void* replacement) {
    return _hx_atomic_compare_exchange_ptr(
        reinterpret_cast<volatile void**>(address), expected, replacement);
}

namespace cpp {
using VirtualArray_obj = ::Array_obj<::Dynamic>;
using VirtualArray = ::Array<::Dynamic>;
using Int64 = std::int64_t;
using UInt64 = std::uint64_t;
using Float32 = float;
using Float64 = double;
using Int16 = std::int16_t;
using UInt16 = std::uint16_t;
using Int8 = std::int8_t;
using UInt8 = std::uint8_t;

template<class T>
class Pointer final {
public:
    struct ReinterpretProxy final {
        void* value = nullptr;

        template<class U>
        [[nodiscard]] operator Pointer<U>() const noexcept {
            return Pointer<U>(reinterpret_cast<U*>(value));
        }
    };

    T* ptr = nullptr;
    ::hx::gc::RefSlot mPtr{0};

    Pointer() noexcept = default;
    Pointer(std::nullptr_t) noexcept {}
    Pointer(::HxNullValue) noexcept {}
    explicit Pointer(T* value) noexcept : ptr(value) {}
    Pointer(const ::Dynamic& value)
        : ptr(reinterpret_cast<T*>(value.nativePointer())) {}
    template<class U>
    explicit Pointer(U* value) noexcept : ptr(reinterpret_cast<T*>(value)) {}

    [[nodiscard]] T* get_raw() const noexcept { return ptr; }
    [[nodiscard]] const T* get_constRaw() const noexcept { return ptr; }
    [[nodiscard]] Pointer* operator->() noexcept { return this; }
    [[nodiscard]] const Pointer* operator->() const noexcept { return this; }
    [[nodiscard]] T& operator[](std::ptrdiff_t index) const { return ptr[index]; }
    [[nodiscard]] explicit operator bool() const noexcept { return ptr != nullptr; }
    [[nodiscard]] operator T*() const noexcept { return ptr; }
    [[nodiscard]] operator ::Dynamic() const {
        return ::Dynamic::fromNativePointer(ptr);
    }
    [[nodiscard]] Pointer add(std::ptrdiff_t offset) const noexcept {
        return Pointer(ptr == nullptr ? nullptr : ptr + offset);
    }
    [[nodiscard]] Pointer sub(std::ptrdiff_t offset) const noexcept {
        return Pointer(ptr == nullptr ? nullptr : ptr - offset);
    }
    [[nodiscard]] Pointer inc() const noexcept { return add(1); }
    [[nodiscard]] Pointer dec() const noexcept { return sub(1); }
    [[nodiscard]] Pointer incBy(std::ptrdiff_t offset) const noexcept {
        return add(offset);
    }
    [[nodiscard]] Pointer decBy(std::ptrdiff_t offset) const noexcept {
        return sub(offset);
    }
    [[nodiscard]] ReinterpretProxy reinterpret() const noexcept {
        return {const_cast<void*>(static_cast<const void*>(ptr))};
    }
    [[nodiscard]] ReinterpretProxy rawCast() const noexcept {
        return reinterpret();
    }
    template<class U>
    void setRaw(U* value) noexcept {
        ptr = reinterpret_cast<T*>(value);
    }
    [[nodiscard]] T& at(int index) const { return ptr[index]; }
    void setAt(int index, const T& value) const { ptr[index] = value; }
    [[nodiscard]] T& __get(int index) const { return ptr[index]; }
    T& __set(int index, const T& value) const {
        ptr[index] = value;
        return ptr[index];
    }
    [[nodiscard]] T& get_value() const { return *ptr; }
    [[nodiscard]] T& get_ref() const { return *ptr; }
    T& set_ref(const T& value) const {
        *ptr = value;
        return *ptr;
    }
    void destroy() const { delete ptr; }
    void destroyArray() const { delete[] ptr; }
    [[nodiscard]] bool lt(const Pointer& other) const noexcept {
        return ptr < other.ptr;
    }
    [[nodiscard]] bool leq(const Pointer& other) const noexcept {
        return ptr <= other.ptr;
    }
    [[nodiscard]] bool gt(const Pointer& other) const noexcept {
        return ptr > other.ptr;
    }
    [[nodiscard]] bool geq(const Pointer& other) const noexcept {
        return ptr >= other.ptr;
    }
    Pointer& operator=(T* value) noexcept { ptr = value; return *this; }
    Pointer& operator=(::HxNullValue) noexcept { ptr = nullptr; return *this; }
    [[nodiscard]] bool operator==(std::nullptr_t) const noexcept {
        return ptr == nullptr;
    }
    [[nodiscard]] bool operator!=(std::nullptr_t) const noexcept {
        return ptr != nullptr;
    }
};

struct Pointer_obj final {
    template<class T>
    [[nodiscard]] static Pointer<std::remove_const_t<T>> fromRaw(
            T* value) noexcept {
        using Mutable = std::remove_const_t<T>;
        return Pointer<Mutable>(const_cast<Mutable*>(value));
    }
    template<class T>
    [[nodiscard]] static Pointer<std::remove_const_t<T>> fromPointer(
            T* value) noexcept {
        using Mutable = std::remove_const_t<T>;
        return Pointer<Mutable>(const_cast<Mutable*>(value));
    }
    template<class T>
    [[nodiscard]] static Pointer<T> addressOf(T& value) noexcept {
        return Pointer<T>(&value);
    }
    template<class T>
    [[nodiscard]] static Pointer<T> ofArray(const ::Array<T>& value) noexcept {
        return arrayElem(value, 0);
    }
    template<class T>
    [[nodiscard]] static Pointer<T> arrayElem(const ::Array<T>& value,
                                              int index) noexcept {
        if (!value || index < 0 || index > value->length)
            return Pointer<T>(nullptr);
        auto* array = value.get();
        void* buffer = ::hx::gc::loadBarrierFast(array->buffer);
        if (buffer == nullptr)
            return Pointer<T>(nullptr);

        // cpp.NativeArray.setUnmanagedData stores a descriptor containing the
        // producer-owned pointer, not inline ArrayBuffer elements. Lime uses
        // this representation for decoded image pixels. Treating the
        // descriptor itself as an ArrayBuffer returned descriptor+8 and made
        // OpenGL read past a tiny GC object as if it were a complete texture.
        if (array->capacity < 0) {
            auto* external = static_cast<
                ::hx::detail::ExternalArrayBuffer<T>*>(buffer);
            return Pointer<T>(external->elements == nullptr
                ? nullptr : external->elements + index);
        }

        // Scalar buffers are allocated in address-stable TypePinned regions
        // from birth. A permanent late pin here is unnecessary and, for a
        // large StringBuf Array<cpp.Char>, would stop the world and remap the
        // complete heap merely to copy the finished string. Reference-element
        // arrays remain movable and retain the conservative escaping-pointer
        // behavior.
        if constexpr (::hx::detail::ArrayReferenceTraits<T>::Managed)
            buffer = ::hx::gc::Runtime::instance().pinForNative(buffer);
        auto* typedBuffer = static_cast<::hx::detail::ArrayBuffer<T>*>(buffer);
        return Pointer<T>(::hx::detail::arrayElements(typedBuffer) + index);
    }
};

template<class Signature, class ABI = void>
class Function;

namespace detail {

template<class T, class Enable = void>
struct ExternalPrimeAbi final {
    using Native = T;

    [[nodiscard]] static Native toNative(T input) noexcept {
        return input;
    }
    [[nodiscard]] static T fromNative(Native input) noexcept {
        return input;
    }
};

template<>
struct ExternalPrimeAbi<void, void> final {
    using Native = void;
};

template<>
struct ExternalPrimeAbi<::Dynamic, void> final {
    using Native = void*;

    [[nodiscard]] static Native toNative(const ::Dynamic& input) {
        return ::hx::cffi::primeDynamicArgument(input);
    }
    [[nodiscard]] static ::Dynamic fromNative(Native input) {
        return ::hx::cffi::primeDynamicResult(input);
    }
};

template<>
struct ExternalPrimeAbi<::String, void> final {
    using Native = ::hx::cffi::LegacyPrimeString;

    [[nodiscard]] static Native toNative(const ::String& input) {
        return ::hx::cffi::primeStringArgument(input);
    }
    [[nodiscard]] static ::String fromNative(Native input) {
        return ::hx::cffi::primeStringResult(input);
    }
};

template<class T>
struct ExternalPrimeAbi<T*, std::enable_if_t<
        ::hx::IsCompleteManagedObject<std::remove_cv_t<T>>::value>> final {
    using Native = void*;

    [[nodiscard]] static Native toNative(T* input) {
        auto* object = const_cast<::hx::Object*>(
            static_cast<const ::hx::Object*>(input));
        return ::hx::cffi::primeObjectArgument(object);
    }
    [[nodiscard]] static T* fromNative(Native input) {
        return static_cast<T*>(::hx::cffi::primeObjectResult(input));
    }
};

template<class T>
using ExternalPrimeAbiFor = ExternalPrimeAbi<std::remove_cvref_t<T>>;

template<class Return, class... Arguments>
using ExternalPrimePointer =
    typename ExternalPrimeAbiFor<Return>::Native (*)(
        typename ExternalPrimeAbiFor<Arguments>::Native...);

} // namespace detail

template<class Return, class... Arguments, class ABI>
class Function<Return(Arguments...), ABI> final {
public:
    using PointerType = Return (*)(Arguments...);
    PointerType pointer = nullptr;

    Function() noexcept = default;
    Function(std::nullptr_t) noexcept {}
    Function(::HxNullValue) noexcept {}
    Function(PointerType value) noexcept : pointer(value) {}
    Function(const ::Dynamic& value)
        : pointer(value.isNull() ? nullptr :
              reinterpret_cast<PointerType>(value.nativePointer())),
          externalPrime_(pointer != nullptr && ::hx::cffi::isExternalPrime(
              reinterpret_cast<void*>(pointer))) {}
    explicit Function(void (*value)()) noexcept
        requires (!std::is_same_v<PointerType, void (*)()>)
        : pointer(reinterpret_cast<PointerType>(value)) {}
    Function& operator=(const ::Dynamic& value) {
        pointer = value.isNull() ? nullptr :
            reinterpret_cast<PointerType>(value.nativePointer());
        externalPrime_ = pointer != nullptr && ::hx::cffi::isExternalPrime(
            reinterpret_cast<void*>(pointer));
        return *this;
    }
    Function& operator=(PointerType value) noexcept {
        pointer = value;
        externalPrime_ = false;
        return *this;
    }
    Function& operator=(::HxNullValue) noexcept {
        pointer = nullptr;
        externalPrime_ = false;
        return *this;
    }
    [[nodiscard]] operator PointerType() const noexcept { return pointer; }
    [[nodiscard]] operator ::Dynamic() const noexcept {
        return ::Dynamic::fromNativePointer(
            reinterpret_cast<void*>(pointer));
    }
    Return operator()(Arguments... arguments) const {
        if (pointer == nullptr)
            throw std::runtime_error("attempted to call a null cpp.Function");

        if (!externalPrime_) {
            if constexpr (std::is_void_v<Return>) {
                pointer(std::forward<Arguments>(arguments)...);
                return;
            } else {
                return pointer(std::forward<Arguments>(arguments)...);
            }
        }

        ::hx::cffi::ExternalPrimeCallScope callScope;
        using ExternalPointer =
            ::cpp::detail::ExternalPrimePointer<Return, Arguments...>;
        static_assert(sizeof(ExternalPointer) == sizeof(PointerType));
        auto external = std::bit_cast<ExternalPointer>(pointer);
        if constexpr (std::is_void_v<Return>) {
            callScope.invoke(
                external,
                ::cpp::detail::ExternalPrimeAbiFor<Arguments>::toNative(
                    std::forward<Arguments>(arguments))...);
            return;
        } else {
            auto result = callScope.invoke(
                external,
                ::cpp::detail::ExternalPrimeAbiFor<Arguments>::toNative(
                    std::forward<Arguments>(arguments))...);
            return ::cpp::detail::ExternalPrimeAbiFor<Return>::fromNative(
                result);
        }
    }

private:
    bool externalPrime_ = false;
};

template<class T, class H = void>
class Struct final {
public:
    T value;

    Struct() requires std::default_initializable<T> = default;
    Struct(std::nullptr_t) requires std::default_initializable<T>
        : value{} {}
    Struct(::HxNullValue) requires std::default_initializable<T>
        : value{} {}
    Struct(const T& input) : value(input) {}
    Struct(T&& input) noexcept(std::is_nothrow_move_constructible_v<T>)
        : value(std::move(input)) {}

    Struct& operator=(const T& input) {
        value = input;
        return *this;
    }
    Struct& operator=(T&& input) noexcept(
            std::is_nothrow_move_assignable_v<T>) {
        value = std::move(input);
        return *this;
    }
    Struct& operator=(::HxNullValue) requires std::default_initializable<T> {
        value = T{};
        return *this;
    }

    [[nodiscard]] T* operator->() noexcept { return &value; }
    [[nodiscard]] const T* operator->() const noexcept { return &value; }
    [[nodiscard]] T& operator*() noexcept { return value; }
    [[nodiscard]] const T& operator*() const noexcept { return value; }
    [[nodiscard]] T* get_raw() noexcept { return &value; }
    [[nodiscard]] const T* get_constRaw() const noexcept { return &value; }
    [[nodiscard]] T& get() noexcept { return value; }
    [[nodiscard]] const T& get() const noexcept { return value; }
    [[nodiscard]] explicit operator bool() const noexcept { return true; }
    [[nodiscard]] operator T&() noexcept { return value; }
    [[nodiscard]] operator const T&() const noexcept { return value; }
    [[nodiscard]] bool operator==(const Struct& other) const
        requires requires(const T& left, const T& right) { left == right; }
    {
        return value == other.value;
    }
    [[nodiscard]] bool operator!=(const Struct& other) const
        requires requires(const T& left, const T& right) { left != right; }
    {
        return value != other.value;
    }
    [[nodiscard]] constexpr bool operator==(::HxNullValue) const noexcept {
        return false;
    }
    [[nodiscard]] constexpr bool operator!=(::HxNullValue) const noexcept {
        return true;
    }
};

using ArrayBase = ::Array_obj<unsigned char>*;
} // namespace cpp

#include <hx/class.hpp>

inline Dynamic hx::Object::__Field(const String& name, int access) {
    return Dynamic(this).__Field(name, access);
}

inline Dynamic hx::Object::__SetField(const String& name,
                                      const Dynamic& value, int access) {
    return Dynamic(this).__SetField(name, value, access);
}

inline String operator+(const String& left, const Dynamic& right) {
    return left + right.toString();
}

inline String operator+(const Dynamic& left, const String& right) {
    return left.toString() + right;
}

inline String operator+(::HxNullValue, const String& right) {
    return ::String("null") + right;
}

inline String operator+(const String& left, ::HxNullValue) {
    return left + ::String("null");
}

inline bool Dynamic::__IsArray() const noexcept {
    if (tag_ != Tag::Object)
        return false;
    try {
        const auto* type = ::hx::gc::Runtime::instance().typeOf(
            const_cast<Dynamic*>(this)->managedPointer());
        return type != nullptr && (type->flags & ::hx::gc::TypeArray) != 0;
    } catch (...) {
        return false;
    }
}

template<class T>
template<class U>
    requires (requires { typename T::nova_array_element; } &&
              requires { typename U::nova_array_element; })
hx::ObjectPtr<T>::operator hx::ObjectPtr<U>() const {
    using Destination = typename U::nova_array_element;
    // cpp.VirtualArray is an ABI view of the original concrete Array<T>, not
    // a converted copy.  Its Array<Dynamic> methods inspect the allocation's
    // real descriptor and dispatch element operations through the typed
    // callbacks.  Preserving identity here is required for abstract methods
    // such as MyVector.push and haxe.ds.ArraySort to mutate their caller.
    if constexpr (std::is_same_v<Destination, ::Dynamic>) {
        ::hx::ObjectPtr<U> result;
        result.mPtr.store(mPtr.load(std::memory_order_acquire),
                          std::memory_order_release);
        return result;
    }
    ::hx::ObjectPtr<U> result = U::__new();
    ::hx::gc::RefSlot* roots[]{&result.mPtr};
    ::hx::gc::RootScope rootScope(roots, 1);
    T* source = get();
    if (source == nullptr)
        return nullptr;
    for (int index = 0; index < source->get_length(); ++index) {
        result->push(::hx::unboxDynamic<Destination>(
            ::hx::boxDynamic(source->__get(index))));
    }
    return result;
}

namespace hx {

class FieldRef final {
public:
    FieldRef(::hx::gc::RefSlot& owner, const ::String& name)
        : owner_(owner), name_(name) {}

    int operator++(int) { return update(1, false); }
    int operator--(int) { return update(-1, false); }
    int operator++() { return update(1, true); }
    int operator--() { return update(-1, true); }

private:
    int update(int delta, bool returnUpdated) {
        auto* object = static_cast<::hx::Object*>(
            ::hx::gc::loadBarrierFast(owner_));
        if (object == nullptr)
            throw std::runtime_error("dynamic field update on null object");
        ::Dynamic target(object);
        const int previous = target.__Field(name_, ::hx::paccDynamic).Cast<int>();
        const int updated = previous + delta;
        (void)target.__SetField(name_, ::Dynamic(updated), ::hx::paccDynamic);
        return returnUpdated ? updated : previous;
    }

    ::hx::gc::RefSlot& owner_;
    ::String name_;
};

class IndexRef final {
public:
    IndexRef(::hx::gc::RefSlot& owner, int index)
        : owner_(owner), index_(index) {}

    int operator++(int) { return update(1, false); }
    int operator--(int) { return update(-1, false); }
    int operator++() { return update(1, true); }
    int operator--() { return update(-1, true); }

private:
    int update(int delta, bool returnUpdated) {
        auto* object = static_cast<::hx::Object*>(
            ::hx::gc::loadBarrierFast(owner_));
        if (object == nullptr)
            throw std::runtime_error("dynamic index update on null object");
        ::Dynamic target(object);
        const int previous = target.__GetItem(index_).Cast<int>();
        const int updated = previous + delta;
        (void)target.__SetItem(index_, ::Dynamic(updated));
        return returnUpdated ? updated : previous;
    }

    ::hx::gc::RefSlot& owner_;
    int index_;
};

// Match hxcpp's CompareTraits null semantics: native scalar values are never
// null.  This overload is also required by optimized/inlined Haxe code where a
// nullable formal parameter has already been specialized to a native scalar.
template<class T>
    requires std::is_arithmetic_v<T>
inline bool IsNull(T) noexcept { return false; }

inline bool IsNull(const ::Dynamic& value) noexcept { return value.isNull(); }
inline bool IsNull(const ::String& value) noexcept { return value.isNull(); }
inline bool IsNull(std::nullptr_t) noexcept { return true; }
template<class T>
inline bool IsNull(const ::hx::ObjectPtr<T>& value) { return !value; }
template<class T>
inline bool IsNull(const ::hx::Null<T>& value) noexcept { return value.isNull(); }
template<class T>
inline bool IsNull(T* value) noexcept { return value == nullptr; }
// cpp.Struct is an inline value wrapper.  Its all-zero/default state may be
// meaningful native data, but it is never Haxe null.  Original hxcpp reaches
// this result through CompareTraits and Struct::operator==(null); keep the
// same semantics without allocating a temporary boxed Dynamic.
template<class T, class Handler>
inline bool IsNull(const ::cpp::Struct<T, Handler>&) noexcept { return false; }
template<class T>
    requires requires(const T& value) { value.get_raw(); }
inline bool IsNull(const T& value) noexcept { return value.get_raw() == nullptr; }

template<class T>
inline bool IsNotNull(const T& value) { return !IsNull(value); }

inline int Mod(int left, int right) { return left % right; }
template<class Left, class Right>
    requires (std::is_integral_v<Left> && std::is_integral_v<Right>)
inline auto Mod(Left left, Right right) {
    using Result = std::common_type_t<Left, Right>;
    return static_cast<Result>(left) % static_cast<Result>(right);
}
inline double Mod(double left, double right) { return std::fmod(left, right); }
inline double Mod(const ::Dynamic& left, const ::Dynamic& right) {
    return std::fmod(left.Cast<double>(), right.Cast<double>());
}
inline double Mod(const ::Dynamic& left, double right) {
    return std::fmod(left.Cast<double>(), right);
}
inline double Mod(double left, const ::Dynamic& right) {
    return std::fmod(left, right.Cast<double>());
}
template<class Integer>
    requires std::is_integral_v<Integer>
inline double Mod(const ::Dynamic& left, Integer right) {
    return std::fmod(left.Cast<double>(), static_cast<double>(right));
}
template<class Integer>
    requires std::is_integral_v<Integer>
inline double Mod(Integer left, const ::Dynamic& right) {
    return std::fmod(static_cast<double>(left), right.Cast<double>());
}
template<class Left, class Right>
    requires ((std::is_floating_point_v<Left> ||
               std::is_floating_point_v<Right>) &&
              std::is_arithmetic_v<Left> && std::is_arithmetic_v<Right>)
inline double Mod(Left left, Right right) {
    return std::fmod(static_cast<double>(left), static_cast<double>(right));
}
inline double DivByZero(double numerator) {
    // Keep the divide observable: MSVC and MinGW must both produce the IEEE
    // infinity/NaN result expected by Haxe instead of folding this into an
    // integer-style divide-by-zero trap.
    volatile double zero = 0.0;
    return numerator / zero;
}
inline int UShr(int value, int bits) {
    return static_cast<int>(static_cast<std::uint32_t>(value) >>
                            static_cast<unsigned>(bits & 31));
}

template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool IsGreater(const ::Dynamic& left, Number right) {
    return left.Cast<double>() > static_cast<double>(right);
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool IsGreaterEq(const ::Dynamic& left, Number right) {
    return left.Cast<double>() >= static_cast<double>(right);
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool IsLess(const ::Dynamic& left, Number right) {
    return left.Cast<double>() < static_cast<double>(right);
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool IsLessEq(const ::Dynamic& left, Number right) {
    return left.Cast<double>() <= static_cast<double>(right);
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool IsGreater(Number left, const ::Dynamic& right) {
    return static_cast<double>(left) > right.Cast<double>();
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool IsGreaterEq(Number left, const ::Dynamic& right) {
    return static_cast<double>(left) >= right.Cast<double>();
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool IsLess(Number left, const ::Dynamic& right) {
    return static_cast<double>(left) < right.Cast<double>();
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool IsLessEq(Number left, const ::Dynamic& right) {
    return static_cast<double>(left) <= right.Cast<double>();
}

template<class Left, class Right>
    requires (std::is_arithmetic_v<Left> && std::is_arithmetic_v<Right>)
inline bool IsGreater(Left left, Right right) {
    return left > right;
}
template<class Left, class Right>
    requires (std::is_arithmetic_v<Left> && std::is_arithmetic_v<Right>)
inline bool IsGreaterEq(Left left, Right right) {
    return left >= right;
}
template<class Left, class Right>
    requires (std::is_arithmetic_v<Left> && std::is_arithmetic_v<Right>)
inline bool IsLess(Left left, Right right) {
    return left < right;
}
template<class Left, class Right>
    requires (std::is_arithmetic_v<Left> && std::is_arithmetic_v<Right>)
inline bool IsLessEq(Left left, Right right) {
    return left <= right;
}

inline bool IsPointerEq(const ::Dynamic& left, const ::Dynamic& right) {
    if ((left.tag() == ::Dynamic::Tag::Object ||
         left.tag() == ::Dynamic::Tag::String) &&
        (right.tag() == ::Dynamic::Tag::Object ||
         right.tag() == ::Dynamic::Tag::String))
        return left.managedPointer() == right.managedPointer();
    if (left.tag() == ::Dynamic::Tag::NativePointer &&
        right.tag() == ::Dynamic::Tag::NativePointer)
        return left.nativePointer() == right.nativePointer();
    return ::hx::IsEq(left, right);
}

template<class Left, class Right>
inline bool IsPointerEq(const Left& left, const Right& right) {
    return left == right;
}

template<class T>
inline bool IsPointerEq(const ::hx::ObjectPtr<T>& left,
                        const ::Dynamic& right) {
    return right.tag() == ::Dynamic::Tag::Object &&
           left.get() == right.managedPointer();
}

template<class T>
inline bool IsPointerEq(const ::Dynamic& left,
                        const ::hx::ObjectPtr<T>& right) {
    return IsPointerEq(right, left);
}

template<class Left, class Right>
inline bool IsPointerNotEq(const Left& left, const Right& right) {
    return !IsPointerEq(left, right);
}

inline bool IsInstanceEq(const ::Dynamic& left, const ::Dynamic& right) {
    return IsPointerEq(left, right);
}

template<class T>
inline bool IsInstanceEq(const ::Dynamic& left,
                         const ::hx::ObjectPtr<T>& right) {
    return IsPointerEq(left, right);
}

template<class T>
inline bool IsInstanceEq(const ::hx::ObjectPtr<T>& left,
                         const ::Dynamic& right) {
    return IsPointerEq(left, right);
}

template<class Left, class Right>
inline bool IsInstanceEq(const Left& left, const Right& right) {
    return left == right;
}

template<class Left, class Right>
inline bool IsInstanceNotEq(const Left& left, const Right& right) {
    return !IsInstanceEq(left, right);
}

template<class T>
inline ::Dynamic interface_check(const ::hx::ObjectPtr<T>& value, int hash) {
    T* object = value.get();
    if (object == nullptr || object->_hx_getInterface(hash) == nullptr)
        ::hx::throwBadCast();
    return ::Dynamic(value);
}

inline ::Dynamic interface_check(const ::Dynamic& value, int hash) {
    auto* object = static_cast<::hx::Object*>(value.managedPointer());
    if (object == nullptr || object->_hx_getInterface(hash) == nullptr)
        ::hx::throwBadCast();
    return value;
}

inline bool IsLess(const ::Dynamic& left, const ::Dynamic& right) {
    return ::hx::DynamicCompare(left, right) < 0;
}
inline bool IsLessEq(const ::Dynamic& left, const ::Dynamic& right) {
    return ::hx::DynamicCompare(left, right) <= 0;
}
inline bool IsGreater(const ::Dynamic& left, const ::Dynamic& right) {
    return ::hx::DynamicCompare(left, right) > 0;
}
inline bool IsGreaterEq(const ::Dynamic& left, const ::Dynamic& right) {
    return ::hx::DynamicCompare(left, right) >= 0;
}

void SetTopOfStack(int* marker, bool force);

} // namespace hx

inline int operator+(int left, const ::Dynamic& right) {
    return left + right.Cast<int>();
}
inline int operator-(int left, const ::Dynamic& right) {
    return left - right.Cast<int>();
}
inline int operator+(const ::Dynamic& left, int right) {
    return left.Cast<int>() + right;
}
inline int operator-(const ::Dynamic& left, int right) {
    return left.Cast<int>() - right;
}
inline int operator*(const ::Dynamic& left, int right) {
    return left.Cast<int>() * right;
}
inline int operator/(const ::Dynamic& left, int right) {
    return left.Cast<int>() / right;
}
inline double operator-(const ::Dynamic& left, double right) {
    return left.Cast<double>() - right;
}
inline double operator*(const ::Dynamic& left, double right) {
    return left.Cast<double>() * right;
}
inline double operator/(const ::Dynamic& left, double right) {
    return left.Cast<double>() / right;
}
inline double operator-(double left, const ::Dynamic& right) {
    return left - right.Cast<double>();
}
inline double operator*(double left, const ::Dynamic& right) {
    return left * right.Cast<double>();
}
inline double operator/(double left, const ::Dynamic& right) {
    return left / right.Cast<double>();
}
inline double operator-(const ::Dynamic& value) {
    return -value.Cast<double>();
}
inline ::Dynamic operator+(const ::Dynamic& left, const ::Dynamic& right) {
    if (left.tag() == ::Dynamic::Tag::String ||
        right.tag() == ::Dynamic::Tag::String)
        return ::Dynamic(left.toString() + right.toString());
    return ::Dynamic(left.Cast<double>() + right.Cast<double>());
}
inline ::Dynamic operator-(const ::Dynamic& left, const ::Dynamic& right) {
    if (left.tag() == ::Dynamic::Tag::Integer &&
        right.tag() == ::Dynamic::Tag::Integer)
        return ::Dynamic(left.Cast<int>() - right.Cast<int>());
    return ::Dynamic(left.Cast<double>() - right.Cast<double>());
}
inline ::Dynamic operator*(const ::Dynamic& left, const ::Dynamic& right) {
    if (left.tag() == ::Dynamic::Tag::Integer &&
        right.tag() == ::Dynamic::Tag::Integer)
        return ::Dynamic(left.Cast<int>() * right.Cast<int>());
    return ::Dynamic(left.Cast<double>() * right.Cast<double>());
}
inline double operator/(const ::Dynamic& left, const ::Dynamic& right) {
    return left.Cast<double>() / right.Cast<double>();
}
inline double operator%(const ::Dynamic& left, const ::Dynamic& right) {
    return ::hx::Mod(left, right);
}
inline int operator&(const ::Dynamic& left, const ::Dynamic& right) {
    return left.Cast<int>() & right.Cast<int>();
}
inline int operator|(const ::Dynamic& left, const ::Dynamic& right) {
    return left.Cast<int>() | right.Cast<int>();
}
inline int operator^(const ::Dynamic& left, const ::Dynamic& right) {
    return left.Cast<int>() ^ right.Cast<int>();
}
inline int operator<<(const ::Dynamic& left, const ::Dynamic& right) {
    return left.Cast<int>() << (right.Cast<int>() & 31);
}
inline int operator>>(const ::Dynamic& left, const ::Dynamic& right) {
    return left.Cast<int>() >> (right.Cast<int>() & 31);
}
inline double operator+(double left, const ::Dynamic& right) {
    return left + right.Cast<double>();
}
inline double operator+(const ::Dynamic& left, double right) {
    return left.Cast<double>() + right;
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool operator<(const ::Dynamic& left, Number right) {
    return left.Cast<double>() < static_cast<double>(right);
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool operator<=(const ::Dynamic& left, Number right) {
    return left.Cast<double>() <= static_cast<double>(right);
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool operator>(const ::Dynamic& left, Number right) {
    return left.Cast<double>() > static_cast<double>(right);
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool operator>=(const ::Dynamic& left, Number right) {
    return left.Cast<double>() >= static_cast<double>(right);
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool operator<(Number left, const ::Dynamic& right) {
    return static_cast<double>(left) < right.Cast<double>();
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool operator<=(Number left, const ::Dynamic& right) {
    return static_cast<double>(left) <= right.Cast<double>();
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool operator>(Number left, const ::Dynamic& right) {
    return static_cast<double>(left) > right.Cast<double>();
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool operator>=(Number left, const ::Dynamic& right) {
    return static_cast<double>(left) >= right.Cast<double>();
}
inline bool operator<(const ::Dynamic& left, const ::Dynamic& right) {
    return ::hx::IsLess(left, right);
}
inline bool operator<=(const ::Dynamic& left, const ::Dynamic& right) {
    return ::hx::IsLessEq(left, right);
}
inline bool operator>(const ::Dynamic& left, const ::Dynamic& right) {
    return ::hx::IsGreater(left, right);
}
inline bool operator>=(const ::Dynamic& left, const ::Dynamic& right) {
    return ::hx::IsGreaterEq(left, right);
}
inline bool operator==(const ::Dynamic& left, int right) {
    if (left.tag() == ::Dynamic::Tag::Integer ||
        left.tag() == ::Dynamic::Tag::Boolean)
        return left.Cast<std::int64_t>() == right;
    if (left.tag() == ::Dynamic::Tag::Floating)
        return left.Cast<double>() == static_cast<double>(right);
    return false;
}
inline bool operator==(int left, const ::Dynamic& right) {
    return right == left;
}
inline bool operator!=(const ::Dynamic& left, int right) {
    return !(left == right);
}
inline bool operator!=(int left, const ::Dynamic& right) {
    return !(right == left);
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool operator==(Number, const ::String&) {
    return false;
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool operator==(const ::String&, Number) {
    return false;
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool operator!=(Number left, const ::String& right) {
    return !(left == right);
}
template<class Number>
    requires std::is_arithmetic_v<Number>
inline bool operator!=(const ::String& left, Number right) {
    return !(left == right);
}
inline bool operator==(const ::Dynamic& left, const ::String& right) {
    return left.tag() == ::Dynamic::Tag::String && left.Cast<::String>() == right;
}
inline bool operator==(const ::String& left, const ::Dynamic& right) {
    return right == left;
}
inline bool operator!=(const ::Dynamic& left, const ::String& right) {
    return !(left == right);
}
inline bool operator!=(const ::String& left, const ::Dynamic& right) {
    return !(left == right);
}

inline constexpr HxNullValue null() noexcept { return {}; }

#define HXCPP_ZGC_LOAD(type, slot) ::hx::load<type>(slot)
#define HXCPP_ZGC_STORE(owner, slot, value) ::hx::store((owner), (slot), (value))
#define HXCPP_ZGC_SAFEPOINT() ::hx::safepoint()

#define HX_DECLARE_CLASS0(name) \
    class name##_obj; using name = ::hx::ObjectPtr< name##_obj >;
#define HX_DECLARE_CLASS1(ns, name) \
    namespace ns { class name##_obj; using name = ::hx::ObjectPtr< name##_obj >; }
#define HX_DECLARE_CLASS2(ns1, ns2, name) \
    namespace ns1 { namespace ns2 { class name##_obj; \
    using name = ::hx::ObjectPtr< name##_obj >; } }
#define HX_DECLARE_CLASS3(ns1, ns2, ns3, name) \
    namespace ns1 { namespace ns2 { namespace ns3 { class name##_obj; \
    using name = ::hx::ObjectPtr< name##_obj >; } } }
#define HX_DECLARE_CLASS4(ns1, ns2, ns3, ns4, name) \
    namespace ns1 { namespace ns2 { namespace ns3 { namespace ns4 { \
    class name##_obj; using name = ::hx::ObjectPtr< name##_obj >; } } } }
#define HX_DECLARE_CLASS5(ns1, ns2, ns3, ns4, ns5, name) \
    namespace ns1 { namespace ns2 { namespace ns3 { namespace ns4 { \
    namespace ns5 { class name##_obj; \
    using name = ::hx::ObjectPtr< name##_obj >; } } } } }
#define HX_DECLARE_CLASS6(ns1, ns2, ns3, ns4, ns5, ns6, name) \
    namespace ns1 { namespace ns2 { namespace ns3 { namespace ns4 { \
    namespace ns5 { namespace ns6 { class name##_obj; \
    using name = ::hx::ObjectPtr< name##_obj >; } } } } } }

#define HX_DECLARE_STACK_FRAME(name) extern ::hx::StackPosition name;
#define HX_DEFINE_STACK_FRAME(name, className, functionName, classFunctionHash, fullName, fileName, lineNumber, fileHash) \
    ::hx::StackPosition name((className), (functionName), (fullName), \
                             (fileName), (lineNumber), \
                             (classFunctionHash), (fileHash));
#define HX_LOCAL_STACK_FRAME(name, className, functionName, classFunctionHash, fullName, fileName, lineNumber, fileHash) \
    static HX_DEFINE_STACK_FRAME(name, className, functionName, \
                                 classFunctionHash, fullName, fileName, \
                                 lineNumber, fileHash)
#define HX_STACKFRAME(position) ::hx::StackFrame _hx_stackframe(position);
#define HX_GC_STACKFRAME(position) ::hx::StackFrame _hx_stackframe(position);
#define HX_JUST_GC_STACKFRAME ::hx::JustGcStackFrame _hx_stackframe;
#define HX_TOP_OF_STACK
#define HXLINE(number) _hx_stackframe.lineNumber = (number);
#define HXDLIN(number) _hx_stackframe.lineNumber = (number);
#define HX_ZGC_ROOTED_LITERAL(text) \
    ([]() -> const ::String& { \
        static ::hx::RootedStringLiteral _hx_literal( \
            (text), sizeof(text) - 1U); \
        return _hx_literal.value; \
    }())
#define HX_ZGC_ROOTED_WIDE_LITERAL(text) \
    ([]() -> const ::String& { \
        static ::hx::RootedWideStringLiteral _hx_literal((text)); \
        return _hx_literal.value; \
    }())
#define HX_(text, a, b, c, d) HX_ZGC_ROOTED_LITERAL(text)
#define HX_W(text, a, b) HX_ZGC_ROOTED_WIDE_LITERAL(text)
#define HX_CSTRING(text) HX_ZGC_ROOTED_LITERAL(text)
#define HX_STACK_THIS(...)
#define HX_STACK_ARG(...)
#define HX_STACK_CATCHABLE(...)
#define HX_STACK_BEGIN_CATCH ::__hxcpp_stack_begin_catch();
template<typename Value>
inline void __hxcpp_unsafe_set(::hx::ObjectPtr<Value>& output,
                               const ::Dynamic& input) {
    // "Unsafe" bypasses the runtime type test, but it must not bypass the
    // precise collector's colored-reference store barrier.
    output = static_cast<Value*>(input.managedPointer());
}

#define HX_STACK_DO_THROW(value) ::hx::Throw(::Dynamic(value))
#define HX_STACK_DO_RETHROW(value) ::hx::Rethrow(::Dynamic(value))
#define HX_INVALID_OBJECT ::Dynamic(::String("Invalid object"))
#define HX_NULL_FUNCTION_POINTER ::Dynamic(::String("Null Function Pointer"))
#define HX_DYN_ARGS_1 const ::Dynamic& a0
#define HX_DYN_ARGS_2 HX_DYN_ARGS_1,const ::Dynamic& a1
#define HX_DYN_ARGS_3 HX_DYN_ARGS_2,const ::Dynamic& a2
#define HX_DYN_ARGS_4 HX_DYN_ARGS_3,const ::Dynamic& a3
#define HX_DYN_ARGS_5 HX_DYN_ARGS_4,const ::Dynamic& a4
#define HX_DYN_ARGS_6 HX_DYN_ARGS_5,const ::Dynamic& a5
#define HX_DYN_ARGS_7 HX_DYN_ARGS_6,const ::Dynamic& a6
#define HX_DYN_ARGS_8 HX_DYN_ARGS_7,const ::Dynamic& a7
#define HX_DYN_ARGS_9 HX_DYN_ARGS_8,const ::Dynamic& a8
#define HX_DYN_ARGS_10 HX_DYN_ARGS_9,const ::Dynamic& a9
#define HX_DYN_ARGS_11 HX_DYN_ARGS_10,const ::Dynamic& a10
#define HX_DYN_ARGS_12 HX_DYN_ARGS_11,const ::Dynamic& a11
#define HX_DYN_ARGS_13 HX_DYN_ARGS_12,const ::Dynamic& a12
#define HX_DYN_ARGS_14 HX_DYN_ARGS_13,const ::Dynamic& a13
#define HX_DYN_ARGS_15 HX_DYN_ARGS_14,const ::Dynamic& a14
#define HX_DYN_CALL_1 a0
#define HX_DYN_CALL_2 HX_DYN_CALL_1,a1
#define HX_DYN_CALL_3 HX_DYN_CALL_2,a2
#define HX_DYN_CALL_4 HX_DYN_CALL_3,a3
#define HX_DYN_CALL_5 HX_DYN_CALL_4,a4
#define HX_DYN_CALL_6 HX_DYN_CALL_5,a5
#define HX_DYN_CALL_7 HX_DYN_CALL_6,a6
#define HX_DYN_CALL_8 HX_DYN_CALL_7,a7
#define HX_DYN_CALL_9 HX_DYN_CALL_8,a8
#define HX_DYN_CALL_10 HX_DYN_CALL_9,a9
#define HX_DYN_CALL_11 HX_DYN_CALL_10,a10
#define HX_DYN_CALL_12 HX_DYN_CALL_11,a11
#define HX_DYN_CALL_13 HX_DYN_CALL_12,a12
#define HX_DYN_CALL_14 HX_DYN_CALL_13,a13
#define HX_DYN_CALL_15 HX_DYN_CALL_14,a14

#define HX_DEFINE_DYNAMIC_FUNC0(cls,name,ret) \
    static ::Dynamic _hx_wrap_dyn_##cls##_##name(::hx::Object* obj) { \
        ret reinterpret_cast<cls*>(obj)->name(); return ::Dynamic(); \
    } \
    ::Dynamic cls::name##_dyn() { \
        return ::hx::CreateMemberFunction0(#name,this, \
            &_hx_wrap_dyn_##cls##_##name); \
    }
#define HX_DEFINE_DYNAMIC_FUNC_FIXED(N,cls,name,ret,args,call) \
    static ::Dynamic _hx_wrap_dyn_##cls##_##name( \
            ::hx::Object* obj,args) { \
        ret reinterpret_cast<cls*>(obj)->name(call); return ::Dynamic(); \
    } \
    ::Dynamic cls::name##_dyn() { \
        return ::hx::CreateMemberFunction##N(#name,this, \
            &_hx_wrap_dyn_##cls##_##name); \
    }
#define HX_DEFINE_DYNAMIC_FUNC1(cls,name,ret) HX_DEFINE_DYNAMIC_FUNC_FIXED(1,cls,name,ret,HX_DYN_ARGS_1,HX_DYN_CALL_1)
#define HX_DEFINE_DYNAMIC_FUNC2(cls,name,ret) HX_DEFINE_DYNAMIC_FUNC_FIXED(2,cls,name,ret,HX_DYN_ARGS_2,HX_DYN_CALL_2)
#define HX_DEFINE_DYNAMIC_FUNC3(cls,name,ret) HX_DEFINE_DYNAMIC_FUNC_FIXED(3,cls,name,ret,HX_DYN_ARGS_3,HX_DYN_CALL_3)
#define HX_DEFINE_DYNAMIC_FUNC4(cls,name,ret) HX_DEFINE_DYNAMIC_FUNC_FIXED(4,cls,name,ret,HX_DYN_ARGS_4,HX_DYN_CALL_4)
#define HX_DEFINE_DYNAMIC_FUNC5(cls,name,ret) HX_DEFINE_DYNAMIC_FUNC_FIXED(5,cls,name,ret,HX_DYN_ARGS_5,HX_DYN_CALL_5)
#define HX_DEFINE_DYNAMIC_FUNC6(cls,name,ret) HX_DEFINE_DYNAMIC_FUNC_FIXED(6,cls,name,ret,HX_DYN_ARGS_6,HX_DYN_CALL_6)
#define HX_DEFINE_DYNAMIC_FUNC7(cls,name,ret) HX_DEFINE_DYNAMIC_FUNC_FIXED(7,cls,name,ret,HX_DYN_ARGS_7,HX_DYN_CALL_7)
#define HX_DEFINE_DYNAMIC_FUNC8(cls,name,ret) HX_DEFINE_DYNAMIC_FUNC_FIXED(8,cls,name,ret,HX_DYN_ARGS_8,HX_DYN_CALL_8)
#define HX_DEFINE_DYNAMIC_FUNC9(cls,name,ret) HX_DEFINE_DYNAMIC_FUNC_FIXED(9,cls,name,ret,HX_DYN_ARGS_9,HX_DYN_CALL_9)
#define HX_DEFINE_DYNAMIC_FUNC10(cls,name,ret) HX_DEFINE_DYNAMIC_FUNC_FIXED(10,cls,name,ret,HX_DYN_ARGS_10,HX_DYN_CALL_10)
#define HX_DEFINE_DYNAMIC_FUNC11(cls,name,ret) HX_DEFINE_DYNAMIC_FUNC_FIXED(11,cls,name,ret,HX_DYN_ARGS_11,HX_DYN_CALL_11)
#define HX_DEFINE_DYNAMIC_FUNC12(cls,name,ret) HX_DEFINE_DYNAMIC_FUNC_FIXED(12,cls,name,ret,HX_DYN_ARGS_12,HX_DYN_CALL_12)
#define HX_DEFINE_DYNAMIC_FUNC13(cls,name,ret) HX_DEFINE_DYNAMIC_FUNC_FIXED(13,cls,name,ret,HX_DYN_ARGS_13,HX_DYN_CALL_13)
#define HX_DEFINE_DYNAMIC_FUNC14(cls,name,ret) HX_DEFINE_DYNAMIC_FUNC_FIXED(14,cls,name,ret,HX_DYN_ARGS_14,HX_DYN_CALL_14)
#define HX_DEFINE_DYNAMIC_FUNC15(cls,name,ret) HX_DEFINE_DYNAMIC_FUNC_FIXED(15,cls,name,ret,HX_DYN_ARGS_15,HX_DYN_CALL_15)

#define STATIC_HX_DEFINE_DYNAMIC_FUNC0(cls,name,ret) \
    static ::Dynamic _hx_wrap_static_dyn_##cls##_##name() { \
        ret cls::name(); return ::Dynamic(); \
    } \
    ::Dynamic cls::name##_dyn() { \
        return ::hx::CreateStaticFunction0(#name, \
            &_hx_wrap_static_dyn_##cls##_##name); \
    }
#define STATIC_HX_DEFINE_DYNAMIC_FUNC_FIXED(N,cls,name,ret,args,call) \
    static ::Dynamic _hx_wrap_static_dyn_##cls##_##name(args) { \
        ret cls::name(call); return ::Dynamic(); \
    } \
    ::Dynamic cls::name##_dyn() { \
        return ::hx::CreateStaticFunction##N(#name, \
            &_hx_wrap_static_dyn_##cls##_##name); \
    }
#define STATIC_HX_DEFINE_DYNAMIC_FUNC1(cls,name,ret) STATIC_HX_DEFINE_DYNAMIC_FUNC_FIXED(1,cls,name,ret,HX_DYN_ARGS_1,HX_DYN_CALL_1)
#define STATIC_HX_DEFINE_DYNAMIC_FUNC2(cls,name,ret) STATIC_HX_DEFINE_DYNAMIC_FUNC_FIXED(2,cls,name,ret,HX_DYN_ARGS_2,HX_DYN_CALL_2)
#define STATIC_HX_DEFINE_DYNAMIC_FUNC3(cls,name,ret) STATIC_HX_DEFINE_DYNAMIC_FUNC_FIXED(3,cls,name,ret,HX_DYN_ARGS_3,HX_DYN_CALL_3)
#define STATIC_HX_DEFINE_DYNAMIC_FUNC4(cls,name,ret) STATIC_HX_DEFINE_DYNAMIC_FUNC_FIXED(4,cls,name,ret,HX_DYN_ARGS_4,HX_DYN_CALL_4)
#define STATIC_HX_DEFINE_DYNAMIC_FUNC5(cls,name,ret) STATIC_HX_DEFINE_DYNAMIC_FUNC_FIXED(5,cls,name,ret,HX_DYN_ARGS_5,HX_DYN_CALL_5)
#define STATIC_HX_DEFINE_DYNAMIC_FUNC6(cls,name,ret) STATIC_HX_DEFINE_DYNAMIC_FUNC_FIXED(6,cls,name,ret,HX_DYN_ARGS_6,HX_DYN_CALL_6)
#define STATIC_HX_DEFINE_DYNAMIC_FUNC7(cls,name,ret) STATIC_HX_DEFINE_DYNAMIC_FUNC_FIXED(7,cls,name,ret,HX_DYN_ARGS_7,HX_DYN_CALL_7)
#define STATIC_HX_DEFINE_DYNAMIC_FUNC8(cls,name,ret) STATIC_HX_DEFINE_DYNAMIC_FUNC_FIXED(8,cls,name,ret,HX_DYN_ARGS_8,HX_DYN_CALL_8)
#define STATIC_HX_DEFINE_DYNAMIC_FUNC9(cls,name,ret) STATIC_HX_DEFINE_DYNAMIC_FUNC_FIXED(9,cls,name,ret,HX_DYN_ARGS_9,HX_DYN_CALL_9)
#define STATIC_HX_DEFINE_DYNAMIC_FUNC10(cls,name,ret) STATIC_HX_DEFINE_DYNAMIC_FUNC_FIXED(10,cls,name,ret,HX_DYN_ARGS_10,HX_DYN_CALL_10)
#define STATIC_HX_DEFINE_DYNAMIC_FUNC11(cls,name,ret) STATIC_HX_DEFINE_DYNAMIC_FUNC_FIXED(11,cls,name,ret,HX_DYN_ARGS_11,HX_DYN_CALL_11)
#define STATIC_HX_DEFINE_DYNAMIC_FUNC12(cls,name,ret) STATIC_HX_DEFINE_DYNAMIC_FUNC_FIXED(12,cls,name,ret,HX_DYN_ARGS_12,HX_DYN_CALL_12)
#define STATIC_HX_DEFINE_DYNAMIC_FUNC13(cls,name,ret) STATIC_HX_DEFINE_DYNAMIC_FUNC_FIXED(13,cls,name,ret,HX_DYN_ARGS_13,HX_DYN_CALL_13)
#define STATIC_HX_DEFINE_DYNAMIC_FUNC14(cls,name,ret) STATIC_HX_DEFINE_DYNAMIC_FUNC_FIXED(14,cls,name,ret,HX_DYN_ARGS_14,HX_DYN_CALL_14)
#define STATIC_HX_DEFINE_DYNAMIC_FUNC15(cls,name,ret) STATIC_HX_DEFINE_DYNAMIC_FUNC_FIXED(15,cls,name,ret,HX_DYN_ARGS_15,HX_DYN_CALL_15)
#define HX_CTX nullptr
#define HX_CTX_GET nullptr
#define HX_DO_INTERFACE_RTTI
#define HX_DO_ENUM_RTTI_INTERNAL \
    ::hx::Val __Field(const ::String& inName, \
                      ::hx::PropertyAccess inCallProp); \
    static int __FindIndex(::String inName); \
    static int __FindArgCount(::String inName)
#define HX_DO_ENUM_RTTI \
    HX_DO_ENUM_RTTI_INTERNAL; \
    static ::hx::Class __mClass; \
    ::hx::Class __GetClass() const { return __mClass; } \
    static ::hx::Class& __SGetClass() { return __mClass; }

#define HX_DEFINE_CREATE_ENUM(enum_obj) \
    static ::Dynamic Create##enum_obj( \
            ::String inName, ::cpp::VirtualArray inArgs) { \
        const int expected = enum_obj::__FindArgCount(inName); \
        const int actual = inArgs ? inArgs->length : 0; \
        if (expected < 0) \
            throw ::Dynamic(::String("Invalid enum constructor for " #enum_obj \
                                     ": ") + inName); \
        if (actual != expected) \
            throw ::Dynamic(::String("Invalid enum arg count for " #enum_obj \
                                     ".") + inName + ::String(": expected ") + \
                            ::String(expected) + ::String(", got ") + \
                            ::String(actual)); \
        ::Dynamic result; \
        if (!enum_obj::__GetStatic(inName, result, ::hx::paccDynamic)) \
            throw ::Dynamic(::String("Invalid enum constructor for " #enum_obj \
                                     ": ") + inName); \
        return actual == 0 ? result : result.__Run(inArgs); \
    }

#define HX_NOVAGC_MUTATOR_SCOPE() \
    ::hx::ManagedMutatorScope _hx_novagc_mutator_scope;
#define HX_NOVAGC_SAFEPOINT() ::hx::safepoint();
#define HX_NOVAGC_LOOP_SAFEPOINT() ::hx::safepoint();
#define HX_NOVAGC_PIN_THIS(value) \
    ::hx::ScopedPin _hx_zgc_pin_this((value));

#define HX_NOVAGC_ROOT_ARG(var) \
    ::hx::gc::RefSlot* _hx_root_slots_##var[] = { &(var).mPtr }; \
    ::hx::gc::RootScope _hx_root_scope_##var(_hx_root_slots_##var, 1);

#define HX_NOVAGC_VAR(type, var) \
    type var; \
    ::hx::gc::RefSlot* _hx_root_slots_##var[] = { &(var).mPtr }; \
    ::hx::gc::RootScope _hx_root_scope_##var(_hx_root_slots_##var, 1)

#define HX_NOVAGC_VARI(type, var) \
    type var; \
    ::hx::gc::RefSlot* _hx_root_slots_##var[] = { &(var).mPtr }; \
    ::hx::gc::RootScope _hx_root_scope_##var(_hx_root_slots_##var, 1); \
    var

#define HX_NOVAGC_WB(owner, valueSlot, targetSlot) do { \
    void* _hx_value = ::hx::gc::loadBarrierFast((valueSlot)); \
    ::hx::gc::storeBarrierFast((owner), (targetSlot), _hx_value); \
} while (false)

extern "C" {
void __hxcpp_nova_initialize();
void __hxcpp_nova_shutdown();
void __hxcpp_gc_safe_point();
void __hxcpp_collect(bool major);
void __hxcpp_gc_request_full();
void __hxcpp_gc_compact();
void __hxcpp_enable(bool enabled);
std::uint64_t __hxcpp_gc_last_relocated_objects();
double __hxcpp_gc_used_bytes();

int __hxcpp_memory_get_byte(const ::Array<unsigned char>& bytes, int position);
int __hxcpp_memory_get_i16(const ::Array<unsigned char>& bytes, int position);
int __hxcpp_memory_get_ui16(const ::Array<unsigned char>& bytes, int position);
int __hxcpp_memory_get_ui32(const ::Array<unsigned char>& bytes, int position);
float __hxcpp_memory_get_float(const ::Array<unsigned char>& bytes, int position);
double __hxcpp_memory_get_double(const ::Array<unsigned char>& bytes, int position);
void __hxcpp_memory_set_byte(const ::Array<unsigned char>& bytes, int position,
                             int value);
void __hxcpp_memory_set_i16(const ::Array<unsigned char>& bytes, int position,
                            int value);
void __hxcpp_memory_set_ui16(const ::Array<unsigned char>& bytes, int position,
                             int value);
void __hxcpp_memory_set_i32(const ::Array<unsigned char>& bytes, int position,
                            int value);
void __hxcpp_memory_set_ui32(const ::Array<unsigned char>& bytes, int position,
                             int value);
void __hxcpp_memory_set_float(const ::Array<unsigned char>& bytes, int position,
                              float value);
void __hxcpp_memory_set_double(const ::Array<unsigned char>& bytes, int position,
                               double value);
void __hxcpp_memory_memset(const ::Array<unsigned char>& bytes, int position,
                           int length, int value);

int __hxcpp_reinterpret_float32_as_le_int32(float value) noexcept;
int __hxcpp_reinterpret_float64_as_le_int32_high(double value) noexcept;
int __hxcpp_reinterpret_float64_as_le_int32_low(double value) noexcept;
float __hxcpp_reinterpret_le_int32_as_float32(int value) noexcept;
double __hxcpp_reinterpret_le_int32s_as_float64(int low, int high) noexcept;
::Dynamic __hxcpp_thread_current();
::Dynamic __hxcpp_thread_create(::Dynamic job);
void __hxcpp_thread_send(::Dynamic thread, ::Dynamic message);
::Dynamic __hxcpp_thread_read_message(bool block);
void __hxcpp_zgc_join_threads();
void __hxcpp_set_critical_error_handler(::Dynamic handler);
bool __hxcpp_invoke_critical_error_handler(::String message);
::Dynamic __hxcpp_mutex_create();
void __hxcpp_mutex_acquire(::Dynamic mutex);
bool __hxcpp_mutex_try(::Dynamic mutex);
void __hxcpp_mutex_release(::Dynamic mutex);
::Dynamic __hxcpp_lock_create();
bool __hxcpp_lock_wait(::Dynamic lock, ::hx::Null<double> timeout);
void __hxcpp_lock_release(::Dynamic lock);
::Dynamic __hxcpp_tls_get(int slot);
void __hxcpp_tls_set(int slot, ::Dynamic value);
::Dynamic __hxcpp_deque_create();
void __hxcpp_deque_add(::Dynamic deque, ::Dynamic value);
void __hxcpp_deque_push(::Dynamic deque, ::Dynamic value);
::Dynamic __hxcpp_deque_pop(::Dynamic deque, bool block);
::Dynamic __hxcpp_zgc_load_prime(::String library, ::String primitive,
                                 ::String signature, bool quietFail);
}

#include <hx/native_sys.hpp>

void __boot_all();

#if __has_include(<Std.h>) && !defined(INCLUDED_Std)
#include <Std.h>
#endif

#endif // NOVA_HXCPP_H_INCLUDED

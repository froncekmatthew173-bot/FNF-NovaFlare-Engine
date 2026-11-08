#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <stdexcept>

namespace hx {

class LocalFunc : public Object {
public:
    // Generated hxcpp closures expose the historical __ArgCount hook.  Keep
    // argCount as the runtime-facing spelling while dispatching through that
    // hook so HXARGC on local/default closures is not silently ignored.
    virtual int __ArgCount() const noexcept { return -1; }
    virtual int argCount() const noexcept { return __ArgCount(); }
    virtual bool sameClosure(const LocalFunc& other) const {
        return this == &other;
    }
    virtual Dynamic invoke0() {
        throw std::runtime_error("Haxe closure arity mismatch");
    }
    virtual Dynamic invoke1(const Dynamic&) {
        throw std::runtime_error("Haxe closure arity mismatch");
    }
    virtual Dynamic invoke2(const Dynamic&, const Dynamic&) {
        throw std::runtime_error("Haxe closure arity mismatch");
    }
    virtual Dynamic invoke3(const Dynamic&, const Dynamic&, const Dynamic&) {
        throw std::runtime_error("Haxe closure arity mismatch");
    }
    virtual Dynamic invokeMany(const Dynamic* arguments, int count) {
        switch (count) {
            case 0: return invoke0();
            case 1: return invoke1(arguments[0]);
            case 2: return invoke2(arguments[0], arguments[1]);
            case 3: return invoke3(arguments[0], arguments[1], arguments[2]);
            default:
                throw std::runtime_error("Haxe closure arity mismatch");
        }
    }
};

using LocalThisFunc = LocalFunc;

template<class Function, int Arity>
class StaticFunction final : public LocalFunc {
public:
    explicit StaticFunction(Function function) noexcept : function_(function) {
        ::hx::gc::Runtime::instance().publish(this);
    }

    Dynamic invokeMany(const Dynamic* arguments, int count) override {
        if (count != Arity)
            throw std::runtime_error("Haxe closure arity mismatch");
        return invoke(arguments, std::make_index_sequence<Arity>{});
    }
    int argCount() const noexcept override { return Arity; }

    static void trace(void*, std::size_t, ::hx::gc::TraceVisitor&) noexcept {}
    static const ::hx::gc::TypeDescriptor descriptor;
    static void* operator new(std::size_t size) {
        if (size != sizeof(StaticFunction))
            throw std::bad_alloc();
        return ::hx::gc::Runtime::instance().allocate(descriptor);
    }
    static void operator delete(void*) noexcept {}
    static void operator delete(void*, std::size_t) noexcept {}

private:
    template<std::size_t... Indices>
    Dynamic invoke(const Dynamic* arguments,
                   std::index_sequence<Indices...>) {
        return function_(arguments[Indices]...);
    }

    Function function_;
};

template<class Function, int Arity>
const ::hx::gc::TypeDescriptor StaticFunction<Function, Arity>::descriptor{
    0x535441544943464eULL ^ static_cast<std::uint64_t>(Arity) ^
        static_cast<std::uint64_t>(sizeof(Function)),
    "StaticFunction",
    nullptr,
    sizeof(StaticFunction<Function, Arity>),
    alignof(StaticFunction<Function, Arity>),
    0,
    ::hx::gc::TypeFunction,
    0,
    nullptr,
    &StaticFunction<Function, Arity>::trace,
    nullptr,
};

template<class Function, int Arity>
class MemberFunction final : public LocalFunc {
public:
    MemberFunction(Object* owner, Function function) noexcept
        : owner_(owner), function_(function) {
        ::hx::gc::Runtime::instance().publish(this);
    }

    Dynamic invokeMany(const Dynamic* arguments, int count) override {
        if (count != Arity)
            throw std::runtime_error("Haxe closure arity mismatch");
        Object* owner = owner_.get();
        if (owner == nullptr)
            throw std::runtime_error("null Haxe member closure receiver");
        return invoke(owner, arguments, std::make_index_sequence<Arity>{});
    }
    int argCount() const noexcept override { return Arity; }

    static void trace(void* object, std::size_t,
                      ::hx::gc::TraceVisitor& visitor) {
        auto* self = static_cast<MemberFunction*>(object);
        visitor.visit(self->owner_.mPtr);
    }
    static const ::hx::gc::TypeDescriptor descriptor;
    static void* operator new(std::size_t size) {
        if (size != sizeof(MemberFunction))
            throw std::bad_alloc();
        return ::hx::gc::Runtime::instance().allocate(descriptor);
    }
    static void operator delete(void*) noexcept {}
    static void operator delete(void*, std::size_t) noexcept {}

private:
    template<std::size_t... Indices>
    Dynamic invoke(Object* owner, const Dynamic* arguments,
                   std::index_sequence<Indices...>) {
        return function_(owner, arguments[Indices]...);
    }

    ::hx::ObjectPtr<Object> owner_;
    Function function_;
};

template<class Function, int Arity>
const ::hx::gc::TypeDescriptor MemberFunction<Function, Arity>::descriptor{
    0x4d454d424552464eULL ^ static_cast<std::uint64_t>(Arity) ^
        static_cast<std::uint64_t>(sizeof(Function)),
    "MemberFunction",
    nullptr,
    sizeof(MemberFunction<Function, Arity>),
    alignof(MemberFunction<Function, Arity>),
    0,
    ::hx::gc::TypeContainsReferences | ::hx::gc::TypeFunction,
    0,
    nullptr,
    &MemberFunction<Function, Arity>::trace,
    nullptr,
};

template<int Arity, class Function>
Dynamic createStaticFunction(Function function) {
    return Dynamic(new StaticFunction<Function, Arity>(function));
}

template<int Arity, class Function>
Dynamic createMemberFunction(Object* owner, Function function) {
    static_assert(std::is_pointer_v<Function> &&
                  std::is_function_v<std::remove_pointer_t<Function>>,
                  "generated bound methods require a native function pointer");
    static_assert(sizeof(Function) <= sizeof(std::uintptr_t),
                  "native function pointer does not fit Dynamic payload");
    return Dynamic::fromMemberFunction(
        owner, reinterpret_cast<std::uintptr_t>(function), Arity);
}

#define HX_ZGC_DECLARE_FUNCTION_FACTORY(N) \
    template<class Function> \
    inline ::Dynamic CreateStaticFunction##N(const char*, Function function) { \
        return createStaticFunction<N>(function); \
    } \
    template<class Owner, class Function> \
    inline ::Dynamic CreateMemberFunction##N(const char*, Owner* owner, \
                                               Function function) { \
        return createMemberFunction<N>(static_cast<::hx::Object*>(owner), \
                                       function); \
    }

HX_ZGC_DECLARE_FUNCTION_FACTORY(0)
HX_ZGC_DECLARE_FUNCTION_FACTORY(1)
HX_ZGC_DECLARE_FUNCTION_FACTORY(2)
HX_ZGC_DECLARE_FUNCTION_FACTORY(3)
HX_ZGC_DECLARE_FUNCTION_FACTORY(4)
HX_ZGC_DECLARE_FUNCTION_FACTORY(5)
HX_ZGC_DECLARE_FUNCTION_FACTORY(6)
HX_ZGC_DECLARE_FUNCTION_FACTORY(7)
HX_ZGC_DECLARE_FUNCTION_FACTORY(8)
HX_ZGC_DECLARE_FUNCTION_FACTORY(9)
HX_ZGC_DECLARE_FUNCTION_FACTORY(10)
HX_ZGC_DECLARE_FUNCTION_FACTORY(11)
HX_ZGC_DECLARE_FUNCTION_FACTORY(12)
HX_ZGC_DECLARE_FUNCTION_FACTORY(13)
HX_ZGC_DECLARE_FUNCTION_FACTORY(14)
HX_ZGC_DECLARE_FUNCTION_FACTORY(15)

#undef HX_ZGC_DECLARE_FUNCTION_FACTORY

template<class T>
void traceCaptured(::hx::gc::TraceVisitor&, T&) noexcept {}

template<class T>
void traceCaptured(::hx::gc::TraceVisitor& visitor,
                   ::hx::ObjectPtr<T>& value) {
    visitor.visit(value.mPtr);
}

inline void traceCaptured(::hx::gc::TraceVisitor& visitor, String& value) {
    visitor.visit(value.mPtr);
}

inline void traceCaptured(::hx::gc::TraceVisitor& visitor, Dynamic& value) {
    visitor.visit(value.mPtr);
}

inline LocalFunc* resolveFunction(const Dynamic& function) {
    // Function invocation is observable Haxe control flow.  The original
    // hxcpp raises a Dynamic value here (HX_NULL_FUNCTION_POINTER), allowing
    // ordinary `try/catch` and libraries such as utest.Assert.raises to catch
    // it.  A native std::exception escapes every generated Haxe catch block.
    if (function.isNull())
        throw ::Dynamic(::String("Null Function Pointer"));
    if (function.tag() != Dynamic::Tag::Object)
        throw ::Dynamic(::String("Dynamic value is not callable"));
    auto& slot = const_cast<::hx::gc::RefSlot&>(function.mPtr);
    auto* raw = static_cast<LocalFunc*>(
        ::hx::gc::Runtime::instance().loadBarrier(slot));
    if (raw == nullptr)
        throw ::Dynamic(::String("Null Function Pointer"));
    return raw;
}

template<std::size_t>
using BoundMethodArgument = const Dynamic&;

template<std::size_t... Indices>
inline Dynamic invokeBoundMethod(const Dynamic& function, Object* owner,
                                 const Dynamic* arguments,
                                 std::index_sequence<Indices...>) {
    using Entry = Dynamic (*)(Object*, BoundMethodArgument<Indices>...);
    auto entry = reinterpret_cast<Entry>(function.memberFunctionAddress());
    return entry(owner, arguments[Indices]...);
}

inline bool IsNotEq(const Dynamic& left, int right) {
    switch (left.tag()) {
        case Dynamic::Tag::Integer:
        case Dynamic::Tag::Boolean:
            return left.integer_ != right;
        case Dynamic::Tag::Floating:
            return left.floating_ != static_cast<double>(right);
        default:
            return true;
    }
}

inline bool IsEq(const Dynamic& left, int right) {
    return !IsNotEq(left, right);
}

inline bool IsNotEq(const Dynamic& left, bool right) {
    switch (left.tag()) {
        case Dynamic::Tag::Integer:
        case Dynamic::Tag::Boolean:
            return left.integer_ != static_cast<std::int64_t>(right);
        case Dynamic::Tag::Floating:
            return left.floating_ != static_cast<double>(right);
        default:
            return true;
    }
}

inline bool IsEq(const Dynamic& left, bool right) {
    return !IsNotEq(left, right);
}

inline bool IsNotEq(const Dynamic& left, double right) {
    switch (left.tag()) {
        case Dynamic::Tag::Integer:
        case Dynamic::Tag::Boolean:
            return static_cast<double>(left.integer_) != right;
        case Dynamic::Tag::Floating:
            return left.floating_ != right;
        default:
            return true;
    }
}

inline bool IsEq(const Dynamic& left, double right) {
    return !IsNotEq(left, right);
}

inline bool IsNotEq(const Dynamic& left, const String& right) {
    if (left.isNull())
        return !right.isNull();
    if (left.tag() != Dynamic::Tag::String)
        return true;
    return left.Cast<String>() != right;
}

inline bool IsEq(const Dynamic& left, const String& right) {
    return !IsNotEq(left, right);
}

inline bool IsEq(const Dynamic& left, const Dynamic& right) {
    if (left.isNull() || right.isNull())
        return left.isNull() == right.isNull();

    const bool leftNumeric =
        left.tag() == Dynamic::Tag::Integer ||
        left.tag() == Dynamic::Tag::Boolean ||
        left.tag() == Dynamic::Tag::Floating;
    const bool rightNumeric =
        right.tag() == Dynamic::Tag::Integer ||
        right.tag() == Dynamic::Tag::Boolean ||
        right.tag() == Dynamic::Tag::Floating;
    if (leftNumeric && rightNumeric) {
        if (left.tag() == Dynamic::Tag::Floating ||
            right.tag() == Dynamic::Tag::Floating) {
            const double leftValue = left.tag() == Dynamic::Tag::Floating
                ? left.floating_ : static_cast<double>(left.integer_);
            const double rightValue = right.tag() == Dynamic::Tag::Floating
                ? right.floating_ : static_cast<double>(right.integer_);
            return leftValue == rightValue;
        }
        return left.integer_ == right.integer_;
    }

    if (left.tag() != right.tag())
        return false;
    if (left.isMemberFunction()) {
        return left.memberFunctionAddress() == right.memberFunctionAddress() &&
               left.referencePointer() == right.referencePointer();
    }
    switch (left.tag()) {
        case Dynamic::Tag::Integer:
        case Dynamic::Tag::Boolean:
        case Dynamic::Tag::Floating:
        case Dynamic::Tag::Null:
            return false;
        case Dynamic::Tag::String:
            return left.Cast<String>() == right.Cast<String>();
        case Dynamic::Tag::NativePointer:
            return left.nativePointer() == right.nativePointer();
        case Dynamic::Tag::Object:
            return left.managedPointer() == right.managedPointer();
        default:
            return false;
    }
    return false;
}

inline int DynamicCompare(const Dynamic& left, const Dynamic& right) {
    if (IsEq(left, right))
        return 0;
    if (left.isNull())
        return -1;
    if (right.isNull())
        return 1;

    const auto numeric = [](Dynamic::Tag tag) noexcept {
        return tag == Dynamic::Tag::Integer ||
               tag == Dynamic::Tag::Boolean ||
               tag == Dynamic::Tag::Floating;
    };
    if (numeric(left.tag()) && numeric(right.tag())) {
        const double leftValue = left.tag() == Dynamic::Tag::Floating
            ? left.floating_ : static_cast<double>(left.integer_);
        const double rightValue = right.tag() == Dynamic::Tag::Floating
            ? right.floating_ : static_cast<double>(right.integer_);
        return leftValue < rightValue ? -1 : 1;
    }
    if (left.tag() == Dynamic::Tag::String &&
        right.tag() == Dynamic::Tag::String)
        return left.Cast<String>().compare(right.Cast<String>());

    if (left.isMemberFunction() && right.isMemberFunction()) {
        const std::uint64_t leftOwner = left.referencePointer() == nullptr ? 0 :
            ::hx::gc::Runtime::instance().identityOf(left.referencePointer());
        const std::uint64_t rightOwner = right.referencePointer() == nullptr ? 0 :
            ::hx::gc::Runtime::instance().identityOf(right.referencePointer());
        if (leftOwner != rightOwner)
            return leftOwner < rightOwner ? -1 : 1;
        if (left.memberFunctionAddress() != right.memberFunctionAddress())
            return left.memberFunctionAddress() < right.memberFunctionAddress()
                ? -1 : 1;
        return left.memberFunctionArity() < right.memberFunctionArity() ? -1 : 1;
    }
    if (left.tag() == Dynamic::Tag::Object &&
        right.tag() == Dynamic::Tag::Object) {
        const std::uint64_t leftIdentity =
            ::hx::gc::Runtime::instance().identityOf(left.managedPointer());
        const std::uint64_t rightIdentity =
            ::hx::gc::Runtime::instance().identityOf(right.managedPointer());
        return leftIdentity < rightIdentity ? -1 : 1;
    }
    if (left.tag() == Dynamic::Tag::NativePointer &&
        right.tag() == Dynamic::Tag::NativePointer) {
        return reinterpret_cast<std::uintptr_t>(left.nativePointer()) <
               reinterpret_cast<std::uintptr_t>(right.nativePointer()) ? -1 : 1;
    }

    // Original hxcpp only defines useful cross-representation ordering for
    // homogeneous numeric, String, and Object pairs.  Give the remaining
    // combinations a deterministic total order so sort/tree algorithms cannot
    // collapse distinct keys into one bucket.
    return static_cast<unsigned>(left.tag()) <
           static_cast<unsigned>(right.tag()) ? -1 : 1;
}

inline bool IsNotEq(const Dynamic& left, const Dynamic& right) {
    return !IsEq(left, right);
}

} // namespace hx

inline Dynamic Dynamic::operator()() const {
    return invoke(nullptr, 0);
}

inline Dynamic Dynamic::operator()(const Dynamic& argument0) const {
    const Dynamic arguments[]{argument0};
    return invoke(arguments, 1);
}

inline Dynamic Dynamic::operator()(const Dynamic& argument0,
                                   const Dynamic& argument1) const {
    const Dynamic arguments[]{argument0, argument1};
    return invoke(arguments, 2);
}

inline Dynamic Dynamic::operator()(const Dynamic& argument0,
                                   const Dynamic& argument1,
                                   const Dynamic& argument2) const {
    const Dynamic arguments[]{argument0, argument1, argument2};
    return invoke(arguments, 3);
}

#define HXARGC(x) int __ArgCount() const noexcept override { return x; }

#define HX_BEGIN_DEFAULT_FUNC(name, cls) \
    namespace { \
    struct name : public ::hx::LocalThisFunc { \
        ::hx::ObjectPtr<cls> __this; \
        HX_ZGC_CLOSURE_ALLOC(name, \
            ::hx::traceCaptured(visitor, self->__this);) \
        name() : __this(nullptr) { \
            ::hx::gc::Runtime::instance().publish(this); \
        } \
        explicit name(cls* owner) : __this(owner) { \
            ::hx::gc::Runtime::instance().publish(this); \
        }

#define HX_END_DEFAULT_FUNC }

#define HX_ZGC_CLOSURE_ALLOC(name, trace_body) \
    static void _hx_trace(void* object, std::size_t, \
                          ::hx::gc::TraceVisitor& visitor) { \
        auto* self = static_cast<name*>(object); \
        trace_body \
    } \
    static const ::hx::gc::TypeDescriptor& _hx_type() { \
        static const ::hx::gc::TypeDescriptor descriptor{ \
            0x434c4f535552455aULL ^ sizeof(name), #name, nullptr, \
            sizeof(name), alignof(name), 0, \
            ::hx::gc::TypeContainsReferences | ::hx::gc::TypeFunction, 0, nullptr, \
            &_hx_trace, nullptr}; \
        return descriptor; \
    } \
    static void* operator new(std::size_t size) { \
        if (size != sizeof(name)) throw std::bad_alloc(); \
        return ::hx::gc::Runtime::instance().allocate(_hx_type()); \
    } \
    static void operator delete(void*) noexcept {} \
    static void operator delete(void*, std::size_t) noexcept {}

#define HX_BEGIN_LOCAL_FUNC_S0(SUPER, name) \
    struct name : public SUPER { \
        HX_ZGC_CLOSURE_ALLOC(name, (void)self; (void)visitor;) \
        name() { ::hx::gc::Runtime::instance().publish(this); }

#define HX_BEGIN_LOCAL_FUNC_S1(SUPER, name, t0, v0) \
    struct name : public SUPER { \
        t0 v0; \
        HX_ZGC_CLOSURE_ALLOC(name, ::hx::traceCaptured(visitor, self->v0);) \
        explicit name(t0 _hx_0) : v0(_hx_0) { \
            ::hx::gc::Runtime::instance().publish(this); \
        }

#define HX_BEGIN_LOCAL_FUNC_S2(SUPER, name, t0, v0, t1, v1) \
    struct name : public SUPER { \
        t0 v0; t1 v1; \
        HX_ZGC_CLOSURE_ALLOC(name, \
            ::hx::traceCaptured(visitor, self->v0); \
            ::hx::traceCaptured(visitor, self->v1);) \
        name(t0 _hx_0, t1 _hx_1) : v0(_hx_0), v1(_hx_1) { \
            ::hx::gc::Runtime::instance().publish(this); \
        }

#define HX_BEGIN_LOCAL_FUNC_S3(SUPER, name, t0, v0, t1, v1, t2, v2) \
    struct name : public SUPER { \
        t0 v0; t1 v1; t2 v2; \
        HX_ZGC_CLOSURE_ALLOC(name, \
            ::hx::traceCaptured(visitor, self->v0); \
            ::hx::traceCaptured(visitor, self->v1); \
            ::hx::traceCaptured(visitor, self->v2);) \
        name(t0 _hx_0, t1 _hx_1, t2 _hx_2) \
            : v0(_hx_0), v1(_hx_1), v2(_hx_2) { \
            ::hx::gc::Runtime::instance().publish(this); \
        }

#define HX_BEGIN_LOCAL_FUNC_S4(SUPER, name, t0, v0, t1, v1, t2, v2, t3, v3) \
    struct name : public SUPER { t0 v0; t1 v1; t2 v2; t3 v3; \
        HX_ZGC_CLOSURE_ALLOC(name, ::hx::traceCaptured(visitor,self->v0); ::hx::traceCaptured(visitor,self->v1); ::hx::traceCaptured(visitor,self->v2); ::hx::traceCaptured(visitor,self->v3);) \
        name(t0 a0,t1 a1,t2 a2,t3 a3):v0(a0),v1(a1),v2(a2),v3(a3){::hx::gc::Runtime::instance().publish(this);}

#define HX_BEGIN_LOCAL_FUNC_S5(SUPER, name, t0, v0, t1, v1, t2, v2, t3, v3, t4, v4) \
    struct name : public SUPER { t0 v0; t1 v1; t2 v2; t3 v3; t4 v4; \
        HX_ZGC_CLOSURE_ALLOC(name, ::hx::traceCaptured(visitor,self->v0); ::hx::traceCaptured(visitor,self->v1); ::hx::traceCaptured(visitor,self->v2); ::hx::traceCaptured(visitor,self->v3); ::hx::traceCaptured(visitor,self->v4);) \
        name(t0 a0,t1 a1,t2 a2,t3 a3,t4 a4):v0(a0),v1(a1),v2(a2),v3(a3),v4(a4){::hx::gc::Runtime::instance().publish(this);}

#define HX_BEGIN_LOCAL_FUNC_S6(SUPER, name, t0, v0, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5) \
    struct name : public SUPER { t0 v0; t1 v1; t2 v2; t3 v3; t4 v4; t5 v5; \
        HX_ZGC_CLOSURE_ALLOC(name, ::hx::traceCaptured(visitor,self->v0); ::hx::traceCaptured(visitor,self->v1); ::hx::traceCaptured(visitor,self->v2); ::hx::traceCaptured(visitor,self->v3); ::hx::traceCaptured(visitor,self->v4); ::hx::traceCaptured(visitor,self->v5);) \
        name(t0 a0,t1 a1,t2 a2,t3 a3,t4 a4,t5 a5):v0(a0),v1(a1),v2(a2),v3(a3),v4(a4),v5(a5){::hx::gc::Runtime::instance().publish(this);}

#define HX_BEGIN_LOCAL_FUNC_S7(SUPER, name, t0, v0, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5, t6, v6) \
    struct name : public SUPER { t0 v0; t1 v1; t2 v2; t3 v3; t4 v4; t5 v5; t6 v6; \
        HX_ZGC_CLOSURE_ALLOC(name, ::hx::traceCaptured(visitor,self->v0); ::hx::traceCaptured(visitor,self->v1); ::hx::traceCaptured(visitor,self->v2); ::hx::traceCaptured(visitor,self->v3); ::hx::traceCaptured(visitor,self->v4); ::hx::traceCaptured(visitor,self->v5); ::hx::traceCaptured(visitor,self->v6);) \
        name(t0 a0,t1 a1,t2 a2,t3 a3,t4 a4,t5 a5,t6 a6):v0(a0),v1(a1),v2(a2),v3(a3),v4(a4),v5(a5),v6(a6){::hx::gc::Runtime::instance().publish(this);}

#define HX_BEGIN_LOCAL_FUNC_S8(SUPER, name, t0, v0, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5, t6, v6, t7, v7) \
    struct name : public SUPER { t0 v0; t1 v1; t2 v2; t3 v3; t4 v4; t5 v5; t6 v6; t7 v7; \
        HX_ZGC_CLOSURE_ALLOC(name, ::hx::traceCaptured(visitor,self->v0); ::hx::traceCaptured(visitor,self->v1); ::hx::traceCaptured(visitor,self->v2); ::hx::traceCaptured(visitor,self->v3); ::hx::traceCaptured(visitor,self->v4); ::hx::traceCaptured(visitor,self->v5); ::hx::traceCaptured(visitor,self->v6); ::hx::traceCaptured(visitor,self->v7);) \
        name(t0 a0,t1 a1,t2 a2,t3 a3,t4 a4,t5 a5,t6 a6,t7 a7):v0(a0),v1(a1),v2(a2),v3(a3),v4(a4),v5(a5),v6(a6),v7(a7){::hx::gc::Runtime::instance().publish(this);}

#define HX_BEGIN_LOCAL_FUNC_S9(SUPER, name, t0, v0, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5, t6, v6, t7, v7, t8, v8) \
    struct name : public SUPER { t0 v0; t1 v1; t2 v2; t3 v3; t4 v4; t5 v5; t6 v6; t7 v7; t8 v8; \
        HX_ZGC_CLOSURE_ALLOC(name, ::hx::traceCaptured(visitor,self->v0); ::hx::traceCaptured(visitor,self->v1); ::hx::traceCaptured(visitor,self->v2); ::hx::traceCaptured(visitor,self->v3); ::hx::traceCaptured(visitor,self->v4); ::hx::traceCaptured(visitor,self->v5); ::hx::traceCaptured(visitor,self->v6); ::hx::traceCaptured(visitor,self->v7); ::hx::traceCaptured(visitor,self->v8);) \
        name(t0 a0,t1 a1,t2 a2,t3 a3,t4 a4,t5 a5,t6 a6,t7 a7,t8 a8):v0(a0),v1(a1),v2(a2),v3(a3),v4(a4),v5(a5),v6(a6),v7(a7),v8(a8){::hx::gc::Runtime::instance().publish(this);}

#define HX_BEGIN_LOCAL_FUNC_S10(SUPER, name, t0, v0, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5, t6, v6, t7, v7, t8, v8, t9, v9) \
    struct name : public SUPER { t0 v0; t1 v1; t2 v2; t3 v3; t4 v4; t5 v5; t6 v6; t7 v7; t8 v8; t9 v9; \
        HX_ZGC_CLOSURE_ALLOC(name, ::hx::traceCaptured(visitor,self->v0); ::hx::traceCaptured(visitor,self->v1); ::hx::traceCaptured(visitor,self->v2); ::hx::traceCaptured(visitor,self->v3); ::hx::traceCaptured(visitor,self->v4); ::hx::traceCaptured(visitor,self->v5); ::hx::traceCaptured(visitor,self->v6); ::hx::traceCaptured(visitor,self->v7); ::hx::traceCaptured(visitor,self->v8); ::hx::traceCaptured(visitor,self->v9);) \
        name(t0 a0,t1 a1,t2 a2,t3 a3,t4 a4,t5 a5,t6 a6,t7 a7,t8 a8,t9 a9):v0(a0),v1(a1),v2(a2),v3(a3),v4(a4),v5(a5),v6(a6),v7(a7),v8(a8),v9(a9){::hx::gc::Runtime::instance().publish(this);}

#define HX_BEGIN_LOCAL_FUNC_S11(SUPER, name, t0, v0, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5, t6, v6, t7, v7, t8, v8, t9, v9, t10, v10) \
    struct name : public SUPER { t0 v0; t1 v1; t2 v2; t3 v3; t4 v4; t5 v5; t6 v6; t7 v7; t8 v8; t9 v9; t10 v10; \
        HX_ZGC_CLOSURE_ALLOC(name, ::hx::traceCaptured(visitor,self->v0); ::hx::traceCaptured(visitor,self->v1); ::hx::traceCaptured(visitor,self->v2); ::hx::traceCaptured(visitor,self->v3); ::hx::traceCaptured(visitor,self->v4); ::hx::traceCaptured(visitor,self->v5); ::hx::traceCaptured(visitor,self->v6); ::hx::traceCaptured(visitor,self->v7); ::hx::traceCaptured(visitor,self->v8); ::hx::traceCaptured(visitor,self->v9); ::hx::traceCaptured(visitor,self->v10);) \
        name(t0 a0,t1 a1,t2 a2,t3 a3,t4 a4,t5 a5,t6 a6,t7 a7,t8 a8,t9 a9,t10 a10):v0(a0),v1(a1),v2(a2),v3(a3),v4(a4),v5(a5),v6(a6),v7(a7),v8(a8),v9(a9),v10(a10){::hx::gc::Runtime::instance().publish(this);}

#define HX_BEGIN_LOCAL_FUNC_S12(SUPER, name, t0, v0, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5, t6, v6, t7, v7, t8, v8, t9, v9, t10, v10, t11, v11) \
    struct name : public SUPER { t0 v0; t1 v1; t2 v2; t3 v3; t4 v4; t5 v5; t6 v6; t7 v7; t8 v8; t9 v9; t10 v10; t11 v11; \
        HX_ZGC_CLOSURE_ALLOC(name, ::hx::traceCaptured(visitor,self->v0); ::hx::traceCaptured(visitor,self->v1); ::hx::traceCaptured(visitor,self->v2); ::hx::traceCaptured(visitor,self->v3); ::hx::traceCaptured(visitor,self->v4); ::hx::traceCaptured(visitor,self->v5); ::hx::traceCaptured(visitor,self->v6); ::hx::traceCaptured(visitor,self->v7); ::hx::traceCaptured(visitor,self->v8); ::hx::traceCaptured(visitor,self->v9); ::hx::traceCaptured(visitor,self->v10); ::hx::traceCaptured(visitor,self->v11);) \
        name(t0 a0,t1 a1,t2 a2,t3 a3,t4 a4,t5 a5,t6 a6,t7 a7,t8 a8,t9 a9,t10 a10,t11 a11):v0(a0),v1(a1),v2(a2),v3(a3),v4(a4),v5(a5),v6(a6),v7(a7),v8(a8),v9(a9),v10(a10),v11(a11){::hx::gc::Runtime::instance().publish(this);}

#define HX_BEGIN_LOCAL_FUNC_S13(SUPER, name, t0, v0, t1, v1, t2, v2, t3, v3, t4, v4, t5, v5, t6, v6, t7, v7, t8, v8, t9, v9, t10, v10, t11, v11, t12, v12) \
    struct name : public SUPER { t0 v0; t1 v1; t2 v2; t3 v3; t4 v4; t5 v5; t6 v6; t7 v7; t8 v8; t9 v9; t10 v10; t11 v11; t12 v12; \
        HX_ZGC_CLOSURE_ALLOC(name, ::hx::traceCaptured(visitor,self->v0); ::hx::traceCaptured(visitor,self->v1); ::hx::traceCaptured(visitor,self->v2); ::hx::traceCaptured(visitor,self->v3); ::hx::traceCaptured(visitor,self->v4); ::hx::traceCaptured(visitor,self->v5); ::hx::traceCaptured(visitor,self->v6); ::hx::traceCaptured(visitor,self->v7); ::hx::traceCaptured(visitor,self->v8); ::hx::traceCaptured(visitor,self->v9); ::hx::traceCaptured(visitor,self->v10); ::hx::traceCaptured(visitor,self->v11); ::hx::traceCaptured(visitor,self->v12);) \
        name(t0 a0,t1 a1,t2 a2,t3 a3,t4 a4,t5 a5,t6 a6,t7 a7,t8 a8,t9 a9,t10 a10,t11 a11,t12 a12):v0(a0),v1(a1),v2(a2),v3(a3),v4(a4),v5(a5),v6(a6),v7(a7),v8(a8),v9(a9),v10(a10),v11(a11),v12(a12){::hx::gc::Runtime::instance().publish(this);}

#define HX_END_LOCAL_FUNC0(ret) \
        ::Dynamic invoke0() override { ret _hx_run(); return ::Dynamic(); } \
    };
#define HX_END_LOCAL_FUNC1(ret) \
        ::Dynamic invoke1(const ::Dynamic& _hx_0) override { \
            ret _hx_run(_hx_0); return ::Dynamic(); \
        } \
    };
#define HX_END_LOCAL_FUNC2(ret) \
        ::Dynamic invoke2(const ::Dynamic& _hx_0, \
                          const ::Dynamic& _hx_1) override { \
            ret _hx_run(_hx_0, _hx_1); return ::Dynamic(); \
        } \
    };
#define HX_END_LOCAL_FUNC3(ret) \
        ::Dynamic invoke3(const ::Dynamic& _hx_0, \
                          const ::Dynamic& _hx_1, \
                          const ::Dynamic& _hx_2) override { \
            ret _hx_run(_hx_0, _hx_1, _hx_2); return ::Dynamic(); \
        } \
    };

#define HX_ZGC_END_LOCAL_MANY_4(ret) ret _hx_run(arguments[0],arguments[1],arguments[2],arguments[3]);
#define HX_ZGC_END_LOCAL_MANY_5(ret) ret _hx_run(arguments[0],arguments[1],arguments[2],arguments[3],arguments[4]);
#define HX_ZGC_END_LOCAL_MANY_6(ret) ret _hx_run(arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],arguments[5]);
#define HX_ZGC_END_LOCAL_MANY_7(ret) ret _hx_run(arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],arguments[5],arguments[6]);
#define HX_ZGC_END_LOCAL_MANY_8(ret) ret _hx_run(arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],arguments[5],arguments[6],arguments[7]);
#define HX_ZGC_DEFINE_END_LOCAL(N) \
    ::Dynamic invokeMany(const ::Dynamic* arguments, int count) override { \
        if (count != N) throw std::runtime_error("Haxe closure arity mismatch"); \
        HX_ZGC_END_LOCAL_MANY_##N(ret) return ::Dynamic(); \
    } \
    };
#define HX_END_LOCAL_FUNC4(ret) ::Dynamic invokeMany(const ::Dynamic* arguments,int count) override { if(count!=4) throw std::runtime_error("Haxe closure arity mismatch"); ret _hx_run(arguments[0],arguments[1],arguments[2],arguments[3]); return ::Dynamic(); } };
#define HX_END_LOCAL_FUNC5(ret) ::Dynamic invokeMany(const ::Dynamic* arguments,int count) override { if(count!=5) throw std::runtime_error("Haxe closure arity mismatch"); ret _hx_run(arguments[0],arguments[1],arguments[2],arguments[3],arguments[4]); return ::Dynamic(); } };
#define HX_END_LOCAL_FUNC6(ret) ::Dynamic invokeMany(const ::Dynamic* arguments,int count) override { if(count!=6) throw std::runtime_error("Haxe closure arity mismatch"); ret _hx_run(arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],arguments[5]); return ::Dynamic(); } };
#define HX_END_LOCAL_FUNC7(ret) ::Dynamic invokeMany(const ::Dynamic* arguments,int count) override { if(count!=7) throw std::runtime_error("Haxe closure arity mismatch"); ret _hx_run(arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],arguments[5],arguments[6]); return ::Dynamic(); } };
#define HX_END_LOCAL_FUNC8(ret) ::Dynamic invokeMany(const ::Dynamic* arguments,int count) override { if(count!=8) throw std::runtime_error("Haxe closure arity mismatch"); ret _hx_run(arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],arguments[5],arguments[6],arguments[7]); return ::Dynamic(); } };

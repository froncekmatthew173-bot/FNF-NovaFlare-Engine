#include <hxcpp.h>

#include <cstddef>
#include <iostream>

class DynamicNode final : public ::hx::Object {
public:
    int value = 0;
    explicit DynamicNode(int inValue) : value(inValue) {}

    int __ToInt() const override { return value; }
    double __ToDouble() const override {
        return static_cast<double>(value) + 0.5;
    }
    String toString() override {
        return String("DynamicNode(") + value + String(")");
    }

    static const ::hx::gc::TypeDescriptor descriptor;
};

const ::hx::gc::TypeDescriptor DynamicNode::descriptor{
    0x44594e4e4f444531ULL, "DynamicNode", nullptr,
    sizeof(DynamicNode), alignof(DynamicNode), 0,
    ::hx::gc::TypeNone, 0, nullptr, nullptr, nullptr,
};

class CatchBase : public ::hx::Object {
public:
    static const ::hx::gc::TypeDescriptor __novaType;
};

class CatchDerived final : public CatchBase {
public:
    static const ::hx::gc::TypeDescriptor __novaType;
};

const ::hx::gc::TypeDescriptor CatchBase::__novaType{
    0x4341544348424153ULL, "CatchBase", nullptr,
    sizeof(CatchBase), alignof(CatchBase), 0,
    ::hx::gc::TypeNone, 0, nullptr, nullptr, nullptr,
};

const ::hx::gc::TypeDescriptor CatchDerived::__novaType{
    0x4341544348444552ULL, "CatchDerived", &CatchBase::__novaType,
    sizeof(CatchDerived), alignof(CatchDerived), 0,
    ::hx::gc::TypeNone, 0, nullptr, nullptr, nullptr,
};

class DynamicHolder final : public ::hx::Object {
public:
    Dynamic value;

    void set(const Dynamic& incoming) {
        auto& source = const_cast<::hx::gc::RefSlot&>(incoming.mPtr);
        void* object = ::hx::gc::Runtime::instance().loadBarrier(source);
        ::hx::gc::Runtime::instance().storeBarrier(this, value.mPtr, object);
        value.copyNonReferenceFrom(incoming);
    }

    static const std::uint32_t referenceOffsets[1];
    static const ::hx::gc::TypeDescriptor descriptor;
};

static ::Dynamic addDynamic(const ::Dynamic& left,
                            const ::Dynamic& right) {
    return ::Dynamic(left.Cast<int>() + right.Cast<int>());
}

const std::uint32_t DynamicHolder::referenceOffsets[1]{
    static_cast<std::uint32_t>(offsetof(DynamicHolder, value)),
};

const ::hx::gc::TypeDescriptor DynamicHolder::descriptor{
    0x44594e484f4c4431ULL, "DynamicHolder", nullptr,
    sizeof(DynamicHolder), alignof(DynamicHolder), 0,
    ::hx::gc::TypeContainsReferences, 1,
    DynamicHolder::referenceOffsets, nullptr, nullptr,
};

class DynamicClosureOwner;

HX_BEGIN_LOCAL_FUNC_S0(::hx::LocalFunc, RelocatingArgumentClosure) HXARGC(3)
bool _hx_run(::String fieldName, ::Dynamic value,
             ::Dynamic optionalArgument) {
    // Generated local Haxe closures do not emit HX_NOVAGC_ROOT_ARG for their
    // converted C++ parameters.  The reflective invocation boundary must keep
    // every referenced argument stable while the closure allocates or reaches
    // a moving collection, otherwise a String/Dynamic parameter can retain a
    // relocation-source address for the rest of the call.
    for (int cycle = 0; cycle < 3; ++cycle) {
        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        // Reuse evacuated regions so merely leaving a forwarding record at
        // the old address cannot make this regression pass accidentally.
        for (int allocation = 0; allocation < 20000; ++allocation) {
            ::String churn(std::string(96, static_cast<char>(
                'a' + (allocation % 26))));
            (void)churn;
        }
    }
    return fieldName == "splashLeft.visible" && value.Cast<bool>() &&
           optionalArgument.isNull();
}
HX_END_LOCAL_FUNC3(return)

HX_BEGIN_DEFAULT_FUNC(DynamicClosureDefault, DynamicClosureOwner)
int _hx_run(int left, int right);
HX_END_LOCAL_FUNC2(return )
HX_END_DEFAULT_FUNC

class DynamicClosureOwner final : public ::hx::Object {
public:
    int value = 0;
    Dynamic callable;

    DynamicClosureOwner() : callable(new DynamicClosureDefault(this)) {}

    static const std::uint32_t referenceOffsets[1];
    static const ::hx::gc::TypeDescriptor descriptor;
};

int DynamicClosureDefault::_hx_run(int left, int right) {
    return __this->value + left + right;
}

const std::uint32_t DynamicClosureOwner::referenceOffsets[1]{
    static_cast<std::uint32_t>(offsetof(DynamicClosureOwner, callable)),
};

const ::hx::gc::TypeDescriptor DynamicClosureOwner::descriptor{
    0x44594e434c4f5731ULL, "DynamicClosureOwner", nullptr,
    sizeof(DynamicClosureOwner), alignof(DynamicClosureOwner), 0,
    ::hx::gc::TypeContainsReferences, 1,
    DynamicClosureOwner::referenceOffsets, nullptr, nullptr,
};

int main() {
    __hxcpp_nova_initialize();
    {
        Dynamic empty;
        Dynamic emptyString(String(""));
        if (empty.Cast<bool>() || empty.Cast<int>() != 0 ||
            empty.Cast<double>() != 0.0 || !empty.Cast<String>().isNull() ||
            empty.Cast<::hx::ObjectPtr<DynamicNode>>() != nullptr) {
            std::cerr << "Dynamic null default conversion failed\n";
            return 1;
        }
        // Lua/JSON compatibility depends on all four falsey-looking Haxe
        // values remaining observably distinct.  In particular an empty
        // String must classify as String, while false and integer zero retain
        // their own tags; none may collapse into Dynamic null.
        if (emptyString.isNull() || emptyString.tag() != Dynamic::Tag::String ||
            emptyString.__GetType() != 3 ||
            emptyString.Cast<String>().isNull() ||
            emptyString.Cast<String>() != String("") ||
            Dynamic(false).isNull() ||
            Dynamic(false).__GetType() != 2 ||
            Dynamic(0).isNull() || Dynamic(0).__GetType() != 0xff) {
            std::cerr << "Dynamic null/false/zero/empty-string distinction failed\n";
            return 22;
        }
        if (::hx::IsEq(empty, false) || ::hx::IsEq(empty, 0) ||
            !::hx::IsNotEq(empty, false) ||
            !::hx::IsEq(Dynamic(false), false) ||
            !::hx::IsEq(Dynamic(true), 1) ||
            !::hx::IsEq(Dynamic(7), 7.0) ||
            !::hx::IsEq(Dynamic(7), Dynamic(7.0)) ||
            ::hx::IsEq(Dynamic(String("0")), 0)) {
            std::cerr << "Dynamic null/numeric equality semantics failed\n";
            return 4;
        }

        auto catchDerived =
            ::hx::allocate<CatchDerived>(CatchDerived::__novaType);
        Dynamic caughtObject(catchDerived);
        if (!Dynamic(7).IsClass<int>() ||
            Dynamic(7).IsClass<bool>() ||
            !Dynamic(7).IsClass<double>() ||
            !Dynamic(7.5).IsClass<double>() ||
            Dynamic(7.5).IsClass<int>() ||
            !Dynamic(true).IsClass<bool>() ||
            Dynamic(true).IsClass<int>() ||
            !Dynamic(String("caught")).IsClass<String>() ||
            !caughtObject.IsClass<::hx::ObjectPtr<CatchDerived>>() ||
            !caughtObject.IsClass<::hx::ObjectPtr<CatchBase>>() ||
            !::__instanceof(caughtObject,
                ::hx::ClassOf<::hx::ObjectPtr<CatchDerived>>()) ||
            !::__instanceof(caughtObject,
                ::hx::ClassOf<::hx::ObjectPtr<CatchBase>>())) {
            std::cerr << "Dynamic catch/isOfType classification failed\n";
            return 16;
        }

        bool caughtNullFunction = false;
        try {
            empty();
        } catch (const ::Dynamic& error) {
            caughtNullFunction =
                error.Cast<::String>() == "Null Function Pointer";
        }
        if (!caughtNullFunction) {
            std::cerr << "null closure call did not raise a Haxe exception\n";
            return 14;
        }

        Dynamic reflected = ::hx::CreateStaticFunction2(
            "addDynamic", &addDynamic);
        if (reflected(Dynamic(5)).Cast<int>() != 5 ||
            reflected(Dynamic(5), Dynamic(6), Dynamic(99)).Cast<int>() != 11) {
            std::cerr << "reflective closure arity normalization failed\n";
            return 15;
        }

        // Lua callbacks build a VirtualArray and Reflect.callMethod forwards
        // it through Dynamic::__Run.  Reproduce that exact two-argument call
        // into a three-argument closure (the last parameter is optional), and
        // force a moving collection after the generated String conversion.
        // The real failure changed "splashLeft.visible" into "true" before
        // LuaUtils.setVarInArray observed it.
        HX_NOVAGC_VARI(::cpp::VirtualArray, luaArguments) =
            ::cpp::VirtualArray_obj::__new(0);
        luaArguments->set(0, ::Dynamic(::String("splashLeft.visible")));
        luaArguments->set(1, ::Dynamic(true));
        HX_NOVAGC_VARI(::Dynamic, relocatingArgumentClosure) =
            ::Dynamic(new RelocatingArgumentClosure());
        if (!relocatingArgumentClosure->__Run(luaArguments).Cast<bool>() ||
            luaArguments->__get(0).Cast<::String>() !=
                "splashLeft.visible" ||
            !luaArguments->__get(1).Cast<bool>()) {
            std::cerr << "reflective closure arguments changed during relocation\n";
            return 20;
        }

        // Use the same two-phase helper as generated Haxe classes: the owner
        // is published as an all-null shell and pinned before its native
        // constructor allocates the default dynamic-method closure.
        HX_NOVAGC_VARI(::hx::ObjectPtr<DynamicClosureOwner>,
                       rootedClosureOwner) =
            ::hx::allocate<DynamicClosureOwner>(DynamicClosureOwner::descriptor);
        rootedClosureOwner->value = 100;
        // A default dynamic method captures its receiver while that receiver
        // is still under construction.  Force a complete cycle before the
        // first call so the test covers precise tracing, colored-reference
        // healing and receiver relocation rather than only the allocation
        // fast path.
        __hxcpp_collect(true);
        if (rootedClosureOwner->callable(1, 2).Cast<int>() != 103) {
            std::cerr << "dynamic default closure lost its receiver\n";
            return 17;
        }

        // Exercise the allocation-triggered young/full collection windows as
        // well as an explicit post-construction collection.  A pinning bug can
        // otherwise leave the closure pointing at the zero-filled relocation
        // source while the returned owner receives the Haxe field writes.
        for (int iteration = 0; iteration < 100000; ++iteration) {
            HX_NOVAGC_VARI(::hx::ObjectPtr<DynamicClosureOwner>, stressOwner) =
                ::hx::allocate<DynamicClosureOwner>(
                    DynamicClosureOwner::descriptor);
            stressOwner->value = iteration + 100;
            if (stressOwner->callable(1, 2).Cast<int>() != iteration + 103) {
                std::cerr << "dynamic default closure captured relocation source at "
                          << iteration << '\n';
                return 18;
            }
        }

        Dynamic assignedScalar;
        assignedScalar = 0;
        if (assignedScalar.tag() != Dynamic::Tag::Integer ||
            assignedScalar.Cast<int>() != 0 || assignedScalar.isNull()) {
            std::cerr << "Dynamic integer zero assignment became null\n";
            return 6;
        }
        assignedScalar = false;
        if (assignedScalar.tag() != Dynamic::Tag::Boolean ||
            assignedScalar.Cast<bool>()) {
            std::cerr << "Dynamic Boolean assignment failed\n";
            return 7;
        }
        assignedScalar = 2.5;
        if (assignedScalar.tag() != Dynamic::Tag::Floating ||
            assignedScalar.Cast<double>() != 2.5) {
            std::cerr << "Dynamic floating assignment failed\n";
            return 8;
        }

        if (Dynamic(42).Cast<String>() != String("42") ||
            Dynamic(true).Cast<String>() != String("true") ||
            Dynamic(2.5).Cast<String>() != String("2.5") ||
            Dynamic(String("17")).Cast<int>() != 17 ||
            Dynamic(String("3.25")).Cast<double>() != 3.25 ||
            !Dynamic(String("17")).Cast<bool>() ||
            Dynamic(String("0")).Cast<bool>() ||
            Dynamic(0.5).Cast<bool>()) {
            std::cerr << "Dynamic hxcpp scalar coercion semantics failed\n";
            return 12;
        }

        // linc_luajit emits TCast<String>::cast(lua_tostring(...)).  A C
        // string must take the native-string path, never the standard
        // pointer-to-bool conversion that previously produced "true" for
        // every non-null Lua string (including splashLeft.alpha).
        const char* luaProperty = "splashLeft.alpha";
        char mutableLuaProperty[] = "splashDown.alpha";
        const char16_t* wideNativeProperty = u"camera.zoom";
        if (::hx::TCast<::String>::cast(luaProperty) !=
                "splashLeft.alpha" ||
            ::hx::TCast<::String>::cast(mutableLuaProperty) !=
                "splashDown.alpha" ||
            ::hx::TCast<::String>::cast(wideNativeProperty) !=
                "camera.zoom" ||
            ::Dynamic(luaProperty).Cast<::String>() !=
                "splashLeft.alpha") {
            std::cerr << "native C string was coerced through bool\n";
            return 21;
        }

        HX_NOVAGC_VARI(::Array<int>, typedArray) =
            ::Array_obj<int>::__new(2);
        HX_NOVAGC_VARI(::hx::ObjectPtr<::hx::Anon_obj>, anonymous) =
            ::hx::Anon_obj::Create(1)->setFixed(
                0, String("value"), Dynamic(1));
        Dynamic stringValue(String("type"));
        Dynamic arrayValue(typedArray);
        Dynamic anonymousValue(anonymous);
        Dynamic classValue(::hx::ClassOf<::hx::ObjectPtr<DynamicNode>>());
        auto classInstance = ::hx::allocate<DynamicNode>(
            DynamicNode::descriptor, 5);
        Dynamic instanceValue(classInstance);
        // Base hx::Object indexing is a reflection probe in Haxe/Lua code.
        // Original hxcpp returns null for non-indexable managed objects rather
        // than leaking a std::runtime_error through the native CFFI callback.
        if (!instanceValue.__GetItem(0).isNull() ||
            !instanceValue.__SetItem(0, Dynamic(9)).isNull() ||
            classInstance->value != 5) {
            std::cerr << "Dynamic non-indexable object fallback failed\n";
            return 19;
        }
        auto instanceClass = instanceValue.__GetClass();
        auto anonymousClass = anonymousValue.__GetClass();
        if (stringValue.__GetType() != 3 ||
            arrayValue.__GetType() != 5 ||
            anonymousValue.__GetType() != 4 ||
            classValue.__GetType() != 4 ||
            instanceValue.__GetType() != 8 ||
            arrayValue.__GetClass() != ::hx::ArrayClass() ||
            !instanceClass || !anonymousClass ||
            Dynamic(instanceClass).toString() != String("DynamicNode") ||
            Dynamic(anonymousClass).toString() != String("__Anon") ||
            classValue.toString() != String("DynamicNode")) {
            std::cerr << "Dynamic Haxe runtime type classification failed\n";
            return 11;
        }
        if (instanceValue.Cast<int>() != 5 ||
            instanceValue.Cast<double>() != 5.5 ||
            instanceValue.Cast<String>() != String("DynamicNode(5)")) {
            std::cerr << "Dynamic object virtual coercion semantics failed\n";
            return 13;
        }

        ::hx::ObjectPtr<DynamicNode> typedNull;
        Dynamic boxedTypedNull(typedNull);
        Dynamic boxedRawNull(static_cast<DynamicNode*>(nullptr));
        Dynamic assignedTypedNull(42);
        assignedTypedNull = typedNull;
        if (!boxedTypedNull.isNull() || !boxedRawNull.isNull() ||
            !assignedTypedNull.isNull() ||
            ::hx::IsNotNull(boxedTypedNull) ||
            boxedTypedNull.Cast<bool>() ||
            boxedTypedNull.Cast<::hx::ObjectPtr<DynamicNode>>() != nullptr ||
            boxedTypedNull.toString() != String("null") ||
            boxedTypedNull.__GetType() != 0 ||
            !boxedTypedNull.__GetItem(0).isNull() ||
            !boxedTypedNull.__SetItem(0, Dynamic(7)).isNull()) {
            std::cerr << "Dynamic typed-null normalization failed\n";
            return 5;
        }

        HX_NOVAGC_VARI(::hx::ObjectPtr<DynamicHolder>, root) =
            ::hx::allocate<DynamicHolder>(DynamicHolder::descriptor);
        auto node = ::hx::allocate<DynamicNode>(DynamicNode::descriptor, 73);

        Dynamic assignedManagedPointer;
        assignedManagedPointer = node.get();
        if (assignedManagedPointer.tag() != Dynamic::Tag::Object ||
            assignedManagedPointer.Cast<::hx::ObjectPtr<DynamicNode>>() != node) {
            std::cerr << "Dynamic managed raw-pointer assignment became scalar\n";
            return 9;
        }
        assignedManagedPointer = static_cast<DynamicNode*>(nullptr);
        if (!assignedManagedPointer.isNull()) {
            std::cerr << "Dynamic managed null-pointer assignment failed\n";
            return 10;
        }

        root->set(Dynamic(node));

        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        auto recovered = root->value.Cast<::hx::ObjectPtr<DynamicNode>>();
        if (!recovered || recovered->value != 73) {
            std::cerr << "Dynamic object reference failed after relocation\n";
            return 2;
        }

        root->set(Dynamic(42));
        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        if (root->value.Cast<int>() != 42) {
            std::cerr << "Dynamic scalar tag failed after collection\n";
            return 3;
        }
    }
    __hxcpp_nova_shutdown();
    std::cout << "precise Dynamic tagged reference test passed\n";
    return 0;
}

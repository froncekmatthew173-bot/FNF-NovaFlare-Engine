#include <hx/CFFI.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <bit>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

[[nodiscard]] vkind alloc_kind();
[[nodiscard]] int val_type(value input);
[[nodiscard]] vkind val_kind(value input);
[[nodiscard]] int val_fun_nargs(value input);
[[nodiscard]] bool val_is_buffer(value input);
void buffer_set_size(buffer target, int length);

namespace {
extern "C" void* hx_cffi(const char* name) noexcept;

value expectedManagedFinalizerToken = nullptr;
int managedFinalizerCalls = 0;
value expectedPointerFinalizerToken = nullptr;
int pointerFinalizerPayload = 91;
int pointerFinalizerCalls = 0;
value expectedOwnedAbstractToken = nullptr;
void* expectedOwnedAbstractData = nullptr;
int ownedAbstractFinalizerCalls = 0;
std::atomic<bool> externalWaitEntered{false};
std::atomic<bool> releaseExternalWait{false};

struct FieldIteration final {
    std::vector<field> fields;
    std::vector<value> values;
};

void managedFinalizer(value input) {
    assert(input == expectedManagedFinalizerToken);
    assert(val_is_array(input));
    assert(val_int(val_array_i(input, 0)) == 73);
    ++managedFinalizerCalls;
}

void pointerFinalizer(value input) {
    assert(input == expectedPointerFinalizerToken);
    assert(val_data(input) == &pointerFinalizerPayload);
    ++pointerFinalizerCalls;
}

void ownedAbstractFinalizer(value input) {
    assert(input == expectedOwnedAbstractToken);
    assert(val_data(input) == expectedOwnedAbstractData);
    ++ownedAbstractFinalizerCalls;
}

void collectField(value, field fieldId, void* context) {
    static_cast<FieldIteration*>(context)->fields.push_back(fieldId);
}

void collectFieldValue(value input, field fieldId, void* context) {
    auto* iteration = static_cast<FieldIteration*>(context);
    iteration->fields.push_back(fieldId);
    iteration->values.push_back(input);
}

int nova_prime_add(int left, int right) { return left + right; }
value nova_prime_echo(value input) { return input; }

Dynamic closureMemberEcho(::hx::Object*, const Dynamic& input) {
    return input;
}

Dynamic closureMemberOther(::hx::Object*, const Dynamic& input) {
    return Dynamic(input.Cast<int>() + 1);
}

Dynamic closureStaticEcho(const Dynamic& input) { return input; }

Dynamic closureStaticZero() { return Dynamic(88); }

Dynamic closureStaticOther(const Dynamic& input) {
    return Dynamic(input.Cast<int>() + 1);
}

void* nova_external_prime_echo(void* input) {
    value token = reinterpret_cast<value>(input);
    assert(::hx::cffi::toDynamic(token).Cast<String>() == "external");
    __hxcpp_collect(true);
    __hxcpp_gc_compact();
    assert(::hx::cffi::toDynamic(token).Cast<String>() == "external");
    return input;
}

::hx::cffi::LegacyPrimeString nova_external_prime_string_echo(
        ::hx::cffi::LegacyPrimeString input) {
    assert(input.length == 12);
    assert(std::memcmp(input.data, "prime-string", 12) == 0);
    __hxcpp_collect(true);
    __hxcpp_gc_compact();
    assert(std::memcmp(input.data, "prime-string", 12) == 0);
    return input;
}

::hx::cffi::LegacyPrimeString nova_external_prime_unicode_echo(
        ::hx::cffi::LegacyPrimeString input) {
    static constexpr char Utf8Text[] =
        "\xe6\x96\xb0\xe6\x98\x9f\xe9\x97\xaa\xe8\x80\x80";
    assert(input.length == 12);
    assert(std::memcmp(input.data, Utf8Text, 12) == 0);
    return input;
}

void nova_external_prime_fill(void* image, void* rect, int rg, int ba) {
    value imageToken = reinterpret_cast<value>(image);
    value rectToken = reinterpret_cast<value>(rect);
    assert(val_int(val_field(imageToken, val_id("width"))) == 320);
    assert(val_int(val_field(rectToken, val_id("height"))) == 180);
    assert(rg == 0x12345678);
    assert(ba == 0x10203040);
}

int nova_external_prime_wait(int input) {
    externalWaitEntered.store(true, std::memory_order_release);
    while (!releaseExternalWait.load(std::memory_order_acquire))
        std::this_thread::yield();
    return input;
}

void* nova_external_prime_callback(void* function) {
    return reinterpret_cast<void*>(val_call0(reinterpret_cast<value>(function)));
}

template<class Function>
void* nativeAddress(Function function) {
    static_assert(sizeof(Function) == sizeof(void*));
    return std::bit_cast<void*>(function);
}

template<class Function>
Function resolvedCffi(const char* name) {
    static_assert(sizeof(Function) == sizeof(void*));
    void* address = hx_cffi(name);
    assert(address != nullptr);
    return std::bit_cast<Function>(address);
}
}

DEFINE_PRIME2(nova_prime_add);
DEFINE_PRIME1(nova_prime_echo);

int main() {
    __hxcpp_nova_initialize();

    assert((::hx::Anon_obj::descriptor.flags &
            ::hx::gc::TypeVariableSize) != 0);
    assert((::hx::Anon_obj::descriptor.flags &
            ::hx::gc::TypeFinalizable) == 0);
    {
        auto packed = ::hx::Anon_obj::Create(2);
        ::hx::gc::RefSlot* packedRoots[]{&packed.mPtr};
        ::hx::gc::RootScope packedRootScope(packedRoots, 1);
        packed = packed->setFixed(0, String("real"), Dynamic(1.25));
        packed = packed->setFixed(1, String("label"),
                                  Dynamic(String("precise")));
        Dynamic packedDynamic(packed);
        assert(packedDynamic->__Field(String("real"), ::hx::paccDynamic)
                   .Cast<double>() == 1.25);
        assert(packedDynamic->__Field(String("label"), ::hx::paccDynamic)
                   .Cast<String>() == "precise");
        HX_NOVAGC_VARI(::Array<::String>, fixedFields) =
            ::Array_obj<::String>::__new();
        packedDynamic->__GetFields(fixedFields);
        assert(fixedFields->contains(String("real")));
        assert(fixedFields->contains(String("label")));

        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        assert(packedDynamic->__Field(String("label"), ::hx::paccDynamic)
                   .Cast<String>() == "precise");

        // A fixed generated record may still gain Reflect fields.  Verify the
        // cold overflow dictionary without making every fixed record
        // finalizable again.
        (void)packedDynamic->__SetField(String("overflow"), Dynamic(73),
                                        ::hx::paccDynamic);
        assert(packedDynamic->__Field(String("overflow"), ::hx::paccDynamic)
                   .Cast<int>() == 73);
        HX_NOVAGC_VARI(::Array<::String>, overflowFields) =
            ::Array_obj<::String>::__new();
        packedDynamic->__GetFields(overflowFields);
        assert(overflowFields->contains(String("real")));
        assert(overflowFields->contains(String("label")));
        assert(overflowFields->contains(String("overflow")));
        assert(__hxcpp_anon_remove(packedDynamic, String("real")));
        assert(packedDynamic->__Field(String("real"), ::hx::paccDynamic)
                   .isNull());
        assert(__hxcpp_anon_remove(packedDynamic, String("overflow")));
    }

    {
        auto dictionary = ::hx::Anon_obj::Create(0);
        ::hx::gc::RefSlot* dictionaryRoots[]{&dictionary.mPtr};
        ::hx::gc::RootScope dictionaryRootScope(dictionaryRoots, 1);
        Dynamic dynamicDictionary(dictionary);
        (void)dynamicDictionary->__SetField(String("dynamic"), Dynamic(99),
                                             ::hx::paccDynamic);
        HX_NOVAGC_VARI(::Array<::String>, dynamicFields) =
            ::Array_obj<::String>::__new();
        dynamicDictionary->__GetFields(dynamicFields);
        assert(dynamicFields->contains(String("dynamic")));

        // Promote the dictionary, then store a young managed value in its
        // native FieldState side table. The slot itself is outside the managed
        // heap, so correctness depends on the setter explicitly dirtying the
        // dictionary's old owner region.
        (void)::hx::gc::Runtime::instance().collectYoung();
        dictionary = static_cast<::hx::Anon_obj*>(
            ::hx::gc::Runtime::instance().loadBarrier(dictionary.mPtr));
        Dynamic promotedDictionary(dictionary);
        {
            Dynamic youngSideValue(String("young-side-table"));
            ::hx::gc::RefSlot* youngRoots[]{&youngSideValue.mPtr};
            ::hx::gc::RootScope youngRootScope(youngRoots, 1);
            (void)promotedDictionary->__SetField(
                String("youngManaged"), youngSideValue, ::hx::paccDynamic);
        }
        const auto youngSideTable =
            ::hx::gc::Runtime::instance().collectYoung();
        assert(youngSideTable.rememberedRegions != 0);
        assert(promotedDictionary->__Field(
                   String("youngManaged"), ::hx::paccDynamic)
                   .Cast<String>() == "young-side-table");

        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        assert(dynamicDictionary->__Field(String("dynamic"), ::hx::paccDynamic)
                   .Cast<int>() == 99);
        assert(__hxcpp_anon_remove(dynamicDictionary, String("dynamic")));
    }

    value* longLivedRoot = alloc_root();
    value externalRoot = nullptr;
    {
        ::hx::cffi::CallScope call;
        const char* requiredCffiSymbols[]{
            "create_abstract", "free_abstract", "alloc_best_int",
            "alloc_empty_string",
            "alloc_int32", "val_array_set_size", "val_array_bool",
            "val_array_int", "val_array_float", "val_array_value",
            "val_buffer", "buffer_append_sub", "buffer_to_string",
            "val_gc_ptr", "val_call0_traceexcept", "val_ocall0",
            "val_ocall1", "val_ocall2", "val_ocallN",
            "alloc_field_numeric", "val_field_numeric",
            "val_field_name", "val_iter_fields", "val_iter_field_vals",
            "gc_set_top_of_stack", "gc_enter_blocking",
            "gc_exit_blocking", "gc_try_blocking", "gc_try_unblocking",
            "gc_safe_point", "gc_change_managed_memory",
            "val_gc_add_root", "val_gc_remove_root", "hx_alloc",
            "hx_register_prim", "hx_error", "hx_fail",
        };
        for (const char* symbol : requiredCffiSymbols)
            assert(hx_cffi(symbol) != nullptr);

        value object = alloc_empty_object();
        assert(val_int(alloc_best_int(-19)) == -19);
        assert(val_int(alloc_int32(0x12345678)) == 0x12345678);
        assert(val_strlen(alloc_empty_string(7)) == 7);
        assert(val_type(alloc_null()) == 0);
        assert(val_type(alloc_int(7)) == 0xff);
        assert(val_type(alloc_float(1.5)) == 1);
        assert(val_type(alloc_bool(true)) == 2);
        assert(val_type(alloc_string("type")) == 3);
        assert(val_type(object) == 4);
        Array<int> nullManagedArray;
        assert(Dynamic(nullManagedArray).tag() == Dynamic::Tag::Object);
        assert(::hx::cffi::fromDynamic(Dynamic(nullManagedArray)) == nullptr);
        const int name = val_id("name");
        const int items = val_id("items");
        alloc_field(object, name, alloc_string("nova"));
        const int numeric = val_id("numeric");
        alloc_field_numeric(object, numeric, 19.25);
        assert(val_field_numeric(object, numeric) == 19.25);
        assert(std::strcmp(val_string(val_field_name(numeric)), "numeric") ==
               0);

        FieldIteration fieldNames;
        val_iter_fields(object, &collectField, &fieldNames);
        assert(std::find(fieldNames.fields.begin(), fieldNames.fields.end(),
                         name) != fieldNames.fields.end());
        assert(std::find(fieldNames.fields.begin(), fieldNames.fields.end(),
                         numeric) != fieldNames.fields.end());
        FieldIteration fieldValues;
        val_iter_field_vals(object, &collectFieldValue, &fieldValues);
        assert(fieldValues.fields.size() == fieldValues.values.size());
        assert(std::find(fieldValues.fields.begin(), fieldValues.fields.end(),
                         numeric) != fieldValues.fields.end());

        value array = alloc_array(0);
        assert(val_type(array) == 5);
        val_array_push(array, alloc_int(7));
        val_array_push(array, alloc_string("moving"));
        alloc_field(object, items, array);
        *longLivedRoot = object;

        __hxcpp_collect(true);
        __hxcpp_gc_compact();

        assert(std::strcmp(val_string(val_field(object, name)), "nova") == 0);
        value movedArray = val_field(object, items);
        assert(val_array_size(movedArray) == 2);
        assert(val_int(val_array_i(movedArray, 0)) == 7);
        assert(std::strcmp(val_string(val_array_i(movedArray, 1)),
                           "moving") == 0);
        Dynamic dynamicObject = ::hx::cffi::toDynamic(object);
        Dynamic dynamicItems = dynamicObject->__Field(
            String("items"), ::hx::paccDynamic);
        assert(dynamicItems.Cast<Array<Dynamic>>()->get_length() == 2);
        (void)dynamicObject->__SetField(String("dynamicWrite"), Dynamic(99),
                                        ::hx::paccDynamic);
        assert(val_int(val_field(object, val_id("dynamicWrite"))) == 99);

        buffer bytes = alloc_buffer_len(4);
        std::memcpy(buffer_data(bytes), "ZGC!", 4);
        assert(buffer_size(bytes) == 4);
        assert(std::memcmp(buffer_data(bytes), "ZGC!", 4) == 0);
        assert(buffer_val(bytes) == bytes);
        Dynamic bytesDynamic = ::hx::cffi::toDynamic(buffer_val(bytes));
        assert(::hx::gc::Runtime::instance().typeOf(
                   bytesDynamic.managedPointer()) ==
               &Array_obj<unsigned char>::descriptor);
        Array<unsigned char> bytesArray =
            bytesDynamic.Cast<Array<unsigned char>>();
        assert(bytesArray->get_length() == 4);
        assert(bytesArray->__get(0) == static_cast<unsigned char>('Z'));
        assert(bytesArray->__get(3) == static_cast<unsigned char>('!'));
        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        assert(bytesArray->get_length() == 4);
        assert(bytesArray->__get(1) == static_cast<unsigned char>('G'));
        buffer_set_size(bytes, 6);
        buffer_append_char(bytes, '?');
        assert(bytesArray->get_length() == 7);
        assert(bytesArray->__get(6) == static_cast<unsigned char>('?'));
        buffer_append_sub(bytes, "SUBTAIL", 3);
        val_buffer(bytes, alloc_int(73));
        value bytesAsString = buffer_to_string(bytes);
        assert(std::string_view(val_string(bytesAsString),
                                static_cast<std::size_t>(
                                    val_strlen(bytesAsString))) ==
               std::string_view("ZGC!\0\0?SUB73", 12));

        Array<unsigned char> byteArray =
            Array_obj<unsigned char>::__new(4);
        value byteArrayToken = ::hx::cffi::fromDynamic(Dynamic(byteArray));
        assert(val_is_buffer(byteArrayToken));
        assert(val_to_buffer(byteArrayToken) == byteArrayToken);
        char* byteData = buffer_data(byteArrayToken);
        std::memcpy(byteData, "MOVE", 4);
        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        assert(std::memcmp(byteData, "MOVE", 4) == 0);
        assert(buffer_size(byteArrayToken) == 4);
        buffer_set_size(byteArrayToken, 6);
        assert(buffer_size(byteArrayToken) == 6);
        buffer_append_char(byteArrayToken, '!');
        assert(buffer_size(byteArrayToken) == 7);
        assert(buffer_data(byteArrayToken)[6] == '!');

        Array<int> intArray = Array_obj<int>::__new();
        intArray->push(11);
        intArray->push(22);
        value intArrayToken = ::hx::cffi::fromDynamic(Dynamic(intArray));
        assert(val_is_array(intArrayToken));
        assert(val_array_size(intArrayToken) == 2);
        assert(val_int(val_array_i(intArrayToken, 1)) == 22);
        val_array_set_i(intArrayToken, 0, alloc_int(33));
        val_array_push(intArrayToken, alloc_int(44));
        assert(intArray->__get(0) == 33);
        assert(intArray->__get(2) == 44);
        assert(val_array_double(intArrayToken)[1] == 22.0);
        int* directInts = val_array_int(intArrayToken);
        assert(directInts != nullptr);
        directInts[1] = 55;
        assert(intArray->__get(1) == 55);
        val_array_set_size(intArrayToken, 5);
        assert(intArray->get_length() == 5);

        Array<bool> boolArray = Array_obj<bool>::__new(2);
        value boolArrayToken = ::hx::cffi::fromDynamic(Dynamic(boolArray));
        bool* directBools = val_array_bool(boolArrayToken);
        assert(directBools != nullptr);
        directBools[0] = true;
        directBools[1] = false;
        assert(boolArray->__get(0) && !boolArray->__get(1));

        Array<float> floatArray = Array_obj<float>::__new(2);
        value floatArrayToken = ::hx::cffi::fromDynamic(Dynamic(floatArray));
        float* directFloats = val_array_float(floatArrayToken);
        assert(directFloats != nullptr);
        directFloats[0] = 3.25F;
        directFloats[1] = -7.5F;
        assert(floatArray->__get(0) == 3.25F &&
               floatArray->__get(1) == -7.5F);

        Array<Dynamic> valueArray = Array_obj<Dynamic>::__new(2);
        valueArray->set(0, Dynamic(1));
        valueArray->set(1, Dynamic(String("before")));
        value valueArrayToken = ::hx::cffi::fromDynamic(Dynamic(valueArray));
        {
            ::hx::cffi::CallScope proxyScope;
            value* directValues = val_array_value(valueArrayToken);
            assert(directValues != nullptr);
            directValues[0] = alloc_int(72);
            directValues[1] = alloc_string("after");
        }
        assert(valueArray->__get(0).Cast<int>() == 72);
        assert(valueArray->__get(1).Cast<String>() == "after");

        Array<int> firstClosureOwner;
        Array<int> secondClosureOwner;
        Dynamic memberFirst;
        Dynamic memberSame;
        Dynamic memberDifferentOwner;
        Dynamic memberDifferentFunction;
        Dynamic staticZero;
        Dynamic staticFirst;
        Dynamic staticSame;
        Dynamic staticDifferent;
        ::hx::gc::RefSlot* closureRoots[]{
            &firstClosureOwner.mPtr,
            &secondClosureOwner.mPtr,
            &memberFirst.mPtr,
            &memberSame.mPtr,
            &memberDifferentOwner.mPtr,
            &memberDifferentFunction.mPtr,
            &staticZero.mPtr,
            &staticFirst.mPtr,
            &staticSame.mPtr,
            &staticDifferent.mPtr,
        };
        ::hx::gc::RootScope closureRootScope(closureRoots, 10);
        firstClosureOwner = Array_obj<int>::__new();
        secondClosureOwner = Array_obj<int>::__new();
        memberFirst = ::hx::CreateMemberFunction1(
            "memberEcho", firstClosureOwner.get(), &closureMemberEcho);
        memberSame = ::hx::CreateMemberFunction1(
            "memberEcho", firstClosureOwner.get(), &closureMemberEcho);
        memberDifferentOwner = ::hx::CreateMemberFunction1(
            "memberEcho", secondClosureOwner.get(), &closureMemberEcho);
        memberDifferentFunction = ::hx::CreateMemberFunction1(
            "memberOther", firstClosureOwner.get(), &closureMemberOther);
        staticZero = ::hx::CreateStaticFunction0(
            "staticZero", &closureStaticZero);
        staticFirst = ::hx::CreateStaticFunction1(
            "staticEcho", &closureStaticEcho);
        staticSame = ::hx::CreateStaticFunction1(
            "staticEcho", &closureStaticEcho);
        staticDifferent = ::hx::CreateStaticFunction1(
            "staticOther", &closureStaticOther);

        assert(memberFirst.isMemberFunction());
        assert(memberFirst.memberFunctionArity() == 1);
        assert(memberFirst.__GetType() == 6);
        assert(memberFirst.__ArgCount() == 1);
        assert(memberFirst(Dynamic(73)).Cast<int>() == 73);
        value memberToken = ::hx::cffi::fromDynamic(memberFirst);
        assert(val_type(memberToken) == 6);
        assert(val_fun_nargs(memberToken) == 1);
        assert(::hx::cffi::toDynamic(memberToken)(Dynamic(74)).Cast<int>() ==
               74);
        value staticZeroToken = ::hx::cffi::fromDynamic(staticZero);
        assert(val_int(val_call0_traceexcept(staticZeroToken)) == 88);

        HX_NOVAGC_VARI(::Array<::Dynamic>, escapedMethods) =
            ::Array_obj<::Dynamic>::__new();
        escapedMethods->push(memberFirst);
        const int callbackField = val_id("callback");
        alloc_field(object, callbackField, memberToken);
        assert(val_int(val_ocall1(object, callbackField, alloc_int(77))) ==
               77);

        assert(__hxcpp_same_closure(memberFirst, memberSame));
        assert(!__hxcpp_same_closure(memberFirst, memberDifferentOwner));
        assert(!__hxcpp_same_closure(memberFirst, memberDifferentFunction));
        assert(__hxcpp_same_closure(staticFirst, staticSame));
        assert(!__hxcpp_same_closure(staticFirst, staticDifferent));
        assert(!__hxcpp_same_closure(memberFirst, staticFirst));
        assert(__hxcpp_same_closure(Dynamic(), Dynamic()));

        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        assert(escapedMethods->__get(0)(Dynamic(75)).Cast<int>() == 75);
        value escapedToken = val_field(object, callbackField);
        assert(val_type(escapedToken) == 6);
        assert(::hx::cffi::toDynamic(escapedToken)(Dynamic(76)).Cast<int>() ==
               76);
        assert(__hxcpp_same_closure(memberFirst, memberSame));
        assert(!__hxcpp_same_closure(memberFirst, memberDifferentOwner));
        assert(!__hxcpp_same_closure(memberFirst, memberDifferentFunction));
        assert(__hxcpp_same_closure(staticFirst, staticSame));
        assert(!__hxcpp_same_closure(staticFirst, staticDifferent));

        Dynamic add = __hxcpp_zgc_load_prime(
            String("self"), String("nova_prime_add"), String("iii"), false);
        value addToken = ::hx::cffi::fromDynamic(add);
        assert(val_type(addToken) == 6);
        assert(val_fun_nargs(addToken) == 2);
        assert(add(Dynamic(20), Dynamic(22)).Cast<int>() == 42);
        Dynamic echo = __hxcpp_zgc_load_prime(
            String("self"), String("nova_prime_echo"), String("oo"), false);
        value echoed = ::hx::cffi::fromDynamic(echo(Dynamic(String("exact"))));
        assert(std::strcmp(val_string(echoed), "exact") == 0);

        ::cpp::Function<int(int, int)> direct(&nova_prime_add);
        assert(direct(19, 23) == 42);

        void* echoAddress = nativeAddress(&nova_external_prime_echo);
        ::hx::cffi::registerExternalPrime(echoAddress);
        ::cpp::Function<Dynamic(Dynamic)> externalEcho(
            Dynamic::fromNativePointer(echoAddress));
        Dynamic externalResult = externalEcho(Dynamic(String("external")));
        assert(externalResult.tag() == Dynamic::Tag::String);
        assert(externalResult.Cast<String>() == "external");

        void* callbackAddress = nativeAddress(
            &nova_external_prime_callback);
        ::hx::cffi::registerExternalPrime(callbackAddress);
        ::cpp::Function<Dynamic(Dynamic)> externalCallback(
            Dynamic::fromNativePointer(callbackAddress));
        Dynamic callbackResult = externalCallback(
            ::hx::CreateStaticFunction0("closureStaticZero",
                                        &closureStaticZero));
        assert(callbackResult.Cast<int>() == 88);

        // A Prime that waits in a driver/NDLL has no generated safepoint to
        // acknowledge. The wrapper must publish its exact argument handles
        // and mark the thread native-blocked for the duration of that call.
        void* waitAddress = nativeAddress(&nova_external_prime_wait);
        ::hx::cffi::registerExternalPrime(waitAddress);
        ::cpp::Function<int(int)> externalWait(
            Dynamic::fromNativePointer(waitAddress));
        std::exception_ptr waitFailure;
        std::thread waitingPrime([&] {
            auto& runtime = ::hx::gc::Runtime::instance();
            try {
                runtime.attachCurrentThread();
                if (externalWait(73) != 73)
                    throw std::runtime_error(
                        "external Prime returned an invalid result");
                runtime.detachCurrentThread();
            } catch (...) {
                waitFailure = std::current_exception();
                if (runtime.currentThreadAttached())
                    runtime.detachCurrentThread();
            }
        });
        while (!externalWaitEntered.load(std::memory_order_acquire))
            std::this_thread::yield();
        __hxcpp_collect(true);
        releaseExternalWait.store(true, std::memory_order_release);
        waitingPrime.join();
        if (waitFailure != nullptr)
            std::rethrow_exception(waitFailure);

        void* stringAddress = nativeAddress(&nova_external_prime_string_echo);
        ::hx::cffi::registerExternalPrime(stringAddress);
        ::cpp::Function<String(String)> externalStringEcho(
            Dynamic::fromNativePointer(stringAddress));
        assert(externalStringEcho(String("prime-string")) == "prime-string");

        static constexpr char Utf8Text[] =
            "\xe6\x96\xb0\xe6\x98\x9f\xe9\x97\xaa\xe8\x80\x80";
        void* unicodeAddress = nativeAddress(
            &nova_external_prime_unicode_echo);
        ::hx::cffi::registerExternalPrime(unicodeAddress);
        ::cpp::Function<String(String)> externalUnicodeEcho(
            Dynamic::fromNativePointer(unicodeAddress));
        const String unicodeText(Utf8Text);
        assert(unicodeText.length == 4);
        assert(externalUnicodeEcho(unicodeText) == unicodeText);
        value unicodeToken = ::hx::cffi::fromDynamic(Dynamic(unicodeText));
        assert(val_strlen(unicodeToken) == 12);
        assert(std::memcmp(val_string(unicodeToken), Utf8Text, 12) == 0);

        using LegacyString = ::hx::cffi::LegacyPrimeString;
        using ToUtf8 = const char* (*)(const LegacyString&, void*);
        using ToWchar = const wchar_t* (*)(const LegacyString&, void*);
        using ToUtf16 = const char16_t* (*)(const LegacyString&, void*);
        using AllocateUtf8 = LegacyString (*)(const char*, int);
        using Encoding = int (*)(const LegacyString&);
        const LegacyString legacy{11, "legacy-text"};
        assert(std::strcmp(resolvedCffi<ToUtf8>("hxs_utf8")(
                               legacy, nullptr), "legacy-text") == 0);
        assert(std::wcscmp(resolvedCffi<ToWchar>("hxs_wchar")(
                               legacy, nullptr), L"legacy-text") == 0);
        assert(std::char_traits<char16_t>::compare(
                   resolvedCffi<ToUtf16>("hxs_utf16")(legacy, nullptr),
                   u"legacy-text", 12) == 0);
        assert(resolvedCffi<Encoding>("hxs_encoding")(legacy) == 1);
        const LegacyString allocated =
            resolvedCffi<AllocateUtf8>("alloc_hxs_utf8")("made", 4);
        assert(allocated.length == 4);
        assert(std::memcmp(allocated.data, "made", 5) == 0);

        int nativePayload = 17;
        vkind kind = alloc_kind();
        vkind sharedKind = nullptr;
        kind_share(&sharedKind, "novagc-shared-kind");
        assert(sharedKind != nullptr);
        value abstract = alloc_abstract(kind, &nativePayload);
        assert(val_type(abstract) >= 0x100);
        assert(val_kind(abstract) == kind);
        assert(val_data(abstract) == &nativePayload);

        expectedOwnedAbstractToken = create_abstract(
            kind, 32, &ownedAbstractFinalizer);
        expectedOwnedAbstractData = val_data(expectedOwnedAbstractToken);
        assert(expectedOwnedAbstractData != nullptr);
        std::memset(expectedOwnedAbstractData, 0x5a, 32);
        free_abstract(expectedOwnedAbstractToken);
        assert(ownedAbstractFinalizerCalls == 1);
        assert(val_data(expectedOwnedAbstractToken) == nullptr);
        expectedOwnedAbstractToken = nullptr;
        expectedOwnedAbstractData = nullptr;

        bool rawFinalizerRejected = false;
        try {
            val_gc_ptr(&nativePayload, nullptr);
        } catch (const std::runtime_error&) {
            rawFinalizerRejected = true;
        }
        assert(rawFinalizerRejected);

        void* nativeBytes = hx_alloc(64);
        assert(nativeBytes != nullptr);
        std::memset(nativeBytes, 0xa5, 64);
        void* registeredAddress = nativeAddress(&nova_prime_add);
        assert(hx_register_prim("novagc_static_probe",
                                registeredAddress) == 0);
        assert(__hxcpp_get_proc_address(String(),
                   String("novagc_static_probe"), false, false) ==
               registeredAddress);

        // Prebuilt NDLLs use the established integer vkind encoding even
        // though the ABI slot is pointer-shaped.  It must not be dereferenced
        // as one of this runtime's KindRecord addresses.
        constexpr std::uintptr_t LegacyKindCode = 0x102U;
        vkind legacyKind = reinterpret_cast<vkind>(LegacyKindCode);
        value legacyAbstract = alloc_abstract(legacyKind, &nativePayload);
        assert(val_type(legacyAbstract) ==
               static_cast<int>(LegacyKindCode));
        assert(val_kind(legacyAbstract) == legacyKind);
        assert(val_data(legacyAbstract) == &nativePayload);

        // CFFI token handles keep wrappers alive but do not by themselves
        // stop concurrent relocation.  Once a native helper resolves a raw
        // CffiPointer address, the enclosing CallScope must pin it until the
        // external call finishes.  A compact inside that scope must therefore
        // leave every address observed by val_type unchanged.
        std::vector<value> relocationTokens;
        std::vector<void*> relocationAddresses;
        relocationTokens.reserve(128);
        relocationAddresses.reserve(128);
        for (int index = 0; index < 128; ++index) {
            relocationTokens.push_back(alloc_abstract(kind, &nativePayload));
            relocationAddresses.push_back(
                ::hx::cffi::toDynamic(relocationTokens.back()).managedPointer());
            assert(val_type(relocationTokens.back()) >= 0x100);
        }
        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        for (std::size_t index = 0; index < relocationTokens.size(); ++index) {
            assert(::hx::cffi::toDynamic(relocationTokens[index]).managedPointer() ==
                   relocationAddresses[index]);
            assert(val_data(relocationTokens[index]) == &nativePayload);
        }

        value image = alloc_empty_object();
        alloc_field(image, val_id("width"), alloc_int(320));
        value rect = alloc_empty_object();
        alloc_field(rect, val_id("height"), alloc_int(180));
        void* fillAddress = nativeAddress(&nova_external_prime_fill);
        ::hx::cffi::registerExternalPrime(fillAddress);
        ::cpp::Function<void(Dynamic, Dynamic, int, int)> externalFill(
            Dynamic::fromNativePointer(fillAddress));
        const auto primePinsBefore =
            ::hx::gc::Runtime::instance().heapStats();
        externalFill(::hx::cffi::toDynamic(image),
                     ::hx::cffi::toDynamic(rect),
                     0x12345678, 0x10203040);
        const auto primePinsAfter =
            ::hx::gc::Runtime::instance().heapStats();
        // Prime arguments are address-stable only for this dynamic call.
        // They must use the cheap handshake-published scoped-pin protocol,
        // not persistent Pinned-handle isolation and its whole-heap remap.
        assert(primePinsAfter.isolatedPinnedObjects ==
               primePinsBefore.isolatedPinnedObjects);

        externalRoot = alloc_string("external-root");
        val_gc_add_root(&externalRoot);

        assert(gc_try_blocking());
        auto* blockedState = ::hx::gc::currentThread;
        assert(blockedState != nullptr);
        assert(blockedState->nativeBlockedLocal);
        assert(blockedState->managedExecutionDepth.load(
                   std::memory_order_acquire) == 0);
        // Lime image decoders allocate their managed Bytes backing store while
        // an External Prime call remains native-blocked. The CFFI entry point
        // must temporarily participate as a managed mutator, then restore the
        // exact outer native state before returning.
        buffer blockedBytes = alloc_buffer_len(1024 * 1024);
        assert(blockedBytes != nullptr);
        assert(buffer_size(blockedBytes) == 1024 * 1024);
        assert(buffer_data(blockedBytes)[0] == 0);
        assert(buffer_data(blockedBytes)[1024 * 1024 - 1] == 0);
        assert(blockedState->nativeBlockedLocal);
        assert(blockedState->managedExecutionDepth.load(
                   std::memory_order_acquire) == 0);
        assert(!gc_try_blocking());
        assert(gc_try_unblocking());
        assert(!gc_try_unblocking());
        gc_safe_point();
        gc_change_managed_memory(4096, "cffi-test");
        gc_change_managed_memory(-4096, "cffi-test");
    }

    ::hx::cffi::clearExternalPrimes();

    __hxcpp_collect(true);
    __hxcpp_gc_compact();
    {
        ::hx::cffi::CallScope call;
        assert(std::strcmp(val_string(externalRoot), "external-root") == 0);
    }
    val_gc_remove_root(&externalRoot);
    assert(externalRoot == nullptr);

    // A persistent CFFI token is represented by an authoritative GC Handle
    // plus a non-root Dynamic cache.  Move the referent, reclaim its old
    // source region in a later cycle, and only then resolve the token while a
    // concurrent mark is active.  The cache refresh must shade the new object
    // without SATB-scanning its now-stale previous address.
    {
        int relocatedPayload = 314;
        AutoGCRoot relocatedToken;
        {
            ::hx::cffi::CallScope call;
            relocatedToken.set(
                ::hx::cffi::alloc_pointer(&relocatedPayload));
        }
        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        __hxcpp_collect(true);

        auto& runtime = ::hx::gc::Runtime::instance();
        runtime.startConcurrentMark();
        {
            ::hx::cffi::CallScope call;
            assert(val_data(relocatedToken.get()) == &relocatedPayload);
        }
        (void)runtime.finishConcurrentMark();
    }

    {
        ::hx::cffi::CallScope call;
        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        value object = *longLivedRoot;
        assert(std::strcmp(val_string(val_field(object, val_id("name"))),
                           "nova") == 0);
    }

    free_root(longLivedRoot);
    __hxcpp_collect(true);
    (void)::hx::gc::Runtime::instance().drainFinalizers();

    {
        ::hx::cffi::CallScope call;
        Array<int> finalizable = Array_obj<int>::__new();
        finalizable->push(73);
        expectedManagedFinalizerToken =
            ::hx::cffi::fromDynamic(Dynamic(finalizable));
        val_gc(expectedManagedFinalizerToken, &managedFinalizer);
    }
    __hxcpp_collect(true);
    assert(::hx::gc::Runtime::instance().drainFinalizers().completed == 0);
    assert(managedFinalizerCalls == 1);
    expectedManagedFinalizerToken = nullptr;

    // Lime and other NDLLs cache the exact alloc_pointer token in native
    // maps.  It must survive the originating CFFI call and remain resolvable
    // inside val_gc's callback so that the callback can erase that cache.
    {
        ::hx::cffi::CallScope call;
        expectedPointerFinalizerToken =
            ::hx::cffi::alloc_pointer(&pointerFinalizerPayload);
        val_gc(expectedPointerFinalizerToken, &pointerFinalizer);
    }
    __hxcpp_collect(true);
    assert(::hx::gc::Runtime::instance().drainFinalizers().completed == 0);
    assert(pointerFinalizerCalls == 1);
    expectedPointerFinalizerToken = nullptr;

    {
        ::hx::cffi::CallScope call;
        Array<int> disabled = Array_obj<int>::__new();
        disabled->push(73);
        value disabledToken = ::hx::cffi::fromDynamic(Dynamic(disabled));
        val_gc(disabledToken, &managedFinalizer);
        val_gc(disabledToken, nullptr);
    }
    __hxcpp_collect(true);
    (void)::hx::gc::Runtime::instance().drainFinalizers();
    assert(managedFinalizerCalls == 1);

    __hxcpp_nova_shutdown();
    std::cout << "cffi exact-token relocation test passed\n";
}

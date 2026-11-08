#pragma once

#include <hx/CFFI.h>

#include <bit>
#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace hx::cffi {

using PrimeInvoker = ::Dynamic (*)(const ::Dynamic* arguments, int count);

void registerPrime(const char* name, PrimeInvoker invoker);

class PrimeRegistration final {
public:
    PrimeRegistration(const char* name, PrimeInvoker invoker) {
        registerPrime(name, invoker);
    }
};

template<class T>
inline constexpr bool AlwaysFalse = false;

template<class T>
[[nodiscard]] T primeArgument(const ::Dynamic& input) {
    using Value = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<Value, value>) {
        return fromDynamic(input);
    } else if constexpr (std::is_same_v<Value, ::Dynamic>) {
        return input;
    } else if constexpr (std::is_same_v<Value, ::String>) {
        return input.Cast<::String>();
    } else if constexpr (std::is_same_v<Value, const char*>) {
        return val_string(fromDynamic(input));
    } else if constexpr (std::is_same_v<Value, char*>) {
        return const_cast<char*>(val_string(fromDynamic(input)));
    } else if constexpr (std::is_arithmetic_v<Value>) {
        if constexpr (std::is_same_v<Value, bool>)
            return input.Cast<bool>();
        else if constexpr (std::is_floating_point_v<Value>)
            return static_cast<Value>(input.Cast<double>());
        else
            return static_cast<Value>(input.Cast<int>());
    } else if constexpr (std::is_pointer_v<Value> &&
                         std::is_base_of_v<::hx::Object,
                             std::remove_pointer_t<Value>>) {
        return static_cast<Value>(input.managedPointer());
    } else {
        static_assert(AlwaysFalse<T>, "unsupported hxcpp-zgc Prime argument");
    }
}

template<class T>
[[nodiscard]] ::Dynamic primeResult(T&& input) {
    using Value = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<Value, value>) {
        return toDynamic(input);
    } else if constexpr (std::is_same_v<Value, ::Dynamic>) {
        return std::forward<T>(input);
    } else if constexpr (std::is_same_v<Value, ::String>) {
        return ::Dynamic(input);
    } else if constexpr (std::is_same_v<Value, bool>) {
        return ::Dynamic(input);
    } else if constexpr (std::is_integral_v<Value>) {
        return ::Dynamic(static_cast<int>(input));
    } else if constexpr (std::is_floating_point_v<Value>) {
        return ::Dynamic(static_cast<double>(input));
    } else if constexpr (std::is_pointer_v<Value> &&
                         std::is_base_of_v<::hx::Object,
                             std::remove_pointer_t<Value>>) {
        return ::Dynamic(input);
    } else {
        static_assert(AlwaysFalse<T>, "unsupported hxcpp-zgc Prime result");
    }
}

template<auto Function>
struct PrimeThunk;

template<class Result, class... Arguments,
         Result (*Function)(Arguments...)>
struct PrimeThunk<Function> final {
    template<std::size_t... Indices>
    static ::Dynamic invokeImpl(const ::Dynamic* arguments,
                                std::index_sequence<Indices...>) {
        if constexpr (std::is_void_v<Result>) {
            Function(primeArgument<Arguments>(arguments[Indices])...);
            return ::Dynamic();
        } else {
            return primeResult(
                Function(primeArgument<Arguments>(arguments[Indices])...));
        }
    }

    static ::Dynamic invoke(const ::Dynamic* arguments, int count) {
        if (count != static_cast<int>(sizeof...(Arguments)))
            throw std::runtime_error("Prime native arity mismatch");
        return invokeImpl(arguments, std::index_sequence_for<Arguments...>{});
    }
};

template<auto Function>
[[nodiscard]] void* primeAddress() noexcept {
    PrimeInvoker invoker = &PrimeThunk<Function>::invoke;
    static_assert(sizeof(invoker) == sizeof(void*));
    return std::bit_cast<void*>(invoker);
}

} // namespace hx::cffi

#if defined(_WIN32)
#define HXCPP_ZGC_CFFI_EXPORT __declspec(dllexport)
#else
#define HXCPP_ZGC_CFFI_EXPORT __attribute__((visibility("default")))
#endif

#define HXCPP_ZGC_DEFINE_PRIME(name) \
    extern "C" HXCPP_ZGC_CFFI_EXPORT void* name##__prime(const char*) { \
        return ::hx::cffi::primeAddress<&name>(); \
    } \
    static ::hx::cffi::PrimeRegistration _hx_prime_registration_##name( \
        #name, &::hx::cffi::PrimeThunk<&name>::invoke)

#define DEFINE_PRIME0(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME1(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME2(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME3(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME4(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME5(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME6(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME7(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME8(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME9(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME10(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME11(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME12(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME0v(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME1v(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME2v(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME3v(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME4v(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME5v(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME6v(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME7v(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME8v(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME9v(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME10v(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME11v(name) HXCPP_ZGC_DEFINE_PRIME(name)
#define DEFINE_PRIME12v(name) HXCPP_ZGC_DEFINE_PRIME(name)

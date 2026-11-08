#pragma once

#include <hxcpp.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <random>

class Math_obj final {
public:
    static const ::hx::gc::TypeDescriptor descriptor;
    inline static const double PI = 3.141592653589793238462643383279502884;
    inline static const double NaN = std::numeric_limits<double>::quiet_NaN();
    inline static const double POSITIVE_INFINITY =
        std::numeric_limits<double>::infinity();
    inline static const double NEGATIVE_INFINITY =
        -std::numeric_limits<double>::infinity();

    static int floor(double value) { return __int__(std::floor(value)); }
    static int ceil(double value) { return __int__(std::ceil(value)); }
    static int round(double value) { return floor(value + 0.5); }
    static double ffloor(double value) { return std::floor(value); }
    static double fceil(double value) { return std::ceil(value); }
    static double fround(double value) { return std::floor(value + 0.5); }
    static double abs(double value) { return std::fabs(value); }
    static int abs(int value) { return std::abs(value); }
    static double min(double left, double right) {
        return left < right ? left : left == left ? right : left;
    }
    static double max(double left, double right) {
        return left > right ? left : left == left ? right : left;
    }
    static ::Dynamic abs_dyn() {
        return ::hx::CreateStaticFunction1("abs", &_hx_abs);
    }
    static ::Dynamic min_dyn() {
        return ::hx::CreateStaticFunction2("min", &_hx_min);
    }
    static ::Dynamic max_dyn() {
        return ::hx::CreateStaticFunction2("max", &_hx_max);
    }
    static double sqrt(double value) { return std::sqrt(value); }
    static double sin(double value) { return std::sin(value); }
    static double cos(double value) { return std::cos(value); }
    static double tan(double value) { return std::tan(value); }
    static double asin(double value) { return std::asin(value); }
    static double acos(double value) { return std::acos(value); }
    static double atan(double value) { return std::atan(value); }
    static double atan2(double y, double x) { return std::atan2(y, x); }
    static double pow(double value, double exponent) {
        return std::pow(value, exponent);
    }
    static double exp(double value) { return std::exp(value); }
    static double log(double value) { return std::log(value); }
    static bool isNaN(double value) { return std::isnan(value); }
    static bool isFinite(double value) { return std::isfinite(value); }
    static double random() {
        thread_local std::mt19937_64 generator(std::random_device{}());
        thread_local std::uniform_real_distribution<double> distribution(0.0, 1.0);
        return distribution(generator);
    }

private:
    static ::Dynamic _hx_abs(const ::Dynamic& value) {
        return ::Dynamic(abs(value.Cast<double>()));
    }
    static ::Dynamic _hx_min(const ::Dynamic& left,
                             const ::Dynamic& right) {
        return ::Dynamic(min(left.Cast<double>(), right.Cast<double>()));
    }
    static ::Dynamic _hx_max(const ::Dynamic& left,
                             const ::Dynamic& right) {
        return ::Dynamic(max(left.Cast<double>(), right.Cast<double>()));
    }
};

inline const ::hx::gc::TypeDescriptor Math_obj::descriptor{
    0x4d415448434c4153ULL, "Math", nullptr,
    sizeof(Math_obj), alignof(Math_obj), 0,
    ::hx::gc::TypeNone, 0, nullptr, nullptr, nullptr};

using Math = ::hx::ObjectPtr<Math_obj>;

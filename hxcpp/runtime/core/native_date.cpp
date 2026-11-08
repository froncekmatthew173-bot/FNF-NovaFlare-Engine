#include <hxcpp.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <stdexcept>

namespace {

std::time_t asTime(double seconds) {
    if (!std::isfinite(seconds))
        throw std::invalid_argument("non-finite Haxe date");
    return static_cast<std::time_t>(seconds);
}

std::tm localParts(double seconds) {
    const std::time_t value = asTime(seconds);
    std::tm output{};
#ifdef _WIN32
    if (localtime_s(&output, &value) != 0)
#else
    if (localtime_r(&value, &output) == nullptr)
#endif
        throw std::runtime_error("unable to convert local date");
    return output;
}

std::tm utcParts(double seconds) {
    const std::time_t value = asTime(seconds);
    std::tm output{};
#ifdef _WIN32
    if (gmtime_s(&output, &value) != 0)
#else
    if (gmtime_r(&value, &output) == nullptr)
#endif
        throw std::runtime_error("unable to convert UTC date");
    return output;
}

std::tm makeParts(int year, int month, int day,
                  int hour, int minute, int second) {
    std::tm value{};
    value.tm_year = year - 1900;
    value.tm_mon = month;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_min = minute;
    value.tm_sec = second;
    value.tm_isdst = -1;
    return value;
}

} // namespace

double __hxcpp_new_date(int year, int month, int day,
                        int hour, int minute, int second) {
    std::tm value = makeParts(year, month, day, hour, minute, second);
    const std::time_t result = std::mktime(&value);
    // Haxe's Date.fromTime constructs a zero-filled Date as a temporary and
    // immediately replaces mSeconds.  The Windows CRT rejects year zero,
    // while POSIX implementations commonly normalize it.  Preserve mktime's
    // sentinel instead of throwing so this portable Haxe construction path
    // remains valid; subsequent field access still validates the final epoch.
    return static_cast<double>(result);
}

double __hxcpp_utc_date(int year, int month, int day,
                        int hour, int minute, int second) {
    std::tm value = makeParts(year, month, day, hour, minute, second);
#ifdef _WIN32
    const std::time_t result = _mkgmtime(&value);
#else
    const std::time_t result = timegm(&value);
#endif
    return static_cast<double>(result);
}

int __hxcpp_get_hours(double seconds) { return localParts(seconds).tm_hour; }
int __hxcpp_get_minutes(double seconds) { return localParts(seconds).tm_min; }
int __hxcpp_get_seconds(double seconds) { return localParts(seconds).tm_sec; }
int __hxcpp_get_year(double seconds) { return localParts(seconds).tm_year + 1900; }
int __hxcpp_get_month(double seconds) { return localParts(seconds).tm_mon; }
int __hxcpp_get_date(double seconds) { return localParts(seconds).tm_mday; }
int __hxcpp_get_day(double seconds) { return localParts(seconds).tm_wday; }
int __hxcpp_get_utc_hours(double seconds) { return utcParts(seconds).tm_hour; }
int __hxcpp_get_utc_minutes(double seconds) { return utcParts(seconds).tm_min; }
int __hxcpp_get_utc_seconds(double seconds) { return utcParts(seconds).tm_sec; }
int __hxcpp_get_utc_year(double seconds) { return utcParts(seconds).tm_year + 1900; }
int __hxcpp_get_utc_month(double seconds) { return utcParts(seconds).tm_mon; }
int __hxcpp_get_utc_date(double seconds) { return utcParts(seconds).tm_mday; }
int __hxcpp_get_utc_day(double seconds) { return utcParts(seconds).tm_wday; }

double __hxcpp_timezone_offset(double seconds) {
    std::tm local = localParts(seconds);
    std::tm utc = utcParts(seconds);
    const std::time_t localEpoch = std::mktime(&local);
    utc.tm_isdst = local.tm_isdst;
    const std::time_t utcAsLocal = std::mktime(&utc);
    return std::difftime(localEpoch, utcAsLocal);
}

int __hxcpp_is_dst(double seconds) {
    return localParts(seconds).tm_isdst > 0 ? 1 : 0;
}

::String __hxcpp_to_string(double seconds) {
    const std::tm value = localParts(seconds);
    char buffer[32]{};
    const int length = std::snprintf(buffer, sizeof(buffer),
        "%04d-%02d-%02d %02d:%02d:%02d",
        value.tm_year + 1900, value.tm_mon + 1, value.tm_mday,
        value.tm_hour, value.tm_min, value.tm_sec);
    return ::String(std::string_view(buffer,
        static_cast<std::size_t>(std::max(length, 0))));
}

::String __hxcpp_to_utc_string(double seconds) {
    const std::tm value = utcParts(seconds);
    char buffer[32]{};
    const int length = std::snprintf(buffer, sizeof(buffer),
        "%04d-%02d-%02d %02d:%02d:%02d",
        value.tm_year + 1900, value.tm_mon + 1, value.tm_mday,
        value.tm_hour, value.tm_min, value.tm_sec);
    return ::String(std::string_view(buffer,
        static_cast<std::size_t>(std::max(length, 0))));
}

double __hxcpp_from_utc(int year, int month, int day, int hour, int minute,
                        int second, int milliseconds) {
    return __hxcpp_utc_date(year, month, day, hour, minute, second) +
           static_cast<double>(milliseconds) / 1000.0;
}

double __hxcpp_date_now() {
    return std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

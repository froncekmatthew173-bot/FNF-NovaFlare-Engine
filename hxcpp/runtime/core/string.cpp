#include <hxcpp.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

struct StringPayload final {
    // Haxe's cpp target exposes UTF-16 code-unit indexing while native/CFFI
    // APIs consume UTF-8.  Keeping both canonical representations in one
    // precisely traced, movable payload avoids per-call transcoding and keeps
    // c_str() stable for the duration of an ordinary GC pin/call scope.
    std::uint32_t byteLength;
    std::uint32_t unitLength;
    std::uint32_t unitOffset;
    char data[1];
};

constexpr ::hx::gc::TypeDescriptor StringDescriptor{
    0x535452494e475a47ULL,
    "String",
    nullptr,
    static_cast<std::uint32_t>(offsetof(StringPayload, data) + 1U),
    static_cast<std::uint16_t>(alignof(StringPayload)),
    0,
    ::hx::gc::TypeVariableSize | ::hx::gc::TypeByteArray |
        ::hx::gc::TypeString,
    0,
    nullptr,
    nullptr,
    nullptr,
};

[[nodiscard]] StringPayload* resolve(::hx::gc::RefSlot& slot) {
    return static_cast<StringPayload*>(
        ::hx::gc::Runtime::instance().loadBarrier(slot));
}

[[nodiscard]] constexpr std::size_t alignUp(std::size_t value,
                                             std::size_t alignment) noexcept {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

void appendUtf8(std::string& output, int codePoint) {
    std::uint32_t value = codePoint < 0 ? 0xfffdU :
        static_cast<std::uint32_t>(codePoint);
    if (value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU))
        value = 0xfffdU;
    if (value <= 0x7fU) {
        output.push_back(static_cast<char>(value));
    } else if (value <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (value >> 6U)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    } else if (value <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (value >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (value >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    }
}

// Advances by one complete scalar on success.  On malformed input it consumes
// exactly one byte and returns U+FFFD, which makes String construction total
// while still letting validation helpers report the malformed sequence.
bool decodeUtf8(const char*& cursor, const char* end, int& output) {
    if (cursor >= end)
        return false;
    const char* const start = cursor;
    const auto first = static_cast<unsigned char>(*cursor++);
    if (first < 0x80U) {
        output = first;
        return true;
    }

    int continuationCount = 0;
    std::uint32_t value = 0;
    std::uint32_t minimum = 0;
    if ((first & 0xe0U) == 0xc0U) {
        continuationCount = 1;
        value = first & 0x1fU;
        minimum = 0x80U;
    } else if ((first & 0xf0U) == 0xe0U) {
        continuationCount = 2;
        value = first & 0x0fU;
        minimum = 0x800U;
    } else if ((first & 0xf8U) == 0xf0U) {
        continuationCount = 3;
        value = first & 0x07U;
        minimum = 0x10000U;
    } else {
        cursor = start + 1;
        output = 0xfffd;
        return false;
    }
    if (end - cursor < continuationCount) {
        cursor = start + 1;
        output = 0xfffd;
        return false;
    }
    for (int index = 0; index < continuationCount; ++index) {
        const auto byte = static_cast<unsigned char>(cursor[index]);
        if ((byte & 0xc0U) != 0x80U) {
            cursor = start + 1;
            output = 0xfffd;
            return false;
        }
        value = (value << 6U) | (byte & 0x3fU);
    }
    cursor += continuationCount;
    if (value < minimum || value > 0x10ffffU ||
        (value >= 0xd800U && value <= 0xdfffU)) {
        cursor = start + 1;
        output = 0xfffd;
        return false;
    }
    output = static_cast<int>(value);
    return true;
}

[[nodiscard]] std::vector<char16_t> decodeToUtf16(
        std::string_view input, bool* valid = nullptr) {
    std::vector<char16_t> units;
    units.reserve(input.size());
    bool allValid = true;
    const char* cursor = input.data();
    const char* const end = cursor + input.size();
    while (cursor < end) {
        int codePoint = 0xfffd;
        if (!decodeUtf8(cursor, end, codePoint))
            allValid = false;
        const auto value = static_cast<std::uint32_t>(codePoint);
        if (value <= 0xffffU) {
            units.push_back(static_cast<char16_t>(value));
        } else {
            const std::uint32_t adjusted = value - 0x10000U;
            units.push_back(static_cast<char16_t>(
                0xd800U + (adjusted >> 10U)));
            units.push_back(static_cast<char16_t>(
                0xdc00U + (adjusted & 0x3ffU)));
        }
    }
    if (valid != nullptr)
        *valid = allValid;
    return units;
}

[[nodiscard]] std::vector<char16_t> normalizeUtf16(
        const char16_t* input, std::size_t length) {
    std::vector<char16_t> units;
    units.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        const std::uint32_t value = input[index];
        if (value >= 0xd800U && value <= 0xdbffU) {
            if (index + 1U < length && input[index + 1U] >= 0xdc00U &&
                input[index + 1U] <= 0xdfffU) {
                units.push_back(input[index]);
                units.push_back(input[++index]);
            } else {
                units.push_back(static_cast<char16_t>(0xfffd));
            }
        } else if (value >= 0xdc00U && value <= 0xdfffU) {
            units.push_back(static_cast<char16_t>(0xfffd));
        } else {
            units.push_back(input[index]);
        }
    }
    return units;
}

[[nodiscard]] std::string encodeUtf16(const char16_t* units,
                                       std::size_t length) {
    std::string utf8;
    utf8.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        std::uint32_t codePoint = units[index];
        if (codePoint >= 0xd800U && codePoint <= 0xdbffU &&
            index + 1U < length && units[index + 1U] >= 0xdc00U &&
            units[index + 1U] <= 0xdfffU) {
            codePoint = 0x10000U + ((codePoint - 0xd800U) << 10U) +
                        (units[++index] - 0xdc00U);
        }
        appendUtf8(utf8, static_cast<int>(codePoint));
    }
    return utf8;
}

[[nodiscard]] const char16_t* payloadUnits(
        const StringPayload* payload) noexcept {
    return reinterpret_cast<const char16_t*>(
        reinterpret_cast<const std::byte*>(payload) + payload->unitOffset);
}

[[nodiscard]] String createStringFromUnits(
        const std::vector<char16_t>& units) {
    const auto normalized = normalizeUtf16(units.data(), units.size());
    const std::string utf8 = encodeUtf16(normalized.data(), normalized.size());
    if (utf8.size() > std::numeric_limits<std::uint32_t>::max() ||
        normalized.size() > static_cast<std::size_t>(
            std::numeric_limits<std::int32_t>::max()))
        throw std::length_error("Haxe string exceeds 32-bit length");

    const std::size_t unitOffset = alignUp(
        offsetof(StringPayload, data) + utf8.size() + 1U,
        alignof(char16_t));
    const std::size_t payloadBytes = unitOffset +
        (normalized.size() + 1U) * sizeof(char16_t);
    auto& runtime = ::hx::gc::Runtime::instance();
    auto* payload = static_cast<StringPayload*>(
        runtime.allocateVariable(StringDescriptor, payloadBytes));
    payload->byteLength = static_cast<std::uint32_t>(utf8.size());
    payload->unitLength = static_cast<std::uint32_t>(normalized.size());
    payload->unitOffset = static_cast<std::uint32_t>(unitOffset);
    if (!utf8.empty())
        std::memcpy(payload->data, utf8.data(), utf8.size());
    payload->data[utf8.size()] = '\0';
    auto* unitOutput = reinterpret_cast<char16_t*>(
        reinterpret_cast<std::byte*>(payload) + unitOffset);
    if (!normalized.empty())
        std::memcpy(unitOutput, normalized.data(),
                    normalized.size() * sizeof(char16_t));
    unitOutput[normalized.size()] = 0;
    runtime.publish(payload);

    String result;
    result.mPtr.store(runtime.makeRef(payload).bits(),
                      std::memory_order_release);
    result.length = static_cast<int>(normalized.size());
    return result;
}

[[nodiscard]] String createString(std::string_view value) {
    return createStringFromUnits(decodeToUtf16(value));
}

[[nodiscard]] std::vector<char16_t> stringUnits(const String& value) {
    if (value.isNull())
        return {};
    auto& slot = const_cast<::hx::gc::RefSlot&>(value.mPtr);
    if (auto* payload = resolve(slot)) {
        const char16_t* source = payloadUnits(payload);
        return std::vector<char16_t>(source,
            source + payload->unitLength);
    }
    return decodeToUtf16(value.view());
}

[[nodiscard]] std::string formatFloating(double value) {
    // Match original hxcpp's HX_DOUBLE_PATTERN default.  std::to_string uses
    // fixed six-decimal formatting, which turns Haxe values such as 7.0 into
    // "7.000000" and leaks an implementation detail into every UI string.
    char buffer[100];
    const int length = std::snprintf(buffer, sizeof(buffer), "%.15g", value);
    if (length < 0)
        throw std::runtime_error("failed to format Haxe Float");
    return std::string(buffer, static_cast<std::size_t>(
        std::min(length, static_cast<int>(sizeof(buffer) - 1U))));
}

// Wide literals reached before the GC starts cannot use managed storage yet.
// Keep their UTF-8 bytes in process-lifetime native storage; the corresponding
// String stays an immortal literal and therefore needs no GC root registration.
[[nodiscard]] const char* retainPreRuntimeLiteral(std::string_view value) {
    static std::mutex storageMutex;
    static std::vector<std::unique_ptr<char[]>> storage;
    auto bytes = std::make_unique<char[]>(value.size() + 1U);
    if (!value.empty())
        std::memcpy(bytes.get(), value.data(), value.size());
    bytes[value.size()] = '\0';
    const char* result = bytes.get();
    std::scoped_lock lock(storageMutex);
    storage.push_back(std::move(bytes));
    return result;
}

[[nodiscard]] char16_t simpleUpper(char16_t value) noexcept {
    if (value >= u'a' && value <= u'z') return value - 0x20;
    if ((value >= 0x00e0 && value <= 0x00f6) ||
        (value >= 0x00f8 && value <= 0x00fe)) return value - 0x20;
    if ((value >= 0x03b1 && value <= 0x03c1) ||
        (value >= 0x03c3 && value <= 0x03cb)) return value - 0x20;
    if (value == 0x03c2) return 0x03a3;
    if (value >= 0x0430 && value <= 0x044f) return value - 0x20;
    if (value == 0x0451) return 0x0401;
    return value;
}

[[nodiscard]] char16_t simpleLower(char16_t value) noexcept {
    if (value >= u'A' && value <= u'Z') return value + 0x20;
    if ((value >= 0x00c0 && value <= 0x00d6) ||
        (value >= 0x00d8 && value <= 0x00de)) return value + 0x20;
    if ((value >= 0x0391 && value <= 0x03a1) ||
        (value >= 0x03a3 && value <= 0x03ab)) return value + 0x20;
    if (value >= 0x0410 && value <= 0x042f) return value + 0x20;
    if (value == 0x0401) return 0x0451;
    return value;
}

[[nodiscard]] String mapStringCase(const String& input, bool upper) {
    auto units = stringUnits(input);
#if defined(_WIN32)
    if (!units.empty()) {
        const DWORD flags = upper ? LCMAP_UPPERCASE : LCMAP_LOWERCASE;
        const int required = ::LCMapStringEx(
            LOCALE_NAME_INVARIANT, flags,
            reinterpret_cast<LPCWCH>(units.data()),
            static_cast<int>(units.size()), nullptr, 0, nullptr, nullptr, 0);
        if (required > 0) {
            std::vector<char16_t> mapped(static_cast<std::size_t>(required));
            const int written = ::LCMapStringEx(
                LOCALE_NAME_INVARIANT, flags,
                reinterpret_cast<LPCWCH>(units.data()),
                static_cast<int>(units.size()),
                reinterpret_cast<LPWSTR>(mapped.data()), required,
                nullptr, nullptr, 0);
            if (written > 0) {
                mapped.resize(static_cast<std::size_t>(written));
                return createStringFromUnits(mapped);
            }
        }
    }
#endif
    for (char16_t& value : units)
        value = upper ? simpleUpper(value) : simpleLower(value);
    return createStringFromUnits(units);
}

} // namespace

int ::hx::literalStringLength(const char* utf8) noexcept {
    if (utf8 == nullptr)
        return 0;
    try {
        return static_cast<int>(decodeToUtf16(std::string_view(utf8)).size());
    } catch (...) {
        return static_cast<int>(std::strlen(utf8));
    }
}

::hx::RootedStringLiteral::RootedStringLiteral(
        const char* text, std::size_t inputLength) {
    // HX_ passes a C++ string literal whose storage lasts for the process.
    // Retain that address as the fast c_str representation.  Once the runtime
    // is active we still materialize the managed representation because a
    // String can be boxed into Dynamic, whose current ABI carries its GC ref.
    auto& runtime = ::hx::gc::Runtime::instance();
    if (!runtime.initialized()) {
        value.literal_ = text;
        value.length = static_cast<int>(
            decodeToUtf16(std::string_view(text, inputLength)).size());
        return;
    }
    value = String(std::string_view(text, inputLength));
    value.literal_ = text;
    runtime.registerStaticRoot(value.mPtr);
}

::hx::RootedWideStringLiteral::RootedWideStringLiteral(const char16_t* text) {
    const std::size_t inputLength = text == nullptr ? 0 :
        std::char_traits<char16_t>::length(text);
    const auto units = text == nullptr ? std::vector<char16_t>{} :
        normalizeUtf16(text, inputLength);
    auto& runtime = ::hx::gc::Runtime::instance();
    if (!runtime.initialized()) {
        const std::string utf8 = encodeUtf16(units.data(), units.size());
        value.literal_ = retainPreRuntimeLiteral(utf8);
        value.length = static_cast<int>(units.size());
        return;
    }
    value = createStringFromUnits(units);
    runtime.registerStaticRoot(value.mPtr);
}

String::String(const char* value) {
    if (value == nullptr)
        return;
    if (!::hx::gc::Runtime::instance().initialized()) {
        literal_ = value;
        length = ::hx::literalStringLength(value);
        return;
    }
    *this = createString(value);
}

String::String(std::string_view value) : String(createString(value)) {}

String::String(double value) : String(formatFloating(value)) {}

String::String(const String& other) noexcept
    : mPtr(other.mPtr.load(std::memory_order_acquire)), length(other.length),
      literal_(other.literal_) {}

String::String(String&& other) noexcept
    : mPtr(other.mPtr.exchange(0, std::memory_order_acq_rel)),
      length(other.length), literal_(other.literal_) {
    other.length = 0;
    other.literal_ = nullptr;
}

String::String(const char16_t* value) {
    if (value == nullptr)
        return;
    const auto units = normalizeUtf16(
        value, std::char_traits<char16_t>::length(value));
    if (!::hx::gc::Runtime::instance().initialized()) {
        const std::string utf8 = encodeUtf16(units.data(), units.size());
        literal_ = retainPreRuntimeLiteral(utf8);
        length = static_cast<int>(units.size());
        return;
    }
    *this = createStringFromUnits(units);
}

String::String(const char16_t* value, int inputLength) {
    if (value == nullptr)
        return;
    const auto units = normalizeUtf16(
        value, static_cast<std::size_t>(std::max(inputLength, 0)));
    if (!::hx::gc::Runtime::instance().initialized()) {
        const std::string utf8 = encodeUtf16(units.data(), units.size());
        literal_ = retainPreRuntimeLiteral(utf8);
        length = static_cast<int>(units.size());
        return;
    }
    *this = createStringFromUnits(units);
}

String& String::operator=(const String& other) {
    if (this != &other) {
        auto& runtime = ::hx::gc::Runtime::instance();
        if (runtime.initialized()) {
            auto& source = const_cast<::hx::gc::RefSlot&>(other.mPtr);
            auto* value = runtime.loadBarrier(source);
            runtime.storeBarrier(nullptr, mPtr, value);
        } else {
            mPtr.store(other.mPtr.load(std::memory_order_acquire),
                       std::memory_order_release);
        }
        length = other.length;
        literal_ = other.literal_;
    }
    return *this;
}

String& String::operator=(String&& other) {
    if (this != &other) {
        auto& runtime = ::hx::gc::Runtime::instance();
        if (runtime.initialized()) {
            auto* value = runtime.loadBarrier(other.mPtr);
            runtime.storeBarrier(nullptr, mPtr, value);
        } else {
            mPtr.store(other.mPtr.load(std::memory_order_acquire),
                       std::memory_order_release);
        }
        length = other.length;
        literal_ = other.literal_;
        other.mPtr.store(0, std::memory_order_release);
        other.length = 0;
        other.literal_ = nullptr;
    }
    return *this;
}

String& String::operator=(std::nullptr_t) {
    auto& runtime = ::hx::gc::Runtime::instance();
    if (runtime.initialized())
        runtime.storeBarrier(nullptr, mPtr, nullptr);
    else
        mPtr.store(0, std::memory_order_release);
    length = 0;
    literal_ = nullptr;
    return *this;
}

String& String::operator=(const char* value) {
    *this = String(value);
    return *this;
}

const char* String::c_str() const {
    if (literal_ != nullptr)
        return literal_;
    auto& slot = const_cast<::hx::gc::RefSlot&>(mPtr);
    StringPayload* payload = resolve(slot);
    return payload == nullptr ? "" : payload->data;
}

std::string_view String::view() const {
    if (literal_ != nullptr)
        return std::string_view(literal_, std::strlen(literal_));
    auto& slot = const_cast<::hx::gc::RefSlot&>(mPtr);
    StringPayload* payload = resolve(slot);
    return payload == nullptr ? std::string_view() :
        std::string_view(payload->data, payload->byteLength);
}

bool String::operator==(const String& other) const {
    if (isNull() || other.isNull())
        return isNull() && other.isNull();
    return view() == other.view();
}

bool String::operator==(const char* other) const {
    if (other == nullptr)
        return isNull();
    return !isNull() && view() == std::string_view(other);
}

int String::cca(int index) const {
    if (index < 0 || index >= length)
        return 0;
    auto& slot = const_cast<::hx::gc::RefSlot&>(mPtr);
    if (auto* payload = resolve(slot))
        return static_cast<int>(payloadUnits(payload)[index]);
    const auto units = stringUnits(*this);
    return index < static_cast<int>(units.size()) ? units[index] : 0;
}

Dynamic String::charCodeAt(int index) const {
    if (index < 0 || index >= length)
        return nullptr;
    return Dynamic(cca(index));
}

int String::compare(const String& other) const {
    if (isNull() || other.isNull()) {
        if (isNull() == other.isNull())
            return 0;
        return isNull() ? -1 : 1;
    }
    const auto left = stringUnits(*this);
    const auto right = stringUnits(other);
    const auto mismatch = std::mismatch(left.begin(), left.end(),
                                        right.begin(), right.end());
    if (mismatch.first == left.end() || mismatch.second == right.end()) {
        if (left.size() == right.size()) return 0;
        return left.size() < right.size() ? -1 : 1;
    }
    return *mismatch.first < *mismatch.second ? -1 : 1;
}

int String::hashCode() const {
    if (isNull())
        return 0;
    std::uint32_t hash = 2166136261U;
    for (const unsigned char byte : view()) {
        hash ^= byte;
        hash *= 16777619U;
    }
    return static_cast<int>((hash ^ (hash >> 16U)) & 0x7fffffffU);
}

String String::toUpperCase() const {
    return mapStringCase(*this, true);
}

String String::toLowerCase() const {
    return mapStringCase(*this, false);
}

String String::__URLEncode() const {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string output;
    output.reserve(static_cast<std::size_t>(length) * 3U);
    for (const unsigned char byte : view()) {
        if (std::isalnum(byte) != 0 || byte == '-' || byte == '_' ||
            byte == '.' || byte == '!' || byte == '~' || byte == '*' ||
            byte == '\'' || byte == '(' || byte == ')') {
            output.push_back(static_cast<char>(byte));
        } else {
            output.push_back('%');
            output.push_back(hex[byte >> 4U]);
            output.push_back(hex[byte & 0x0fU]);
        }
    }
    return String(output);
}

String String::__URLDecode() const {
    auto hexValue = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    const auto input = view();
    std::string output;
    output.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] == '%' && index + 2U < input.size()) {
            const int high = hexValue(input[index + 1U]);
            const int low = hexValue(input[index + 2U]);
            if (high >= 0 && low >= 0) {
                output.push_back(static_cast<char>((high << 4) | low));
                index += 2U;
                continue;
            }
        }
        output.push_back(input[index]);
    }
    return String(output);
}

String String::charAt(int index) const {
    if (index < 0 || index >= length)
        return String("");
    return fromCharCode(cca(index));
}

int String::indexOf(const String& needle,
                    ::hx::Null<int> startIndex) const {
    int start = startIndex.Default(0);
    const auto source = stringUnits(*this);
    const auto sought = stringUnits(needle);
    if (sought.empty())
        return std::clamp(start, 0, length);
    start = std::max(start, 0);
    if (start > length || sought.size() > source.size())
        return -1;
    for (std::size_t index = static_cast<std::size_t>(start);
         index + sought.size() <= source.size(); ++index) {
        if (std::equal(sought.begin(), sought.end(), source.begin() + index))
            return static_cast<int>(index);
    }
    return -1;
}

int String::lastIndexOf(const String& needle,
                        ::hx::Null<int> startIndex) const {
    const auto source = stringUnits(*this);
    const auto sought = stringUnits(needle);
    int start = startIndex.isNull() ? length : startIndex.Default(0);
    if (sought.empty())
        return std::clamp(start, 0, length);
    if (start < 0 || sought.size() > source.size())
        return -1;
    start = std::min(start, length - static_cast<int>(sought.size()));
    for (int index = start; index >= 0; --index) {
        if (std::equal(sought.begin(), sought.end(),
                       source.begin() + index))
            return index;
    }
    return -1;
}

::Array<String> String::split(const String& delimiter) const {
    const auto source = stringUnits(*this);
    const auto separator = stringUnits(delimiter);
    ::Array<String> result = ::Array_obj<String>::__new();
    ::hx::gc::RefSlot* roots[]{&result.mPtr};
    ::hx::gc::RootScope rootScope(roots, 1);
    if (separator.empty()) {
        for (const char16_t character : source)
            result->push(String::fromCharCode(character));
        return result;
    }
    std::size_t position = 0;
    while (true) {
        const auto found = std::search(source.begin() + position, source.end(),
                                       separator.begin(), separator.end());
        if (found == source.end()) {
            result->push(createStringFromUnits(std::vector<char16_t>(
                source.begin() + position, source.end())));
            break;
        }
        const std::size_t next = static_cast<std::size_t>(
            found - source.begin());
        result->push(createStringFromUnits(std::vector<char16_t>(
            source.begin() + position, source.begin() + next)));
        position = next + separator.size();
    }
    return result;
}

String String::substr(int position,
                      ::hx::Null<int> requestedLength) const {
    int start = position;
    int count = requestedLength.isNull() ? length :
        requestedLength.Default(0);
    if (start < 0) start += length;
    if (start < 0) start = 0;
    if (count < 0) {
        count += length;
        if (start + count >= length)
            count = 0;
    }
    if (count <= 0 || start >= length)
        return String("");
    count = std::min(count, length - start);
    const auto units = stringUnits(*this);
    return createStringFromUnits(std::vector<char16_t>(
        units.begin() + start, units.begin() + start + count));
}

String String::substring(int startIndex,
                         ::hx::Null<int> endIndex) const {
    int start = std::clamp(startIndex, 0, length);
    int end = endIndex.isNull() ? length :
        std::clamp(endIndex.Default(0), 0, length);
    if (start > end)
        std::swap(start, end);
    return substr(start, end - start);
}

String String::fromCharCode(int code) {
    std::uint32_t value = code < 0 ? 0xfffdU :
        static_cast<std::uint32_t>(code);
    if (value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU))
        value = 0xfffdU;
    std::vector<char16_t> units;
    if (value <= 0xffffU) {
        units.push_back(static_cast<char16_t>(value));
    } else {
        value -= 0x10000U;
        units.push_back(static_cast<char16_t>(0xd800U + (value >> 10U)));
        units.push_back(static_cast<char16_t>(0xdc00U + (value & 0x3ffU)));
    }
    return createStringFromUnits(units);
}

String operator+(const String& left, const String& right) {
    if (left.isNull())
        return String("null") + right;
    if (right.isNull())
        return left + String("null");
    std::string joined;
    joined.reserve(left.view().size() + right.view().size());
    joined.append(left.view());
    joined.append(right.view());
    return String(joined);
}

String operator+(const String& left, const char* right) {
    return left + String(right);
}

String operator+(const char* left, const String& right) {
    return String(left) + right;
}

String operator+(const String& left, int right) {
    return left + String(std::to_string(right));
}

String operator+(const String& left, double right) {
    return left + String(right);
}

String operator+(const String& left, bool right) {
    return left + String(right ? "true" : "false");
}

String operator+(int left, const String& right) {
    return String(std::to_string(left)) + right;
}

String operator+(double left, const String& right) {
    return String(left) + right;
}

String operator+(bool left, const String& right) {
    return String(left ? "true" : "false") + right;
}

::String __hxcpp_char_array_to_utf8_string(
        const ::Array<int>& characters, int first, int requestedLength) {
    if (!characters)
        return nullptr;
    const int begin = std::clamp(first, 0, characters->length);
    const int available = characters->length - begin;
    const int count = requestedLength < 0 ? available :
        std::clamp(requestedLength, 0, available);
    std::vector<char16_t> output;
    output.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index)
        output.push_back(static_cast<char16_t>(
            characters->__get(begin + index)));
    return createStringFromUnits(output);
}

::Array<int> __hxcpp_utf8_string_to_char_array(::String& input) {
    const auto units = stringUnits(input);
    auto output = ::Array_obj<int>::__new(static_cast<int>(units.size()));
    for (std::size_t index = 0; index < units.size(); ++index)
        output->setCtx(nullptr, static_cast<int>(index),
                       static_cast<int>(units[index]));
    return output;
}

::String __hxcpp_char_bytes_to_utf8_string(::String bytes) {
    return bytes;
}

::String __hxcpp_utf8_string_to_char_bytes(::String utf8) {
    return utf8;
}

void __hxcpp_bytes_of_string(::Array<unsigned char>& output,
                             const ::String& input) {
    const auto bytes = input.view();
    output = ::Array_obj<unsigned char>::fromData(
        reinterpret_cast<const unsigned char*>(bytes.data()),
        static_cast<int>(bytes.size()));
}

void __hxcpp_string_of_bytes(const ::Array<unsigned char>& input,
                             ::String& output, int position, int length,
                             bool reference) {
    (void)reference;
    if (!input || position < 0 || length < 0 ||
        position > input->length || length > input->length - position)
        throw std::out_of_range("invalid byte string range");
    std::string bytes;
    bytes.reserve(static_cast<std::size_t>(length));
    for (int index = 0; index < length; ++index)
        bytes.push_back(static_cast<char>(input->__get(position + index)));
    output = ::String(bytes);
}

int _hx_utf8_decode_advance(char*& pointer) {
    if (pointer == nullptr || *pointer == '\0')
        return -1;
    const char* cursor = pointer;
    const char* end = pointer + std::strlen(pointer);
    int codePoint = 0;
    if (!decodeUtf8(cursor, end, codePoint))
        throw std::runtime_error("invalid UTF-8 sequence");
    pointer = const_cast<char*>(cursor);
    return codePoint;
}

int _hx_string_compare(::String left, ::String right) {
    return left.compare(right);
}

int _hx_utf8_char_code_at(::String input, int index) {
    return index < 0 || index >= input.length ? 0 : input.cca(index);
}

int _hx_utf8_length(::String input) {
    return input.length;
}

bool _hx_utf8_is_valid(::String input) {
    (void)input;
    return true;
}

::String _hx_utf8_sub(::String input, int start, int requestedLength) {
    return input.substr(start, requestedLength);
}

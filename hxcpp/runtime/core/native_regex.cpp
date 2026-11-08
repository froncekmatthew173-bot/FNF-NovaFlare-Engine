#include <hxcpp.h>

#include <mutex>
#include <stdexcept>
#include <string>

// Haxe/cpp exposes UTF-16 code-unit positions.  Using the 16-bit PCRE2 API
// keeps every match offset in that same coordinate system and also restores
// hxcpp's PCRE syntax/options.  std::regex operates on UTF-8 bytes here and is
// neither syntax- nor replacement-compatible with EReg.
#define PCRE2_STATIC
#define PCRE2_CODE_UNIT_WIDTH 16
#include <pcre2.h>

namespace {

std::u16string toUtf16(const ::String& value) {
    std::u16string result;
    result.reserve(static_cast<std::size_t>(std::max(value.length, 0)));
    for (int index = 0; index < value.length; ++index)
        result.push_back(static_cast<char16_t>(value.cca(index)));
    return result;
}

[[noreturn]] void throwCompilationError(const ::String& pattern,
                                        int errorCode) {
    PCRE2_UCHAR buffer[256]{};
    const int written = pcre2_get_error_message(
        errorCode, buffer, sizeof(buffer) / sizeof(buffer[0]));
    const ::String message = written > 0
        ? ::String(reinterpret_cast<const char16_t*>(buffer), written)
        : ::String("unknown PCRE2 error");
    throw ::Dynamic(::String("Regexp compilation error : ") + message +
                    ::String(" in ") + pattern);
}

std::uint32_t parseFlags(const ::String& options) {
    std::uint32_t flags = PCRE2_UCP | PCRE2_UTF;
    for (int index = 0; index < options.length; ++index) {
        const int option = options.cca(index);
        switch (option) {
            case 'i': flags |= PCRE2_CASELESS; break;
            case 's': flags |= PCRE2_DOTALL; break;
            case 'm': flags |= PCRE2_MULTILINE; break;
            // Global replacement/iteration is implemented by EReg itself;
            // it is not PCRE's ungreedy compilation mode.
            case 'g': break;
            case 'u': break;
            default:
                throw ::Dynamic(::String("Regexp unknown modifier : ") +
                                ::String::fromCharCode(option));
        }
    }
    return flags;
}

struct NativeRegex final {
    explicit NativeRegex(const ::String& pattern,
                         const ::String& options) {
        const std::u16string units = toUtf16(pattern);
        int errorCode = 0;
        PCRE2_SIZE errorOffset = 0;
        expression = pcre2_compile(
            reinterpret_cast<PCRE2_SPTR>(units.data()), units.size(),
            parseFlags(options), &errorCode, &errorOffset, nullptr);
        if (expression == nullptr)
            throwCompilationError(pattern, errorCode);
        matchData = pcre2_match_data_create_from_pattern(expression, nullptr);
        if (matchData == nullptr) {
            pcre2_code_free(expression);
            expression = nullptr;
            throw std::bad_alloc();
        }
        std::uint32_t captures = 0;
        (void)pcre2_pattern_info(expression, PCRE2_INFO_CAPTURECOUNT,
                                 &captures);
        groupCount = static_cast<int>(captures + 1U);
    }

    ~NativeRegex() {
        pcre2_match_data_free(matchData);
        pcre2_code_free(expression);
    }

    pcre2_code* expression = nullptr;
    pcre2_match_data* matchData = nullptr;
    std::u16string subject;
    int groupCount = 0;
    bool matched = false;
    std::mutex mutex;
};

struct RegexShell final : ::hx::Object {
    NativeRegex* native = nullptr;
};

void finalizeRegex(void* object) {
    auto* shell = static_cast<RegexShell*>(object);
    delete shell->native;
    shell->native = nullptr;
}

const ::hx::gc::TypeDescriptor RegexShellDescriptor{
    0x4e4f564152454745ULL,
    "NovaGC.RegexStorage",
    nullptr,
    sizeof(RegexShell),
    alignof(RegexShell),
    0,
    ::hx::gc::TypeFinalizable,
    0,
    nullptr,
    nullptr,
    &finalizeRegex,
};

RegexShell* shellFrom(const ::Dynamic& value) {
    if (value.tag() != ::Dynamic::Tag::Object)
        throw ::Dynamic(::String("invalid regular expression handle"));
    auto* shell = static_cast<RegexShell*>(value.managedPointer());
    if (shell == nullptr ||
        ::hx::gc::Runtime::instance().typeOf(shell) !=
            &RegexShellDescriptor || shell->native == nullptr) {
        throw ::Dynamic(::String("closed regular expression handle"));
    }
    return shell;
}

PCRE2_SIZE* matchOffsets(NativeRegex& native) {
    return pcre2_get_ovector_pointer(native.matchData);
}

bool validGroup(const NativeRegex& native, int group) {
    return native.matched && group >= 0 && group < native.groupCount;
}

} // namespace

::Dynamic _hx_regexp_new_options(::String pattern, ::String options) {
    NativeRegex* native = new NativeRegex(pattern, options);
    auto shell = ::hx::allocate<RegexShell>(RegexShellDescriptor);
    shell->native = native;
    return ::Dynamic(shell);
}

bool _hx_regexp_match(::Dynamic regexp, ::String subject,
                      int position, int length) {
    ::hx::ScopedPin pin(shellFrom(regexp));
    auto* native = static_cast<RegexShell*>(pin.get())->native;
    std::scoped_lock guard(native->mutex);
    if (position < 0 || length < 0 || position > subject.length ||
        length > subject.length - position) {
        native->matched = false;
        native->subject.clear();
        return false;
    }

    native->subject = toUtf16(subject);
    const PCRE2_SIZE visibleLength =
        static_cast<PCRE2_SIZE>(position + length);
    const int result = pcre2_match(
        native->expression,
        reinterpret_cast<PCRE2_SPTR>(native->subject.data()),
        visibleLength, static_cast<PCRE2_SIZE>(position),
        PCRE2_NO_UTF_CHECK, native->matchData, nullptr);
    native->matched = result >= 0;
    if (!native->matched)
        native->subject.clear();
    return native->matched;
}

::String _hx_regexp_matched(::Dynamic regexp, int group) {
    ::hx::ScopedPin pin(shellFrom(regexp));
    auto* native = static_cast<RegexShell*>(pin.get())->native;
    std::scoped_lock guard(native->mutex);
    if (!validGroup(*native, group))
        throw ::Dynamic(::String("regexp_matched - no valid match"));

    const PCRE2_SIZE* offsets = matchOffsets(*native);
    const PCRE2_SIZE start = offsets[static_cast<std::size_t>(group) * 2U];
    const PCRE2_SIZE end = offsets[static_cast<std::size_t>(group) * 2U + 1U];
    if (start == PCRE2_UNSET)
        return nullptr;
    return ::String(native->subject.data() + start,
                    static_cast<int>(end - start));
}

::Dynamic _hx_regexp_matched_pos(::Dynamic regexp, int group) {
    ::hx::ScopedPin pin(shellFrom(regexp));
    auto* native = static_cast<RegexShell*>(pin.get())->native;
    std::scoped_lock guard(native->mutex);
    if (!validGroup(*native, group))
        return nullptr;

    const PCRE2_SIZE* offsets = matchOffsets(*native);
    const PCRE2_SIZE start = offsets[static_cast<std::size_t>(group) * 2U];
    const PCRE2_SIZE end = offsets[static_cast<std::size_t>(group) * 2U + 1U];
    const int position = start == PCRE2_UNSET ? -1 :
        static_cast<int>(start);
    const int length = start == PCRE2_UNSET ? 0 :
        static_cast<int>(end - start);
    return ::Dynamic(::hx::Anon_obj::Create(2)
        ->setFixed(0, ::String("pos"), position)
        ->setFixed(1, ::String("len"), length));
}

int _hx_regexp_matched_num(::Dynamic regexp) {
    ::hx::ScopedPin pin(shellFrom(regexp));
    auto* native = static_cast<RegexShell*>(pin.get())->native;
    std::scoped_lock guard(native->mutex);
    return native->matched ? native->groupCount : -1;
}

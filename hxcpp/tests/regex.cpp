#include <hxcpp.h>

#include <iostream>

int main() {
    __hxcpp_nova_initialize();
    {
        HX_NOVAGC_VARI(::Dynamic, expression) =
            _hx_regexp_new_options(String(u"(äb):(a|ä)(b)?c"), String("u"));
        const String subject(u"先äb:äbc后");
        if (!_hx_regexp_match(expression, subject, 1, subject.length - 2)) {
            std::cerr << "UTF-16 PCRE2 match failed\n";
            return 1;
        }
        const Dynamic wholePosition = _hx_regexp_matched_pos(expression, 0);
        if (_hx_regexp_matched(expression, 0) != String(u"äb:äbc") ||
            _hx_regexp_matched(expression, 1) != String(u"äb") ||
            wholePosition.__Field(String("pos"), ::hx::paccDynamic)
                    .Cast<int>() != 1 ||
            wholePosition.__Field(String("len"), ::hx::paccDynamic)
                    .Cast<int>() != 6 ||
            _hx_regexp_matched_num(expression) != 4) {
            std::cerr << "UTF-16 PCRE2 capture coordinates failed\n";
            return 2;
        }

        HX_NOVAGC_VARI(::Dynamic, optional) =
            _hx_regexp_new_options(String("(a)?b"), String(""));
        if (!_hx_regexp_match(optional, String("b"), 0, 1) ||
            !_hx_regexp_matched(optional, 1).isNull()) {
            std::cerr << "unmatched optional capture semantics failed\n";
            return 3;
        }
        const Dynamic optionalPosition = _hx_regexp_matched_pos(optional, 1);
        if (optionalPosition.__Field(String("pos"), ::hx::paccDynamic)
                    .Cast<int>() != -1 ||
            optionalPosition.__Field(String("len"), ::hx::paccDynamic)
                    .Cast<int>() != 0) {
            std::cerr << "unmatched optional capture coordinates failed\n";
            return 4;
        }

        bool invalidPatternCaught = false;
        try {
            (void)_hx_regexp_new_options(String("["), String(""));
        } catch (const Dynamic&) {
            invalidPatternCaught = true;
        }
        bool missingMatchCaught = false;
        try {
            (void)_hx_regexp_matched(optional, 99);
        } catch (const Dynamic&) {
            missingMatchCaught = true;
        }
        if (!invalidPatternCaught || !missingMatchCaught) {
            std::cerr << "PCRE2 Haxe exception semantics failed\n";
            return 5;
        }
    }
    __hxcpp_nova_shutdown();
    std::cout << "UTF-16 PCRE2 compatibility test passed\n";
    return 0;
}

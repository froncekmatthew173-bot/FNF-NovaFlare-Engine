#include <hxcpp.h>

#include <cstddef>
#include <iostream>
#include <string>

static const String preMainLiterals[]{String("{"), String("static-literal")};
static const String& preMainRootedLiteral =
    HX_("pre-main-rooted-literal", 0, 0, 0, 0);
static const String& preMainRootedWideLiteral = HX_W(u"\u4f60\u597d", 0, 0);

class TextHolder final : public ::hx::Object {
public:
    String text;

    static const std::uint32_t referenceOffsets[1];
    static const ::hx::gc::TypeDescriptor descriptor;
};

const std::uint32_t TextHolder::referenceOffsets[1]{
    static_cast<std::uint32_t>(offsetof(TextHolder, text) +
                               StringOffset::Ptr),
};

const ::hx::gc::TypeDescriptor TextHolder::descriptor{
    0x54455854484f4c44ULL,
    "TextHolder",
    nullptr,
    sizeof(TextHolder),
    alignof(TextHolder),
    0,
    ::hx::gc::TypeContainsReferences,
    1,
    TextHolder::referenceOffsets,
    nullptr,
    nullptr,
};

const String& frameLiteral() {
    return HX_("rooted-frame-literal", 0, 0, 0, 0);
}

const String& wideFrameLiteral() {
    return HX_W(u"\u00e9", 0, 0);
}

int main() {
    if (preMainLiterals[0].view() != "{" ||
        preMainLiterals[1].view() != "static-literal" ||
        preMainRootedLiteral.view() != "pre-main-rooted-literal" ||
        preMainRootedWideLiteral.view() != "\xe4\xbd\xa0\xe5\xa5\xbd") {
        std::cerr << "pre-main literal String view failed\n";
        return 10;
    }
    __hxcpp_nova_initialize();
    {
        HX_NOVAGC_VARI(::cpp::VirtualArray, stringCtorArguments) =
            ::cpp::VirtualArray_obj::__new(1);
        stringCtorArguments->init(0, 7);
        HX_NOVAGC_VARI(::Dynamic, reflectedString) =
            ::hx::ClassOf<::String>()->ConstructArgs(stringCtorArguments);
        HX_NOVAGC_VARI(::Dynamic, reflectedEmptyString) =
            ::hx::ClassOf<::String>()->ConstructEmpty();
        if (reflectedString.Cast<::String>() != "7" ||
            reflectedEmptyString.Cast<::String>() != "") {
            std::cerr << "reflected String construction failed\n";
            return 21;
        }

        const String& rootedLiteral = frameLiteral();
        const auto literalAllocationBaseline =
            ::hx::gc::Runtime::instance().allocationStats().allocatedBytes;
        const char* literalAddress = rootedLiteral.c_str();
        for (int index = 0; index < 100000; ++index) {
            const String& repeated = frameLiteral();
            if (repeated.c_str() != literalAddress)
                return 12;
        }
        if (::hx::gc::Runtime::instance().allocationStats().allocatedBytes !=
            literalAllocationBaseline) {
            std::cerr << "generated string literal allocated per access\n";
            return 13;
        }
        Dynamic boxedLiteral(rootedLiteral);
        if (boxedLiteral.Cast<String>() != "rooted-frame-literal")
            return 14;
        // Generated static Array<String> data is initialized before main and
        // therefore uses immortal literal storage with a zero GC ref.  Boxing
        // it must retain the literal instead of normalizing it to Dynamic
        // null; Array.map over static strings exercises exactly this path.
        Dynamic boxedPreMainLiteral(preMainLiterals[1]);
        Dynamic boxedPreMainRootedLiteral(preMainRootedLiteral);
        Dynamic boxedPreMainWideLiteral(preMainRootedWideLiteral);
        if (boxedPreMainLiteral.isNull() ||
            boxedPreMainLiteral.Cast<String>() != "static-literal" ||
            boxedPreMainWideLiteral.Cast<String>().length != 2 ||
            boxedPreMainWideLiteral.Cast<String>().cca(0) != 0x4f60 ||
            boxedPreMainRootedLiteral.__Field(
                String("toUpperCase"), ::hx::paccDynamic)()
                    .Cast<String>() != "PRE-MAIN-ROOTED-LITERAL") {
            std::cerr << "pre-main literal Dynamic boxing failed\n";
            return 20;
        }

        // Smart-string compatibility: Haxe observes UTF-16 code units, while
        // native APIs and Bytes observe canonical UTF-8.  Astral characters
        // therefore occupy two Haxe indices but four native bytes.
        HX_NOVAGC_VARI(::String, unicode) =
            ::String(u"\u00e9\u3042\U0001f602");
        HX_NOVAGC_VARI(::Array<::String>, unicodePieces) =
            unicode.split(::String(""));
        if (unicode.length != 4 || unicode.view().size() != 9 ||
            unicode.cca(0) != 0x00e9 || unicode.cca(1) != 0x3042 ||
            unicode.cca(2) != 0xd83d || unicode.cca(3) != 0xde02 ||
            unicode.charAt(0) != ::String(u"\u00e9") ||
            unicode.substr(1, 3) != ::String(u"\u3042\U0001f602") ||
            unicode.substring(4, 1) != ::String(u"\u3042\U0001f602") ||
            unicode.indexOf(::String(u"\U0001f602")) != 2 ||
            unicode.lastIndexOf(::String(u"\u00e9")) != 0 ||
            !unicodePieces || unicodePieces->length != 4 ||
            unicodePieces->__get(0) != ::String(u"\u00e9") ||
            unicodePieces->__get(1) != ::String(u"\u3042") ||
            unicodePieces->__get(2) != ::String(u"\ufffd") ||
            unicodePieces->__get(3) != ::String(u"\ufffd") ||
            ::String(u"\u00e4\U0001f602").toUpperCase() !=
                ::String(u"\u00c4\U0001f602") ||
            ::String(u"\u03c3").toUpperCase() != ::String(u"\u03a3") ||
            ::String(u"\u03a3").toLowerCase() != ::String(u"\u03c3") ||
            ::hx::DynamicCompare(::Dynamic(::String("abcd")),
                                 ::Dynamic(::String("e"))) >= 0 ||
            ::String("012345").substr(1, -2) != "1234" ||
            ::String("012345").substr(2, -1) != "") {
            std::cerr << "UTF-16 Haxe String semantics failed\n";
            return 22;
        }

        HX_NOVAGC_VARI(::Array<unsigned char>, unicodeBytes) =
            ::Array_obj<unsigned char>::__new();
        __hxcpp_bytes_of_string(unicodeBytes, unicode);
        HX_NOVAGC_VARI(::String, unicodeRoundTrip);
        __hxcpp_string_of_bytes(unicodeBytes, unicodeRoundTrip, 0,
                                unicodeBytes->length, false);
        if (unicodeBytes->length != 9 || unicodeRoundTrip != unicode ||
            unicodeRoundTrip.length != 4) {
            std::cerr << "UTF-8 Bytes/String bridge failed\n";
            return 23;
        }
        const String& wideLiteral = wideFrameLiteral();
        const auto wideBaseline =
            ::hx::gc::Runtime::instance().allocationStats().allocatedBytes;
        for (int index = 0; index < 100000; ++index) {
            if (wideFrameLiteral() != wideLiteral)
                return 15;
        }
        if (::hx::gc::Runtime::instance().allocationStats().allocatedBytes !=
            wideBaseline)
            return 16;

        HX_NOVAGC_VARI(::hx::ObjectPtr<TextHolder>, root) =
            ::hx::allocate<TextHolder>(TextHolder::descriptor);
        root->text = String("precise-") + String("string");
        String copiedLiteral = preMainLiterals[1];
        if (copiedLiteral != "static-literal")
            return 11;
        if (String(7.0) != "7" || Dynamic(7.0).toString() != "7" ||
            (String("value=") + 7.0) != "value=7" ||
            String(1.25) != "1.25") {
            std::cerr << "Haxe compact floating-point formatting regressed\n";
            return 17;
        }

        // A field typed Dynamic can still contain a String. Generated Haxe
        // then dispatches methods such as json.image.split(',') through
        // Dynamic::__Field, so the complete original hxcpp String method table
        // must produce receiver-rooted closures rather than null.
        HX_NOVAGC_VARI(::Dynamic, dynamicText) = ::String("GF_assets,shared");
        HX_NOVAGC_VARI(::Dynamic, split) = dynamicText.__Field(
            ::String("split"), ::hx::paccDynamic);
        HX_NOVAGC_VARI(::Array<::String>, pieces) =
            split(::String(",")).Cast<::Array<::String>>();
        if (dynamicText.__Field(::String("length"),
                                ::hx::paccDynamic).Cast<int>() != 16 ||
            !pieces || pieces->length != 2 || pieces->__get(0) != "GF_assets" ||
            pieces->__get(1) != "shared" ||
            dynamicText.__Field(::String("charAt"), ::hx::paccDynamic)(0)
                    .Cast<::String>() != "G" ||
            dynamicText.__Field(::String("indexOf"), ::hx::paccDynamic)(
                    ::String("assets"), ::Dynamic()).Cast<int>() != 3 ||
            dynamicText.__Field(::String("toUpperCase"),
                                ::hx::paccDynamic)().Cast<::String>() !=
                "GF_ASSETS,SHARED") {
            std::cerr << "dynamic String method dispatch failed\n";
            return 18;
        }
        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        HX_NOVAGC_VARI(::Array<::String>, relocatedPieces) =
            split(::String(",")).Cast<::Array<::String>>();
        if (!relocatedPieces || relocatedPieces->length != 2 ||
            relocatedPieces->__get(0) != "GF_assets" ||
            split.__ArgCount() != 1) {
            std::cerr << "dynamic String closure failed after relocation\n";
            return 19;
        }

        std::string large(200000, 'z');
        String largeString(large);
        if (largeString.length != 200000 || largeString.c_str()[199999] != 'z')
            return 1;

        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        if (root->text != "precise-string" || root->text.length != 14) {
            std::cerr << "managed String failed after relocation\n";
            return 2;
        }
    }
    __hxcpp_nova_shutdown();
    std::cout << "managed String variable allocation test passed\n";
    return 0;
}

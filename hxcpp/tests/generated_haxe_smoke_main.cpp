#include <hxcpp.h>
#include <hx/CFFI.h>

#include <Main.h>
#include <Node.h>

#include <cstdint>
#include <iostream>

void __boot_all();

int main() {
    __hxcpp_nova_initialize();
    __boot_all();
    Main_obj::main();

    __hxcpp_collect(true);
    __hxcpp_gc_compact();

    const auto relocated = __hxcpp_gc_last_relocated_objects();
    const bool hasRetained = static_cast<bool>(Main_obj::retained);
    const int retainedValue = hasRetained ? Main_obj::retained->value : -1;
    const bool hasNext = hasRetained &&
                         static_cast<bool>(Main_obj::retained->next);
    const int nextValue = hasNext ? Main_obj::retained->next->value : -1;
    if (!hasRetained || retainedValue != 1 || !hasNext || nextValue != 2) {
        std::cerr << "Haxe-generated exact graph failed after relocation: "
                  << "relocated=" << relocated
                  << " retained=" << hasRetained
                  << " retainedValue=" << retainedValue
                  << " next=" << hasNext
                  << " nextValue=" << nextValue << '\n';
        return 1;
    }
    if (Main_obj::retained->label != "root-updated" ||
        Main_obj::retained->next->label != "child" ||
        Main_obj::retained->baseLabel != "base-root" ||
        Main_obj::retained->next->baseLabel != "base-child") {
        std::cerr << "Haxe-generated managed String graph failed\n";
        return 2;
    }
    auto dynamicNode = Main_obj::retained->any.Cast<::Node>();
    if (!dynamicNode || dynamicNode->value != 2) {
        std::cerr << "Haxe-generated Dynamic reference failed\n";
        return 3;
    }
    auto arrayNode = Main_obj::nodes->__get(0).StaticCast<::Node>();
    if (Main_obj::ints->length != 5 || Main_obj::ints->__get(4) != 5 ||
        Main_obj::nodes->length != 2 || !arrayNode || arrayNode->value != 2) {
        std::cerr << "Haxe-generated precise Array failed\n";
        return 4;
    }

    {
        ::hx::cffi::CallScope call;
        value retained = ::hx::cffi::fromDynamic(Dynamic(Main_obj::retained));
        if (val_int(val_field(retained, val_id("value"))) != 1 ||
            std::strcmp(val_string(val_field(retained, val_id("label"))),
                        "root-updated") != 0 ||
            std::strcmp(val_string(val_field(retained, val_id("baseLabel"))),
                        "base-root") != 0) {
            std::cerr << "generated descriptor CFFI field read failed\n";
            return 5;
        }
        alloc_field(retained, val_id("value"), alloc_int(91));
        alloc_field(retained, val_id("label"), alloc_string("cffi-updated"));
        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        if (Main_obj::retained->value != 91 ||
            Main_obj::retained->label != "cffi-updated") {
            std::cerr << "generated descriptor CFFI field write failed\n";
            return 6;
        }
    }

    ::hx::gc::Runtime::instance().unregisterStaticRoot(
        Main_obj::retained.mPtr);
    ::hx::gc::Runtime::instance().unregisterStaticRoot(Main_obj::ints.mPtr);
    ::hx::gc::Runtime::instance().unregisterStaticRoot(Main_obj::nodes.mPtr);
    ::hx::gc::Runtime::instance().unregisterStaticRoot(
        Main_obj::retainedXml.mPtr);
    Main_obj::retained = nullptr;
    Main_obj::ints = nullptr;
    Main_obj::nodes = nullptr;
    Main_obj::retainedXml = nullptr;
    __hxcpp_nova_shutdown();
    std::cout << "Haxe-generated hxcpp ZGC smoke test passed\n";
    return 0;
}

#include <hxcpp.h>

#include <iostream>
#include <string>

HX_LOCAL_STACK_FRAME(positionLeaf, "tests.StackTrace", "leaf", 0,
                     "tests.StackTrace.leaf", "tests/StackTrace.hx", 31, 0)
HX_LOCAL_STACK_FRAME(positionCaller, "tests.StackTrace", "caller", 0,
                     "tests.StackTrace.caller", "tests/StackTrace.hx", 20, 0)
HX_LOCAL_STACK_FRAME(positionNativeLeaf, "tests.StackTrace", "nativeLeaf", 0,
                     "tests.StackTrace.nativeLeaf", "tests/StackTrace.hx", 50, 0)
HX_LOCAL_STACK_FRAME(positionCastLeaf, "tests.StackTrace", "castLeaf", 0,
                     "tests.StackTrace.castLeaf", "tests/StackTrace.hx", 60, 0)

namespace {

void leaf() {
    HX_STACKFRAME(&positionLeaf)
    HXLINE(37)
    HX_STACK_DO_THROW(::String("stack probe"));
}

void caller() {
    HX_STACKFRAME(&positionCaller)
    HXLINE(24)
    leaf();
}

void nativeLeaf() {
    HX_STACKFRAME(&positionNativeLeaf)
    HXLINE(53)
    throw std::runtime_error("native stack probe");
}

void castLeaf() {
    HX_STACKFRAME(&positionCastLeaf)
    HXLINE(63)
    (void)::Dynamic(7).Cast<::Array<int>>();
}

bool contains(const ::Array<::String>& stack, const char* text) {
    for (int index = 0; index < stack->length; ++index) {
        if (std::string(stack->__get(index).c_str()).find(text) !=
            std::string::npos)
            return true;
    }
    return false;
}

} // namespace

int main() {
    __hxcpp_nova_initialize();
    try {
        caller();
    } catch (const ::Dynamic&) {
        HX_STACK_BEGIN_CATCH
        const auto stack = __hxcpp_get_exception_stack();
        if (stack->length < 2 ||
            !contains(stack, "tests.StackTrace::leaf::tests/StackTrace.hx::37") ||
            !contains(stack, "tests.StackTrace::caller::tests/StackTrace.hx::24")) {
            std::cerr << "Haxe exception stack did not preserve frames/lines\n";
            return 1;
        }
    }
    try {
        nativeLeaf();
    } catch (const std::runtime_error&) {
        const auto stack = __hxcpp_get_exception_stack();
        if (!contains(stack,
                      "tests.StackTrace::nativeLeaf::tests/StackTrace.hx::53") ||
            contains(stack, "tests.StackTrace::leaf")) {
            std::cerr << "native exception retained a stale Haxe stack\n";
            return 2;
        }
    }
    try {
        castLeaf();
    } catch (const ::Dynamic& error) {
        HX_STACK_BEGIN_CATCH
        const auto stack = __hxcpp_get_exception_stack();
        if (std::string(error.Cast<::String>().c_str()).find(
                "source=Int") == std::string::npos ||
            !contains(stack,
                      "tests.StackTrace::castLeaf::tests/StackTrace.hx::63")) {
            std::cerr << "Dynamic cast was not a Haxe exception at its cast site\n";
            return 3;
        }
    }
    __hxcpp_nova_shutdown();
    std::cout << "Haxe shadow stack trace test passed\n";
    return 0;
}

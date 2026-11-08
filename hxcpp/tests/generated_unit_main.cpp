#include <hxcpp.h>

#include <unit/_TestMain/TestMain_Fields_.h>

#include <cstdlib>
#include <iostream>

void __boot_all();

::Array<::String> __get_args() {
    return ::Array_obj<::String>::__new();
}

[[noreturn]] void __hxcpp_exit(int code) {
    // Sys.exit is called from inside generated Haxe frames.  Let process exit
    // own their teardown rather than requiring an impossible detach while
    // exact RootScopes are still active.
    try {
        __hxcpp_nova_shutdown();
    } catch (...) {
    }
    std::_Exit(code);
}

int main() {
    bool runtimeInitialized = false;
    try {
        __hxcpp_nova_initialize();
        runtimeInitialized = true;
        ::hx::Boot();
        __boot_all();
        ::unit::_TestMain::TestMain_Fields__obj::main();
        __hxcpp_nova_shutdown();
        runtimeInitialized = false;
        return 0;
    } catch (const ::Dynamic& error) {
        std::cerr << "uncaught Haxe unit exception: "
                  << error.toString().view() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "uncaught native unit exception: "
                  << error.what() << '\n';
    } catch (...) {
        std::cerr << "uncaught unknown unit exception\n";
    }
    if (runtimeInitialized)
        __hxcpp_nova_shutdown();
    return 1;
}

#include <hxcpp.h>

#include <atomic>
#include <exception>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

extern "C" void __hxcpp_lib_main();

// Windows hybrid-GPU drivers inspect these exported variables before the
// application creates its graphics context.  Lime emits equivalent exports
// for the MSVC toolchain, but intentionally excludes MinGW.  Nova hxcpp uses
// MinGW for its self-contained backend, so the runtime executable must carry
// the ABI contract itself or Optimus/PowerXpress silently select the
// integrated GPU.
#if defined(_WIN32)
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace {
std::vector<std::string> commandLineArguments;
std::atomic<bool> runtimeInitialized{false};

void shutdownRuntimeNoexcept() noexcept {
    if (!runtimeInitialized.exchange(false, std::memory_order_acq_rel))
        return;
    try {
        __hxcpp_nova_shutdown();
    } catch (...) {
    }
}

std::string capturedHaxeStack() {
    std::string output;
    const auto stack = __hxcpp_get_exception_stack();
    for (int index = 0; index < stack->length; ++index) {
        output += "\n  at ";
        output += stack->__get(index).c_str();
    }
    return output;
}
}

::Array<::String> __get_args() {
    auto result = ::Array_obj<::String>::__new();
    for (const auto& argument : commandLineArguments)
        result->push(::String(argument));
    return result;
}

[[noreturn]] void __hxcpp_exit(int code) {
    // Lime implements window close/Sys.exit by calling this function from
    // inside __hxcpp_lib_main.  Returning through main is therefore
    // impossible: stop the automatic collector first, then use _Exit so
    // process-static std::thread objects and detached native services are not
    // destructed while still joinable/running.
    shutdownRuntimeNoexcept();
    std::_Exit(code);
}

int main(int argc, char** argv) {
    const auto shutdownAfterFailure = []() noexcept {
        shutdownRuntimeNoexcept();
    };
    try {
        commandLineArguments.clear();
        for (int index = 1; index < argc; ++index)
            commandLineArguments.emplace_back(argv[index] == nullptr ? "" :
                                               argv[index]);
        __hxcpp_nova_initialize();
        runtimeInitialized.store(true, std::memory_order_release);
        __hxcpp_lib_main();
        __hxcpp_nova_shutdown();
        runtimeInitialized.store(false, std::memory_order_release);
        return 0;
    } catch (const ::Dynamic& error) {
        bool handled = false;
        std::string nativeMessage;
        std::string nativeStack;
        {
            ::Dynamic rootedError = error;
            ::hx::gc::RefSlot* errorRoots[]{&rootedError.mPtr};
            ::hx::gc::RootScope errorRootScope(errorRoots, 1);
            ::String message = rootedError.toString();
            ::hx::gc::RefSlot* messageRoots[]{&message.mPtr};
            ::hx::gc::RootScope messageRootScope(messageRoots, 1);
            nativeMessage = message.c_str();
            nativeStack = capturedHaxeStack();
            try {
                handled = __hxcpp_invoke_critical_error_handler(message);
            } catch (...) {}
        }
        if (!handled)
            std::cerr << "uncaught Haxe exception: " << nativeMessage
                      << nativeStack << '\n';
        shutdownAfterFailure();
        return 1;
    } catch (const std::exception& error) {
        bool handled = false;
        std::string nativeStack;
        try {
            nativeStack = capturedHaxeStack();
            handled = __hxcpp_invoke_critical_error_handler(
                ::String(error.what()));
        } catch (...) {}
        if (!handled)
            std::cerr << "uncaught Haxe/C++ exception: " << error.what()
                      << nativeStack << '\n';
        shutdownAfterFailure();
        return 1;
    } catch (...) {
        bool handled = false;
        try {
            handled = __hxcpp_invoke_critical_error_handler(
                ::String("uncaught non-standard Haxe/C++ exception"));
        } catch (...) {}
        if (!handled)
            std::cerr << "uncaught non-standard Haxe/C++ exception\n";
        shutdownAfterFailure();
        return 1;
    }
}

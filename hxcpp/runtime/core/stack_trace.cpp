#include <hxcpp.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <string>

namespace {

constexpr std::size_t MaximumCapturedFrames = 512;

struct CapturedFrame final {
    const ::hx::StackPosition* position = nullptr;
    int line = 0;
};

struct NativeStackState final {
    std::array<CapturedFrame, MaximumCapturedFrames> exception{};
    std::size_t exceptionCount = 0;
    bool unwindingException = false;
};

thread_local NativeStackState currentStack;

[[nodiscard]] std::string formatFrame(const ::hx::StackPosition* position,
                                      int line) {
    const char* className = position != nullptr && position->className != nullptr
        ? position->className : "";
    const char* functionName =
        position != nullptr && position->functionName != nullptr
            ? position->functionName : "";
    const char* fileName = position != nullptr ? position->fileName : nullptr;
    if (fileName == nullptr || fileName[0] == '?' || fileName[0] == '\0') {
        return std::string(className) + "::" + functionName;
    }
    if (className[0] == '\0') {
        return std::string(functionName) + "::" + fileName + "::" +
               std::to_string(line);
    }
    return std::string(className) + "::" + functionName + "::" + fileName +
           "::" + std::to_string(line);
}

void snapshotCurrentStack(bool preserveExisting) noexcept {
    if (preserveExisting && currentStack.exceptionCount != 0)
        return;
    currentStack.exceptionCount = 0;
    for (auto* frame = ::hx::currentStackTop;
         frame != nullptr &&
         currentStack.exceptionCount < MaximumCapturedFrames;
         frame = frame->previous()) {
        currentStack.exception[currentStack.exceptionCount++] =
            CapturedFrame{frame->position, frame->lineNumber};
    }
}

} // namespace

namespace hx {

thread_local StackFrame* currentStackTop = nullptr;

void leaveStackFrame(StackFrame* frame, int currentUncaught) noexcept {
    // Native helpers can throw std::exception directly. Capture their Haxe
    // caller before C++ unwinding removes it, even when HX_STACK_DO_THROW was
    // not involved.
    if (currentUncaught > frame->uncaughtOnEntry_ &&
        !currentStack.unwindingException) {
        snapshotCurrentStack(false);
        currentStack.unwindingException = true;
    }
    if (currentStackTop == frame)
        currentStackTop = frame->previous_;
    else {
        // A corrupt shadow stack must not make exception handling terminate.
        // Recover the nearest predecessor and leave diagnostics available.
        auto* cursor = currentStackTop;
        while (cursor != nullptr && cursor->previous() != frame)
            cursor = cursor->previous();
        if (cursor != nullptr)
            cursor->previous_ = frame->previous_;
    }
}

void captureCurrentExceptionStack(bool preserveExisting) noexcept {
    snapshotCurrentStack(preserveExisting);
}

void beginHaxeCatch() noexcept {
    // Match original hxcpp's StackContext::beginCatch: preserve the completed
    // exception snapshot, but stop treating later unwinds as part of it.  A
    // native C++ exception thrown after this catch must replace the old stack.
    currentStack.unwindingException = false;
}

[[noreturn]] ::Dynamic Throw(::Dynamic value) {
    snapshotCurrentStack(false);
    currentStack.unwindingException = true;
    // haxe.Exception is a wildcard wrapper.  Rethrowing it must recover the
    // original native Haxe value stored in __nativeException; otherwise a
    // catch for the original concrete class observes the wrapper instead.
    ::hx::RootedValue<::Dynamic> rootedValue(value);
    if (rootedValue.get().tag() == ::Dynamic::Tag::Object) {
        ::Dynamic native;
        bool haxeException = false;
        auto* object = static_cast<::hx::Object*>(
            rootedValue.get().managedPointer());
        {
            ::hx::ScopedPin pin(object);
            object = pin.get();
            for (const auto* type =
                     ::hx::gc::Runtime::instance().typeOf(object);
                 type != nullptr; type = type->super) {
                if (type->name != nullptr &&
                    std::string_view(type->name) == "haxe.Exception")
                    haxeException = true;
                if (haxeException && type->dynamicFieldGet != nullptr &&
                    type->dynamicFieldGet(object, "__nativeException",
                                          ::hx::paccNever, &native))
                    break;
            }
        }
        if (haxeException && !native.isNull()) {
            ::hx::RootedValue<::Dynamic> rootedNative(native);
            throw rootedNative.get();
        }
    }
    throw rootedValue.get();
}

[[noreturn]] ::Dynamic Rethrow(::Dynamic value) {
    snapshotCurrentStack(true);
    currentStack.unwindingException = true;
    throw value;
}

} // namespace hx

void __hx_stack_set_last_exception() noexcept {
    ::hx::captureCurrentExceptionStack(false);
}

void __hx_stack_push_last_exception() noexcept {
    ::hx::captureCurrentExceptionStack(true);
}

void __hxcpp_stack_begin_catch() noexcept {
    ::hx::beginHaxeCatch();
}

void __hxcpp_set_stack_frame_line(int line) noexcept {
    if (::hx::currentStackTop != nullptr)
        ::hx::currentStackTop->lineNumber = line;
}

::Array<::String> __hxcpp_get_call_stack(bool skipLast) {
    auto result = ::Array_obj<::String>::__new();
    auto* frame = ::hx::currentStackTop;
    if (skipLast && frame != nullptr)
        frame = frame->previous();
    for (; frame != nullptr; frame = frame->previous())
        result->push(::String(formatFrame(frame->position, frame->lineNumber)));
    return result;
}

::Array<::String> __hxcpp_get_exception_stack() {
    auto result = ::Array_obj<::String>::__new();
    for (std::size_t index = 0; index < currentStack.exceptionCount; ++index) {
        const auto& frame = currentStack.exception[index];
        result->push(::String(formatFrame(frame.position, frame.line)));
    }
    return result;
}

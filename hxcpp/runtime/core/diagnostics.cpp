#include <hxcpp.h>

// The backup runtime exposes these names even when debugger, HXT, profiler
// and Tracy support are not compiled in.  Preserve those default-build
// semantics so cpp.vm.Debugger/Profiler and optional libraries fail or no-op
// predictably instead of producing missing symbols.

void __hxcpp_dbg_getScriptableFiles(::Array<::String>) {}
void __hxcpp_dbg_getScriptableFilesFullPath(::Array<::String>) {}
void __hxcpp_dbg_getScriptableClasses(::Array<::String>) {}
void __hxcpp_dbg_getScriptableVariables(void*, ::Array<::Dynamic>) {}
bool __hxcpp_dbg_getScriptableValue(void*, ::String, ::Dynamic&) {
    return false;
}
bool __hxcpp_dbg_setScriptableValue(void*, ::String, ::Dynamic) {
    return false;
}

void __hxcpp_dbg_setEventNotificationHandler(::Dynamic) {
    ::hx::Throw(::Dynamic(::String(
        "Debugging is not enabled for this program; rebuild with "
        "HXCPP_DEBUGGER")));
}
void __hxcpp_dbg_enableCurrentThreadDebugging(bool) {}
int __hxcpp_dbg_getCurrentThreadNumber() {
    return __hxcpp_GetCurrentThreadNumber();
}
::Array<::String> __hxcpp_dbg_getFiles() {
    return ::Array_obj<::String>::__new();
}
::Array<::String> __hxcpp_dbg_getFilesFullPath() {
    return ::Array_obj<::String>::__new();
}
::Array<::String> __hxcpp_dbg_getClasses() {
    return ::Array_obj<::String>::__new();
}
::Array<::Dynamic> __hxcpp_dbg_getThreadInfos() {
    return ::Array_obj<::Dynamic>::__new();
}
::Dynamic __hxcpp_dbg_getThreadInfo(int, bool) { return nullptr; }
int __hxcpp_dbg_addFileLineBreakpoint(::String, int) { return -1; }
int __hxcpp_dbg_addClassFunctionBreakpoint(::String, ::String) { return -1; }
void __hxcpp_dbg_deleteAllBreakpoints() {}
void __hxcpp_dbg_deleteBreakpoint(int) {}
void __hxcpp_dbg_breakNow(bool) {}
void __hxcpp_dbg_continueThreads(int, int) {}
void __hxcpp_dbg_stepThread(int, int, int) {}
::Array<::Dynamic> __hxcpp_dbg_getStackVariables(
        int, int, bool, ::Dynamic) {
    return ::Array_obj<::Dynamic>::__new();
}
::Dynamic __hxcpp_dbg_getStackVariableValue(
        int, int, ::String, bool, ::Dynamic, ::Dynamic) {
    return nullptr;
}
::Dynamic __hxcpp_dbg_setStackVariableValue(
        int, int, ::String, ::Dynamic, bool, ::Dynamic, ::Dynamic) {
    return nullptr;
}
void __hxcpp_dbg_setNewParameterFunction(::Dynamic) {}
void __hxcpp_dbg_setNewStackFrameFunction(::Dynamic) {}
void __hxcpp_dbg_setNewThreadInfoFunction(::Dynamic) {}
void __hxcpp_dbg_setAddParameterToStackFrameFunction(::Dynamic) {}
void __hxcpp_dbg_setAddStackFrameToThreadInfoFunction(::Dynamic) {}
void __hxcpp_dbg_setOnScriptLoadedFunction(::Dynamic) {}
bool __hxcpp_dbg_fix_critical_error(::String) { return false; }
void __hxcpp_dbg_threadCreatedOrTerminated(int, bool) {}
::Dynamic __hxcpp_dbg_checkedThrow(::Dynamic value) {
    return ::hx::Throw(value);
}
::Dynamic __hxcpp_dbg_checkedRethrow(::Dynamic value) {
    return ::hx::Rethrow(value);
}

int __hxcpp_hxt_start_telemetry(bool, bool) {
    // Matches backup hxcpp when HXT/stack telemetry is not enabled.
    return 0;
}
void __hxcpp_hxt_stash_telemetry() {}
TelemetryFrame* __hxcpp_hxt_dump_telemetry(int) { return nullptr; }
void __hxcpp_hxt_ignore_allocs(int) {}
void __hxcpp_set_hxt_finalizer(void*, void*) {}
void __hxcpp_start_profiler(::String) {}
void __hxcpp_stop_profiler() {}
void __hxcpp_spam_collects(int) {}

void __hxcpp_tracy_framemark() {}
void __hxcpp_tracy_plot(::String, ::Float) {}
void __hxcpp_tracy_plot_config(::String, std::uint8_t, bool, bool, int) {}
void __hxcpp_tracy_message(::String, int) {}
void __hxcpp_tracy_message_app_info(::String) {}
void __hxcpp_tracy_set_thread_name_and_group(::String, int) {}
int __hxcpp_tracy_get_zone_count() { return 0; }

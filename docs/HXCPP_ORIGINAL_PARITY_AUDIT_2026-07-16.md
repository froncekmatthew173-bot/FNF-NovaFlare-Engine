# Original hxcpp/gencpp versus NovaGC exhaustive machine audit

> Generated: 2026-07-17T06:53:26.0562762+08:00  
> Original hxcpp teacher: `D:\game\superbackup\git`  
> Original generator: `D:\app\haxe\haxe\haxe.exe`  
> Exact names and generated-output parity are only first-layer evidence. Tests must still prove behavior, exceptions, threading, and GC lifetime semantics.

## Summary

- Reference generated sources: 2740; NovaGC: 2742; missing: 0.
- Reference-generated methods missing from NovaGC: 0.
- Reference-generated field names structurally different in NovaGC: 4; classified replacements 4; unaccounted 0.
- Reference Build.xml/native entries structurally different in NovaGC: 4; classified replacements 4; unaccounted 0.
- Backup public `__hxcpp_*` names: 229; exact-name coverage 229; missing 0.
- Backup public CFFI-style names: 462; exact-name coverage 308; missing 154.
- Backup public `HX_*` macros: 606; exact-name coverage 93; missing 513.
- Active Haxe classpath sites depending on compiler-host `Sys.systemName()`: 5; macro-context sites 1.

## Reference-generated methods missing from NovaGC

- None.

## Reference-generated source files missing from NovaGC

- None.

## Reference Build.xml/native entries missing from NovaGC

- `file:${HXCPP}\src\hx\NoFiles.cpp`
- `file:src\haxe\NativeStackTrace.cpp`
- `file:src\sys\thread\_Thread\HaxeThread.cpp`
- `include:${HXCPP}\build-tool\BuildCommon.xml`

## Reference-generated field names missing from NovaGC

- `cpp\Int64Map.h::h`
- `haxe\ds\IntMap.h::h`
- `haxe\ds\ObjectMap.h::h`
- `haxe\ds\StringMap.h::h`

## Classified precise-runtime replacements

- `cpp\Int64Map.h::h` -> Private legacy hash handle replaced by exact-scannable novaKeys/novaValues/novaStates storage; official Haxe Map tests pass.
- `haxe\ds\IntMap.h::h` -> Private legacy hash handle replaced by exact-scannable novaKeys/novaValues/novaStates storage; official Haxe Map tests pass.
- `haxe\ds\ObjectMap.h::h` -> Private legacy hash handle replaced by exact-scannable novaKeys/novaValues/novaStates storage; official Haxe Map tests pass.
- `haxe\ds\StringMap.h::h` -> Private legacy hash handle replaced by exact-scannable novaKeys/novaValues/novaStates storage; official Haxe Map tests pass.
- `file:${HXCPP}\src\hx\NoFiles.cpp` -> NovaGC omits the debugger file table and its boot call together when HXCPP_DEBUGGER is disabled; stack-trace regressions cover the active path.
- `file:src\haxe\NativeStackTrace.cpp` -> haxe.NativeStackTrace is implemented by hxcpp/runtime/core/stack_trace.cpp and the NativeStackTrace_obj facade; official CallStack tests pass.
- `file:src\sys\thread\_Thread\HaxeThread.cpp` -> The cpp std Thread backend is replaced by exact-rooted ZgcThread plus hxcpp/runtime/core/thread.cpp; thread/event-loop probes and kernel thread tests pass.
- `include:${HXCPP}\build-tool\BuildCommon.xml` -> The legacy XML build driver is replaced by hxcpp/tools/HxcppZgcBuild.hx, which compiled the complete Windows source graph.

## Unaccounted generated field differences

- None.

## Unaccounted Build.xml/native entry differences

- None.

## Missing backup public __hxcpp_* names

- None.

## Missing backup public CFFI-style names

- `alloc_abstract_func`
- `alloc_array_func`
- `alloc_buffer_func`
- `alloc_empty_string_func`
- `alloc_field_func`
- `alloc_float_func`
- `alloc_object`
- `alloc_object_func`
- `alloc_private_func`
- `alloc_root_func`
- `alloc_string_func`
- `api_alloc_array`
- `api_alloc_bool`
- `api_alloc_buffer_len`
- `api_alloc_empty_object`
- `api_alloc_field_numeric`
- `api_alloc_int`
- `api_alloc_kind`
- `api_alloc_null`
- `api_alloc_raw_string`
- `api_alloc_root`
- `api_alloc_string`
- `api_alloc_string_data`
- `api_alloc_string_len`
- `api_alloc_wstring_len`
- `api_buffer_append_char`
- `api_buffer_data`
- `api_buffer_set_size`
- `api_buffer_size`
- `api_buffer_to_string`
- `api_buffer_val`
- `api_create_abstract`
- `api_empty`
- `api_free_abstract`
- `api_gc_change_managed_memory`
- `api_gc_try_blocking`
- `api_gc_try_unblocking`
- `api_hx_error`
- `api_val_array_i`
- `api_val_array_push`
- `api_val_array_set_i`
- `api_val_array_set_size`
- `api_val_array_size`
- `api_val_array_value`
- `api_val_bool`
- `api_val_call0_traceexcept`
- `api_val_data`
- `api_val_dup_string`
- `api_val_dup_wstring`
- `api_val_field_numeric`
- `api_val_float`
- `api_val_fun_nargs`
- `api_val_gc`
- `api_val_int`
- `api_val_is_buffer`
- `api_val_number`
- `api_val_string`
- `api_val_strlen`
- `api_val_to_buffer`
- `api_val_to_kind`
- `api_val_type`
- `api_val_wstring`
- `buffer_append_sub_func`
- `hx_add_finalizable`
- `hx_addRef`
- `hx_allocate_extended`
- `hx_array_memcmp`
- `hx_array_unsafe_set`
- `hx_atomic_dec`
- `hx_atomic_exchange_if`
- `hx_atomic_inc`
- `hx_bytes_to_nsdata`
- `hx_cast_int`
- `hx_customStack`
- `hx_decRef`
- `hx_fast_floor`
- `hx_gc_freeze`
- `hx_getFixed`
- `hx_idiv`
- `hx_imod`
- `hx_int64_to_string`
- `hx_mysql_close`
- `hx_mysql_connect`
- `hx_mysql_escape`
- `hx_mysql_request`
- `hx_mysql_result_get`
- `hx_mysql_result_get_fields_names`
- `hx_mysql_result_get_float`
- `hx_mysql_result_get_int`
- `hx_mysql_result_get_length`
- `hx_mysql_result_get_nfields`
- `hx_mysql_result_next`
- `hx_mysql_select_db`
- `hx_mysql_set_conversion`
- `hx_nsdata_to_bytes`
- `hx_nsdictionary_to_obj`
- `hx_nullptr`
- `hx_obj_to_nsdictionary`
- `hx_objc_to_bytes`
- `hx_objc_to_dynamic`
- `hx_setIdentity`
- `hx_sqlite_close`
- `hx_sqlite_connect`
- `hx_sqlite_last_insert_id`
- `hx_sqlite_request`
- `hx_sqlite_result_get`
- `hx_sqlite_result_get_float`
- `hx_sqlite_result_get_int`
- `hx_sqlite_result_get_length`
- `hx_sqlite_result_get_nfields`
- `hx_sqlite_result_next`
- `hx_ssl_debug_set`
- `hx_ssl_get_verify_result`
- `hx_stack_ctx`
- `hx_std_random_float`
- `hx_std_random_int`
- `hx_std_random_new`
- `hx_std_random_set_seed`
- `hx_std_socket_fast_select`
- `hx_std_socket_poll`
- `hx_std_socket_poll_alloc`
- `hx_std_socket_poll_events`
- `hx_std_socket_poll_prepare`
- `hx_std_socket_recv_from`
- `hx_std_socket_send_to`
- `hx_std_socket_set_broadcast`
- `hx_std_sys_exit`
- `hx_std_sys_get_pid`
- `hx_std_sys_is64`
- `hx_string_create`
- `hx_T0`
- `hx_T1`
- `hx_T2`
- `hx_T3`
- `hx_T4`
- `hx_tracy_scoped_zone`
- `hx_tracy_str_buffer`
- `hx_tracy_str_buffer_ptr`
- `hx_tracy_str_length`
- `hx_utf8_to_utf16`
- `hx_value_to_objc`
- `val_buffer_func`
- `val_call1_func`
- `val_false`
- `val_field_func`
- `val_gc_func`
- `val_hdata`
- `val_id_func`
- `val_null`
- `val_ocall1_func`
- `val_set_length`
- `val_set_size`
- `val_tag`
- `val_true`

## Missing backup public HX_* macros

- `HX_ANDROID`
- `HX_ANON_H`
- `HX_APPEND_DYNAMIC_FIELDS`
- `HX_ARG_LIST0`
- `HX_ARG_LIST1`
- `HX_ARG_LIST10`
- `HX_ARG_LIST11`
- `HX_ARG_LIST12`
- `HX_ARG_LIST13`
- `HX_ARG_LIST14`
- `HX_ARG_LIST15`
- `HX_ARG_LIST16`
- `HX_ARG_LIST17`
- `HX_ARG_LIST18`
- `HX_ARG_LIST19`
- `HX_ARG_LIST2`
- `HX_ARG_LIST20`
- `HX_ARG_LIST21`
- `HX_ARG_LIST22`
- `HX_ARG_LIST23`
- `HX_ARG_LIST24`
- `HX_ARG_LIST25`
- `HX_ARG_LIST26`
- `HX_ARG_LIST3`
- `HX_ARG_LIST4`
- `HX_ARG_LIST5`
- `HX_ARG_LIST6`
- `HX_ARG_LIST7`
- `HX_ARG_LIST8`
- `HX_ARG_LIST9`
- `HX_ARITH_VARIANT`
- `HX_ARITHMETIC_NULL_OP`
- `HX_ARR_LIST0`
- `HX_ARR_LIST1`
- `HX_ARR_LIST10`
- `HX_ARR_LIST11`
- `HX_ARR_LIST12`
- `HX_ARR_LIST13`
- `HX_ARR_LIST14`
- `HX_ARR_LIST15`
- `HX_ARR_LIST16`
- `HX_ARR_LIST17`
- `HX_ARR_LIST18`
- `HX_ARR_LIST19`
- `HX_ARR_LIST2`
- `HX_ARR_LIST20`
- `HX_ARR_LIST21`
- `HX_ARR_LIST22`
- `HX_ARR_LIST23`
- `HX_ARR_LIST24`
- `HX_ARR_LIST25`
- `HX_ARR_LIST26`
- `HX_ARR_LIST3`
- `HX_ARR_LIST4`
- `HX_ARR_LIST5`
- `HX_ARR_LIST6`
- `HX_ARR_LIST7`
- `HX_ARR_LIST8`
- `HX_ARR_LIST9`
- `HX_ARRAY_H`
- `HX_ARRAY_WB`
- `HX_BEGIN_LIB_MAIN`
- `HX_BEGIN_LOCAL_FUNC_S14`
- `HX_BEGIN_LOCAL_FUNC_S15`
- `HX_BEGIN_LOCAL_FUNC_S16`
- `HX_BEGIN_LOCAL_FUNC_S17`
- `HX_BEGIN_LOCAL_FUNC_S18`
- `HX_BEGIN_LOCAL_FUNC_S19`
- `HX_BEGIN_LOCAL_FUNC_S20`
- `HX_BEGIN_LOCAL_FUNC_S21`
- `HX_BEGIN_LOCAL_FUNC_S22`
- `HX_BEGIN_LOCAL_FUNC_S23`
- `HX_BEGIN_LOCAL_FUNC_S24`
- `HX_BEGIN_LOCAL_FUNC_S25`
- `HX_BEGIN_LOCAL_FUNC_S26`
- `HX_BEGIN_LOCAL_FUNC_S27`
- `HX_BEGIN_LOCAL_FUNC_S28`
- `HX_BEGIN_LOCAL_FUNC_S29`
- `HX_BEGIN_LOCAL_FUNC_S30`
- `HX_BEGIN_LOCAL_FUNC_S31`
- `HX_BEGIN_LOCAL_FUNC_S32`
- `HX_BEGIN_LOCAL_FUNC_S33`
- `HX_BEGIN_LOCAL_FUNC_S34`
- `HX_BEGIN_LOCAL_FUNC_S35`
- `HX_BEGIN_LOCAL_FUNC_S36`
- `HX_BEGIN_LOCAL_FUNC_S37`
- `HX_BEGIN_LOCAL_FUNC_S38`
- `HX_BEGIN_LOCAL_FUNC_S39`
- `HX_BEGIN_LOCAL_FUNC_S40`
- `HX_BEGIN_LOCAL_FUNC_S41`
- `HX_BEGIN_LOCAL_FUNC_S42`
- `HX_BEGIN_LOCAL_FUNC_S43`
- `HX_BEGIN_LOCAL_FUNC_S44`
- `HX_BEGIN_LOCAL_FUNC_S45`
- `HX_BEGIN_LOCAL_FUNC_S46`
- `HX_BEGIN_LOCAL_FUNC_S47`
- `HX_BEGIN_LOCAL_FUNC_S48`
- `HX_BEGIN_LOCAL_FUNC_S49`
- `HX_BEGIN_LOCAL_FUNC_S50`
- `HX_BEGIN_LOCAL_FUNC_S51`
- `HX_BEGIN_LOCAL_FUNC_S52`
- `HX_BEGIN_LOCAL_FUNC_S53`
- `HX_BEGIN_LOCAL_FUNC_S54`
- `HX_BEGIN_LOCAL_FUNC_S55`
- `HX_BEGIN_LOCAL_FUNC_S56`
- `HX_BEGIN_LOCAL_FUNC_S57`
- `HX_BEGIN_LOCAL_FUNC_S58`
- `HX_BEGIN_LOCAL_FUNC_S59`
- `HX_BEGIN_LOCAL_FUNC_S60`
- `HX_BEGIN_LOCAL_FUNC_S61`
- `HX_BEGIN_LOCAL_FUNC0`
- `HX_BEGIN_LOCAL_FUNC1`
- `HX_BEGIN_LOCAL_FUNC10`
- `HX_BEGIN_LOCAL_FUNC11`
- `HX_BEGIN_LOCAL_FUNC12`
- `HX_BEGIN_LOCAL_FUNC13`
- `HX_BEGIN_LOCAL_FUNC14`
- `HX_BEGIN_LOCAL_FUNC15`
- `HX_BEGIN_LOCAL_FUNC16`
- `HX_BEGIN_LOCAL_FUNC17`
- `HX_BEGIN_LOCAL_FUNC18`
- `HX_BEGIN_LOCAL_FUNC19`
- `HX_BEGIN_LOCAL_FUNC2`
- `HX_BEGIN_LOCAL_FUNC20`
- `HX_BEGIN_LOCAL_FUNC21`
- `HX_BEGIN_LOCAL_FUNC22`
- `HX_BEGIN_LOCAL_FUNC23`
- `HX_BEGIN_LOCAL_FUNC24`
- `HX_BEGIN_LOCAL_FUNC25`
- `HX_BEGIN_LOCAL_FUNC26`
- `HX_BEGIN_LOCAL_FUNC27`
- `HX_BEGIN_LOCAL_FUNC28`
- `HX_BEGIN_LOCAL_FUNC29`
- `HX_BEGIN_LOCAL_FUNC3`
- `HX_BEGIN_LOCAL_FUNC30`
- `HX_BEGIN_LOCAL_FUNC31`
- `HX_BEGIN_LOCAL_FUNC32`
- `HX_BEGIN_LOCAL_FUNC33`
- `HX_BEGIN_LOCAL_FUNC34`
- `HX_BEGIN_LOCAL_FUNC35`
- `HX_BEGIN_LOCAL_FUNC36`
- `HX_BEGIN_LOCAL_FUNC37`
- `HX_BEGIN_LOCAL_FUNC38`
- `HX_BEGIN_LOCAL_FUNC39`
- `HX_BEGIN_LOCAL_FUNC4`
- `HX_BEGIN_LOCAL_FUNC40`
- `HX_BEGIN_LOCAL_FUNC41`
- `HX_BEGIN_LOCAL_FUNC42`
- `HX_BEGIN_LOCAL_FUNC43`
- `HX_BEGIN_LOCAL_FUNC44`
- `HX_BEGIN_LOCAL_FUNC45`
- `HX_BEGIN_LOCAL_FUNC46`
- `HX_BEGIN_LOCAL_FUNC47`
- `HX_BEGIN_LOCAL_FUNC48`
- `HX_BEGIN_LOCAL_FUNC49`
- `HX_BEGIN_LOCAL_FUNC5`
- `HX_BEGIN_LOCAL_FUNC50`
- `HX_BEGIN_LOCAL_FUNC51`
- `HX_BEGIN_LOCAL_FUNC52`
- `HX_BEGIN_LOCAL_FUNC53`
- `HX_BEGIN_LOCAL_FUNC54`
- `HX_BEGIN_LOCAL_FUNC55`
- `HX_BEGIN_LOCAL_FUNC56`
- `HX_BEGIN_LOCAL_FUNC57`
- `HX_BEGIN_LOCAL_FUNC58`
- `HX_BEGIN_LOCAL_FUNC59`
- `HX_BEGIN_LOCAL_FUNC6`
- `HX_BEGIN_LOCAL_FUNC60`
- `HX_BEGIN_LOCAL_FUNC61`
- `HX_BEGIN_LOCAL_FUNC7`
- `HX_BEGIN_LOCAL_FUNC8`
- `HX_BEGIN_LOCAL_FUNC9`
- `HX_BEGIN_MAIN`
- `HX_BOOT_H`
- `HX_CFFI_API_VERSION`
- `HX_CFFI_H`
- `HX_CFFI_LOADER_H`
- `HX_CFFI_NEKO_LOADER_H`
- `HX_CFFIPRIME_INCLUDED`
- `HX_CHAR`
- `HX_CHECK_DYNAMIC_GET_FIELD`
- `HX_CHECK_DYNAMIC_GET_INT_FIELD`
- `HX_CLASS_H`
- `HX_COMPARE_NULL_MOST_OPS`
- `HX_COMPARE_NULL_OP`
- `HX_COMPARE_NULL_OPS`
- `HX_COMPARE_VARIANT_OP`
- `HX_CSTRING2`
- `HX_DEBUG_H`
- `HX_DECLARE_CLASS10`
- `HX_DECLARE_CLASS11`
- `HX_DECLARE_CLASS12`
- `HX_DECLARE_CLASS13`
- `HX_DECLARE_CLASS14`
- `HX_DECLARE_CLASS15`
- `HX_DECLARE_CLASS16`
- `HX_DECLARE_CLASS17`
- `HX_DECLARE_CLASS18`
- `HX_DECLARE_CLASS19`
- `HX_DECLARE_CLASS20`
- `HX_DECLARE_CLASS7`
- `HX_DECLARE_CLASS8`
- `HX_DECLARE_CLASS9`
- `HX_DECLARE_DYNAMIC_FUNC`
- `HX_DECLARE_DYNAMIC_FUNCTIONS`
- `HX_DECLARE_IMPLEMENT_DYNAMIC`
- `HX_DECLARE_MAIN`
- `HX_DECLARE_NATIVE0`
- `HX_DECLARE_NATIVE1`
- `HX_DECLARE_NATIVE2`
- `HX_DECLARE_NATIVE3`
- `HX_DECLARE_NATIVE4`
- `HX_DECLARE_NATIVE5`
- `HX_DECLARE_NATIVE6`
- `HX_DECLARE_NATIVE7`
- `HX_DECLARE_NATIVE8`
- `HX_DECLARE_NATIVE9`
- `HX_DECLARE_VARIANT_FUNCTIONS`
- `HX_DEFINE_DYNAMIC_FUNC_EXTRA`
- `HX_DEFINE_DYNAMIC_FUNC16`
- `HX_DEFINE_DYNAMIC_FUNC17`
- `HX_DEFINE_DYNAMIC_FUNC18`
- `HX_DEFINE_DYNAMIC_FUNC19`
- `HX_DEFINE_DYNAMIC_FUNC20`
- `HX_DEFINE_DYNAMIC_FUNC21`
- `HX_DEFINE_DYNAMIC_FUNC22`
- `HX_DEFINE_DYNAMIC_FUNC23`
- `HX_DEFINE_DYNAMIC_FUNC24`
- `HX_DEFINE_DYNAMIC_FUNC25`
- `HX_DEFINE_DYNAMIC_FUNC26`
- `HX_DO_RTTI`
- `HX_DYNAMIC_ARG_LIST0`
- `HX_DYNAMIC_ARG_LIST1`
- `HX_DYNAMIC_ARG_LIST10`
- `HX_DYNAMIC_ARG_LIST11`
- `HX_DYNAMIC_ARG_LIST12`
- `HX_DYNAMIC_ARG_LIST13`
- `HX_DYNAMIC_ARG_LIST14`
- `HX_DYNAMIC_ARG_LIST15`
- `HX_DYNAMIC_ARG_LIST16`
- `HX_DYNAMIC_ARG_LIST17`
- `HX_DYNAMIC_ARG_LIST18`
- `HX_DYNAMIC_ARG_LIST19`
- `HX_DYNAMIC_ARG_LIST2`
- `HX_DYNAMIC_ARG_LIST20`
- `HX_DYNAMIC_ARG_LIST21`
- `HX_DYNAMIC_ARG_LIST22`
- `HX_DYNAMIC_ARG_LIST23`
- `HX_DYNAMIC_ARG_LIST24`
- `HX_DYNAMIC_ARG_LIST25`
- `HX_DYNAMIC_ARG_LIST26`
- `HX_DYNAMIC_ARG_LIST3`
- `HX_DYNAMIC_ARG_LIST4`
- `HX_DYNAMIC_ARG_LIST5`
- `HX_DYNAMIC_ARG_LIST6`
- `HX_DYNAMIC_ARG_LIST7`
- `HX_DYNAMIC_ARG_LIST8`
- `HX_DYNAMIC_ARG_LIST9`
- `HX_DYNAMIC_CALL`
- `HX_DYNAMIC_CALL0`
- `HX_DYNAMIC_CALL1`
- `HX_DYNAMIC_CALL10`
- `HX_DYNAMIC_CALL11`
- `HX_DYNAMIC_CALL12`
- `HX_DYNAMIC_CALL13`
- `HX_DYNAMIC_CALL14`
- `HX_DYNAMIC_CALL15`
- `HX_DYNAMIC_CALL16`
- `HX_DYNAMIC_CALL17`
- `HX_DYNAMIC_CALL18`
- `HX_DYNAMIC_CALL19`
- `HX_DYNAMIC_CALL2`
- `HX_DYNAMIC_CALL20`
- `HX_DYNAMIC_CALL21`
- `HX_DYNAMIC_CALL22`
- `HX_DYNAMIC_CALL23`
- `HX_DYNAMIC_CALL24`
- `HX_DYNAMIC_CALL25`
- `HX_DYNAMIC_CALL26`
- `HX_DYNAMIC_CALL27`
- `HX_DYNAMIC_CALL28`
- `HX_DYNAMIC_CALL29`
- `HX_DYNAMIC_CALL3`
- `HX_DYNAMIC_CALL30`
- `HX_DYNAMIC_CALL31`
- `HX_DYNAMIC_CALL32`
- `HX_DYNAMIC_CALL33`
- `HX_DYNAMIC_CALL34`
- `HX_DYNAMIC_CALL35`
- `HX_DYNAMIC_CALL36`
- `HX_DYNAMIC_CALL37`
- `HX_DYNAMIC_CALL38`
- `HX_DYNAMIC_CALL39`
- `HX_DYNAMIC_CALL4`
- `HX_DYNAMIC_CALL40`
- `HX_DYNAMIC_CALL41`
- `HX_DYNAMIC_CALL42`
- `HX_DYNAMIC_CALL43`
- `HX_DYNAMIC_CALL44`
- `HX_DYNAMIC_CALL45`
- `HX_DYNAMIC_CALL46`
- `HX_DYNAMIC_CALL47`
- `HX_DYNAMIC_CALL48`
- `HX_DYNAMIC_CALL49`
- `HX_DYNAMIC_CALL5`
- `HX_DYNAMIC_CALL50`
- `HX_DYNAMIC_CALL51`
- `HX_DYNAMIC_CALL52`
- `HX_DYNAMIC_CALL53`
- `HX_DYNAMIC_CALL54`
- `HX_DYNAMIC_CALL55`
- `HX_DYNAMIC_CALL56`
- `HX_DYNAMIC_CALL57`
- `HX_DYNAMIC_CALL58`
- `HX_DYNAMIC_CALL59`
- `HX_DYNAMIC_CALL6`
- `HX_DYNAMIC_CALL60`
- `HX_DYNAMIC_CALL61`
- `HX_DYNAMIC_CALL7`
- `HX_DYNAMIC_CALL8`
- `HX_DYNAMIC_CALL9`
- `HX_DYNAMIC_H`
- `HX_DYNAMIC_OP_ISEQ`
- `HX_DYNAMIC_SET_FIELD`
- `HX_END_LIB_MAIN`
- `HX_END_LOCAL_FUNC10`
- `HX_END_LOCAL_FUNC11`
- `HX_END_LOCAL_FUNC12`
- `HX_END_LOCAL_FUNC13`
- `HX_END_LOCAL_FUNC14`
- `HX_END_LOCAL_FUNC15`
- `HX_END_LOCAL_FUNC16`
- `HX_END_LOCAL_FUNC17`
- `HX_END_LOCAL_FUNC18`
- `HX_END_LOCAL_FUNC19`
- `HX_END_LOCAL_FUNC20`
- `HX_END_LOCAL_FUNC21`
- `HX_END_LOCAL_FUNC22`
- `HX_END_LOCAL_FUNC23`
- `HX_END_LOCAL_FUNC24`
- `HX_END_LOCAL_FUNC25`
- `HX_END_LOCAL_FUNC26`
- `HX_END_LOCAL_FUNC27`
- `HX_END_LOCAL_FUNC28`
- `HX_END_LOCAL_FUNC29`
- `HX_END_LOCAL_FUNC30`
- `HX_END_LOCAL_FUNC31`
- `HX_END_LOCAL_FUNC32`
- `HX_END_LOCAL_FUNC33`
- `HX_END_LOCAL_FUNC34`
- `HX_END_LOCAL_FUNC35`
- `HX_END_LOCAL_FUNC36`
- `HX_END_LOCAL_FUNC37`
- `HX_END_LOCAL_FUNC38`
- `HX_END_LOCAL_FUNC39`
- `HX_END_LOCAL_FUNC40`
- `HX_END_LOCAL_FUNC41`
- `HX_END_LOCAL_FUNC42`
- `HX_END_LOCAL_FUNC43`
- `HX_END_LOCAL_FUNC44`
- `HX_END_LOCAL_FUNC45`
- `HX_END_LOCAL_FUNC46`
- `HX_END_LOCAL_FUNC47`
- `HX_END_LOCAL_FUNC48`
- `HX_END_LOCAL_FUNC49`
- `HX_END_LOCAL_FUNC50`
- `HX_END_LOCAL_FUNC51`
- `HX_END_LOCAL_FUNC52`
- `HX_END_LOCAL_FUNC53`
- `HX_END_LOCAL_FUNC54`
- `HX_END_LOCAL_FUNC55`
- `HX_END_LOCAL_FUNC56`
- `HX_END_LOCAL_FUNC57`
- `HX_END_LOCAL_FUNC58`
- `HX_END_LOCAL_FUNC59`
- `HX_END_LOCAL_FUNC60`
- `HX_END_LOCAL_FUNC61`
- `HX_END_LOCAL_FUNC9`
- `HX_END_MAIN`
- `HX_ENDIAN_MARK_ID_BYTE`
- `HX_ENUM_H`
- `HX_ERROR_CODES`
- `HX_EXTERN_NATIVE_IMPLEMENTATION`
- `HX_FIELD_REF_H`
- `HX_FIELD_REF_IMPL_MEM_OP`
- `HX_FIELD_REF_MEM_OP`
- `HX_FIELD_REF_OP`
- `HX_FUNCTIONS_H`
- `HX_GC_CONST_ALLOC_BIT`
- `HX_GC_CONST_ALLOC_MARK_BIT`
- `HX_GC_CONST_ALLOC_MARK_OFFSET`
- `HX_GC_CTX`
- `HX_GC_H`
- `HX_GC_REMEMBERED`
- `HX_GC_STRING_CHAR16_T`
- `HX_GC_STRING_HASH`
- `HX_GC_STRING_HASH_BIT`
- `HX_GC_STRING_HASH_OFFSET`
- `HX_GC_TYPE_INFERENCE_H`
- `HX_GCC_ATOMICS`
- `HX_HAS_ATOMIC`
- `HX_HASH_JOIN`
- `HX_HASH_OF`
- `HX_HASH_OF_W`
- `HX_HCSTRING`
- `HX_HEX_QUOTE`
- `HX_HEX_QUOTE_W`
- `HX_I32_DEF_FUNC1`
- `HX_I32_DEF_FUNC2`
- `HX_IMPLEMENT_INLINE_VARIANT_FUNCTIONS`
- `HX_INDEX_OUT_OF_BOUNDS`
- `HX_INDEX_REF_H`
- `HX_INDEX_REF_MEM_OP`
- `HX_INDEX_REF_OP`
- `HX_INDEX_REF_OP_DYNAMIC`
- `HX_INIT_IMPLEMENT_DYNAMIC`
- `HX_INTERFACE_H`
- `HX_INVALID_ARG_COUNT`
- `HX_INVALID_CAST`
- `HX_INVALID_CONSTRUCTOR`
- `HX_INVALID_ENUM_ARG_COUNT`
- `HX_INVALID_ENUM_CONSTRUCTOR`
- `HX_INVALID_INTERFACE`
- `HX_IS_INSTANCE_OF`
- `HX_JOIN_PARTS`
- `HX_LESS_THAN_EQ_INCLUDED`
- `HX_LINUX`
- `HX_LOCAL_RUN`
- `HX_MACOS`
- `HX_MACROS_H`
- `HX_MACROS_JUMBO_H`
- `HX_MAP_THIS`
- `HX_MAP_THIS_`
- `HX_MAP_THIS_ARG`
- `HX_MARK_ADD_ARG`
- `HX_MARK_ADD_PARAMS`
- `HX_MARK_ARRAY`
- `HX_MARK_DYNAMIC`
- `HX_MARK_MEMBER`
- `HX_MARK_MEMBER_ARRAY`
- `HX_MARK_OBJECT`
- `HX_MARK_STRING`
- `HX_MATH`
- `HX_MEMORY_H`
- `HX_MEMORY_H_OVERRIDE`
- `HX_MSVC_ATOMICS`
- `HX_NATIVE_INCLUDED_H`
- `HX_NEKO_FUNC_H`
- `HX_NULL_ARITHMETIC_OP`
- `HX_NULL_COMPARE_MOST_OPS`
- `HX_NULL_COMPARE_OP`
- `HX_NULL_COMPARE_OPS`
- `HX_NULL_DEFINE_COMPARE_MOST_OPS`
- `HX_NULL_H`
- `HX_OBJ_WB_CTX`
- `HX_OBJ_WB_GET`
- `HX_OBJ_WB_NEW_MARKED_OBJECT`
- `HX_OBJ_WB_PESSIMISTIC`
- `HX_OBJ_WB_PESSIMISTIC_CTX`
- `HX_OBJ_WB_PESSIMISTIC_GET`
- `HX_OBJC_HELPERS_INCLUDED`
- `HX_OBJECT_H`
- `HX_OPERATORS_H`
- `HX_OS_H`
- `HX_PROP_ALWAYS`
- `HX_PROP_DYNAMIC`
- `HX_PROP_NEVER`
- `HX_QSTR_EQ`
- `HX_QSTR_EQ_AE`
- `HX_QUICKVEC_INCLUDED`
- `HX_SCRIPTABLE`
- `HX_SMART_STRINGS`
- `HX_STACK_CONTEXT_H`
- `HX_STACK_CTX`
- `HX_STACK_CTX_H`
- `HX_STACK_FRAME`
- `HX_STACK_LINE`
- `HX_STACK_LINE_QUICK`
- `HX_STACK_PUSH`
- `HX_STACK_VAR`
- `HX_STD_STRING_INCLUDEDED`
- `HX_STDLIBS_H`
- `HX_STR_QUOTE`
- `HX_STR_QUOTE_W`
- `HX_STRI`
- `HX_STRING_ALLOC`
- `HX_STRING_H`
- `HX_STRINGI`
- `HX_TELEMETRY_H`
- `HX_TELEMETRY_TRACY_H`
- `HX_TELEMETRY_VERSION`
- `HX_THREAD_H`
- `HX_THREAD_H_OVERRIDE`
- `HX_THREAD_SEMAPHORE_LOCKABLE`
- `HX_TLS_H_OVERRIDE`
- `HX_TLS_INCLUDED`
- `HX_UNDEFINE_H`
- `HX_UNORDERED_INCLUDED`
- `HX_USE_INLINE_IMMIX_OPERATOR_NEW`
- `HX_VAR_NAME`
- `HX_VARI_NAME`
- `HX_VARIANT_COMPARE_OP`
- `HX_VARIANT_COMPARE_OP_ALL`
- `HX_VARIANT_OP_ISEQ`
- `HX_VARRAY_DEFINED`
- `HX_VISIT_ALLOCS`
- `HX_VISIT_ARRAY`
- `HX_VISIT_DYNAMIC`
- `HX_VISIT_MEMBER`
- `HX_VISIT_OBJECT`
- `HX_VISIT_STRING`
- `HX_WIN_MAIN`
- `HX_WINRT`

## Exact-name coverage of the 56 original public headers

| Original header | Detected public names | Exact-name coverage | Missing |
|---|---:|---:|---:|
| `Array.h` | 19 | 9 | 10 |
| `cpp\CppInt32__.h` | 3 | 1 | 2 |
| `cpp\FastIterator.h` | 4 | 3 | 1 |
| `cpp\Int64.h` | 27 | 26 | 1 |
| `cpp\Pointer.h` | 3 | 3 | 0 |
| `cpp\Variant.h` | 11 | 0 | 11 |
| `cpp\VirtualArray.h` | 8 | 3 | 5 |
| `Dynamic.h` | 5 | 0 | 5 |
| `Enum.h` | 24 | 19 | 5 |
| `hx\Anon.h` | 16 | 3 | 13 |
| `hx\Boot.h` | 1 | 0 | 1 |
| `hx\CFFI.h` | 59 | 54 | 5 |
| `hx\CFFIAPI.h` | 84 | 84 | 0 |
| `hx\CFFIJsPrime.h` | 80 | 80 | 0 |
| `hx\CFFILoader.h` | 11 | 8 | 3 |
| `hx\CFFINekoLoader.h` | 148 | 76 | 72 |
| `hx\CFFIPrime.h` | 17 | 16 | 1 |
| `hx\Class.h` | 8 | 6 | 2 |
| `hx\Debug.h` | 33 | 32 | 1 |
| `hx\DynamicImpl.h` | 4 | 2 | 2 |
| `hx\ErrorCodes.h` | 11 | 3 | 8 |
| `hx\FieldRef.h` | 8 | 0 | 8 |
| `hx\Functions.h` | 3 | 0 | 3 |
| `hx\GC.h` | 98 | 61 | 37 |
| `hx\GcTypeInference.h` | 5 | 0 | 5 |
| `hx\HeaderVersion.h` | 0 | 0 | 0 |
| `hx\HxcppMain.h` | 9 | 4 | 5 |
| `hx\IndexRef.h` | 1 | 0 | 1 |
| `hx\Interface.h` | 2 | 1 | 1 |
| `hx\LessThanEq.h` | 1 | 0 | 1 |
| `hx\Macros.h` | 262 | 61 | 201 |
| `hx\MacrosFixed.h` | 55 | 18 | 37 |
| `hx\MacrosJumbo.h` | 173 | 1 | 172 |
| `hx\Memory.h` | 2 | 0 | 2 |
| `hx\Native.h` | 16 | 1 | 15 |
| `hx\NekoFunc.h` | 36 | 28 | 8 |
| `hx\ObjcHelpers.h` | 6 | 0 | 6 |
| `hx\Object.h` | 6 | 4 | 2 |
| `hx\Operators.h` | 6 | 3 | 3 |
| `hx\OS.h` | 2 | 0 | 2 |
| `hx\QuickVec.h` | 1 | 0 | 1 |
| `hx\Scriptable.h` | 11 | 10 | 1 |
| `hx\StackContext.h` | 41 | 31 | 10 |
| `hx\StdLibs.h` | 315 | 258 | 57 |
| `hx\StdString.h` | 1 | 0 | 1 |
| `hx\StringAlloc.h` | 1 | 0 | 1 |
| `hx\Telemetry.h` | 8 | 6 | 2 |
| `hx\TelemetryTracy.h` | 13 | 8 | 5 |
| `hx\Thread.h` | 5 | 1 | 4 |
| `hx\Tls.h` | 4 | 1 | 3 |
| `hx\Undefine.h` | 1 | 0 | 1 |
| `hx\Unordered.h` | 1 | 0 | 1 |
| `hxcpp.h` | 28 | 4 | 24 |
| `hxMath.h` | 5 | 2 | 3 |
| `hxString.h` | 18 | 7 | 11 |
| `null.h` | 10 | 0 | 10 |

## Windows target versus Linux/Docker compiler-host branch risks

- [runtime code] `.haxelib\haxeui-core\1,7,0\haxe\ui\backend\PlatformBase.hx:19` - `return Sys.systemName().toLowerCase().indexOf("windows") != -1;`
- [runtime code] `.haxelib\haxeui-core\1,7,0\haxe\ui\backend\PlatformBase.hx:29` - `return Sys.systemName().toLowerCase().indexOf("linux") != -1;`
- [runtime code] `.haxelib\haxeui-core\1,7,0\haxe\ui\backend\PlatformBase.hx:39` - `return Sys.systemName().toLowerCase().indexOf("mac") != -1;`
- [runtime code] `.haxelib\hxvlc\git\source\hxvlc\util\Handle.hx:208` - `LibVLC.set_user_agent(instance.raw, 'hxvlc', 'hxvlc "$hxvlcVersion" (Haxe "$haxeVersion" ${Sys.systemName()})');`
- [macro/compile-time risk] `.haxelib\lime\git\src\lime\system\CFFI.hx:330` - `return Sys.systemName();`

The first proven real divergence is ``trandom.InitMacro.checkWindows()`` checking the Docker/Linux compiler host. The Windows target consequently lacks ``trandom_windows`` and omits ``Native.getWindows``, ``CPPExtern``, and ``trandom_native.c``. Host/target isolation and a generator-differential gate are required.

## Acceptance discipline

1. Every reference-generated method, field, and native entry missing from NovaGC must reach zero or have tested target-inapplicability evidence.
2. Public ABI gaps cannot be satisfied by same-name stubs; each group needs signature, result, exception, thread, and GC-lifetime tests.
3. Regenerate and run the complete game route only after the generator-differential gate passes, instead of discovering one omission per run.
4. Complete raw data is stored in ``artifacts/novagc/hxcpp-original-parity-audit.json``.

#pragma once

#include <hxcpp.h>

#include <cstddef>
#include <cstdint>

// This is a new exact-GC boundary ABI.  The pointer-shaped value is an opaque
// stable token, never a raw address into the moving Haxe heap.
namespace hx::cffi { struct ValueToken; }
using value = ::hx::cffi::ValueToken*;
using buffer = value;
using gcroot = void*;
using vkind = const void*;
using HxString = ::String;
using field = int;
using hxPtrFinalizer = void (*)(void*);
using __hx_field_iter = void(value, field, void*);

enum hxValueType {
    hxvtNull,
    hxvtInt,
    hxvtFloat,
    hxvtBool,
    hxvtString,
    hxvtObject,
    hxvtArray,
    hxvtBuffer,
    hxvtPointer,
};

enum {
    valtUnknown = -1,
    valtNull = 0,
    valtFloat = 1,
    valtBool = 2,
    valtString = 3,
    valtObject = 4,
    valtArray = 5,
    valtFunction = 6,
    valtEnum = 7,
    valtClass = 8,
    valtInt = 0xff,
    valtRoot = 0xff,
    valtAbstractBase = 0x100,
    faNotFunction = -2,
    faVarArgs = -1,
    faArgs0 = 0,
};

namespace hx {
using finalizer = void (*)(value);

namespace cffi {

class CallScope final {
public:
    CallScope();
    ~CallScope() noexcept(false);
    CallScope(const CallScope&) = delete;
    CallScope& operator=(const CallScope&) = delete;

private:
    void* state_ = nullptr;
};

[[nodiscard]] value fromDynamic(const ::Dynamic& input);
[[nodiscard]] ::Dynamic toDynamic(value input);
[[nodiscard]] value alloc_pointer(void* pointer);
[[nodiscard]] vkind createKind(const char* name);

} // namespace cffi
} // namespace hx

namespace cffi {
[[nodiscard]] inline value alloc_pointer(void* pointer) {
    return ::hx::cffi::alloc_pointer(pointer);
}
} // namespace cffi

class AutoGCRoot final {
public:
    AutoGCRoot();
    explicit AutoGCRoot(value input);
    ~AutoGCRoot();
    AutoGCRoot(const AutoGCRoot&) = delete;
    AutoGCRoot& operator=(const AutoGCRoot&) = delete;

    [[nodiscard]] value get() const;
    void set(value input);

private:
    value value_ = nullptr;
};

[[nodiscard]] value alloc_null();
[[nodiscard]] value alloc_int(int input);
[[nodiscard]] value alloc_float(double input);
[[nodiscard]] value alloc_bool(bool input);
[[nodiscard]] value alloc_string(const char* input);
[[nodiscard]] value alloc_wstring(const wchar_t* input);
[[nodiscard]] value alloc_string_len(const char* input, int length);
[[nodiscard]] value alloc_wstring_len(const wchar_t* input, int length);
[[nodiscard]] value alloc_raw_string(int length);
[[nodiscard]] value alloc_empty_string(int length);
[[nodiscard]] value alloc_empty_object();
[[nodiscard]] value alloc_array(int length);
[[nodiscard]] value alloc_array_type(int length, hxValueType type);
[[nodiscard]] value alloc_abstract(vkind kind, void* data);
[[nodiscard]] value create_abstract(vkind kind, int size,
                                    ::hx::finalizer finalizer);
void free_abstract(value input);
[[nodiscard]] value alloc_best_int(int input);
[[nodiscard]] value alloc_int32(int input);

[[nodiscard]] int val_type(value input);
[[nodiscard]] vkind val_kind(value input);
[[nodiscard]] int val_fun_nargs(value input);
[[nodiscard]] bool val_is_null(value input);
[[nodiscard]] bool val_is_int(value input);
[[nodiscard]] bool val_is_float(value input);
[[nodiscard]] bool val_is_bool(value input);
[[nodiscard]] bool val_is_string(value input);
[[nodiscard]] bool val_is_array(value input);
[[nodiscard]] bool val_is_kind(value input, vkind kind);
[[nodiscard]] int val_int(value input);
[[nodiscard]] double val_float(value input);
[[nodiscard]] double val_number(value input);
[[nodiscard]] bool val_bool(value input);
[[nodiscard]] int val_strlen(value input);
[[nodiscard]] const char* val_string(value input);
[[nodiscard]] const wchar_t* val_wstring(value input);
[[nodiscard]] char* val_dup_string(value input);
[[nodiscard]] wchar_t* val_dup_wstring(value input);
[[nodiscard]] char* alloc_string_data(const char* input, int length);
[[nodiscard]] void* val_data(value input);
[[nodiscard]] void* val_to_kind(value input, vkind kind);

[[nodiscard]] int val_id(const char* name);
[[nodiscard]] value val_field(value object, int field);
void alloc_field(value object, int field, value input);

[[nodiscard]] int val_array_size(value array);
[[nodiscard]] value val_array_i(value array, int index);
void val_array_set_i(value array, int index, value input);
void val_array_push(value array, value input);
void val_array_set_size(value array, int length);
[[nodiscard]] value* val_array_ptr(value array);
[[nodiscard]] bool* val_array_bool(value array);
[[nodiscard]] int* val_array_int(value array);
[[nodiscard]] double* val_array_double(value array);
[[nodiscard]] float* val_array_float(value array);
[[nodiscard]] value* val_array_value(value array);

[[nodiscard]] buffer alloc_buffer(const char* initial);
[[nodiscard]] buffer alloc_buffer_len(int length);
void buffer_append(buffer target, const char* text);
void val_buffer(buffer target, value input);
void buffer_append_sub(buffer target, const char* text, int length);
void buffer_append_char(buffer target, int character);
[[nodiscard]] char* buffer_data(buffer target);
[[nodiscard]] int buffer_size(buffer target);
[[nodiscard]] value buffer_val(buffer target);
[[nodiscard]] value buffer_to_string(buffer target);
[[nodiscard]] buffer val_to_buffer(value input);

void val_gc(value input, ::hx::finalizer finalizer);
void val_gc_ptr(void* input, hxPtrFinalizer finalizer);
[[noreturn]] void val_throw(value input);
[[nodiscard]] value val_call0(value function);
[[nodiscard]] value val_call1(value function, value arg0);
[[nodiscard]] value val_call2(value function, value arg0, value arg1);
[[nodiscard]] value val_call3(value function, value arg0, value arg1,
                              value arg2);
[[nodiscard]] value val_callN(value function, value* arguments, int count);
[[nodiscard]] value val_call0_traceexcept(value function);
[[nodiscard]] value val_ocall0(value object, int field);
[[nodiscard]] value val_ocall1(value object, int field, value arg0);
[[nodiscard]] value val_ocall2(value object, int field, value arg0,
                               value arg1);
[[nodiscard]] value val_ocallN(value object, int field, value* arguments,
                               int count);

void alloc_field_numeric(value object, int field, double input);
[[nodiscard]] double val_field_numeric(value object, int field);
[[nodiscard]] value val_field_name(field fieldId);
void val_iter_fields(value object, __hx_field_iter callback, void* context);
void val_iter_field_vals(value object, __hx_field_iter callback,
                         void* context);

[[nodiscard]] value* alloc_root();
void free_root(value* root);
[[nodiscard]] gcroot create_root(value input);
[[nodiscard]] value query_root(gcroot root);
void destroy_root(gcroot root);

void kind_share(vkind* kind, const char* name);

[[nodiscard]] const char* hxs_utf8(HxString input, void* temporary = nullptr);
[[nodiscard]] const wchar_t* hxs_wchar(HxString input,
                                      void* temporary = nullptr);
void gc_set_top_of_stack(void* top, bool force);
void gc_enter_blocking();
void gc_exit_blocking();
[[nodiscard]] bool gc_try_blocking();
[[nodiscard]] bool gc_try_unblocking();
void gc_safe_point();
void gc_change_managed_memory(int delta, const char* reason);
void val_gc_add_root(value* root);
void val_gc_remove_root(value* root);
[[nodiscard]] void* hx_alloc(int size);
int hx_register_prim(const char* name, void* function);
[[noreturn]] void hx_error();
[[noreturn]] void hx_fail(const char* message, const char* file, int line);

[[nodiscard]] inline bool val_is_function(value input) {
    return val_type(input) == valtFunction;
}
[[nodiscard]] inline bool val_is_abstract(value input) {
    return val_type(input) >= valtAbstractBase;
}
[[nodiscard]] inline bool val_is_number(value input) {
    const int type = val_type(input);
    return type == valtInt || type == valtFloat;
}
[[nodiscard]] inline bool val_is_object(value input) {
    const int type = val_type(input);
    return type == valtObject || type == valtEnum || type == valtClass;
}

#define hx_failure(message) hx_fail((message), __FILE__, __LINE__)
#define val_check_kind(v, t) \
    do { if (!val_is_kind((v), (t))) hx_failure("invalid kind"); } while (false)
#define val_check_function(f, n) \
    do { const int _hx_arity = val_fun_nargs((f)); \
         if (!val_is_function((f)) || \
             (_hx_arity != (n) && _hx_arity != faVarArgs)) \
             hx_failure("Bad function"); } while (false)
#define val_check(v, t) \
    do { if (!val_is_##t((v))) hx_failure("type not " #t); } while (false)

[[nodiscard]] inline bool val_get_bool(value input) {
    val_check(input, bool);
    return val_bool(input);
}
[[nodiscard]] inline int val_get_int(value input) {
    val_check(input, int);
    return val_int(input);
}
[[nodiscard]] inline double val_get_double(value input) {
    val_check(input, number);
    return val_number(input);
}
[[nodiscard]] inline const char* val_get_string(value input) {
    val_check(input, string);
    return val_string(input);
}
[[nodiscard]] inline void* val_get_handle(value input, vkind kind) {
    val_check_kind(input, kind);
    return val_to_kind(input, kind);
}
[[nodiscard]] inline void* alloc_private(int size) {
    return hx_alloc(size);
}

#define DEFINE_KIND(name) \
    vkind name = ::hx::cffi::createKind(#name)

#ifndef DECLARE_KIND
#define DECLARE_KIND(name) extern vkind name
#endif

#include <hx/CFFIPrime.h>

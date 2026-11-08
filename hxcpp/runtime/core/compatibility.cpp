#include <hxcpp.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <thread>

namespace {

struct WeakReferenceObject final : ::hx::Object {
    ::hx::gc::Handle referent;
};

void finalizeWeakReference(void* object) {
    auto* reference = static_cast<WeakReferenceObject*>(object);
    if (reference->referent.valid()) {
        ::hx::gc::Runtime::instance().releaseHandle(reference->referent);
        reference->referent = {};
    }
}

const ::hx::gc::TypeDescriptor WeakReferenceDescriptor{
    0x5745414b52454631ULL, "cpp.vm.WeakRef", nullptr,
    sizeof(WeakReferenceObject), alignof(WeakReferenceObject), 0,
    ::hx::gc::TypeFinalizable, 0, nullptr, nullptr,
    &finalizeWeakReference,
};

std::mutex identityMutex;
std::unordered_map<std::uint64_t, int> publicIdByIdentity;
std::unordered_map<int, ::hx::gc::Handle> objectByPublicId;
int nextPublicId = 1;

std::atomic<int> minorBaseDeltaBytes{4 * 1024 * 1024};
std::atomic<int> minorGateMilliseconds{0};
std::atomic<int> minorStartBytes{4 * 1024 * 1024};
std::atomic<int> parallelThreads{1};
std::atomic<int> refineThreads{1};
std::atomic<int> maximumPauseMilliseconds{4};
std::atomic<int> aggressiveSafepoints{0};
std::atomic<int> parallelReferenceProcessing{1};
std::atomic<int> largeRefresh{1};

[[nodiscard]] void* managedObject(const Dynamic& value) {
    return value.tag() == Dynamic::Tag::Object ? value.managedPointer()
                                                : nullptr;
}

} // namespace

void __hxcpp_check_overflow(int) {}

double __hxcpp_drand() {
    thread_local std::mt19937_64 generator(std::random_device{}());
    return std::generate_canonical<double, 53>(generator);
}

void __hxcpp_print_string(const String& value) {
    std::fwrite(value.view().data(), 1, value.view().size(), stdout);
    std::fflush(stdout);
}

void __hxcpp_println_string(const String& value) {
    std::fwrite(value.view().data(), 1, value.view().size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

double __hxcpp_parse_substr_float(const String& value, int start, int length) {
    if (start < 0 || length < 1 || start > value.length ||
        length > value.length - start)
        return std::numeric_limits<double>::quiet_NaN();
    const std::string text(value.view().substr(
        static_cast<std::size_t>(start), static_cast<std::size_t>(length)));
    char* end = nullptr;
    const double result = std::strtod(text.c_str(), &end);
    return end == text.c_str() ? std::numeric_limits<double>::quiet_NaN()
                               : result;
}

int __hxcpp_parse_substr_int(const String& value, int start, int length) {
    if (start < 0 || start > value.length)
        return 0;
    if (length < 0)
        length = value.length - start;
    if (length < 1 || length > value.length - start)
        return 0;
    const std::string text(value.view().substr(
        static_cast<std::size_t>(start), static_cast<std::size_t>(length)));
    const char* cursor = text.c_str();
    while (*cursor != '\0' &&
           std::isspace(static_cast<unsigned char>(*cursor)) != 0)
        ++cursor;
    if (*cursor == '+' || *cursor == '-')
        ++cursor;
    const int base = cursor[0] == '0' &&
                     (cursor[1] == 'x' || cursor[1] == 'X')
        ? 16 : 10;
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, base);
    return end == text.c_str() ? 0 : static_cast<int>(parsed);
}

void __hxcpp_align_set_float32(unsigned char* base, int address, float value) {
    if (base == nullptr || address < 0)
        throw std::out_of_range("invalid aligned float32 store");
    std::memcpy(base + address, &value, sizeof(value));
}

float __hxcpp_align_get_float32(unsigned char* base, int address) {
    if (base == nullptr || address < 0)
        throw std::out_of_range("invalid aligned float32 load");
    float value{};
    std::memcpy(&value, base + address, sizeof(value));
    return value;
}

void __hxcpp_align_set_float64(unsigned char* base, int address, double value) {
    if (base == nullptr || address < 0)
        throw std::out_of_range("invalid aligned float64 store");
    std::memcpy(base + address, &value, sizeof(value));
}

double __hxcpp_align_get_float64(unsigned char* base, int address) {
    if (base == nullptr || address < 0)
        throw std::out_of_range("invalid aligned float64 load");
    double value{};
    std::memcpy(&value, base + address, sizeof(value));
    return value;
}

void __hxcpp_reverse_endian(int& value) {
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    value = static_cast<int>(((bits & 0x000000ffU) << 24U) |
                             ((bits & 0x0000ff00U) << 8U) |
                             ((bits & 0x00ff0000U) >> 8U) |
                             ((bits & 0xff000000U) >> 24U));
}

bool __hxcpp_is_const_string(const String& value) {
    return value.literal_ != nullptr;
}

void __hxcpp_enum_force(::hx::EnumBase value, String name, int index) {
    if (!value)
        throw std::invalid_argument("cannot force a null enum identity");
    value->_hx_tag = name;
    value->_hx_index = index;
}

String __hxcpp_get_kind(Dynamic value) {
    if (value.tag() == Dynamic::Tag::NativePointer)
        return String("Pointer");
    void* object = managedObject(value);
    const auto* type = object == nullptr ? nullptr :
        ::hx::gc::Runtime::instance().typeOf(object);
    return type == nullptr ? String() : String(type->name);
}

unsigned int __hxcpp_obj_hash(Dynamic value) {
    switch (value.tag()) {
        case Dynamic::Tag::Null:
            return 0;
        case Dynamic::Tag::Integer:
        case Dynamic::Tag::Boolean:
            return static_cast<unsigned int>(value.Cast<std::int64_t>());
        case Dynamic::Tag::Floating: {
            const std::uint64_t bits = std::bit_cast<std::uint64_t>(
                value.Cast<double>());
            return static_cast<unsigned int>(bits ^ (bits >> 32U));
        }
        case Dynamic::Tag::String:
            return static_cast<unsigned int>(value.Cast<String>().hashCode());
        case Dynamic::Tag::Object:
            return static_cast<unsigned int>(
                ::hx::gc::Runtime::instance().identityOf(
                    value.managedPointer()));
        default:
            return static_cast<unsigned int>(
                std::hash<std::uintptr_t>{}(value.memberFunctionAddress()));
    }
}

int __hxcpp_obj_id(Dynamic value) {
    void* object = managedObject(value);
    if (object == nullptr)
        return static_cast<int>(__hxcpp_obj_hash(value));
    auto& runtime = ::hx::gc::Runtime::instance();
    const std::uint64_t identity = runtime.identityOf(object);
    std::scoped_lock lock(identityMutex);
    const auto existing = publicIdByIdentity.find(identity);
    if (existing != publicIdByIdentity.end())
        return existing->second;
    if (nextPublicId <= 0)
        throw std::overflow_error("Haxe object ID space exhausted");
    const int id = nextPublicId++;
    publicIdByIdentity.emplace(identity, id);
    objectByPublicId.emplace(id, runtime.createHandle(
        object, ::hx::gc::HandleKind::Weak));
    return id;
}

::hx::Object* __hxcpp_id_obj(int id) {
    auto& runtime = ::hx::gc::Runtime::instance();
    std::scoped_lock lock(identityMutex);
    const auto found = objectByPublicId.find(id);
    if (found == objectByPublicId.end())
        return nullptr;
    void* object = runtime.resolveHandle(found->second);
    if (object == nullptr) {
        runtime.releaseHandle(found->second);
        objectByPublicId.erase(found);
        return nullptr;
    }
    return static_cast<::hx::Object*>(object);
}

::hx::Object* __hxcpp_weak_ref_create(Dynamic value) {
    ::hx::RootedValue<Dynamic> rooted(value);
    void* object = managedObject(rooted.get());
    auto reference = ::hx::allocate<WeakReferenceObject>(
        WeakReferenceDescriptor);
    reference->referent = ::hx::gc::Runtime::instance().createHandle(
        object, ::hx::gc::HandleKind::Weak);
    return reference.get();
}

::hx::Object* __hxcpp_weak_ref_get(Dynamic value) {
    void* raw = managedObject(value);
    if (raw == nullptr || ::hx::gc::Runtime::instance().typeOf(raw) !=
                          &WeakReferenceDescriptor)
        throw std::invalid_argument("value is not a cpp.vm.WeakRef");
    ::hx::ScopedPin pin(static_cast<::hx::Object*>(raw));
    auto* reference = static_cast<WeakReferenceObject*>(pin.get());
    return static_cast<::hx::Object*>(
        ::hx::gc::Runtime::instance().resolveHandle(reference->referent));
}

void _hx_set_finalizer(Dynamic value, void (*callback)(Dynamic)) {
    void* object = managedObject(value);
    if (object == nullptr || callback == nullptr)
        throw std::invalid_argument("invalid Haxe finalizer");
    ::hx::gc::Runtime::instance().addFinalizer(object,
        [callback](void* input) {
            callback(Dynamic(static_cast<::hx::Object*>(input)));
        });
}

void __hxcpp_set_finalizer(Dynamic value, void* callback) {
    _hx_set_finalizer(value,
        reinterpret_cast<void (*)(Dynamic)>(callback));
}

void __hxcpp_add_member_finalizer(::hx::Object* object,
                                  _hx_member_finalizer callback,
                                  bool) {
    if (object == nullptr || callback == nullptr)
        throw std::invalid_argument("invalid member finalizer");
    ::hx::gc::Runtime::instance().addFinalizer(object,
        [callback](void* input) {
            (static_cast<::hx::Object*>(input)->*callback)();
        });
}

void __hxcpp_add_alloc_finalizer(void* allocation,
                                 _hx_alloc_finalizer callback,
                                 bool) {
    if (allocation == nullptr || callback == nullptr)
        throw std::invalid_argument("invalid allocation finalizer");
    ::hx::gc::Runtime::instance().addFinalizer(allocation, callback);
}

void __hxcpp_reachable(::hx::Object* object) {
    if (object != nullptr)
        (void)::hx::gc::Runtime::instance().identityOf(object);
}

double __hxcpp_time_stamp() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void __hxcpp_gc_minor() { __hxcpp_collect(false); }
void __hxcpp_gc_update() { ::hx::gc::Runtime::instance().safepointPoll(); }
int __hxcpp_get_minor_base_delta_bytes() { return minorBaseDeltaBytes.load(); }
void __hxcpp_set_minor_base_delta_bytes(int value) {
    minorBaseDeltaBytes.store(std::max(0, value));
}
int __hxcpp_get_minor_gate_ms() { return minorGateMilliseconds.load(); }
void __hxcpp_set_minor_gate_ms(int value) {
    minorGateMilliseconds.store(std::max(0, value));
}
int __hxcpp_get_minor_start_bytes() { return minorStartBytes.load(); }
void __hxcpp_set_minor_start_bytes(int value) {
    minorStartBytes.store(std::max(0, value));
}
void __hxcpp_gc_large_refresh_enable(int value) { largeRefresh.store(value != 0); }
int __hxcpp_gc_get_large_refresh_enabled() { return largeRefresh.load(); }
void __hxcpp_gc_set_threads(int parallel, int refine) {
    parallelThreads.store(std::max(1, parallel));
    refineThreads.store(std::max(1, refine));
}
int __hxcpp_gc_get_parallel_threads() { return parallelThreads.load(); }
int __hxcpp_gc_get_refine_threads() { return refineThreads.load(); }
void __hxcpp_gc_set_max_pause_ms(int value) {
    maximumPauseMilliseconds.store(std::max(1, value));
}
int __hxcpp_gc_get_max_pause_ms() { return maximumPauseMilliseconds.load(); }
void __hxcpp_gc_aggressive_safepoint(int value) {
    aggressiveSafepoints.store(value != 0);
}
int __hxcpp_gc_get_aggressive_safepoint() { return aggressiveSafepoints.load(); }
void __hxcpp_gc_enable_parallel_ref_proc(int value) {
    parallelReferenceProcessing.store(value != 0);
}
int __hxcpp_gc_get_parallel_ref_proc_enabled() {
    return parallelReferenceProcessing.load();
}

int __hxcpp_GetCurrentThreadNumber() {
    static std::atomic<int> nextThreadNumber{1};
    thread_local const int threadNumber =
        nextThreadNumber.fetch_add(1, std::memory_order_relaxed);
    return threadNumber;
}

void __hxcpp_execution_trace(int) {}
void __hxcpp_on_line_changed(::hx::StackContext*) {}

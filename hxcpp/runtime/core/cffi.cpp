#include <hx/CFFI.h>
#include <hx/gc/internal.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <climits>
#include <cstring>
#include <cwchar>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr std::uintptr_t ImmediateMask = 0x7U;
constexpr std::uintptr_t IntegerTag = 0x1U;
constexpr std::uintptr_t BooleanTag = 0x2U;
constexpr std::uint64_t CellMagic = 0x5a47434646495631ULL;

struct ValueCell final {
    std::uint64_t magic = CellMagic;
    Dynamic value;
    ::hx::gc::Handle strong;
    bool persistent = false;
};

// CFFI only needs a managed address to remain stable for the dynamic extent
// of one native call.  A persistent Pinned handle is deliberately stronger:
// it isolates the object into pinned space with a moving safepoint and an
// exact heap remap.  Using that operation for every GL/audio argument turns a
// high-frequency Prime boundary into repeated whole-heap work.  Keep a cheap
// strong handle for liveness plus Runtime's handshake-published scoped pin for
// address stability instead; moving collectors already exclude the containing
// region while the pin is live. A handle also avoids interleaving synthetic
// RootScope frames with generated roots owned by the native helper.
struct CallScopedPin final {
    explicit CallScopedPin(
            void* input,
            const ::hx::gc::Handle* borrowedStrong = nullptr) {
        auto& runtime = ::hx::gc::Runtime::instance();
        ownsStrong = borrowedStrong == nullptr || !borrowedStrong->valid();
        if (ownsStrong)
            strong = runtime.createHandle(input, ::hx::gc::HandleKind::Strong);
        try {
            void* resolved = runtime.resolveHandle(
                ownsStrong ? strong : *borrowedStrong);
            reference.store(runtime.makeRef(resolved).bits(),
                            std::memory_order_release);
            object = runtime.acquireScopedPin(reference);
        } catch (...) {
            if (ownsStrong) {
                runtime.releaseHandle(strong);
                strong = {};
            }
            throw;
        }
    }

    ~CallScopedPin() noexcept {
        auto& runtime = ::hx::gc::Runtime::instance();
        runtime.releaseScopedPin(object);
        if (ownsStrong)
            runtime.releaseHandle(strong);
    }

    CallScopedPin(const CallScopedPin&) = delete;
    CallScopedPin& operator=(const CallScopedPin&) = delete;

    ::hx::gc::Handle strong;
    ::hx::gc::RefSlot reference{0};
    void* object = nullptr;
    bool ownsStrong = false;
};

struct CallState final {
    struct ArrayValueProxy final {
        value array = nullptr;
        std::vector<value> values;
    };
    CallState* previous = nullptr;
    std::vector<ValueCell*> cells;
    std::vector<::hx::gc::Handle> pins;
    std::vector<std::unique_ptr<CallScopedPin>> scopedPins;
    std::vector<std::unique_ptr<std::byte[]>> nativeBlocks;
    std::vector<std::unique_ptr<ArrayValueProxy>> arrayValueProxies;
};

constexpr std::size_t MaximumCallScopeDepth = 64;

// External Prime calls with scalar-only signatures (the normal graphics and
// audio path) do not need a managed-value ledger. Keep a tiny dynamic-scope
// frame so the ledger can be created lazily if an ABI adapter or a native
// callback actually crosses back into managed values.
struct ExternalPrimeFrame final {
    CallState* callState = nullptr;
    ExternalPrimeFrame* previous = nullptr;
    bool nativeBlocking = false;
};

thread_local CallState* currentCall = nullptr;
thread_local ExternalPrimeFrame* currentExternalPrimeFrame = nullptr;

void ensureExternalPrimeCallState();

[[nodiscard]] ValueCell* asCell(value input);
void destroyCell(ValueCell* cell);

[[nodiscard]] void* allocateCallBytes(std::size_t size) {
    ensureExternalPrimeCallState();
    if (currentCall == nullptr)
        throw std::logic_error(
            "CFFI temporary native storage requested outside call scope");
    auto block = std::make_unique<std::byte[]>(std::max<std::size_t>(size, 1));
    void* result = block.get();
    currentCall->nativeBlocks.push_back(std::move(block));
    return result;
}

struct RootSlot final {
    value current = nullptr;
};

struct RootHandle final {
    value current = nullptr;
};

std::mutex rootsMutex;
std::vector<RootSlot*> rootSlots;

struct FieldState final {
    std::mutex mutex;
    std::unordered_map<int, Dynamic> fields;
};

// Field names are immutable process-lifetime atoms.  Generated anonymous
// records use these atoms exactly as the original hxcpp uses its cached
// VariantKey: the overwhelmingly common read path is a pointer comparison,
// with a content comparison only when a thread first sees a field name.
// Keeping the per-thread cache exact (hash + length + bytes) means hash
// collisions can never alias distinct Haxe fields.
struct FieldAtom final {
    int id = 0;
    std::uint64_t hash = 0;
    std::string name;
    String haxeName;
};

std::mutex idsMutex;
std::unordered_map<std::string, std::unique_ptr<FieldAtom>> fieldAtomsByName;
std::unordered_map<int, const FieldAtom*> fieldAtomsById;
std::atomic<int> nextId{1};

struct FieldAtomCacheEntry final {
    std::uint64_t hash = 0;
    const FieldAtom* atom = nullptr;
};

constexpr std::size_t FieldAtomCacheSize = 64;
thread_local FieldAtomCacheEntry fieldAtomCache[FieldAtomCacheSize];

[[nodiscard]] std::uint64_t hashFieldName(std::string_view name) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : name) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] const FieldAtom* internField(std::string_view name) {
    const std::uint64_t hash = hashFieldName(name);
    auto& cached = fieldAtomCache[hash & (FieldAtomCacheSize - 1U)];
    if (cached.atom != nullptr && cached.hash == hash &&
        cached.atom->name.size() == name.size() &&
        (name.empty() || std::memcmp(cached.atom->name.data(), name.data(),
                                     name.size()) == 0))
        return cached.atom;

    std::scoped_lock lock(idsMutex);
    const std::string key(name);
    auto found = fieldAtomsByName.find(key);
    const FieldAtom* result = nullptr;
    if (found != fieldAtomsByName.end()) {
        result = found->second.get();
    } else {
        auto created = std::make_unique<FieldAtom>();
        created->id = nextId.fetch_add(1, std::memory_order_relaxed);
        created->hash = hash;
        created->name = key;
        if (::hx::gc::Runtime::instance().initialized()) {
            created->haxeName = String(created->name);
            ::hx::gc::Runtime::instance().registerStaticRoot(
                created->haxeName.mPtr);
        }
        result = created.get();
        const std::string stableName = created->name;
        fieldAtomsById.emplace(created->id, result);
        fieldAtomsByName.emplace(stableName, std::move(created));
    }
    cached = {hash, result};
    return result;
}

[[nodiscard]] const FieldAtom* internField(const char* name) {
    return internField(std::string_view(name == nullptr ? "" : name));
}

} // namespace

int __hxcpp_field_to_id(const char* name) {
    return internField(name)->id;
}

const String& __hxcpp_field_from_id(int id) {
    static const String empty;
    std::scoped_lock lock(idsMutex);
    const auto found = fieldAtomsById.find(id);
    if (found == fieldAtomsById.end())
        return empty;
    FieldAtom* atom = const_cast<FieldAtom*>(found->second);
    if (atom->haxeName.isNull() &&
        ::hx::gc::Runtime::instance().initialized()) {
        atom->haxeName = String(atom->name);
        ::hx::gc::Runtime::instance().registerStaticRoot(atom->haxeName.mPtr);
    }
    return atom->haxeName;
}

namespace {

// Generated anonymous structures are overwhelmingly tiny (FFT complex values,
// points, rectangles, option records, ...).  Keeping those fields in a native
// unordered_map used to require a native allocation, a mutex and a GC finalizer
// for every single object.  The compact representation lives entirely in the
// movable managed payload and is traced precisely by traceManagedAnon.
struct ManagedAnonState final {
    std::uint32_t capacity = 0;
    std::uint32_t reserved = 0;
    ::hx::gc::RefSlot overflow{0};
};

struct ManagedAnonEntry final {
    std::atomic<const FieldAtom*> field{nullptr};
    Dynamic value;
};

constexpr std::size_t alignManagedAnon(std::size_t value,
                                       std::size_t alignment) noexcept {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

constexpr std::size_t ManagedAnonStateOffset = alignManagedAnon(
    sizeof(::hx::Anon_obj), alignof(ManagedAnonState));
constexpr std::size_t ManagedAnonEntriesOffset = alignManagedAnon(
    ManagedAnonStateOffset + sizeof(ManagedAnonState),
    alignof(ManagedAnonEntry));

[[nodiscard]] ManagedAnonState* managedAnonState(void* object) noexcept {
    return reinterpret_cast<ManagedAnonState*>(
        static_cast<std::byte*>(object) + ManagedAnonStateOffset);
}

[[nodiscard]] ManagedAnonEntry* managedAnonEntries(void* object) noexcept {
    return reinterpret_cast<ManagedAnonEntry*>(
        static_cast<std::byte*>(object) + ManagedAnonEntriesOffset);
}

[[nodiscard]] std::size_t managedAnonPayloadBytes(
        std::size_t capacity) {
    if (capacity >
        (SIZE_MAX - ManagedAnonEntriesOffset) / sizeof(ManagedAnonEntry))
        throw std::bad_alloc();
    return ManagedAnonEntriesOffset + capacity * sizeof(ManagedAnonEntry);
}

extern const ::hx::gc::TypeDescriptor DynamicAnonDescriptor;
[[nodiscard]] ::hx::ObjectPtr<::hx::Anon_obj> createDynamicAnon();

struct CffiObject final : ::hx::Object {
    explicit CffiObject(FieldState* input) noexcept : state(input) {}
    FieldState* state = nullptr;
};

struct BufferState final {
    std::vector<char> bytes;
};

struct CffiBuffer final : ::hx::Object {
    explicit CffiBuffer(BufferState* input) noexcept : state(input) {}
    BufferState* state = nullptr;
};

struct PointerState final {
    void* data = nullptr;
    vkind kind = nullptr;
    ::hx::finalizer finalizer = nullptr;
    // Native libraries such as Lime cache the exact value returned by
    // alloc_pointer.  val_gc therefore has to keep that token allocation
    // alive until the managed pointer is finalized, without keeping the
    // managed object itself alive.
    value finalizerToken = nullptr;
    bool ownsData = false;
};

struct CffiPointer final : ::hx::Object {
    explicit CffiPointer(PointerState* input) noexcept : state(input) {}
    PointerState* state = nullptr;
};

struct ExternalFinalizerState final {
    value token = nullptr;
    ::hx::finalizer callback = nullptr;
    ::hx::gc::EphemeronHandle association;
    std::uint64_t identity = 0;
};

struct CffiFinalizerAttachment final : ::hx::Object {
    CffiFinalizerAttachment(void* targetObject,
                            ExternalFinalizerState* inputState) noexcept
        : state(inputState) {
        target.store(::hx::gc::Runtime::instance().makeRef(targetObject).bits(),
                     std::memory_order_release);
    }

    ::hx::gc::RefSlot target{0};
    ExternalFinalizerState* state = nullptr;
};

std::mutex externalFinalizersMutex;
std::unordered_map<std::uint64_t, ExternalFinalizerState*> externalFinalizers;
std::mutex externalRootsMutex;
std::unordered_map<value*, value> externalRoots;
std::atomic<std::int64_t> externalManagedBytes{0};
thread_local std::size_t cffiNativeBlockingDepth = 0;

// A native Prime may synchronously call a Haxe closure through val_call*. The
// enclosing Prime remains native-blocked for driver/I/O waits, but the callback
// itself must participate in exact root-frame handshakes. Suspend every CFFI-
// owned blocking level for that dynamic extent, then restore it on return.
class ManagedCffiCallbackScope final {
public:
    ManagedCffiCallbackScope() {
        // A scalar Prime can still invoke a Haxe callback retained by native
        // code. Materialize its ledger before val_call* creates result tokens.
        ensureExternalPrimeCallState();
        auto& runtime = ::hx::gc::Runtime::instance();
        if (!runtime.currentThreadAttached())
            return;
        suspendedDepth_ = cffiNativeBlockingDepth;
        while (cffiNativeBlockingDepth != 0) {
            --cffiNativeBlockingDepth;
            runtime.leaveNativeBlocking();
        }
    }

    ~ManagedCffiCallbackScope() noexcept(false) {
        auto& runtime = ::hx::gc::Runtime::instance();
        if (!runtime.currentThreadAttached())
            return;
        while (cffiNativeBlockingDepth < suspendedDepth_) {
            runtime.enterNativeBlocking();
            ++cffiNativeBlockingDepth;
        }
    }

    ManagedCffiCallbackScope(const ManagedCffiCallbackScope&) = delete;
    ManagedCffiCallbackScope& operator=(
        const ManagedCffiCallbackScope&) = delete;

private:
    std::size_t suspendedDepth_ = 0;
};

[[nodiscard]] void* pinManagedAddressForCall(
        void* managed, const ::hx::gc::Handle* borrowedStrong = nullptr) {
    if (managed == nullptr)
        return nullptr;
    ensureExternalPrimeCallState();
    if (currentCall == nullptr)
        throw std::logic_error(
            "CFFI raw managed pointer requested outside call scope");

    auto& runtime = ::hx::gc::Runtime::instance();
    // ABI adapters normally run before a Prime enters native-blocking state.
    // ManagedCffiCallbackScope remains required for legacy/re-entrant paths
    // that request another raw address while an outer native call is blocked.
    ManagedCffiCallbackScope managedScope;
    if (!runtime.currentThreadAttached()) {
        const ::hx::gc::Handle pin = runtime.createHandle(
            managed, ::hx::gc::HandleKind::Pinned);
        void* result = runtime.resolveHandle(pin);
        currentCall->pins.push_back(pin);
        return result;
    }

    auto pin = std::make_unique<CallScopedPin>(managed, borrowedStrong);
    void* result = pin->object;
    currentCall->scopedPins.push_back(std::move(pin));
    return result;
}

std::mutex nativeBytesMutex;
std::vector<std::unique_ptr<std::byte[]>> nativeBytes;

class PrimeClosure final : public ::hx::LocalFunc {
public:
    PrimeClosure(::hx::cffi::PrimeInvoker input, int inputArity) noexcept
        : invoker(input), arity(inputArity) {}

    Dynamic invokeMany(const Dynamic* arguments, int count) override {
        if (count != arity)
            throw std::runtime_error("Prime closure arity mismatch");
        ::hx::cffi::CallScope scope;
        for (int index = 0; index < count; ++index)
            (void)::hx::cffi::fromDynamic(arguments[index]);
        return invoker(arguments, count);
    }

    ::hx::cffi::PrimeInvoker invoker = nullptr;
    int arity = 0;
};

class VarArgsClosure final : public ::hx::LocalFunc {
public:
    explicit VarArgsClosure(const ::Dynamic& input) : function(input) {
        ::hx::gc::Runtime::instance().publish(this);
    }

    ::Dynamic invokeMany(const ::Dynamic* arguments, int count) override {
        auto packed = ::Array_obj<::Dynamic>::__new();
        ::hx::gc::RefSlot* roots[]{&packed.mPtr};
        ::hx::gc::RootScope rootScope(roots, 1);
        for (int index = 0; index < count; ++index)
            packed->push(arguments[index]);
        return function(::Dynamic(packed));
    }

    static void trace(void* object, std::size_t,
                      ::hx::gc::TraceVisitor& visitor) {
        visitor.visit(static_cast<VarArgsClosure*>(object)->function.mPtr);
    }

    static void* operator new(std::size_t size) {
        if (size != sizeof(VarArgsClosure))
            throw std::bad_alloc();
        return ::hx::gc::Runtime::instance().allocate(descriptor);
    }
    static void operator delete(void*) noexcept {}
    static void operator delete(void*, std::size_t) noexcept {}

    static const ::hx::gc::TypeDescriptor descriptor;
    ::Dynamic function;
};

const ::hx::gc::TypeDescriptor VarArgsClosure::descriptor{
    0x564152415247434cULL, "CFFI.VarArgsClosure", nullptr,
    sizeof(VarArgsClosure), alignof(VarArgsClosure), 0,
    ::hx::gc::TypeContainsReferences | ::hx::gc::TypeFunction,
    0, nullptr, &VarArgsClosure::trace, nullptr};

void storeOwnedDynamic(void* owner, Dynamic& destination,
                       const Dynamic& source) {
    auto& runtime = ::hx::gc::Runtime::instance();
    void* managed = nullptr;
    if (Dynamic::isReferenceTag(source.tag()))
        managed = source.referencePointer();
    runtime.storeBarrier(owner, destination.mPtr, managed);
    destination.copyNonReferenceFrom(source);
}

void traceFields(void* object, std::size_t,
                 ::hx::gc::TraceVisitor& visitor) {
    auto* self = static_cast<CffiObject*>(object);
    if (self->state == nullptr)
        return;
    std::scoped_lock lock(self->state->mutex);
    for (auto& [field, fieldValue] : self->state->fields) {
        (void)field;
        visitor.visit(fieldValue.mPtr);
    }
}

bool getObjectField(void* object, const char* name, int access, void* output) {
    (void)access;
    auto* self = static_cast<CffiObject*>(object);
    if (self->state == nullptr)
        return false;
    const int field = val_id(name);
    std::scoped_lock lock(self->state->mutex);
    auto found = self->state->fields.find(field);
    if (found == self->state->fields.end())
        return false;
    *static_cast<Dynamic*>(output) = found->second;
    return true;
}

bool setObjectField(void* object, const char* name, int access,
                    const void* input) {
    (void)access;
    auto* self = static_cast<CffiObject*>(object);
    if (self->state == nullptr)
        return false;
    const int field = val_id(name);
    std::scoped_lock lock(self->state->mutex);
    storeOwnedDynamic(self, self->state->fields[field],
                      *static_cast<const Dynamic*>(input));
    return true;
}

void traceDynamicAnon(void* object, std::size_t,
                      ::hx::gc::TraceVisitor& visitor) {
    auto* self = static_cast<::hx::Anon_obj*>(object);
    auto* state = static_cast<FieldState*>(self->state);
    if (state == nullptr)
        return;
    std::scoped_lock lock(state->mutex);
    for (auto& [field, fieldValue] : state->fields) {
        (void)field;
        visitor.visit(fieldValue.mPtr);
    }
}

void finalizeDynamicAnon(void* object) {
    auto* self = static_cast<::hx::Anon_obj*>(object);
    delete static_cast<FieldState*>(self->state);
    self->state = nullptr;
}

bool getDynamicAnonField(void* object, const char* name, int access,
                         void* output) {
    (void)access;
    auto* self = static_cast<::hx::Anon_obj*>(object);
    auto* state = static_cast<FieldState*>(self->state);
    if (state == nullptr)
        return false;
    const int field = val_id(name);
    std::scoped_lock lock(state->mutex);
    auto found = state->fields.find(field);
    if (found == state->fields.end())
        return false;
    *static_cast<Dynamic*>(output) = found->second;
    return true;
}

bool setDynamicAnonField(void* object, const char* name, int access,
                         const void* input) {
    (void)access;
    auto* self = static_cast<::hx::Anon_obj*>(object);
    auto* state = static_cast<FieldState*>(self->state);
    if (state == nullptr)
        return false;
    const int field = val_id(name);
    std::scoped_lock lock(state->mutex);
    storeOwnedDynamic(self, state->fields[field],
                      *static_cast<const Dynamic*>(input));
    return true;
}

void snapshotFieldStateNames(FieldState* state,
                             std::vector<const FieldAtom*>& output) {
    if (state == nullptr)
        return;
    std::vector<int> fieldIds;
    {
        std::scoped_lock lock(state->mutex);
        fieldIds.reserve(state->fields.size());
        for (const auto& [field, value] : state->fields) {
            (void)value;
            fieldIds.push_back(field);
        }
    }
    // Do not hold FieldState::mutex while taking idsMutex: setters intern the
    // field first and take these locks in the opposite phases.
    std::scoped_lock idsLock(idsMutex);
    for (const int field : fieldIds) {
        const auto found = fieldAtomsById.find(field);
        if (found != fieldAtomsById.end())
            output.push_back(found->second);
    }
}

void emitFieldNames(const std::vector<const FieldAtom*>& fields, void* context,
                    ::hx::gc::DynamicFieldNameVisitor visitor) {
    for (const FieldAtom* field : fields)
        visitor(context, field->name.data(), field->name.size());
}

void enumerateDynamicAnonFields(
        void* object, void* context,
        ::hx::gc::DynamicFieldNameVisitor visitor) {
    auto* self = static_cast<::hx::Anon_obj*>(object);
    std::vector<const FieldAtom*> fields;
    snapshotFieldStateNames(static_cast<FieldState*>(self->state), fields);
    emitFieldNames(fields, context, visitor);
}

void enumerateObjectFields(
        void* object, void* context,
        ::hx::gc::DynamicFieldNameVisitor visitor) {
    auto* self = static_cast<CffiObject*>(object);
    std::vector<const FieldAtom*> fields;
    snapshotFieldStateNames(self->state, fields);
    emitFieldNames(fields, context, visitor);
}

void enumerateManagedAnonFields(
        void* object, void* context,
        ::hx::gc::DynamicFieldNameVisitor visitor) {
    auto* state = managedAnonState(object);
    std::vector<const FieldAtom*> fields;

    auto& runtime = ::hx::gc::Runtime::instance();
    auto* overflow = static_cast<::hx::Anon_obj*>(
        runtime.loadBarrier(state->overflow));
    if (overflow != nullptr) {
        ::hx::ScopedPin overflowPin(overflow);
        auto* dynamic = static_cast<::hx::Anon_obj*>(overflowPin.get());
        snapshotFieldStateNames(static_cast<FieldState*>(dynamic->state),
                                fields);
    }

    auto* entries = managedAnonEntries(object);
    fields.reserve(fields.size() + state->capacity);
    for (std::uint32_t index = 0; index < state->capacity; ++index) {
        const FieldAtom* field =
            entries[index].field.load(std::memory_order_acquire);
        if (field != nullptr)
            fields.push_back(field);
    }
    // No pointer into the movable anonymous payload is used while the visitor
    // pushes managed Strings and may safepoint.
    emitFieldNames(fields, context, visitor);
}

void traceManagedAnon(void* object, std::size_t payloadBytes,
                      ::hx::gc::TraceVisitor& visitor) {
    if (payloadBytes < ManagedAnonEntriesOffset)
        throw std::logic_error("managed anonymous object payload is truncated");
    auto* state = managedAnonState(object);
    const std::size_t available =
        (payloadBytes - ManagedAnonEntriesOffset) / sizeof(ManagedAnonEntry);
    if (state->capacity > available)
        throw std::logic_error("managed anonymous object capacity is invalid");
    visitor.visit(state->overflow);
    auto* entries = managedAnonEntries(object);
    for (std::uint32_t index = 0; index < state->capacity; ++index) {
        if (entries[index].field.load(std::memory_order_acquire) != nullptr)
            visitor.visit(entries[index].value.mPtr);
    }
}

[[nodiscard]] bool getOverflowAnonField(ManagedAnonState& state,
                                        const char* name, void* output) {
    auto& runtime = ::hx::gc::Runtime::instance();
    auto* overflow = static_cast<::hx::Anon_obj*>(
        runtime.loadBarrier(state.overflow));
    if (overflow == nullptr)
        return false;
    ::hx::ScopedPin overflowPin(overflow);
    return getDynamicAnonField(overflowPin.get(), name, ::hx::paccDynamic,
                               output);
}

bool getManagedAnonField(void* object, const char* name, int access,
                         void* output) {
    (void)access;
    auto* state = managedAnonState(object);
    auto* entries = managedAnonEntries(object);
    const FieldAtom* field = internField(name);
    for (std::uint32_t index = 0; index < state->capacity; ++index) {
        auto& entry = entries[index];
        if (entry.field.load(std::memory_order_acquire) == field) {
            // Snapshot every byte that resides in the anonymous object before
            // Dynamic assignment can enter a barrier/safepoint.  This permits
            // the managed-anon read fast path to run without pinning: the
            // relocation remap handshake cannot reclaim the old region until
            // this mutator reaches a safepoint, and no raw object address is
            // used after the snapshot has been taken.
            Dynamic snapshot(entry.value);
            *static_cast<Dynamic*>(output) = std::move(snapshot);
            return true;
        }
    }
    if (state->overflow.load(std::memory_order_acquire) == 0)
        return false;

    // Overflow lookup can lock and safepoint.  Re-resolve and pin the owner
    // only for that cold path before taking another address into its payload.
    ::hx::ScopedPin ownerPin(static_cast<::hx::Object*>(object));
    state = managedAnonState(ownerPin.get());
    return getOverflowAnonField(*state, name, output);
}

[[nodiscard]] bool storeManagedAnonField(::hx::Anon_obj* self,
                                         const FieldAtom* field,
                                         const char* name,
                                         const Dynamic& input,
                                         int preferredIndex = -1) {
    auto* state = managedAnonState(self);
    auto* entries = managedAnonEntries(self);
    ManagedAnonEntry* vacant = nullptr;

    if (preferredIndex >= 0 &&
        static_cast<std::uint32_t>(preferredIndex) < state->capacity) {
        auto& preferred = entries[preferredIndex];
        const FieldAtom* current =
            preferred.field.load(std::memory_order_acquire);
        if (current == nullptr || current == field)
            vacant = &preferred;
    }

    for (std::uint32_t index = 0; index < state->capacity; ++index) {
        auto& entry = entries[index];
        const FieldAtom* current = entry.field.load(std::memory_order_acquire);
        if (current != nullptr) {
            if (current == field) {
                entry.value = input;
                return true;
            }
        } else if (vacant == nullptr) {
            vacant = &entry;
        }
    }

    if (vacant != nullptr) {
        // Publish the reference before making the entry visible.  If tracing
        // races this insertion, Dynamic's insertion barrier keeps input live.
        vacant->value = input;
        vacant->field.store(field, std::memory_order_release);
        return true;
    }

    auto& runtime = ::hx::gc::Runtime::instance();
    auto* overflow = static_cast<::hx::Anon_obj*>(
        runtime.loadBarrier(state->overflow));
    if (overflow == nullptr) {
        // Allocation can safepoint, so explicitly root a managed input while
        // the parent remains pinned by the caller.
        ::hx::gc::RefSlot* roots[]{
            &const_cast<Dynamic&>(input).mPtr,
        };
        ::hx::gc::RootScope inputRoot(roots, 1);
        auto created = createDynamicAnon();
        overflow = created.get();
        runtime.storeBarrier(self, state->overflow, overflow);
    }
    ::hx::ScopedPin overflowPin(overflow);
    return setDynamicAnonField(overflowPin.get(), name, ::hx::paccDynamic,
                               &input);
}

bool setManagedAnonField(void* object, const char* name, int access,
                         const void* input) {
    (void)access;
    return storeManagedAnonField(static_cast<::hx::Anon_obj*>(object),
                                 internField(name), name,
                                 *static_cast<const Dynamic*>(input));
}

[[nodiscard]] bool removeManagedAnonField(::hx::Anon_obj* self,
                                          const FieldAtom* field) {
    auto* state = managedAnonState(self);
    auto* entries = managedAnonEntries(self);
    for (std::uint32_t index = 0; index < state->capacity; ++index) {
        auto& entry = entries[index];
        if (entry.field.load(std::memory_order_acquire) == field) {
            // SATB deletion barrier runs before the slot becomes invisible to
            // the precise tracer.
            entry.value = nullptr;
            entry.field.store(nullptr, std::memory_order_release);
            return true;
        }
    }
    auto& runtime = ::hx::gc::Runtime::instance();
    auto* overflow = static_cast<::hx::Anon_obj*>(
        runtime.loadBarrier(state->overflow));
    if (overflow == nullptr)
        return false;
    ::hx::ScopedPin overflowPin(overflow);
    auto* dynamic = static_cast<::hx::Anon_obj*>(overflowPin.get());
    auto* fields = static_cast<FieldState*>(dynamic->state);
    if (fields == nullptr)
        return false;
    std::scoped_lock lock(fields->mutex);
    return fields->fields.erase(field->id) != 0;
}

void finalizeFields(void* object) {
    auto* self = static_cast<CffiObject*>(object);
    delete self->state;
    self->state = nullptr;
}

void finalizeBuffer(void* object) {
    auto* self = static_cast<CffiBuffer*>(object);
    delete self->state;
    self->state = nullptr;
}

void finalizePointer(void* object) {
    auto* self = static_cast<CffiPointer*>(object);
    std::unique_ptr<PointerState> state(self->state);
    if (state == nullptr)
        return;

    ValueCell* tokenCell = nullptr;
    if (state->finalizerToken != nullptr) {
        tokenCell = asCell(state->finalizerToken);
        if (tokenCell != nullptr) {
            auto& runtime = ::hx::gc::Runtime::instance();
            if (tokenCell->strong.valid())
                runtime.releaseHandle(tokenCell->strong);
            // Weak handles are cleared once their referent is dead.  The
            // finalizer object itself is pinned here, so temporarily make the
            // original token strongly resolvable while the native callback
            // reads val_data(token) and removes any native identity cache.
            tokenCell->strong = runtime.createHandle(
                self, ::hx::gc::HandleKind::Strong);
            runtime.storeBarrier(nullptr, tokenCell->value.mPtr, self);
        }
    }

    std::exception_ptr failure;
    if (state->finalizer != nullptr) {
        try {
            ::hx::cffi::CallScope scope;
            if (state->finalizerToken != nullptr) {
                state->finalizer(state->finalizerToken);
            } else {
                value proxy = alloc_abstract(state->kind, state->data);
                state->finalizer(proxy);
            }
        } catch (...) {
            failure = std::current_exception();
        }
    }

    self->state = nullptr;
    if (state->ownsData)
        delete[] static_cast<std::byte*>(state->data);
    if (tokenCell != nullptr)
        destroyCell(tokenCell);
    if (failure != nullptr)
        std::rethrow_exception(failure);
}

void traceFinalizerAttachment(void* object, std::size_t,
                              ::hx::gc::TraceVisitor& visitor) {
    visitor.visit(static_cast<CffiFinalizerAttachment*>(object)->target);
}

void finalizeFinalizerAttachment(void* object) {
    auto* self = static_cast<CffiFinalizerAttachment*>(object);
    std::unique_ptr<ExternalFinalizerState> state(self->state);
    self->state = nullptr;
    if (state == nullptr)
        return;

    {
        std::scoped_lock lock(externalFinalizersMutex);
        auto found = externalFinalizers.find(state->identity);
        if (found != externalFinalizers.end() && found->second == state.get())
            externalFinalizers.erase(found);
    }

    if (state->association.valid())
        ::hx::gc::Runtime::instance().releaseEphemeron(state->association);

    std::exception_ptr failure;
    if (state->callback != nullptr) {
        try {
            ::hx::cffi::CallScope scope;
            state->callback(state->token);
        } catch (...) {
            failure = std::current_exception();
        }
    }
    destroyCell(asCell(state->token));
    if (failure != nullptr)
        std::rethrow_exception(failure);
}

constexpr ::hx::gc::TypeDescriptor ObjectDescriptor{
    0x434646494f424a31ULL, "CFFI.Object", nullptr,
    sizeof(CffiObject), alignof(CffiObject), 0,
    ::hx::gc::TypeContainsReferences | ::hx::gc::TypeFinalizable |
        ::hx::gc::TypeDynamicObject,
    0, nullptr, &traceFields, &finalizeFields,
    &getObjectField, &setObjectField,
    nullptr, nullptr, 0, nullptr, &enumerateObjectFields};

const ::hx::gc::TypeDescriptor DynamicAnonDescriptor{
    0x414e4f4e44594e31ULL, "Anon.Dynamic", nullptr,
    sizeof(::hx::Anon_obj), alignof(::hx::Anon_obj), 0,
    ::hx::gc::TypeContainsReferences | ::hx::gc::TypeFinalizable |
        ::hx::gc::TypeDynamicObject,
    0, nullptr, &traceDynamicAnon, &finalizeDynamicAnon,
    &getDynamicAnonField, &setDynamicAnonField,
    nullptr, nullptr, 0, nullptr, &enumerateDynamicAnonFields};

[[nodiscard]] ::hx::ObjectPtr<::hx::Anon_obj> createDynamicAnon() {
    auto fields = std::make_unique<FieldState>();
    auto result = ::hx::allocate<::hx::Anon_obj>(DynamicAnonDescriptor,
                                                 fields.get());
    fields.release();
    return result;
}

constexpr ::hx::gc::TypeDescriptor BufferDescriptor{
    0x4346464942554631ULL, "CFFI.Buffer", nullptr,
    sizeof(CffiBuffer), alignof(CffiBuffer), 0,
    ::hx::gc::TypeFinalizable | ::hx::gc::TypeByteArray,
    0, nullptr, nullptr, &finalizeBuffer};

constexpr ::hx::gc::TypeDescriptor PointerDescriptor{
    0x4346464950545231ULL, "CFFI.Pointer", nullptr,
    sizeof(CffiPointer), alignof(CffiPointer), 0,
    ::hx::gc::TypeFinalizable, 0, nullptr, nullptr, &finalizePointer};

constexpr ::hx::gc::TypeDescriptor FinalizerAttachmentDescriptor{
    0x4346464946494e31ULL, "CFFI.FinalizerAttachment", nullptr,
    sizeof(CffiFinalizerAttachment), alignof(CffiFinalizerAttachment), 0,
    ::hx::gc::TypeContainsReferences | ::hx::gc::TypeFinalizable,
    0, nullptr, &traceFinalizerAttachment, &finalizeFinalizerAttachment};

constexpr ::hx::gc::TypeDescriptor PrimeClosureDescriptor{
    0x4346464950524d31ULL, "CFFI.PrimeClosure", nullptr,
    sizeof(PrimeClosure), alignof(PrimeClosure), 0,
    ::hx::gc::TypeNone, 0, nullptr, nullptr, nullptr};

[[nodiscard]] bool isImmediate(value input, std::uintptr_t tag) noexcept {
    return (reinterpret_cast<std::uintptr_t>(input) & ImmediateMask) == tag;
}

[[nodiscard]] ValueCell* asCell(value input) {
    if (input == nullptr ||
        (reinterpret_cast<std::uintptr_t>(input) & ImmediateMask) != 0)
        return nullptr;
    auto* result = reinterpret_cast<ValueCell*>(input);
    if (result->magic != CellMagic)
        throw std::runtime_error("invalid hxcpp-zgc CFFI value token");
    return result;
}

void initializeCellHandle(ValueCell& cell) {
    void* managed = cell.value.referencePointer();
    if (managed != nullptr) {
        cell.strong = ::hx::gc::Runtime::instance().createHandle(
            managed, ::hx::gc::HandleKind::Strong);
    }
}

[[nodiscard]] value createCell(const Dynamic& input, bool persistent) {
    if (!persistent)
        ensureExternalPrimeCallState();
    auto* cell = new ValueCell;
    cell->value = input;
    cell->persistent = persistent;
    try {
        initializeCellHandle(*cell);
        if (!persistent && currentCall != nullptr)
            currentCall->cells.push_back(cell);
        else if (!persistent)
            cell->persistent = true;
    } catch (...) {
        delete cell;
        throw;
    }
    if (!persistent && currentCall == nullptr)
        cell->persistent = true;
    return reinterpret_cast<value>(cell);
}

void destroyCell(ValueCell* cell) {
    if (cell == nullptr)
        return;
    if (cell->strong.valid())
        ::hx::gc::Runtime::instance().releaseHandle(cell->strong);
    cell->magic = 0;
    delete cell;
}

[[nodiscard]] value clonePersistent(value input) {
    if (input == nullptr || isImmediate(input, IntegerTag) ||
        isImmediate(input, BooleanTag))
        return input;
    return createCell(::hx::cffi::toDynamic(input), true);
}

void releasePersistent(value input) {
    if (input == nullptr || isImmediate(input, IntegerTag) ||
        isImmediate(input, BooleanTag))
        return;
    ValueCell* cell = asCell(input);
    if (cell != nullptr && cell->persistent)
        destroyCell(cell);
}

[[nodiscard]] Dynamic resolveCell(ValueCell& cell) {
    if (cell.strong.valid()) {
        void* object = ::hx::gc::Runtime::instance().resolveHandle(cell.strong);
        if (object == nullptr)
            throw std::runtime_error("CFFI strong handle unexpectedly cleared");
        ::hx::gc::Runtime::instance().refreshUntrackedReference(
            cell.value.mPtr, object);
    }
    return cell.value;
}

[[nodiscard]] void* pinForCallAddress(value token, const Dynamic& input) {
    void* managed = input.managedPointer();
    ValueCell* cell = asCell(token);
    const ::hx::gc::Handle* borrowedStrong =
        cell != nullptr && cell->strong.valid() ? &cell->strong : nullptr;
    return pinManagedAddressForCall(managed, borrowedStrong);
}

[[nodiscard]] CffiBuffer* cffiBuffer(value input) {
    Dynamic dynamic = ::hx::cffi::toDynamic(input);
    // A value token only keeps the object alive.  It does not prevent the
    // concurrent relocator from moving it after managedPointer() returns.
    // Native CFFI helpers keep raw object addresses across several loads, so
    // retain a pinned handle for the enclosing CallScope before exposing the
    // address.
    void* object = pinForCallAddress(input, dynamic);
    if (object == nullptr ||
        ::hx::gc::Runtime::instance().typeOf(object) != &BufferDescriptor)
        throw std::runtime_error("CFFI value is not a buffer");
    return static_cast<CffiBuffer*>(object);
}

[[nodiscard]] Array_obj<unsigned char>* cffiByteArray(value input) {
    Dynamic dynamic = ::hx::cffi::toDynamic(input);
    void* object = dynamic.managedPointer();
    if (object == nullptr ||
        ::hx::gc::Runtime::instance().typeOf(object) !=
            &Array_obj<unsigned char>::descriptor)
        return nullptr;

    auto* array = static_cast<Array_obj<unsigned char>*>(
        pinForCallAddress(input, dynamic));
    auto& runtime = ::hx::gc::Runtime::instance();
    void* storage = runtime.loadBarrier(array->buffer);
    (void)pinManagedAddressForCall(storage);
    return array;
}

[[nodiscard]] bool isCffiByteBuffer(value input) {
    if (input == nullptr || isImmediate(input, IntegerTag) ||
        isImmediate(input, BooleanTag))
        return false;
    Dynamic dynamic = ::hx::cffi::toDynamic(input);
    void* object = dynamic.managedPointer();
    if (object == nullptr)
        return false;
    const auto* type = ::hx::gc::Runtime::instance().typeOf(object);
    return type == &BufferDescriptor ||
           type == &Array_obj<unsigned char>::descriptor;
}

[[nodiscard]] CffiPointer* cffiPointer(value input) {
    Dynamic dynamic = ::hx::cffi::toDynamic(input);
    // CffiPointer::state is native memory and is read after this helper
    // returns.  Pin the managed wrapper for the whole external call; merely
    // resolving its colored reference leaves a relocation race between this
    // return and callers such as val_type/val_data.
    void* object = pinForCallAddress(input, dynamic);
    if (object == nullptr ||
        ::hx::gc::Runtime::instance().typeOf(object) != &PointerDescriptor)
        return nullptr;
    return static_cast<CffiPointer*>(object);
}

struct ErasedCffiArray final {
    void* object = nullptr;
    const ::hx::gc::TypeDescriptor* type = nullptr;
    int length = 0;
};

[[nodiscard]] ErasedCffiArray cffiArray(value input) {
    Dynamic dynamic = ::hx::cffi::toDynamic(input);
    if (dynamic.tag() != Dynamic::Tag::Object)
        throw std::runtime_error("CFFI value is not an array");
    void* object = dynamic.managedPointer();
    const auto* type = object == nullptr ? nullptr :
        ::hx::gc::Runtime::instance().typeOf(object);
    if (type == nullptr || (type->flags & ::hx::gc::TypeArray) == 0 ||
        type->dynamicIndexGet == nullptr || type->dynamicIndexSet == nullptr)
        throw std::runtime_error("CFFI value is not an array");

    object = pinForCallAddress(input, dynamic);
    type = ::hx::gc::Runtime::instance().typeOf(object);
    Dynamic length;
    if (type->dynamicFieldGet == nullptr ||
        !type->dynamicFieldGet(object, "length", ::hx::paccDynamic,
                               &length))
        throw std::runtime_error("CFFI array has no length field");
    return {object, type, length.Cast<int>()};
}

void appendUtf8(std::string& output, std::uint32_t codePoint) {
    if (codePoint <= 0x7fU) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else if (codePoint <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
        output.push_back(static_cast<char>(0x80U |
                                           ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (codePoint >> 18U)));
        output.push_back(static_cast<char>(0x80U |
                                           ((codePoint >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U |
                                           ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    }
}

[[nodiscard]] std::string wideToUtf8(std::wstring_view input) {
    std::string result;
    for (std::size_t index = 0; index < input.size(); ++index) {
        std::uint32_t codePoint = static_cast<std::uint32_t>(input[index]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (codePoint >= 0xd800U && codePoint <= 0xdbffU) {
                const std::uint32_t low = index + 1U < input.size() ?
                    static_cast<std::uint32_t>(input[index + 1U]) : 0U;
                if (low >= 0xdc00U && low <= 0xdfffU) {
                    codePoint = 0x10000U + ((codePoint - 0xd800U) << 10U) +
                                (low - 0xdc00U);
                    ++index;
                } else {
                    codePoint = 0xfffdU;
                }
            } else if (codePoint >= 0xdc00U && codePoint <= 0xdfffU) {
                codePoint = 0xfffdU;
            }
        }
        if (codePoint > 0x10ffffU)
            codePoint = 0xfffdU;
        appendUtf8(result, codePoint);
    }
    return result;
}

[[nodiscard]] std::string utf16ToUtf8(std::u16string_view input) {
    std::string result;
    for (std::size_t index = 0; index < input.size(); ++index) {
        std::uint32_t codePoint = input[index];
        if (codePoint >= 0xd800U && codePoint <= 0xdbffU) {
            const std::uint32_t low = index + 1U < input.size() ?
                input[index + 1U] : 0U;
            if (low >= 0xdc00U && low <= 0xdfffU) {
                codePoint = 0x10000U + ((codePoint - 0xd800U) << 10U) +
                            (low - 0xdc00U);
                ++index;
            } else {
                codePoint = 0xfffdU;
            }
        } else if (codePoint >= 0xdc00U && codePoint <= 0xdfffU) {
            codePoint = 0xfffdU;
        }
        appendUtf8(result, codePoint);
    }
    return result;
}

[[nodiscard]] std::wstring utf8ToWide(std::string_view input) {
    std::wstring result;
    for (std::size_t index = 0; index < input.size();) {
        const auto lead = static_cast<unsigned char>(input[index++]);
        std::uint32_t codePoint = 0xfffdU;
        int continuation = 0;
        if (lead < 0x80U) {
            codePoint = lead;
        } else if ((lead & 0xe0U) == 0xc0U) {
            codePoint = lead & 0x1fU;
            continuation = 1;
        } else if ((lead & 0xf0U) == 0xe0U) {
            codePoint = lead & 0x0fU;
            continuation = 2;
        } else if ((lead & 0xf8U) == 0xf0U) {
            codePoint = lead & 0x07U;
            continuation = 3;
        }
        bool valid = continuation != 0 || lead < 0x80U;
        for (int part = 0; part < continuation; ++part) {
            if (index >= input.size()) {
                valid = false;
                break;
            }
            const auto byte = static_cast<unsigned char>(input[index++]);
            if ((byte & 0xc0U) != 0x80U) {
                valid = false;
                break;
            }
            codePoint = (codePoint << 6U) | (byte & 0x3fU);
        }
        if (!valid || codePoint > 0x10ffffU)
            codePoint = 0xfffdU;

        if constexpr (sizeof(wchar_t) == 2) {
            if (codePoint > 0xffffU) {
                codePoint -= 0x10000U;
                result.push_back(static_cast<wchar_t>(
                    0xd800U + (codePoint >> 10U)));
                result.push_back(static_cast<wchar_t>(
                    0xdc00U + (codePoint & 0x3ffU)));
            } else {
                result.push_back(static_cast<wchar_t>(codePoint));
            }
        } else {
            result.push_back(static_cast<wchar_t>(codePoint));
        }
    }
    return result;
}

[[nodiscard]] std::u16string utf8ToUtf16(std::string_view input) {
    const std::wstring wide = utf8ToWide(input);
    std::u16string result;
    result.reserve(wide.size());
    if constexpr (sizeof(wchar_t) == 2) {
        for (wchar_t codeUnit : wide)
            result.push_back(static_cast<char16_t>(codeUnit));
    } else {
        for (wchar_t wideCodePoint : wide) {
            std::uint32_t codePoint =
                static_cast<std::uint32_t>(wideCodePoint);
            if (codePoint > 0xffffU) {
                codePoint -= 0x10000U;
                result.push_back(static_cast<char16_t>(
                    0xd800U + (codePoint >> 10U)));
                result.push_back(static_cast<char16_t>(
                    0xdc00U + (codePoint & 0x3ffU)));
            } else {
                result.push_back(static_cast<char16_t>(codePoint));
            }
        }
    }
    return result;
}

struct LegacyStringAlloc {
    virtual void* allocBytes(std::size_t bytes) = 0;

protected:
    ~LegacyStringAlloc() = default;
};

template<class Character>
[[nodiscard]] const Character* copyLegacyString(
        std::basic_string_view<Character> input, void* allocator) {
    const std::size_t bytes = (input.size() + 1U) * sizeof(Character);
    void* storage = allocator == nullptr ? allocateCallBytes(bytes) :
        static_cast<LegacyStringAlloc*>(allocator)->allocBytes(bytes);
    auto* output = static_cast<Character*>(storage);
    if (!input.empty())
        std::memcpy(output, input.data(), input.size() * sizeof(Character));
    output[input.size()] = Character{};
    return output;
}

using LegacyHxString = ::hx::cffi::LegacyPrimeString;

[[nodiscard]] LegacyHxString legacyAllocHxsUtf8(const char* input,
                                                 int length) {
    if (input == nullptr)
        return {};
    if (length < 0)
        length = static_cast<int>(std::strlen(input));
    return {length, alloc_string_data(input, length)};
}

[[nodiscard]] LegacyHxString legacyAllocHxsWchar(const wchar_t* input,
                                                  int length) {
    if (input == nullptr)
        return {};
    if (length < 0)
        length = static_cast<int>(std::wcslen(input));
    const std::string utf8 = wideToUtf8(
        std::wstring_view(input, static_cast<std::size_t>(length)));
    return legacyAllocHxsUtf8(utf8.data(), static_cast<int>(utf8.size()));
}

[[nodiscard]] LegacyHxString legacyAllocHxsUtf16(const char16_t* input,
                                                  int length) {
    if (input == nullptr)
        return {};
    if (length < 0) {
        length = 0;
        while (input[length] != 0)
            ++length;
    }
    const std::string utf8 = utf16ToUtf8(
        std::u16string_view(input, static_cast<std::size_t>(length)));
    return legacyAllocHxsUtf8(utf8.data(), static_cast<int>(utf8.size()));
}

[[nodiscard]] const char* legacyHxsUtf8(const LegacyHxString& input,
                                         void*) noexcept {
    return input.data == nullptr ? "" : input.data;
}

[[nodiscard]] const wchar_t* legacyHxsWchar(const LegacyHxString& input,
                                             void* allocator) {
    const std::string_view source = input.data == nullptr ?
        std::string_view{} :
        std::string_view(input.data, static_cast<std::size_t>(
            std::max(input.length, 0)));
    const std::wstring converted = utf8ToWide(source);
    return copyLegacyString<wchar_t>(converted, allocator);
}

[[nodiscard]] const char16_t* legacyHxsUtf16(const LegacyHxString& input,
                                              void* allocator) {
    const std::string_view source = input.data == nullptr ?
        std::string_view{} :
        std::string_view(input.data, static_cast<std::size_t>(
            std::max(input.length, 0)));
    const std::u16string converted = utf8ToUtf16(source);
    return copyLegacyString<char16_t>(converted, allocator);
}

[[nodiscard]] int legacyHxsEncoding(const LegacyHxString&) noexcept {
    return 1; // hx::StringUtf8 in the external hxcpp CFFI ABI.
}

struct KindRecord final {
    KindRecord(std::string input, int inputType)
        : name(std::move(input)), type(inputType) {}
    std::string name;
    int type = 0x100;
};

std::mutex kindsMutex;
std::vector<std::unique_ptr<KindRecord>> kinds;
std::atomic<int> nextKindType{0x100};

[[nodiscard]] int kindType(vkind kind) {
    if (kind == nullptr)
        return 0;

    // Native modules built against established hxcpp CFFI encode vkind as
    // the integer returned by alloc_kind (for example 0x102), cast through
    // the pointer-shaped ABI type.  Kinds created by this runtime are stable
    // KindRecord addresses.  Resolve our records by identity first and only
    // then accept the bounded legacy integer encoding; never dereference an
    // unrecognized native-supplied address.
    {
        std::scoped_lock lock(kindsMutex);
        for (const auto& record : kinds) {
            if (record.get() == kind)
                return record->type;
        }
    }

    const auto encoded = reinterpret_cast<std::uintptr_t>(kind);
    if (encoded <= static_cast<std::uintptr_t>(INT_MAX))
        return static_cast<int>(encoded);
    throw std::runtime_error("unknown CFFI abstract kind encoding");
}

// DEFINE_PRIME registrars run during cross-translation-unit static
// initialization. Function-local storage avoids the platform-dependent
// initialization-order race where a registrar reaches an unconstructed
// unordered_map (observed as SIGFPE in libstdc++'s bucket modulo path).
[[nodiscard]] std::mutex& primeRegistryMutex() {
    static std::mutex mutex;
    return mutex;
}

[[nodiscard]] std::unordered_map<std::string,
    ::hx::cffi::PrimeInvoker>& primeRegistry() {
    static std::unordered_map<std::string, ::hx::cffi::PrimeInvoker> registry;
    return registry;
}
std::mutex externalPrimesMutex;
std::unordered_set<void*> externalPrimes;

[[nodiscard]] Dynamic createPrimeClosure(::hx::cffi::PrimeInvoker invoker,
                                         int arity) {
    auto& runtime = ::hx::gc::Runtime::instance();
    void* memory = runtime.allocate(PrimeClosureDescriptor);
    auto* closure = ::new (memory) PrimeClosure(invoker, arity);
    runtime.publish(closure);
    return Dynamic(static_cast<::hx::Object*>(closure));
}

// Native rendering/audio code can cross the Prime boundary thousands of
// times per frame.  A CallState is only a dynamic-scope ledger; allocating it
// (and the outer CallScope wrapper) on every GL call made CFFI itself the main
// thread bottleneck.  Reuse one ledger per nesting level and retain vector
// capacities between calls.  The exact-GC contract is unchanged: every token,
// pinned handle and native temporary is still released when its dynamic call
// scope exits.
struct ThreadCallStatePool final {
    std::array<CallState, MaximumCallScopeDepth> states;
    std::size_t depth = 0;
};

thread_local ThreadCallStatePool callStatePool;

struct ThreadExternalPrimeFramePool final {
    std::array<ExternalPrimeFrame, MaximumCallScopeDepth> frames;
    std::size_t depth = 0;
};

thread_local ThreadExternalPrimeFramePool externalPrimeFramePool;

[[nodiscard]] CallState* enterCallState() {
    if (callStatePool.depth == MaximumCallScopeDepth)
        throw std::runtime_error("CFFI call-scope nesting limit exceeded");
    CallState* state = &callStatePool.states[callStatePool.depth++];
    if (!state->cells.empty() || !state->pins.empty() ||
        !state->scopedPins.empty() ||
        !state->nativeBlocks.empty() || !state->arrayValueProxies.empty() ||
        state->previous != nullptr) {
        --callStatePool.depth;
        throw std::logic_error("CFFI call-state pool entry was not cleared");
    }
    state->previous = currentCall;
    currentCall = state;
    return state;
}

void ensureExternalPrimeCallState() {
    ExternalPrimeFrame* frame = currentExternalPrimeFrame;
    if (frame != nullptr && frame->callState == nullptr)
        frame->callState = enterCallState();
}

void leaveCallState(CallState* state) {
    if (state == nullptr)
        return;
    if (currentCall != state || callStatePool.depth == 0 ||
        &callStatePool.states[callStatePool.depth - 1U] != state) {
        throw std::logic_error("CFFI call scopes must exit in LIFO order");
    }

    std::exception_ptr deferredFailure;
    try {
        // Scalar-only Prime calls (the overwhelmingly common GL path) do not
        // create temporary ValueCells.  Avoid taking the process-wide root
        // registry lock for those calls: there cannot be a scoped cell for a
        // persistent root to retain.
        if (!state->cells.empty()) {
            std::scoped_lock lock(rootsMutex);
            for (RootSlot* root : rootSlots) {
                for (ValueCell* cell : state->cells) {
                    if (cell->persistent)
                        continue;
                    if (root->current == reinterpret_cast<value>(cell)) {
                        root->current = clonePersistent(root->current);
                        break;
                    }
                }
            }
        }

        for (const auto& proxy : state->arrayValueProxies) {
            for (std::size_t index = 0; index < proxy->values.size(); ++index)
                val_array_set_i(proxy->array, static_cast<int>(index),
                                proxy->values[index]);
        }
    } catch (...) {
        // A writeback/conversion failure must not strand the thread-local
        // scope as current or leak exact roots and pin handles.  Complete the
        // teardown first, then report the original failure to the caller.
        deferredFailure = std::current_exception();
    }

    // Scoped pins are acquired in call order, so unwind them in reverse before
    // destroying temporary cells.
    for (auto iterator = state->scopedPins.rbegin();
         iterator != state->scopedPins.rend(); ++iterator) {
        iterator->reset();
    }
    for (auto iterator = state->pins.rbegin();
         iterator != state->pins.rend(); ++iterator) {
        ::hx::gc::Runtime::instance().releaseHandle(*iterator);
    }
    for (ValueCell* cell : state->cells) {
        if (!cell->persistent)
            destroyCell(cell);
    }

    currentCall = state->previous;
    state->previous = nullptr;
    // clear() destroys native byte blocks and logical entries but deliberately
    // retains all vector storage for the next call at this nesting level.
    state->cells.clear();
    state->pins.clear();
    state->scopedPins.clear();
    state->nativeBlocks.clear();
    state->arrayValueProxies.clear();
    --callStatePool.depth;
    if (deferredFailure)
        std::rethrow_exception(deferredFailure);
}

} // namespace

namespace hx {

const ::hx::gc::TypeDescriptor Anon_obj::descriptor{
    0x414e4f4e4f424a31ULL, "Anon", nullptr,
    sizeof(::hx::Anon_obj), alignof(::hx::Anon_obj), 0,
    ::hx::gc::TypeContainsReferences | ::hx::gc::TypeVariableSize |
        ::hx::gc::TypeDynamicObject,
    0, nullptr, &traceManagedAnon, nullptr,
    &getManagedAnonField, &setManagedAnonField,
    nullptr, nullptr, 0, nullptr, &enumerateManagedAnonFields};

::hx::ObjectPtr<::hx::Anon_obj> Anon_obj::Create(int capacity) {
    if (capacity < 0)
        throw std::length_error("negative anonymous object capacity");
    if (capacity == 0)
        return createDynamicAnon();

    const std::size_t payloadBytes = managedAnonPayloadBytes(
        static_cast<std::size_t>(capacity));
    auto& runtime = ::hx::gc::Runtime::instance();
    void* memory = runtime.allocateVariable(descriptor, payloadBytes);
    auto* object = ::new (memory) ::hx::Anon_obj(nullptr);
    auto* state = ::new (managedAnonState(object)) ManagedAnonState();
    state->capacity = static_cast<std::uint32_t>(capacity);
    auto* entries = managedAnonEntries(object);
    for (int index = 0; index < capacity; ++index)
        ::new (entries + index) ManagedAnonEntry();
    ::hx::gc::detail::publishEmpty(object);
    return ::hx::ObjectPtr<::hx::Anon_obj>(object);
}

::hx::ObjectPtr<::hx::Anon_obj> Anon_obj::setFixed(
    int index, const String& name, const Dynamic& value) {
    // The caller may have supplied temporaries.  Pinning `this` can itself
    // rendezvous with the collector, and dynamic-anon insertion may grow its
    // side table, so copy the managed arguments into precise roots first.
    ::hx::RootedValue<::String> rootedName(name);
    ::hx::RootedValue<::Dynamic> rootedValue(value);
    ::hx::ScopedPin pin(this);
    auto* self = static_cast<::hx::Anon_obj*>(pin.get());
    const FieldAtom* field = internField(rootedName.get().view());
    const auto* type = ::hx::gc::Runtime::instance().typeOf(self);
    if (type == &DynamicAnonDescriptor) {
        auto* fields = static_cast<FieldState*>(self->state);
        if (fields == nullptr)
            throw std::runtime_error("anonymous object has no field state");
        std::scoped_lock lock(fields->mutex);
        storeOwnedDynamic(self, fields->fields[field->id], rootedValue.get());
    } else if (type == &descriptor) {
        if (!storeManagedAnonField(self, field, field->name.c_str(),
                                   rootedValue.get(), index))
            throw std::runtime_error("failed to store anonymous object field");
    } else {
        throw std::runtime_error("anonymous object has an invalid descriptor");
    }
    return ::hx::ObjectPtr<::hx::Anon_obj>(self);
}

::hx::ObjectPtr<::hx::Anon_obj> SourceInfo(
        const ::String& fileName, int lineNumber,
        const ::String& className, const ::String& methodName) {
    ::hx::RootedValue<::String> rootedFile(fileName);
    ::hx::RootedValue<::String> rootedClass(className);
    ::hx::RootedValue<::String> rootedMethod(methodName);
    auto result = ::hx::Anon_obj::Create(4);
    ::hx::gc::RefSlot* roots[]{&result.mPtr};
    ::hx::gc::RootScope rootScope(roots, 1);
    result = result->setFixed(0, ::String("fileName"), rootedFile.get());
    result = result->setFixed(1, ::String("lineNumber"), lineNumber);
    result = result->setFixed(2, ::String("className"), rootedClass.get());
    result = result->setFixed(3, ::String("methodName"), rootedMethod.get());
    return result;
}

} // namespace hx

namespace hx::cffi {

ExternalPrimeCallScope::ExternalPrimeCallScope() {
    if (externalPrimeFramePool.depth == MaximumCallScopeDepth)
        throw std::runtime_error(
            "external Prime call-scope nesting limit exceeded");
    ExternalPrimeFrame* frame =
        &externalPrimeFramePool.frames[externalPrimeFramePool.depth++];
    if (frame->callState != nullptr || frame->previous != nullptr ||
        frame->nativeBlocking) {
        --externalPrimeFramePool.depth;
        throw std::logic_error(
            "external Prime frame-pool entry was not cleared");
    }
    frame->previous = currentExternalPrimeFrame;
    currentExternalPrimeFrame = frame;
    state_ = frame;
}

void ExternalPrimeCallScope::enterNativeBlocking() {
    auto* frame = static_cast<ExternalPrimeFrame*>(state_);
    if (frame == nullptr || frame->nativeBlocking)
        return;
    auto& runtime = ::hx::gc::Runtime::instance();
    if (!runtime.currentThreadAttached())
        return;
    if (cffiNativeBlockingDepth ==
        std::numeric_limits<std::size_t>::max())
        throw std::overflow_error("CFFI native blocking depth overflow");
    // Prime functions execute in third-party modules and can wait inside
    // audio/graphics drivers or I/O without reaching a generated Haxe
    // safepoint. All managed ABI arguments are exact handles/scoped pins by
    // this point; val_call* temporarily restores mutator state for callbacks.
    runtime.enterNativeBlocking();
    ++cffiNativeBlockingDepth;
    frame->nativeBlocking = true;
}

void ExternalPrimeCallScope::leaveNativeBlocking() {
    auto* frame = static_cast<ExternalPrimeFrame*>(state_);
    if (frame == nullptr || !frame->nativeBlocking)
        return;
    if (cffiNativeBlockingDepth == 0)
        throw std::logic_error(
            "external Prime native section was unbalanced");
    frame->nativeBlocking = false;
    --cffiNativeBlockingDepth;
    ::hx::gc::Runtime::instance().leaveNativeBlocking();
}

ExternalPrimeCallScope::~ExternalPrimeCallScope() noexcept(false) {
    auto* frame = static_cast<ExternalPrimeFrame*>(state_);
    std::exception_ptr deferredFailure;
    try {
        leaveNativeBlocking();
    } catch (...) {
        deferredFailure = std::current_exception();
    }
    try {
        if (frame != nullptr)
            leaveCallState(frame->callState);
    } catch (...) {
        if (deferredFailure == nullptr)
            deferredFailure = std::current_exception();
    }
    try {
        if (frame == nullptr || currentExternalPrimeFrame != frame ||
            externalPrimeFramePool.depth == 0 ||
            &externalPrimeFramePool.frames[
                externalPrimeFramePool.depth - 1U] != frame) {
            throw std::logic_error(
                "external Prime call scopes must exit in LIFO order");
        }
        currentExternalPrimeFrame = frame->previous;
        frame->callState = nullptr;
        frame->previous = nullptr;
        frame->nativeBlocking = false;
        --externalPrimeFramePool.depth;
    } catch (...) {
        if (deferredFailure == nullptr)
            deferredFailure = std::current_exception();
    }
    state_ = nullptr;
    if (deferredFailure != nullptr)
        std::rethrow_exception(deferredFailure);
}

void registerExternalPrime(void* address) {
    if (address == nullptr)
        throw std::invalid_argument("cannot register a null external Prime");
    std::scoped_lock lock(externalPrimesMutex);
    externalPrimes.insert(address);
}

void clearExternalPrimes() {
    std::scoped_lock lock(externalPrimesMutex);
    externalPrimes.clear();
}

bool isExternalPrime(void* address) {
    if (address == nullptr)
        return false;
    std::scoped_lock lock(externalPrimesMutex);
    return externalPrimes.contains(address);
}

void* primeDynamicArgument(const Dynamic& input) {
    return reinterpret_cast<void*>(fromDynamic(input));
}

void* primeObjectArgument(::hx::Object* input) {
    if (input == nullptr)
        return nullptr;
    return reinterpret_cast<void*>(fromDynamic(Dynamic(input)));
}

LegacyPrimeString primeStringArgument(const String& input) {
    if (input.isNull())
        return {};
    value token = fromDynamic(Dynamic(input));
    // External Prime modules see the compatibility HxString as UTF-8 (see
    // legacyHxsEncoding).  Its length therefore has to describe the byte
    // buffer, not Haxe's UTF-16 code-unit length.  Mixing the latter with an
    // UTF-8 pointer truncates every non-ASCII string at native boundaries;
    // for example four CJK characters became one complete glyph plus the
    // first byte of the next glyph in HarfBuzz.
    const std::size_t byteLength = input.view().size();
    if (byteLength > static_cast<std::size_t>(INT_MAX))
        throw std::length_error("external Prime String exceeds ABI length");
    return LegacyPrimeString{static_cast<int>(byteLength),
                             val_string(token)};
}

Dynamic primeDynamicResult(void* input) {
    return toDynamic(reinterpret_cast<value>(input));
}

::hx::Object* primeObjectResult(void* input) {
    Dynamic dynamic = toDynamic(reinterpret_cast<value>(input));
    if (dynamic.isNull())
        return nullptr;
    if (dynamic.tag() != Dynamic::Tag::Object)
        throw std::runtime_error(
            "external Prime returned a non-object for a managed pointer");
    auto* object = static_cast<::hx::Object*>(dynamic.managedPointer());
    // Raw managed-pointer Prime signatures are retained only as a defensive
    // compatibility path.  New hxcpp-zgc generation maps cpp.Object to
    // Dynamic, which preserves the token root and all scalar/string tags.
    // Pin here so an explicitly raw result cannot race concurrent relocation
    // between this return and the caller wrapping it.
    if (object != nullptr) {
        object = static_cast<::hx::Object*>(
            ::hx::gc::Runtime::instance().pinForNative(object));
    }
    return object;
}

String primeStringResult(LegacyPrimeString input) {
    if (input.data == nullptr)
        return nullptr;
    if (input.length < 0)
        throw std::runtime_error("external Prime returned a negative String length");
    return String(input.data, input.length);
}

void registerPrime(const char* name, PrimeInvoker invoker) {
    if (name == nullptr || invoker == nullptr)
        throw std::invalid_argument("invalid Prime registration");
    std::scoped_lock lock(primeRegistryMutex());
    primeRegistry()[std::string(name)] = invoker;
}

CallScope::CallScope() {
    state_ = enterCallState();
}

CallScope::~CallScope() noexcept(false) {
    leaveCallState(static_cast<CallState*>(state_));
    state_ = nullptr;
}

value fromDynamic(const Dynamic& input) {
    if (input.isMemberFunction())
        return createCell(input, false);
    switch (input.tag()) {
        case Dynamic::Tag::Null:
            return nullptr;
        case Dynamic::Tag::Object:
            if (input.managedPointer() == nullptr)
                return nullptr;
            return createCell(input, false);
        case Dynamic::Tag::Integer: {
            const auto integer = static_cast<std::intptr_t>(input.Cast<int>());
            return reinterpret_cast<value>(
                (static_cast<std::uintptr_t>(integer) << 3U) | IntegerTag);
        }
        case Dynamic::Tag::Boolean:
            return reinterpret_cast<value>(
                (static_cast<std::uintptr_t>(input.Cast<bool>()) << 3U) |
                BooleanTag);
        default:
            return createCell(input, false);
    }
}

Dynamic toDynamic(value input) {
    if (input == nullptr)
        return Dynamic();
    if (isImmediate(input, IntegerTag)) {
        const auto bits = static_cast<std::intptr_t>(
            reinterpret_cast<std::uintptr_t>(input));
        return Dynamic(static_cast<int>(bits >> 3U));
    }
    if (isImmediate(input, BooleanTag)) {
        return Dynamic((reinterpret_cast<std::uintptr_t>(input) >> 3U) != 0);
    }
    ValueCell* cell = asCell(input);
    if (cell == nullptr)
        throw std::runtime_error("invalid null CFFI cell");
    return resolveCell(*cell);
}

value alloc_pointer(void* pointer) {
    if (pointer == nullptr)
        return nullptr;
    auto* state = new PointerState{
        pointer, nullptr, nullptr, nullptr, false};
    return fromDynamic(Dynamic(::hx::allocate<CffiPointer>(
        PointerDescriptor, state)));
}

vkind createKind(const char* name) {
    auto record = std::make_unique<KindRecord>(
        name == nullptr ? "" : name,
        nextKindType.fetch_add(1, std::memory_order_relaxed));
    vkind result = record.get();
    std::scoped_lock lock(kindsMutex);
    kinds.push_back(std::move(record));
    return result;
}

} // namespace hx::cffi

AutoGCRoot::AutoGCRoot() = default;

AutoGCRoot::AutoGCRoot(value input) : value_(clonePersistent(input)) {}

AutoGCRoot::~AutoGCRoot() { releasePersistent(value_); }

value AutoGCRoot::get() const { return value_; }

void AutoGCRoot::set(value input) {
    value replacement = clonePersistent(input);
    releasePersistent(value_);
    value_ = replacement;
}

value alloc_null() { return nullptr; }
value alloc_int(int input) { return ::hx::cffi::fromDynamic(Dynamic(input)); }
value alloc_float(double input) {
    return ::hx::cffi::fromDynamic(Dynamic(input));
}
value alloc_bool(bool input) { return ::hx::cffi::fromDynamic(Dynamic(input)); }
value alloc_string(const char* input) {
    return input == nullptr ? nullptr :
        alloc_string_len(input, static_cast<int>(std::strlen(input)));
}
value alloc_wstring(const wchar_t* input) {
    if (input == nullptr)
        return nullptr;
    return alloc_wstring_len(input, static_cast<int>(std::wcslen(input)));
}
value alloc_string_len(const char* input, int length) {
    if (input == nullptr)
        return nullptr;
    if (length < 0)
        throw std::length_error("negative CFFI string length");
    return ::hx::cffi::fromDynamic(Dynamic(String(
        std::string_view(input, static_cast<std::size_t>(length)))));
}
value alloc_wstring_len(const wchar_t* input, int length) {
    if (input == nullptr)
        return nullptr;
    if (length < 0)
        throw std::length_error("negative CFFI wide-string length");
    const std::string utf8 = wideToUtf8(
        std::wstring_view(input, static_cast<std::size_t>(length)));
    return alloc_string_len(utf8.data(), static_cast<int>(utf8.size()));
}
value alloc_raw_string(int length) {
    if (length < 0)
        throw std::length_error("negative CFFI string length");
    std::string bytes(static_cast<std::size_t>(length), '\0');
    return ::hx::cffi::fromDynamic(
        Dynamic(String(std::string_view(bytes.data(), bytes.size()))));
}
value alloc_empty_string(int length) { return alloc_raw_string(length); }
value alloc_empty_object() {
    auto* state = new FieldState;
    return ::hx::cffi::fromDynamic(Dynamic(
        ::hx::allocate<CffiObject>(ObjectDescriptor, state)));
}
value alloc_array(int length) {
    return ::hx::cffi::fromDynamic(Dynamic(Array_obj<Dynamic>::__new(length)));
}
value alloc_array_type(int length, hxValueType) { return alloc_array(length); }
value alloc_abstract(vkind kind, void* data) {
    if (data == nullptr)
        return nullptr;
    auto* state = new PointerState{data, kind, nullptr, nullptr, false};
    return ::hx::cffi::fromDynamic(Dynamic(
        ::hx::allocate<CffiPointer>(PointerDescriptor, state)));
}
value create_abstract(vkind kind, int size, ::hx::finalizer finalizer) {
    if (size < 0)
        throw std::length_error("negative CFFI abstract size");
    auto data = std::make_unique<std::byte[]>(
        std::max<std::size_t>(static_cast<std::size_t>(size), 1));
    auto* state = new PointerState{
        data.get(), kind, finalizer, nullptr, true};
    value result = ::hx::cffi::fromDynamic(Dynamic(
        ::hx::allocate<CffiPointer>(PointerDescriptor, state)));
    data.release();
    if (finalizer != nullptr)
        val_gc(result, finalizer);
    return result;
}
void free_abstract(value input) {
    CffiPointer* pointer = cffiPointer(input);
    if (pointer == nullptr || pointer->state == nullptr)
        return;
    PointerState* state = pointer->state;
    if (state->finalizer != nullptr) {
        ::hx::finalizer callback = state->finalizer;
        state->finalizer = nullptr;
        callback(input);
    }
    if (state->ownsData) {
        delete[] static_cast<std::byte*>(state->data);
        state->data = nullptr;
        state->ownsData = false;
    }
}
value alloc_best_int(int input) { return alloc_int(input); }
value alloc_int32(int input) { return alloc_int(input); }
vkind alloc_kind() { return ::hx::cffi::createKind(""); }

int val_type(value input) {
    CffiPointer* pointer = cffiPointer(input);
    if (pointer != nullptr && pointer->state->kind != nullptr)
        return kindType(pointer->state->kind);
    Dynamic dynamic = ::hx::cffi::toDynamic(input);
    if (dynamic.isMemberFunction())
        return 6;
    switch (dynamic.tag()) {
        case Dynamic::Tag::Null: return 0;
        case Dynamic::Tag::Floating: return 1;
        case Dynamic::Tag::Boolean: return 2;
        case Dynamic::Tag::String: return 3;
        case Dynamic::Tag::Integer: return 0xff;
        case Dynamic::Tag::Object: {
            const auto* type = ::hx::gc::Runtime::instance().typeOf(
                dynamic.managedPointer());
            if (type != nullptr &&
                (type->flags & ::hx::gc::TypeFunction) != 0)
                return 6;
            if (type != nullptr && (type->flags & ::hx::gc::TypeEnum) != 0)
                return 7;
            if (type != nullptr && (type->flags & ::hx::gc::TypeArray) != 0)
                return 5;
            return 4;
        }
        case Dynamic::Tag::NativePointer: return 4;
        default: return 6;
    }
}
vkind val_kind(value input) {
    CffiPointer* pointer = cffiPointer(input);
    if (pointer == nullptr || pointer->state->kind == nullptr)
        throw std::runtime_error("CFFI value has no abstract kind");
    return pointer->state->kind;
}
int val_fun_nargs(value input) {
    if (input == nullptr || val_is_int(input) || val_is_bool(input))
        return -2;
    Dynamic dynamic = ::hx::cffi::toDynamic(input);
    if (dynamic.isMemberFunction())
        return dynamic.__ArgCount();
    if (dynamic.tag() != Dynamic::Tag::Object)
        return -2;
    const auto* type = ::hx::gc::Runtime::instance().typeOf(
        dynamic.managedPointer());
    if (type == nullptr || (type->flags & ::hx::gc::TypeFunction) == 0)
        return -2;
    return dynamic.__ArgCount();
}

bool val_is_null(value input) { return input == nullptr; }
bool val_is_int(value input) { return isImmediate(input, IntegerTag); }
bool val_is_float(value input) {
    return input != nullptr && !val_is_int(input) && !val_is_bool(input) &&
           ::hx::cffi::toDynamic(input).tag() == Dynamic::Tag::Floating;
}
bool val_is_bool(value input) { return isImmediate(input, BooleanTag); }
bool val_is_string(value input) {
    return input != nullptr && !val_is_int(input) && !val_is_bool(input) &&
           ::hx::cffi::toDynamic(input).tag() == Dynamic::Tag::String;
}
bool val_is_array(value input) {
    if (input == nullptr || val_is_int(input) || val_is_bool(input))
        return false;
    Dynamic dynamic = ::hx::cffi::toDynamic(input);
    void* object = dynamic.managedPointer();
    const auto* type = object == nullptr ? nullptr :
        ::hx::gc::Runtime::instance().typeOf(object);
    return type != nullptr && (type->flags & ::hx::gc::TypeArray) != 0;
}
bool val_is_kind(value input, vkind kind) {
    CffiPointer* pointer = cffiPointer(input);
    return pointer != nullptr && pointer->state->kind == kind;
}
int val_int(value input) { return ::hx::cffi::toDynamic(input).Cast<int>(); }
double val_float(value input) {
    return ::hx::cffi::toDynamic(input).Cast<double>();
}
double val_number(value input) { return val_float(input); }
bool val_bool(value input) { return ::hx::cffi::toDynamic(input).Cast<bool>(); }
int val_strlen(value input) {
    if (input == nullptr)
        return 0;
    Dynamic dynamic = ::hx::cffi::toDynamic(input);
    if (dynamic.tag() != Dynamic::Tag::String)
        throw std::runtime_error("CFFI value is not a string");
    // val_string exposes UTF-8 bytes, so the paired length API must use the
    // same representation.  Haxe String.length remains UTF-16 code units.
    const std::size_t byteLength = dynamic.Cast<String>().view().size();
    if (byteLength > static_cast<std::size_t>(INT_MAX))
        throw std::length_error("CFFI String exceeds ABI length");
    return static_cast<int>(byteLength);
}
const char* val_string(value input) {
    if (input == nullptr)
        return "";
    Dynamic dynamic = ::hx::cffi::toDynamic(input);
    if (dynamic.tag() != Dynamic::Tag::String)
        throw std::runtime_error("CFFI value is not a string");
    (void)pinForCallAddress(input, dynamic);
    return dynamic.Cast<String>().c_str();
}
const wchar_t* val_wstring(value input) {
    if (input == nullptr)
        return L"";
    Dynamic dynamic = ::hx::cffi::toDynamic(input);
    if (dynamic.tag() != Dynamic::Tag::String)
        throw std::runtime_error("CFFI value is not a string");
    (void)pinForCallAddress(input, dynamic);
    const std::wstring converted = utf8ToWide(dynamic.Cast<String>().view());
    return copyLegacyString<wchar_t>(converted, nullptr);
}
char* val_dup_string(value input) {
    if (input == nullptr)
        return nullptr;
    const int length = val_strlen(input);
    return alloc_string_data(val_string(input), length);
}
wchar_t* val_dup_wstring(value input) {
    if (input == nullptr)
        return nullptr;
    const wchar_t* source = val_wstring(input);
    const std::size_t length = std::wcslen(source);
    auto* output = static_cast<wchar_t*>(allocateCallBytes(
        (length + 1U) * sizeof(wchar_t)));
    std::memcpy(output, source, (length + 1U) * sizeof(wchar_t));
    return output;
}
char* alloc_string_data(const char* input, int length) {
    if (length < 0)
        throw std::length_error("negative CFFI native string length");
    auto* output = static_cast<char*>(allocateCallBytes(
        static_cast<std::size_t>(length) + 1U));
    if (input != nullptr && length != 0)
        std::memcpy(output, input, static_cast<std::size_t>(length));
    output[length] = '\0';
    return output;
}
void* val_data(value input) {
    CffiPointer* pointer = cffiPointer(input);
    return pointer == nullptr ? nullptr : pointer->state->data;
}
void* val_to_kind(value input, vkind kind) {
    return val_is_kind(input, kind) ? val_data(input) : nullptr;
}

int val_id(const char* name) {
    return internField(name)->id;
}

[[nodiscard]] static std::string cffiFieldName(int field) {
    std::scoped_lock lock(idsMutex);
    auto found = fieldAtomsById.find(field);
    if (found == fieldAtomsById.end())
        throw std::runtime_error("unknown CFFI field id");
    return found->second->name;
}

value val_field(value object, int field) {
    Dynamic dynamic = ::hx::cffi::toDynamic(object);
    void* raw = dynamic.managedPointer();
    if (raw == nullptr)
        throw std::runtime_error("val_field requires a managed object");
    ::hx::ScopedPin pin(static_cast<::hx::Object*>(raw));
    raw = pin.get();
    const auto* type = ::hx::gc::Runtime::instance().typeOf(raw);
    if (type == &ObjectDescriptor) {
        auto* target = static_cast<CffiObject*>(raw);
        std::scoped_lock lock(target->state->mutex);
        auto found = target->state->fields.find(field);
        return found == target->state->fields.end() ? nullptr :
            ::hx::cffi::fromDynamic(found->second);
    }

    const std::string name = cffiFieldName(field);
    Dynamic output;
    for (const auto* cursor = type; cursor != nullptr; cursor = cursor->super) {
        if (cursor->dynamicFieldGet != nullptr &&
            cursor->dynamicFieldGet(raw, name.c_str(), ::hx::paccDynamic,
                                    &output))
            return ::hx::cffi::fromDynamic(output);
    }
    return nullptr;
}

void alloc_field(value object, int field, value input) {
    Dynamic dynamic = ::hx::cffi::toDynamic(object);
    void* raw = dynamic.managedPointer();
    if (raw == nullptr)
        throw std::runtime_error("alloc_field requires a managed object");
    Dynamic fieldValue = ::hx::cffi::toDynamic(input);
    ::hx::ScopedPin pin(static_cast<::hx::Object*>(raw));
    raw = pin.get();
    const auto* type = ::hx::gc::Runtime::instance().typeOf(raw);
    if (type == &ObjectDescriptor) {
        auto* target = static_cast<CffiObject*>(raw);
        std::scoped_lock lock(target->state->mutex);
        storeOwnedDynamic(target, target->state->fields[field], fieldValue);
        return;
    }

    const std::string name = cffiFieldName(field);
    for (const auto* cursor = type; cursor != nullptr; cursor = cursor->super) {
        if (cursor->dynamicFieldSet != nullptr &&
            cursor->dynamicFieldSet(raw, name.c_str(), ::hx::paccDynamic,
                                    &fieldValue))
            return;
    }
    throw std::runtime_error("CFFI field is not writable: " + name);
}

void alloc_field_numeric(value object, int fieldId, double input) {
    alloc_field(object, fieldId, alloc_float(input));
}

double val_field_numeric(value object, int fieldId) {
    return val_number(val_field(object, fieldId));
}

value val_field_name(field fieldId) {
    return ::hx::cffi::fromDynamic(Dynamic(__hxcpp_field_from_id(fieldId)));
}

void val_iter_fields(value object, __hx_field_iter callback, void* context) {
    if (object == nullptr || callback == nullptr)
        return;
    Dynamic dynamic = ::hx::cffi::toDynamic(object);
    auto fields = Array_obj<String>::__new();
    ::hx::gc::RefSlot* roots[]{&fields.mPtr};
    ::hx::gc::RootScope rootScope(roots, 1);
    dynamic.__GetFields(fields);
    for (int index = 0; index < fields->length; ++index)
        callback(object, val_id(fields->__get(index).c_str()), context);
}

void val_iter_field_vals(value object, __hx_field_iter callback,
                         void* context) {
    if (object == nullptr || callback == nullptr)
        return;
    Dynamic dynamic = ::hx::cffi::toDynamic(object);
    auto fields = Array_obj<String>::__new();
    ::hx::gc::RefSlot* roots[]{&fields.mPtr};
    ::hx::gc::RootScope rootScope(roots, 1);
    dynamic.__GetFields(fields);
    for (int index = 0; index < fields->length; ++index) {
        const int fieldId = val_id(fields->__get(index).c_str());
        callback(val_field(object, fieldId), fieldId, context);
    }
}

int val_array_size(value array) { return cffiArray(array).length; }
value val_array_i(value array, int index) {
    ErasedCffiArray source = cffiArray(array);
    Dynamic output;
    (void)source.type->dynamicIndexGet(source.object, index, &output);
    return ::hx::cffi::fromDynamic(output);
}
void val_array_set_i(value array, int index, value input) {
    ErasedCffiArray target = cffiArray(array);
    Dynamic converted = ::hx::cffi::toDynamic(input);
    if (!target.type->dynamicIndexSet(target.object, index, &converted))
        throw std::out_of_range("negative CFFI array index");
}
void val_array_push(value array, value input) {
    ErasedCffiArray target = cffiArray(array);
    Dynamic converted = ::hx::cffi::toDynamic(input);
    if (!target.type->dynamicIndexSet(
            target.object, target.length, &converted))
        throw std::runtime_error("CFFI array push failed");
}
void val_array_set_size(value array, int length) {
    if (length < 0)
        throw std::length_error("negative CFFI array length");
    Dynamic target = ::hx::cffi::toDynamic(array);
    (void)target.__Field(String("resize"), ::hx::paccDynamic)(
        Dynamic(length));
}

template<class T>
T* cffiTypedArrayData(value input) {
    Dynamic dynamic = ::hx::cffi::toDynamic(input);
    void* object = dynamic.managedPointer();
    if (object == nullptr || ::hx::gc::Runtime::instance().typeOf(object) !=
                             &Array_obj<T>::descriptor)
        return nullptr;
    auto* array = static_cast<Array_obj<T>*>(
        pinForCallAddress(input, dynamic));
    auto& runtime = ::hx::gc::Runtime::instance();
    void* storage = runtime.loadBarrier(array->buffer);
    if (storage == nullptr)
        return nullptr;
    (void)pinManagedAddressForCall(storage);
    return array->getBase();
}

bool* val_array_bool(value array) { return cffiTypedArrayData<bool>(array); }
int* val_array_int(value array) { return cffiTypedArrayData<int>(array); }
float* val_array_float(value array) { return cffiTypedArrayData<float>(array); }
value* val_array_value(value array) {
    ensureExternalPrimeCallState();
    if (currentCall == nullptr)
        throw std::logic_error(
            "CFFI array pointer requested outside call scope");
    auto proxy = std::make_unique<CallState::ArrayValueProxy>();
    proxy->array = array;
    const int length = val_array_size(array);
    proxy->values.reserve(static_cast<std::size_t>(length));
    for (int index = 0; index < length; ++index)
        proxy->values.push_back(val_array_i(array, index));
    value* output = proxy->values.data();
    currentCall->arrayValueProxies.push_back(std::move(proxy));
    return output;
}
value* val_array_ptr(value array) { return val_array_value(array); }
double* val_array_double(value array) {
    if (double* direct = cffiTypedArrayData<double>(array); direct != nullptr)
        return direct;
    thread_local std::vector<double> values;
    ErasedCffiArray source = cffiArray(array);
    values.resize(static_cast<std::size_t>(source.length));
    for (int index = 0; index < source.length; ++index) {
        Dynamic output;
        (void)source.type->dynamicIndexGet(source.object, index, &output);
        values[static_cast<std::size_t>(index)] = output.Cast<double>();
    }
    return values.data();
}

buffer alloc_buffer(const char* initial) {
    const std::size_t length = initial == nullptr ? 0 : std::strlen(initial);
    buffer result = alloc_buffer_len(static_cast<int>(length));
    if (length != 0)
        std::memcpy(buffer_data(result), initial, length);
    return result;
}
buffer alloc_buffer_len(int length) {
    if (length < 0)
        throw std::length_error("negative CFFI buffer length");
    // External Prime code executes with the calling mutator marked
    // native-blocked. Lime's image decoders call this CFFI entry point from
    // inside that native section, but the returned Bytes backing store is a
    // managed Array<UInt8>. Re-enter managed participation before even the
    // Array shell is allocated; RootScope used to do this only after the first
    // managed allocation had already happened. The scope restores the outer
    // native-blocking contract when the CFFI allocation and token conversion
    // are complete.
    ::hx::ManagedMutatorScope managedScope;
    // Lime stores buffer_val(alloc_buffer_len(...)) directly in haxe.io.Bytes.b.
    // That field is an Array<UInt8>, not an opaque native buffer.  Returning a
    // managed byte array here keeps the CFFI ABI and the generated Haxe layout
    // identical while cffiByteArray() pins both the array and backing storage
    // for the duration of the native call.
    return ::hx::cffi::fromDynamic(
        Dynamic(Array_obj<unsigned char>::__new(length)));
}
void buffer_append(buffer target, const char* text) {
    if (text == nullptr)
        return;
    const std::size_t length = std::strlen(text);
    if (auto* array = cffiByteArray(target); array != nullptr) {
        const int offset = array->length;
        if (length > static_cast<std::size_t>(
                         std::numeric_limits<int>::max() - offset))
            throw std::length_error("CFFI byte buffer append overflow");
        array->resize(offset + static_cast<int>(length));
        array = cffiByteArray(target);
        if (length != 0)
            std::memcpy(array->getBase() + offset, text, length);
        return;
    }
    CffiBuffer* object = cffiBuffer(target);
    object->state->bytes.insert(object->state->bytes.end(), text,
                                text + length);
}
void val_buffer(buffer target, value input) {
    const String text = ::hx::cffi::toDynamic(input).toString();
    const std::string copy(text.view());
    buffer_append(target, copy.c_str());
}
void buffer_append_sub(buffer target, const char* text, int length) {
    if (length < 0 || (length != 0 && text == nullptr))
        throw std::invalid_argument("invalid CFFI buffer substring");
    if (auto* array = cffiByteArray(target); array != nullptr) {
        const int offset = array->length;
        if (length > std::numeric_limits<int>::max() - offset)
            throw std::length_error("CFFI byte buffer append overflow");
        array->resize(offset + length);
        array = cffiByteArray(target);
        if (length != 0)
            std::memcpy(array->getBase() + offset, text,
                        static_cast<std::size_t>(length));
        return;
    }
    CffiBuffer* object = cffiBuffer(target);
    if (length != 0)
        object->state->bytes.insert(object->state->bytes.end(), text,
                                    text + length);
}
void buffer_append_char(buffer target, int character) {
    if (auto* array = cffiByteArray(target); array != nullptr) {
        const int offset = array->length;
        array->resize(offset + 1);
        array = cffiByteArray(target);
        array->getBase()[offset] = static_cast<unsigned char>(character);
        return;
    }
    cffiBuffer(target)->state->bytes.push_back(
        static_cast<char>(character));
}
char* buffer_data(buffer target) {
    if (auto* array = cffiByteArray(target); array != nullptr)
        return reinterpret_cast<char*>(array->getBase());
    auto& bytes = cffiBuffer(target)->state->bytes;
    return bytes.empty() ? nullptr : bytes.data();
}
int buffer_size(buffer target) {
    if (auto* array = cffiByteArray(target); array != nullptr)
        return array->length;
    return static_cast<int>(cffiBuffer(target)->state->bytes.size());
}
value buffer_val(buffer target) { return target; }
value buffer_to_string(buffer target) {
    const int size = buffer_size(target);
    const char* bytes = buffer_data(target);
    return alloc_string_len(bytes == nullptr ? "" : bytes, size);
}
buffer val_to_buffer(value input) {
    if (!isCffiByteBuffer(input))
        throw std::runtime_error("CFFI value is not a buffer");
    return input;
}
bool val_is_buffer(value input) { return isCffiByteBuffer(input); }
void buffer_set_size(buffer target, int length) {
    if (length < 0)
        throw std::length_error("negative CFFI buffer length");
    if (auto* array = cffiByteArray(target); array != nullptr) {
        array->resize(length);
        return;
    }
    cffiBuffer(target)->state->bytes.resize(
        static_cast<std::size_t>(length));
}

void val_gc(value input, ::hx::finalizer finalizer) {
    CffiPointer* pointer = cffiPointer(input);
    if (pointer != nullptr) {
        ValueCell* cell = asCell(input);
        if (cell == nullptr)
            throw std::runtime_error("val_gc requires a managed CFFI value");

        if (finalizer != nullptr &&
            pointer->state->finalizerToken == nullptr) {
            auto& runtime = ::hx::gc::Runtime::instance();
            void* managed = resolveCell(*cell).managedPointer();
            if (cell->strong.valid())
                runtime.releaseHandle(cell->strong);
            cell->strong = runtime.createHandle(
                managed, ::hx::gc::HandleKind::Weak);
            cell->persistent = true;
            pointer->state->finalizerToken = input;
        }
        pointer->state->finalizer = finalizer;
        return;
    }

    ValueCell* cell = asCell(input);
    if (cell == nullptr)
        throw std::runtime_error("val_gc requires a managed CFFI value");
    Dynamic dynamic = resolveCell(*cell);
    void* managed = dynamic.managedPointer();
    if (managed == nullptr)
        throw std::runtime_error("val_gc requires a managed CFFI value");

    auto& runtime = ::hx::gc::Runtime::instance();
    const std::uint64_t identity = runtime.identityOf(managed);
    {
        std::scoped_lock lock(externalFinalizersMutex);
        auto found = externalFinalizers.find(identity);
        if (found != externalFinalizers.end()) {
            found->second->callback = finalizer;
            return;
        }
        if (finalizer == nullptr)
            return;
    }

    // A native library may retain this exact value token as an identity key.
    // Keep the token allocation stable, but downgrade its managed handle to
    // weak so the token itself cannot prevent the Haxe object from dying.
    if (cell->strong.valid())
        runtime.releaseHandle(cell->strong);
    cell->strong = runtime.createHandle(managed, ::hx::gc::HandleKind::Weak);
    cell->persistent = true;

    auto state = std::make_unique<ExternalFinalizerState>();
    state->token = input;
    state->callback = finalizer;
    state->identity = identity;
    auto attachment = ::hx::allocate<CffiFinalizerAttachment>(
        FinalizerAttachmentDescriptor, managed, state.get());
    ::hx::gc::RefSlot* roots[]{&attachment.mPtr};
    ::hx::gc::RootScope attachmentRoot(roots, 1, 0x43464649U);
    state->association = runtime.createEphemeron(managed, attachment.get());

    {
        std::scoped_lock lock(externalFinalizersMutex);
        auto [position, inserted] = externalFinalizers.emplace(identity,
                                                               state.get());
        if (!inserted) {
            runtime.releaseEphemeron(state->association);
            attachment->state = nullptr;
            cell->persistent = false;
            if (cell->strong.valid())
                runtime.releaseHandle(cell->strong);
            cell->strong = runtime.createHandle(managed,
                                                ::hx::gc::HandleKind::Strong);
            position->second->callback = finalizer;
            return;
        }
    }
    attachment->state = state.release();
}
void val_gc_ptr(void*, hxPtrFinalizer) {
    throw std::runtime_error(
        "raw-pointer finalizers conflict with precise GC ownership");
}
[[noreturn]] void val_throw(value input) {
    throw ::hx::cffi::toDynamic(input);
}
value val_call0(value function) {
    ManagedCffiCallbackScope managedCallback;
    return ::hx::cffi::fromDynamic(::hx::cffi::toDynamic(function)());
}
value val_call1(value function, value arg0) {
    ManagedCffiCallbackScope managedCallback;
    return ::hx::cffi::fromDynamic(
        ::hx::cffi::toDynamic(function)(::hx::cffi::toDynamic(arg0)));
}
value val_call2(value function, value arg0, value arg1) {
    ManagedCffiCallbackScope managedCallback;
    return ::hx::cffi::fromDynamic(::hx::cffi::toDynamic(function)(
        ::hx::cffi::toDynamic(arg0), ::hx::cffi::toDynamic(arg1)));
}
value val_call3(value function, value arg0, value arg1, value arg2) {
    ManagedCffiCallbackScope managedCallback;
    return ::hx::cffi::fromDynamic(::hx::cffi::toDynamic(function)(
        ::hx::cffi::toDynamic(arg0), ::hx::cffi::toDynamic(arg1),
        ::hx::cffi::toDynamic(arg2)));
}
value val_callN(value function, value* arguments, int count) {
    ManagedCffiCallbackScope managedCallback;
    if (count < 0 || count > 12 || (count != 0 && arguments == nullptr))
        throw std::invalid_argument("invalid CFFI callback argument vector");
    std::vector<Dynamic> converted(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index)
        converted[static_cast<std::size_t>(index)] =
            ::hx::cffi::toDynamic(arguments[index]);
    return ::hx::cffi::fromDynamic(::hx::cffi::toDynamic(function).invoke(
        converted.data(), count));
}
value val_call0_traceexcept(value function) {
    try {
        return val_call0(function);
    } catch (const Dynamic& error) {
        std::cerr << "Uncaught Haxe/CFFI exception: "
                  << error.toString().view() << '\n';
        throw;
    } catch (const std::exception& error) {
        std::cerr << "Uncaught CFFI exception: " << error.what() << '\n';
        throw;
    }
}
value val_ocall0(value object, int fieldId) {
    return val_call0(val_field(object, fieldId));
}
value val_ocall1(value object, int fieldId, value arg0) {
    return val_call1(val_field(object, fieldId), arg0);
}
value val_ocall2(value object, int fieldId, value arg0, value arg1) {
    return val_call2(val_field(object, fieldId), arg0, arg1);
}
value val_ocallN(value object, int fieldId, value* arguments, int count) {
    return val_callN(val_field(object, fieldId), arguments, count);
}

value* alloc_root() {
    auto* root = new RootSlot;
    std::scoped_lock lock(rootsMutex);
    rootSlots.push_back(root);
    return &root->current;
}
void free_root(value* rootValue) {
    if (rootValue == nullptr)
        return;
    auto* root = reinterpret_cast<RootSlot*>(rootValue);
    {
        std::scoped_lock lock(rootsMutex);
        auto found = std::find(rootSlots.begin(), rootSlots.end(), root);
        if (found != rootSlots.end())
            rootSlots.erase(found);
    }
    releasePersistent(root->current);
    delete root;
}
gcroot create_root(value input) {
    return new RootHandle{clonePersistent(input)};
}
value query_root(gcroot root) {
    return root == nullptr ? nullptr : static_cast<RootHandle*>(root)->current;
}
void destroy_root(gcroot root) {
    if (root == nullptr)
        return;
    auto* handle = static_cast<RootHandle*>(root);
    releasePersistent(handle->current);
    delete handle;
}

void kind_share(vkind* kind, const char* name) {
    if (kind == nullptr)
        return;
    if (*kind == nullptr)
        *kind = ::hx::cffi::createKind(name);
}

const char* hxs_utf8(HxString input, void*) {
    return val_string(::hx::cffi::fromDynamic(Dynamic(input)));
}

const wchar_t* hxs_wchar(HxString input, void*) {
    thread_local std::wstring converted;
    converted = utf8ToWide(input.view());
    return converted.c_str();
}

void gc_set_top_of_stack(void* top, bool force) {
    (void)force;
    auto& runtime = ::hx::gc::Runtime::instance();
    if (top != nullptr) {
        if (!runtime.currentThreadAttached())
            runtime.attachCurrentThread();
        // The original hxcpp entry point doubles as the transition from a
        // foreign/native thread into managed code.  Honor an already-requested
        // exact-GC handshake before the NDLL is allowed to touch Haxe objects.
        runtime.safepointPoll();
    } else if (runtime.currentThreadAttached()) {
        runtime.detachCurrentThread();
    }
}

void gc_enter_blocking() {
    auto& runtime = ::hx::gc::Runtime::instance();
    if (runtime.currentThreadAttached()) {
        if (cffiNativeBlockingDepth ==
            std::numeric_limits<std::size_t>::max())
            throw std::overflow_error("CFFI native blocking depth overflow");
        runtime.enterNativeBlocking();
        ++cffiNativeBlockingDepth;
    }
}

void gc_exit_blocking() {
    auto& runtime = ::hx::gc::Runtime::instance();
    if (runtime.currentThreadAttached() && cffiNativeBlockingDepth != 0) {
        --cffiNativeBlockingDepth;
        runtime.leaveNativeBlocking();
    }
}

bool gc_try_blocking() {
    auto& runtime = ::hx::gc::Runtime::instance();
    if (!runtime.currentThreadAttached() || cffiNativeBlockingDepth != 0)
        return false;
    runtime.enterNativeBlocking();
    cffiNativeBlockingDepth = 1;
    return true;
}

bool gc_try_unblocking() {
    auto& runtime = ::hx::gc::Runtime::instance();
    if (!runtime.currentThreadAttached() || cffiNativeBlockingDepth == 0)
        return false;
    --cffiNativeBlockingDepth;
    runtime.leaveNativeBlocking();
    return true;
}

void gc_safe_point() {
    auto& runtime = ::hx::gc::Runtime::instance();
    if (runtime.currentThreadAttached() && cffiNativeBlockingDepth == 0)
        runtime.safepointPoll();
}

void gc_change_managed_memory(int delta, const char*) {
    externalManagedBytes.fetch_add(delta, std::memory_order_relaxed);
}

void val_gc_add_root(value* root) {
    if (root == nullptr)
        return;
    std::scoped_lock lock(externalRootsMutex);
    if (externalRoots.contains(root))
        return;
    value persistent = clonePersistent(*root);
    externalRoots.emplace(root, persistent);
    *root = persistent;
}

void val_gc_remove_root(value* root) {
    if (root == nullptr)
        return;
    value persistent = nullptr;
    {
        std::scoped_lock lock(externalRootsMutex);
        const auto found = externalRoots.find(root);
        if (found == externalRoots.end())
            return;
        persistent = found->second;
        externalRoots.erase(found);
    }
    *root = nullptr;
    releasePersistent(persistent);
}

void* hx_alloc(int size) {
    if (size < 0)
        throw std::length_error("negative CFFI native allocation");
    auto allocation = std::make_unique<std::byte[]>(
        std::max<std::size_t>(static_cast<std::size_t>(size), 1));
    void* result = allocation.get();
    std::scoped_lock lock(nativeBytesMutex);
    nativeBytes.push_back(std::move(allocation));
    return result;
}

int hx_register_prim(const char* name, void* function) {
    return __hxcpp_register_prim(name, function);
}

[[noreturn]] void hx_error() {
    throw std::runtime_error("native CFFI error");
}

[[noreturn]] void hx_fail(const char* message, const char* file, int line) {
    throw std::runtime_error(std::string(message == nullptr ?
        "native CFFI failure" : message) + " at " +
        (file == nullptr ? "<native>" : file) + ":" +
        std::to_string(line));
}

namespace {

template<typename Function>
[[nodiscard]] void* cffiAddress(Function function) noexcept {
    static_assert(sizeof(Function) == sizeof(void*));
    return std::bit_cast<void*>(function);
}

} // namespace

// Native Lime modules resolve the host CFFI surface through one symbol.  The
// exported resolver is deliberately a narrow ABI boundary: values returned by
// the functions below remain stable exact-GC tokens, never movable heap
// addresses.
extern "C"
#ifdef _WIN32
__declspec(dllexport)
#endif
void* hx_cffi(const char* name) noexcept {
    if (name == nullptr)
        return nullptr;

#define HX_CFFI_RESOLVE(function)                                         \
    if (std::strcmp(name, #function) == 0)                               \
        return cffiAddress(&function)

    HX_CFFI_RESOLVE(alloc_null);
    HX_CFFI_RESOLVE(alloc_int);
    HX_CFFI_RESOLVE(alloc_float);
    HX_CFFI_RESOLVE(alloc_bool);
    HX_CFFI_RESOLVE(alloc_string);
    HX_CFFI_RESOLVE(alloc_wstring);
    HX_CFFI_RESOLVE(alloc_string_len);
    HX_CFFI_RESOLVE(alloc_wstring_len);
    HX_CFFI_RESOLVE(alloc_raw_string);
    HX_CFFI_RESOLVE(alloc_empty_string);
    HX_CFFI_RESOLVE(alloc_empty_object);
    HX_CFFI_RESOLVE(alloc_array);
    HX_CFFI_RESOLVE(alloc_array_type);
    HX_CFFI_RESOLVE(alloc_abstract);
    HX_CFFI_RESOLVE(create_abstract);
    HX_CFFI_RESOLVE(free_abstract);
    HX_CFFI_RESOLVE(alloc_best_int);
    HX_CFFI_RESOLVE(alloc_int32);
    HX_CFFI_RESOLVE(alloc_kind);

    HX_CFFI_RESOLVE(val_type);
    HX_CFFI_RESOLVE(val_kind);
    HX_CFFI_RESOLVE(val_fun_nargs);
    HX_CFFI_RESOLVE(val_is_null);
    HX_CFFI_RESOLVE(val_is_int);
    HX_CFFI_RESOLVE(val_is_float);
    HX_CFFI_RESOLVE(val_is_bool);
    HX_CFFI_RESOLVE(val_is_string);
    HX_CFFI_RESOLVE(val_is_array);
    HX_CFFI_RESOLVE(val_is_kind);
    HX_CFFI_RESOLVE(val_int);
    HX_CFFI_RESOLVE(val_float);
    HX_CFFI_RESOLVE(val_number);
    HX_CFFI_RESOLVE(val_bool);
    HX_CFFI_RESOLVE(val_strlen);
    HX_CFFI_RESOLVE(val_string);
    HX_CFFI_RESOLVE(val_wstring);
    HX_CFFI_RESOLVE(val_dup_string);
    HX_CFFI_RESOLVE(val_dup_wstring);
    HX_CFFI_RESOLVE(alloc_string_data);
    HX_CFFI_RESOLVE(val_data);
    HX_CFFI_RESOLVE(val_to_kind);
    HX_CFFI_RESOLVE(val_id);
    HX_CFFI_RESOLVE(val_field);
    HX_CFFI_RESOLVE(alloc_field);
    HX_CFFI_RESOLVE(alloc_field_numeric);
    HX_CFFI_RESOLVE(val_field_numeric);
    HX_CFFI_RESOLVE(val_field_name);
    HX_CFFI_RESOLVE(val_iter_fields);
    HX_CFFI_RESOLVE(val_iter_field_vals);

    HX_CFFI_RESOLVE(val_array_size);
    HX_CFFI_RESOLVE(val_array_i);
    HX_CFFI_RESOLVE(val_array_set_i);
    HX_CFFI_RESOLVE(val_array_push);
    HX_CFFI_RESOLVE(val_array_set_size);
    HX_CFFI_RESOLVE(val_array_ptr);
    HX_CFFI_RESOLVE(val_array_bool);
    HX_CFFI_RESOLVE(val_array_int);
    HX_CFFI_RESOLVE(val_array_double);
    HX_CFFI_RESOLVE(val_array_float);
    HX_CFFI_RESOLVE(val_array_value);

    HX_CFFI_RESOLVE(alloc_buffer);
    HX_CFFI_RESOLVE(alloc_buffer_len);
    HX_CFFI_RESOLVE(val_buffer);
    HX_CFFI_RESOLVE(buffer_append);
    HX_CFFI_RESOLVE(buffer_append_sub);
    HX_CFFI_RESOLVE(buffer_append_char);
    HX_CFFI_RESOLVE(buffer_data);
    HX_CFFI_RESOLVE(buffer_size);
    HX_CFFI_RESOLVE(buffer_set_size);
    HX_CFFI_RESOLVE(buffer_val);
    HX_CFFI_RESOLVE(buffer_to_string);
    HX_CFFI_RESOLVE(val_to_buffer);
    HX_CFFI_RESOLVE(val_is_buffer);

    HX_CFFI_RESOLVE(val_gc);
    HX_CFFI_RESOLVE(val_gc_ptr);
    HX_CFFI_RESOLVE(val_throw);
    HX_CFFI_RESOLVE(val_call0);
    HX_CFFI_RESOLVE(val_call1);
    HX_CFFI_RESOLVE(val_call2);
    HX_CFFI_RESOLVE(val_call3);
    HX_CFFI_RESOLVE(val_callN);
    HX_CFFI_RESOLVE(val_call0_traceexcept);
    HX_CFFI_RESOLVE(val_ocall0);
    HX_CFFI_RESOLVE(val_ocall1);
    HX_CFFI_RESOLVE(val_ocall2);
    HX_CFFI_RESOLVE(val_ocallN);

    HX_CFFI_RESOLVE(alloc_root);
    HX_CFFI_RESOLVE(free_root);
    HX_CFFI_RESOLVE(create_root);
    HX_CFFI_RESOLVE(query_root);
    HX_CFFI_RESOLVE(destroy_root);
    HX_CFFI_RESOLVE(kind_share);
    HX_CFFI_RESOLVE(hx_alloc);
    HX_CFFI_RESOLVE(val_gc_add_root);
    HX_CFFI_RESOLVE(val_gc_remove_root);
    HX_CFFI_RESOLVE(gc_change_managed_memory);
    HX_CFFI_RESOLVE(gc_set_top_of_stack);
    HX_CFFI_RESOLVE(gc_enter_blocking);
    HX_CFFI_RESOLVE(gc_exit_blocking);
    HX_CFFI_RESOLVE(gc_try_blocking);
    HX_CFFI_RESOLVE(gc_try_unblocking);
    HX_CFFI_RESOLVE(gc_safe_point);
    HX_CFFI_RESOLVE(hx_register_prim);
    HX_CFFI_RESOLVE(hx_error);
    HX_CFFI_RESOLVE(hx_fail);

    // These names are consumed by NDLLs compiled against the established
    // hxcpp Prime ABI.  They must not resolve to the new host String
    // overloads above: external HxString is a two-word {length, data} value.
    if (std::strcmp(name, "alloc_hxs_utf8") == 0)
        return cffiAddress(&legacyAllocHxsUtf8);
    if (std::strcmp(name, "alloc_hxs_wchar") == 0)
        return cffiAddress(&legacyAllocHxsWchar);
    if (std::strcmp(name, "alloc_hxs_utf16") == 0)
        return cffiAddress(&legacyAllocHxsUtf16);
    if (std::strcmp(name, "hxs_utf8") == 0)
        return cffiAddress(&legacyHxsUtf8);
    if (std::strcmp(name, "hxs_wchar") == 0)
        return cffiAddress(&legacyHxsWchar);
    if (std::strcmp(name, "hxs_utf16") == 0)
        return cffiAddress(&legacyHxsUtf16);
    if (std::strcmp(name, "hxs_encoding") == 0)
        return cffiAddress(&legacyHxsEncoding);

#undef HX_CFFI_RESOLVE
    return nullptr;
}

extern "C" Dynamic __hxcpp_zgc_load_prime(String library, String primitive,
                                              String signature,
                                              bool quietFail) {
    const std::string primitiveName(primitive.view());
	const auto signatureView = signature.view();
    const int arity = signature.length == 0 ? 0 : signature.length - 1;
    if (arity < 0 || arity > 12) {
        if (quietFail)
            return Dynamic();
        throw std::runtime_error("Prime signature arity is outside 0..12");
    }

    const ::String factoryName = primitive + ::String("__prime");
    void* factoryAddress = __hxcpp_load_native_symbol(
        library, factoryName, true);
    if (factoryAddress != nullptr) {
        auto factory = reinterpret_cast<void* (*)(const char*)>(factoryAddress);
        ::hx::cffi::CallScope scope;
        void* token = factory(std::string(signature.view()).c_str());
        void* function = token == nullptr ? nullptr :
            val_data(reinterpret_cast<value>(token));
        if (function != nullptr) {
			// Lime's hot OpenGL primitives with scalar-only Prime signatures do
			// not retain or inspect managed objects.  Calling these tiny enqueue
			// stubs through ExternalPrimeCallScope made every GL state/uniform
			// operation enter and leave NovaGC native-blocking state.  Keep the
			// full adapter for strings/objects and all non-GL libraries, but let
			// scalar-only GL leaves use cpp::Function's direct typed call.
			bool scalarOpenGLLeaf = primitiveName.rfind("lime_gl_", 0) == 0;
			for (const char code : signatureView) {
				if (code != 'b' && code != 'i' && code != 'f' && code != 'd'
					&& code != 'v') {
					scalarOpenGLLeaf = false;
					break;
				}
			}
			if (!scalarOpenGLLeaf)
				::hx::cffi::registerExternalPrime(function);
            return ::Dynamic::fromNativePointer(function);
        }
        if (!quietFail)
            throw std::runtime_error("Prime primitive rejected signature: " +
                                     primitiveName);
        return nullptr;
    }

    ::hx::cffi::PrimeInvoker invoker = nullptr;
    {
        std::scoped_lock lock(primeRegistryMutex());
        auto& registry = primeRegistry();
        auto found = registry.find(primitiveName);
        if (found != registry.end())
            invoker = found->second;
    }
    if (invoker == nullptr) {
        if (quietFail)
            return Dynamic();
        throw std::runtime_error("Prime primitive is not registered: " +
                                 primitiveName);
    }
    return createPrimeClosure(invoker, arity);
}

bool __hxcpp_same_closure(::Dynamic left, ::Dynamic right) {
    // Reflect.compareMethods must treat two independently materialized bound
    // method closures as the same callback when both their receiver and their
    // generated wrapper function match.  EventDispatcher asks for a fresh
    // foo_dyn() object when removing a listener, so object identity alone
    // makes every removal fail and can leave per-frame callbacks running
    // forever.
    ::hx::gc::RefSlot* roots[]{&left.mPtr, &right.mPtr};
    ::hx::gc::RootScope rootScope(roots, 2);

    if (left.isNull() || right.isNull())
        return left.isNull() == right.isNull();
    if (left.isMemberFunction() || right.isMemberFunction()) {
        return left.isMemberFunction() && right.isMemberFunction() &&
            left.memberFunctionArity() == right.memberFunctionArity() &&
            left.memberFunctionAddress() == right.memberFunctionAddress() &&
            left.referencePointer() == right.referencePointer();
    }
    if (left.tag() != ::Dynamic::Tag::Object ||
        right.tag() != ::Dynamic::Tag::Object)
        return ::hx::IsPointerEq(left, right);

    auto* leftObject = static_cast<::hx::Object*>(left.managedPointer());
    auto* rightObject = static_cast<::hx::Object*>(right.managedPointer());
    if (leftObject == rightObject)
        return true;
    if (leftObject == nullptr || rightObject == nullptr)
        return false;

    ::hx::ScopedPin leftPin(leftObject);
    ::hx::ScopedPin rightPin(rightObject);
    leftObject = leftPin.get();
    rightObject = rightPin.get();

    auto& runtime = ::hx::gc::Runtime::instance();
    const auto* leftType = runtime.typeOf(leftObject);
    const auto* rightType = runtime.typeOf(rightObject);
    if (leftType == nullptr || leftType != rightType ||
        (leftType->flags & ::hx::gc::TypeFunction) == 0 ||
        leftType->name == nullptr)
        return false;

    // These two closure layouts are part of NovaGC's private generated ABI:
    // LocalFunc base, an optional receiver RefSlot, then one native wrapper
    // function pointer.  Reject an unexpected platform layout instead of
    // comparing padding or arbitrary captured closure state.
    static_assert(sizeof(::hx::LocalFunc) == sizeof(void*));
    static_assert(sizeof(::hx::ObjectPtr<::hx::Object>) ==
                  sizeof(::hx::gc::RefSlot));
    constexpr std::size_t baseBytes = sizeof(::hx::LocalFunc);
    constexpr std::size_t functionBytes = sizeof(void*);

    if (std::strcmp(leftType->name, "StaticFunction") == 0) {
        constexpr std::size_t functionOffset = baseBytes;
        if (leftType->fixedPayloadBytes != functionOffset + functionBytes)
            return false;
        return std::memcmp(
            reinterpret_cast<const std::byte*>(leftObject) + functionOffset,
            reinterpret_cast<const std::byte*>(rightObject) + functionOffset,
            functionBytes) == 0;
    }

    if (std::strcmp(leftType->name, "MemberFunction") == 0) {
        constexpr std::size_t ownerOffset = baseBytes;
        constexpr std::size_t functionOffset =
            ownerOffset + sizeof(::hx::ObjectPtr<::hx::Object>);
        if (leftType->fixedPayloadBytes != functionOffset + functionBytes)
            return false;

        auto& leftOwnerSlot = *reinterpret_cast<::hx::gc::RefSlot*>(
            reinterpret_cast<std::byte*>(leftObject) + ownerOffset);
        auto& rightOwnerSlot = *reinterpret_cast<::hx::gc::RefSlot*>(
            reinterpret_cast<std::byte*>(rightObject) + ownerOffset);
        auto* leftOwner = static_cast<::hx::Object*>(
            runtime.loadBarrier(leftOwnerSlot));
        auto* rightOwner = static_cast<::hx::Object*>(
            runtime.loadBarrier(rightOwnerSlot));
        if (leftOwner != rightOwner)
            return false;

        return std::memcmp(
            reinterpret_cast<const std::byte*>(leftObject) + functionOffset,
            reinterpret_cast<const std::byte*>(rightObject) + functionOffset,
            functionBytes) == 0;
    }

    auto* leftFunction = dynamic_cast<::hx::LocalFunc*>(leftObject);
    auto* rightFunction = dynamic_cast<::hx::LocalFunc*>(rightObject);
    if (leftFunction != nullptr && rightFunction != nullptr)
        return leftFunction->sameClosure(*rightFunction);

    // Capturing/local/varargs/native closures do not have Haxe bound-method
    // identity.  Only the earlier exact object-identity case can match them.
    return false;
}

bool __hxcpp_anon_remove(::Dynamic object, ::String field) {
    if (object.tag() != ::Dynamic::Tag::Object)
        return false;
    auto* raw = static_cast<::hx::Object*>(object.managedPointer());
    if (raw == nullptr)
        return false;
    ::hx::ScopedPin pin(raw);
    auto* anon = static_cast<::hx::Anon_obj*>(pin.get());
    const auto* type = ::hx::gc::Runtime::instance().typeOf(anon);
    const FieldAtom* atom = internField(field.view());
    if (type == &::hx::Anon_obj::descriptor) {
        return removeManagedAnonField(anon, atom);
    }
    if (type == &DynamicAnonDescriptor) {
        auto* state = static_cast<FieldState*>(anon->state);
        if (state == nullptr)
            return false;
        std::scoped_lock lock(state->mutex);
        return state->fields.erase(atom->id) != 0;
    }
    return false;
}

::Dynamic __hxcpp_create_var_args(::Dynamic function) {
    if (function.isNull())
        return nullptr;
    return ::Dynamic(new VarArgsClosure(function));
}

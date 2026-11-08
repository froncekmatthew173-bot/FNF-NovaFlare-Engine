#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#if defined(_MSC_VER)
#define HX_NOVAGC_ABI_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define HX_NOVAGC_ABI_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define HX_NOVAGC_ABI_ALWAYS_INLINE inline
#endif

namespace hx::gc {

enum class View : std::uint8_t {
    Remapped = 1,
    Marked0 = 2,
    Marked1 = 3,
};

class Ref final {
public:
    static constexpr std::uint64_t ViewMask = 0x3ULL;
    static constexpr std::uint64_t AddressMask = ~ViewMask;

    constexpr Ref() noexcept = default;
    explicit constexpr Ref(std::uint64_t bits) noexcept : bits_(bits) {}

    [[nodiscard]] static Ref encode(void* address, View view) noexcept {
        const auto raw = reinterpret_cast<std::uintptr_t>(address);
        return address == nullptr ? Ref{} : Ref(raw | static_cast<std::uint64_t>(view));
    }

    [[nodiscard]] constexpr bool isNull() const noexcept { return bits_ == 0; }
    [[nodiscard]] constexpr View view() const noexcept {
        return static_cast<View>(bits_ & ViewMask);
    }
    [[nodiscard]] void* address() const noexcept {
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(bits_ & AddressMask));
    }
    [[nodiscard]] constexpr std::uint64_t bits() const noexcept { return bits_; }

private:
    std::uint64_t bits_ = 0;
};

struct alignas(8) RefSlot final {
    std::uint64_t storage = 0;

    constexpr RefSlot() noexcept = default;
    explicit constexpr RefSlot(std::uint64_t bits) noexcept : storage(bits) {}
    RefSlot(const RefSlot&) noexcept = default;
    RefSlot& operator=(const RefSlot&) noexcept = default;

    // These wrappers are each one native atomic instruction on the supported
    // 64-bit targets.  At -O2 GCC otherwise outlines the nested load used by
    // the header-only barrier fast paths, turning a single mov into one of the
    // hottest calls in generated update/render code.
    [[nodiscard]] HX_NOVAGC_ABI_ALWAYS_INLINE std::uint64_t load(
        std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return std::atomic_ref<std::uint64_t>(
            const_cast<std::uint64_t&>(storage)).load(order);
    }
    HX_NOVAGC_ABI_ALWAYS_INLINE void store(
            std::uint64_t value,
            std::memory_order order = std::memory_order_seq_cst) noexcept {
        std::atomic_ref<std::uint64_t>(storage).store(value, order);
    }
    [[nodiscard]] HX_NOVAGC_ABI_ALWAYS_INLINE std::uint64_t exchange(
        std::uint64_t value,
        std::memory_order order = std::memory_order_seq_cst) noexcept {
        return std::atomic_ref<std::uint64_t>(storage).exchange(value, order);
    }
    HX_NOVAGC_ABI_ALWAYS_INLINE bool compare_exchange_weak(
        std::uint64_t& expected, std::uint64_t desired,
        std::memory_order success, std::memory_order failure) noexcept {
        return std::atomic_ref<std::uint64_t>(storage).compare_exchange_weak(
            expected, desired, success, failure);
    }
    HX_NOVAGC_ABI_ALWAYS_INLINE bool compare_exchange_strong(
        std::uint64_t& expected, std::uint64_t desired,
        std::memory_order success, std::memory_order failure) noexcept {
        return std::atomic_ref<std::uint64_t>(storage).compare_exchange_strong(
            expected, desired, success, failure);
    }
};

#undef HX_NOVAGC_ABI_ALWAYS_INLINE

class TraceVisitor {
public:
    virtual ~TraceVisitor() = default;
    virtual void visit(RefSlot& slot) = 0;
};

struct TypeDescriptor;
using TraceFunction = void (*)(void* object, std::size_t payloadBytes,
                               TraceVisitor& visitor);
using FinalizeFunction = void (*)(void* object);
using DynamicFieldGetFunction = bool (*)(void* object, const char* name,
                                         int propertyAccess,
                                         void* outputDynamic);
using DynamicFieldSetFunction = bool (*)(void* object, const char* name,
                                         int propertyAccess,
                                         const void* inputDynamic);
using DynamicIndexGetFunction = bool (*)(void* object, int index,
                                         void* outputDynamic);
using DynamicIndexSetFunction = bool (*)(void* object, int index,
                                         const void* inputDynamic);
using DynamicFieldNameVisitor = void (*)(void* context, const char* name,
                                         std::size_t length);
using DynamicFieldEnumerateFunction = void (*)(
    void* object, void* context, DynamicFieldNameVisitor visitor);
using StaticFieldGetFunction = bool (*)(const char* name,
                                        int propertyAccess,
                                        void* outputDynamic);
using StaticFieldSetFunction = bool (*)(const char* name,
                                        int propertyAccess,
                                        const void* inputDynamic);

enum TypeFlag : std::uint32_t {
    TypeNone = 0,
    TypeContainsReferences = 1U << 0,
    TypeVariableSize = 1U << 1,
    TypeReferenceArray = 1U << 2,
    TypeByteArray = 1U << 3,
    TypeString = 1U << 4,
    TypeFinalizable = 1U << 5,
    TypeFunction = 1U << 6,
    TypeEnum = 1U << 7,
    TypeArray = 1U << 8,
    // Values with this flag have Haxe anonymous/dynamic-object semantics.
    // They are the only managed objects reported as vtObject/TObject;
    // ordinary generated instances are vtClass/TClass even though both are
    // represented by GC-managed C++ objects.
    TypeDynamicObject = 1U << 9,
    // Objects of this type are allocated in a dedicated non-moving region.
    // This is reserved for payloads whose address is part of a native ABI;
    // ordinary CFFI code should prefer scoped/pinned handles.
    TypePinned = 1U << 10,
};

struct TypeDescriptor final {
    std::uint64_t typeId;
    const char* name;
    const TypeDescriptor* super;
    std::uint32_t fixedPayloadBytes;
    std::uint16_t alignment;
    std::uint16_t reserved;
    std::uint32_t flags;
    std::uint32_t referenceOffsetCount;
    const std::uint32_t* referenceOffsets;
    TraceFunction trace;
    FinalizeFunction finalize;
    DynamicFieldGetFunction dynamicFieldGet = nullptr;
    DynamicFieldSetFunction dynamicFieldSet = nullptr;
    DynamicIndexGetFunction dynamicIndexGet = nullptr;
    DynamicIndexSetFunction dynamicIndexSet = nullptr;
    std::uint32_t dynamicFieldCount = 0;
    const char* const* dynamicFieldNames = nullptr;
    DynamicFieldEnumerateFunction dynamicFieldEnumerate = nullptr;
    StaticFieldGetFunction staticFieldGet = nullptr;
    StaticFieldSetFunction staticFieldSet = nullptr;
    std::uint32_t staticFieldCount = 0;
    const char* const* staticFieldNames = nullptr;
    std::uint32_t instanceMemberCount = 0;
    const char* const* instanceMemberNames = nullptr;
};

enum ObjectFlag : std::uint16_t {
    ObjectNone = 0,
    ObjectPublished = 1U << 0,
    ObjectPinned = 1U << 1,
    ObjectLarge = 1U << 2,
    ObjectFinalizable = 1U << 3,
    ObjectFinalized = 1U << 4,
    // Exact old-to-young owner is already present in rememberedOwners for the
    // current nursery cohort.  Cleared atomically when that cohort is
    // snapshotted, allowing the first later young edge to register it again.
    ObjectRemembered = 1U << 5,
};

enum ObjectState : std::uint32_t {
    ObjectStable = 0,
    ObjectRelocating = 1U << 0,
    ObjectRelocated = 1U << 1,
};

struct alignas(16) ObjectHeader final {
    std::atomic<std::uint64_t> forwarding{0};
    const TypeDescriptor* type = nullptr;
    std::uint32_t totalBytes = 0;
    std::uint32_t payloadBytes = 0;
    std::atomic<std::uint32_t> markEpoch{0};
    std::atomic<std::uint32_t> state{0};
    std::uint64_t identity = 0;
    std::atomic<std::uint16_t> flags{ObjectNone};
    std::uint16_t age = 0;
    std::uint32_t regionIndex = 0;
    std::atomic<std::uint32_t> pinCount{0};
    std::uint64_t reserved0 = 0;
};

enum class HandleKind : std::uint8_t {
    Strong,
    Weak,
    Pinned,
    Soft,
    Phantom,
};

struct Handle final {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0; }
};

struct EphemeronHandle final {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0; }
};

struct RootFrame final {
    RootFrame* previous = nullptr;
    RefSlot** slots = nullptr;
    std::uint32_t slotCount = 0;
    std::uint32_t mapId = 0;
};

// Public prefix of the private per-mutator state. Generated root scopes only
// need these three fields, which lets their overwhelmingly common push/pop
// path stay inline without exposing the collector's remaining thread state.
struct RootThreadState {
    static constexpr std::size_t FastScopedPinCapacity = 1024;

    RootFrame* roots = nullptr;
    std::size_t rootSlotCount = 0;
    std::size_t rootSnapshotCapacity = 0;
    // Generated receiver pins are strict LIFO scopes. Publishing their raw,
    // already-resolved addresses here lets moving phases exclude the owning
    // regions after the mutator handshake without a vector mutation on every
    // Haxe method entry/exit. Each pin still owns an exact RootScope.
    void* fastScopedPins[FastScopedPinCapacity]{};
    std::size_t fastScopedPinCount = 0;
    // Native libraries are allowed to call managed code synchronously.  Most
    // CFFI entry points explicitly suspend their blocking state, but generated
    // function prologues also guard legacy/direct callback paths.  A collector
    // may treat a native-blocked thread as quiescent only while this depth is
    // zero.
    // Only the owning mutator reads nativeBlockedLocal. Keeping that common
    // generated-code check non-atomic avoids an acquire load on every Haxe
    // method/root scope while nativeBlocked remains the collector publication.
    bool nativeBlockedLocal = false;
    std::atomic<bool> nativeBlocked{false};
    std::atomic<std::size_t> managedExecutionDepth{0};
};

static_assert(sizeof(Ref) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<Ref>);
static_assert(sizeof(RefSlot) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<RefSlot>);
static_assert(alignof(ObjectHeader) == 16);
static_assert(sizeof(ObjectHeader) == 64);

} // namespace hx::gc

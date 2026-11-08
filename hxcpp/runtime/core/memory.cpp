#include <hxcpp.h>

#include <bit>
#include <cstring>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace {

std::mutex selectedMemoryMutex;
::hx::gc::Handle selectedMemoryHandle;

unsigned char* selectedAddress(int address, std::size_t width) {
    (void)width;
    if (__hxcpp_memory == nullptr || address < 0)
        throw std::out_of_range("no byte-array memory has been selected");
    return __hxcpp_memory + address;
}

class PinnedBytes final {
public:
    PinnedBytes(const ::Array<unsigned char>& input, int position,
                std::size_t width) {
        if (!input || position < 0)
            throw std::out_of_range("invalid byte-array access");

        auto* array = input.get();
        auto& runtime = ::hx::gc::Runtime::instance();
        const bool relocationActive = runtime.concurrentRelocationActive();
        if (relocationActive) {
            // A normal Young/Full collection cannot move this shell until the
            // mutator reaches a safepoint, and the scalar memcpy below has no
            // safepoint. Concurrent relocation is the only phase that can copy
            // it beside us, so retain the counted/ledger pin only there.
            arrayPin_.emplace(array);
            array = static_cast<::Array_obj<unsigned char>*>(
                arrayPin_->get());
        }
        if (static_cast<std::size_t>(position) + width >
            static_cast<std::size_t>(array->length))
            throw std::out_of_range("byte-array access exceeds bounds");

        ::hx::Object* buffer = bufferFor(array);
        if (buffer == nullptr && width != 0)
            throw std::out_of_range("byte-array has no storage");
        if (buffer == nullptr)
            return;

        if (array->capacity < 0) {
            // The descriptor of an unmanaged view may itself relocate. Its
            // native data pointer remains externally stable, but pin the
            // descriptor long enough to read that pointer during a concurrent
            // relocation cycle.
            if (relocationActive) {
                bufferPin_.emplace(buffer);
                buffer = bufferPin_->get();
            }
            data_ = static_cast<
                        ::hx::detail::ExternalArrayBuffer<unsigned char>*>(
                        static_cast<void*>(buffer))->elements +
                    position;
            return;
        }

        // Every managed scalar Array buffer has TypePinned from allocation, so
        // pinning it again for each Matrix4/Bytes float access only adds two
        // RootScope/ScopedPin lifetimes without improving address stability.
        data_ = ::hx::detail::arrayElements(
                    static_cast<::hx::detail::ArrayBuffer<unsigned char>*>(
                        static_cast<void*>(buffer))) +
                position;
    }

    PinnedBytes(const PinnedBytes&) = delete;
    PinnedBytes& operator=(const PinnedBytes&) = delete;

    unsigned char* data() const noexcept { return data_; }

private:
    static ::hx::Object* bufferFor(::hx::Object* arrayObject) {
        if (arrayObject == nullptr)
            return nullptr;
        auto* array = static_cast<::Array_obj<unsigned char>*>(arrayObject);
        return static_cast<::hx::Object*>(
            ::hx::gc::loadBarrierFast(array->buffer));
    }

    std::optional<::hx::ScopedPin> arrayPin_;
    std::optional<::hx::ScopedPin> bufferPin_;
    unsigned char* data_ = nullptr;
};

template<class T>
T loadValue(const ::Array<unsigned char>& bytes, int position) {
    static_assert(std::is_trivially_copyable_v<T>);
    PinnedBytes pinned(bytes, position, sizeof(T));
    T value{};
    std::memcpy(&value, pinned.data(), sizeof(T));
    return value;
}

template<class T>
void storeValue(const ::Array<unsigned char>& bytes, int position, T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    PinnedBytes pinned(bytes, position, sizeof(T));
    std::memcpy(pinned.data(), &value, sizeof(T));
}

} // namespace

unsigned char* __hxcpp_memory = nullptr;

void __hxcpp_memory_clear() {
    std::scoped_lock lock(selectedMemoryMutex);
    auto& runtime = ::hx::gc::Runtime::instance();
    if (selectedMemoryHandle.valid())
        runtime.releaseHandle(selectedMemoryHandle);
    selectedMemoryHandle = {};
    __hxcpp_memory = nullptr;
}

void __hxcpp_memory_select(const ::Array<unsigned char>& bytes) {
    if (!bytes) {
        __hxcpp_memory_clear();
        return;
    }
    ::hx::ScopedPin arrayPin(bytes.get());
    auto* array = static_cast<::Array_obj<unsigned char>*>(arrayPin.get());
    auto& runtime = ::hx::gc::Runtime::instance();
    void* storage = runtime.loadBarrier(array->buffer);
    ::hx::gc::Handle replacement;
    unsigned char* address = nullptr;
    if (storage != nullptr) {
        replacement = runtime.createHandle(storage,
                                            ::hx::gc::HandleKind::Pinned);
        auto* resolved = static_cast<::hx::detail::ArrayBuffer<unsigned char>*>(
            runtime.resolveHandle(replacement));
        address = ::hx::detail::arrayElements(resolved);
    }
    std::scoped_lock lock(selectedMemoryMutex);
    if (selectedMemoryHandle.valid())
        runtime.releaseHandle(selectedMemoryHandle);
    selectedMemoryHandle = replacement;
    __hxcpp_memory = address;
}

template<class T>
T selectedLoad(int address) {
    T value{};
    std::memcpy(&value, selectedAddress(address, sizeof(T)), sizeof(T));
    return value;
}

template<class T>
void selectedStore(int address, T value) {
    std::memcpy(selectedAddress(address, sizeof(T)), &value, sizeof(T));
}

int __hxcpp_memory_get_byte(int address) {
    return selectedLoad<std::uint8_t>(address);
}
int __hxcpp_memory_get_i16(int address) {
    return selectedLoad<std::int16_t>(address);
}
int __hxcpp_memory_get_i32(int address) {
    return selectedLoad<std::int32_t>(address);
}
int __hxcpp_memory_get_ui16(int address) {
    return selectedLoad<std::uint16_t>(address);
}
int __hxcpp_memory_get_ui32(int address) {
    return static_cast<int>(selectedLoad<std::uint32_t>(address));
}
float __hxcpp_memory_get_float(int address) {
    return selectedLoad<float>(address);
}
double __hxcpp_memory_get_double(int address) {
    return selectedLoad<double>(address);
}
float __hxcpp_memory_get_f32(int address) {
    return __hxcpp_memory_get_float(address);
}
void __hxcpp_memory_set_byte(int address, int value) {
    selectedStore<std::uint8_t>(address, static_cast<std::uint8_t>(value));
}
void __hxcpp_memory_set_i16(int address, int value) {
    selectedStore<std::int16_t>(address, static_cast<std::int16_t>(value));
}
void __hxcpp_memory_set_i32(int address, int value) {
    selectedStore<std::int32_t>(address, static_cast<std::int32_t>(value));
}
void __hxcpp_memory_set_ui16(int address, int value) {
    selectedStore<std::uint16_t>(address, static_cast<std::uint16_t>(value));
}
void __hxcpp_memory_set_ui32(int address, int value) {
    selectedStore<std::uint32_t>(address, static_cast<std::uint32_t>(value));
}
void __hxcpp_memory_set_float(int address, float value) {
    selectedStore<float>(address, value);
}
void __hxcpp_memory_set_double(int address, double value) {
    selectedStore<double>(address, value);
}
void __hxcpp_memory_set_f32(int address, float value) {
    __hxcpp_memory_set_float(address, value);
}

int __hxcpp_memory_get_byte(const ::Array<unsigned char>& bytes, int position) {
    return loadValue<std::uint8_t>(bytes, position);
}

int __hxcpp_memory_get_i16(const ::Array<unsigned char>& bytes, int position) {
    return loadValue<std::int16_t>(bytes, position);
}

int __hxcpp_memory_get_ui16(const ::Array<unsigned char>& bytes, int position) {
    return loadValue<std::uint16_t>(bytes, position);
}

int __hxcpp_memory_get_ui32(const ::Array<unsigned char>& bytes, int position) {
    return static_cast<int>(loadValue<std::uint32_t>(bytes, position));
}

float __hxcpp_memory_get_float(const ::Array<unsigned char>& bytes, int position) {
    return loadValue<float>(bytes, position);
}

double __hxcpp_memory_get_double(const ::Array<unsigned char>& bytes, int position) {
    return loadValue<double>(bytes, position);
}

void __hxcpp_memory_set_byte(const ::Array<unsigned char>& bytes, int position,
                             int value) {
    storeValue<std::uint8_t>(bytes, position, static_cast<std::uint8_t>(value));
}

void __hxcpp_memory_set_i16(const ::Array<unsigned char>& bytes, int position,
                            int value) {
    storeValue<std::int16_t>(bytes, position, static_cast<std::int16_t>(value));
}

void __hxcpp_memory_set_ui16(const ::Array<unsigned char>& bytes, int position,
                             int value) {
    storeValue<std::uint16_t>(bytes, position, static_cast<std::uint16_t>(value));
}

void __hxcpp_memory_set_i32(const ::Array<unsigned char>& bytes, int position,
                            int value) {
    storeValue<std::int32_t>(bytes, position, static_cast<std::int32_t>(value));
}

void __hxcpp_memory_set_ui32(const ::Array<unsigned char>& bytes, int position,
                             int value) {
    storeValue<std::uint32_t>(bytes, position, static_cast<std::uint32_t>(value));
}

void __hxcpp_memory_set_float(const ::Array<unsigned char>& bytes, int position,
                              float value) {
    storeValue<float>(bytes, position, value);
}

void __hxcpp_memory_set_double(const ::Array<unsigned char>& bytes, int position,
                               double value) {
    storeValue<double>(bytes, position, value);
}

void __hxcpp_memory_memset(const ::Array<unsigned char>& bytes, int position,
                           int length, int value) {
    if (length < 0)
        throw std::out_of_range("negative byte-array fill length");
    PinnedBytes pinned(bytes, position, static_cast<std::size_t>(length));
    if (length != 0)
        std::memset(pinned.data(), value, static_cast<std::size_t>(length));
}

int __hxcpp_reinterpret_float32_as_le_int32(float value) noexcept {
    return static_cast<int>(std::bit_cast<std::uint32_t>(value));
}

int __hxcpp_reinterpret_float64_as_le_int32_high(double value) noexcept {
    return static_cast<int>(std::bit_cast<std::uint64_t>(value) >> 32U);
}

int __hxcpp_reinterpret_float64_as_le_int32_low(double value) noexcept {
    return static_cast<int>(std::bit_cast<std::uint64_t>(value));
}

float __hxcpp_reinterpret_le_int32_as_float32(int value) noexcept {
    return std::bit_cast<float>(static_cast<std::uint32_t>(value));
}

double __hxcpp_reinterpret_le_int32s_as_float64(int low, int high) noexcept {
    const std::uint64_t bits =
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(high)) << 32U) |
        static_cast<std::uint32_t>(low);
    return std::bit_cast<double>(bits);
}

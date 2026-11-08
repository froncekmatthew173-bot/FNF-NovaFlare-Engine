#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace hx::detail {

// Match Haxe/hxcpp's bounded recursive Array formatting without sharing
// mutable state between application threads.  Dynamic arrays may legally
// contain themselves (directly or through another array); formatting such a
// graph must terminate instead of exhausting the native stack.
inline thread_local unsigned arrayStringDepth = 0;

class ArrayStringDepthScope final {
public:
    ArrayStringDepthScope() noexcept { ++arrayStringDepth; }
    ~ArrayStringDepthScope() { --arrayStringDepth; }

    ArrayStringDepthScope(const ArrayStringDepthScope&) = delete;
    ArrayStringDepthScope& operator=(const ArrayStringDepthScope&) = delete;
};

template<class T>
struct ArrayReferenceTraits final {
    static constexpr bool Managed = false;
};

// Haxe Array.pop/shift are nullable even when their element storage is an
// unboxed scalar.  Original hxcpp models this with ReturnNull specializations;
// keep the same language result while retaining compact scalar array storage.
template<class T>
struct ArrayNullableReturn final {
    // cpp.Int64 is an alias of std::int64_t and is therefore covered here.
    static constexpr bool Boxed = std::is_arithmetic_v<T>;
    using type = std::conditional_t<Boxed, ::Dynamic, T>;
};

template<class T>
struct ArrayReferenceTraits<::hx::ObjectPtr<T>> final {
    static constexpr bool Managed = true;
    static ::hx::gc::RefSlot& slot(::hx::ObjectPtr<T>& value) {
        return value.mPtr;
    }
    static void copyMetadata(::hx::ObjectPtr<T>&,
                             const ::hx::ObjectPtr<T>&) noexcept {}
};

template<>
struct ArrayReferenceTraits<::String> final {
    static constexpr bool Managed = true;
    static ::hx::gc::RefSlot& slot(::String& value) { return value.mPtr; }
    static void copyMetadata(::String& destination,
                             const ::String& source) noexcept {
        destination.length = source.length;
        // Generated global literal arrays are initialized before the runtime.
        // Those Strings intentionally have no managed payload and retain their
        // process-lifetime bytes in literal_.  Preserve that representation;
        // copying only length creates an invalid String whose view reads past
        // the empty fallback buffer.
        destination.literal_ = source.literal_;
    }
};

template<>
struct ArrayReferenceTraits<::Dynamic> final {
    static constexpr bool Managed = true;
    static ::hx::gc::RefSlot& slot(::Dynamic& value) { return value.mPtr; }
    static void copyMetadata(::Dynamic& destination,
                             const ::Dynamic& source) noexcept {
        destination.copyNonReferenceFrom(source);
    }
};

template<class T>
struct alignas(T) ArrayBuffer final {
    std::uint32_t capacity;
    // GC-visible logical prefix. Capacity is only an allocation detail:
    // bytes beyond the owning Array's length can retain reference-shaped
    // values after a buffer becomes snapshot-retained or orphaned.
    std::uint32_t traceLength;
    alignas(T) std::byte elements[sizeof(T)];
};

// A native producer can lend an Array a view of memory whose lifetime it
// controls (cpp.NativeArray.setUnmanagedData and ArrayBase.setUnmanagedData).
// Keep that pointer in a managed descriptor object so the Array's existing
// exact buffer edge remains the sole representation root.  Scalar views need
// no tracing; a reference-element view is still traced exactly while it is
// attached.  Mutating an unmanaged Array detaches it into ordinary managed
// storage before the write, which prevents native slots from bypassing the
// generational and concurrent barriers.
template<class T>
struct ExternalArrayBuffer final {
    T* elements = nullptr;
    std::uint32_t count = 0;
    std::uint32_t reserved = 0;
};

template<class T>
[[nodiscard]] T* arrayElements(ArrayBuffer<T>* buffer) noexcept {
    return reinterpret_cast<T*>(buffer->elements);
}

template<class T>
void setArrayTraceLength(ArrayBuffer<T>* buffer,
                         std::uint32_t length) noexcept {
    if (buffer == nullptr)
        return;
    std::atomic_ref<std::uint32_t>(buffer->traceLength).store(
        std::min(length, buffer->capacity), std::memory_order_release);
}

template<class T>
void growArrayTraceLength(ArrayBuffer<T>* buffer,
                          std::uint32_t required) noexcept {
    if (buffer == nullptr)
        return;
    required = std::min(required, buffer->capacity);
    std::atomic_ref<std::uint32_t> traceLength(buffer->traceLength);
    std::uint32_t observed = traceLength.load(std::memory_order_relaxed);
    while (observed < required &&
           !traceLength.compare_exchange_weak(
               observed, required, std::memory_order_release,
               std::memory_order_relaxed)) {
    }
}

template<class T>
void traceArrayBuffer(void* object, std::size_t payloadBytes,
                      ::hx::gc::TraceVisitor& visitor) {
    if constexpr (ArrayReferenceTraits<T>::Managed) {
        auto* buffer = static_cast<ArrayBuffer<T>*>(object);
        const std::size_t available =
            payloadBytes - offsetof(ArrayBuffer<T>, elements);
        const std::size_t physicalCapacity = available / sizeof(T);
        const std::size_t capacity =
            std::min<std::size_t>(buffer->capacity, physicalCapacity);
        const std::size_t traceLength = std::min<std::size_t>(
            std::atomic_ref<std::uint32_t>(buffer->traceLength).load(
                std::memory_order_acquire),
            capacity);
        T* elements = arrayElements(buffer);
        for (std::size_t index = 0; index < traceLength; ++index)
            visitor.visit(ArrayReferenceTraits<T>::slot(elements[index]));
    } else {
        (void)object;
        (void)payloadBytes;
        (void)visitor;
    }
}

template<class T>
void traceExternalArrayBuffer(void* object, std::size_t payloadBytes,
                              ::hx::gc::TraceVisitor& visitor) {
    if constexpr (ArrayReferenceTraits<T>::Managed) {
        if (payloadBytes < sizeof(ExternalArrayBuffer<T>))
            throw std::logic_error("external Array buffer payload is invalid");
        auto* buffer = static_cast<ExternalArrayBuffer<T>*>(object);
        if (buffer->elements == nullptr && buffer->count != 0)
            throw std::logic_error("external Array buffer pointer is null");
        for (std::uint32_t index = 0; index < buffer->count; ++index)
            visitor.visit(ArrayReferenceTraits<T>::slot(
                buffer->elements[index]));
    } else {
        (void)object;
        (void)payloadBytes;
        (void)visitor;
    }
}

template<class T>
inline const ::hx::gc::TypeDescriptor ArrayBufferDescriptor{
    0x4152524255460000ULL ^ static_cast<std::uint64_t>(sizeof(T)),
    "ArrayBuffer",
    nullptr,
    static_cast<std::uint32_t>(offsetof(ArrayBuffer<T>, elements) + sizeof(T)),
    static_cast<std::uint16_t>(alignof(ArrayBuffer<T>)),
    0,
    ::hx::gc::TypeVariableSize |
        (ArrayReferenceTraits<T>::Managed
             ? (::hx::gc::TypeContainsReferences |
                ::hx::gc::TypeReferenceArray)
             : ::hx::gc::TypeByteArray) |
        // cpp.Pointer can expose the backing address of every scalar Array,
        // not only BytesData. Allocate all non-reference buffers in pinned
        // space from birth so StringBuf's Array<cpp.Char>, vertex buffers and
        // audio sample arrays never need a late-pin whole-heap remap.
        (!ArrayReferenceTraits<T>::Managed
             ? ::hx::gc::TypePinned
             : ::hx::gc::TypeNone),
    0,
    nullptr,
    ArrayReferenceTraits<T>::Managed ? &traceArrayBuffer<T> : nullptr,
    nullptr,
};

template<class T>
inline const ::hx::gc::TypeDescriptor ExternalArrayBufferDescriptor{
    0x4558544152520000ULL ^ static_cast<std::uint64_t>(sizeof(T)),
    "ExternalArrayBuffer",
    nullptr,
    sizeof(ExternalArrayBuffer<T>),
    alignof(ExternalArrayBuffer<T>),
    0,
    ArrayReferenceTraits<T>::Managed ? ::hx::gc::TypeContainsReferences
                                     : ::hx::gc::TypeNone,
    0,
    nullptr,
    ArrayReferenceTraits<T>::Managed ? &traceExternalArrayBuffer<T> : nullptr,
    nullptr,
};

} // namespace hx::detail

template<class T>
class Array_obj final : public ::hx::Object {
public:
    using nova_array_element = T;
    ::hx::gc::RefSlot buffer{0};
    int length = 0;
    int capacity = 0;
    // Keep this public reflection surface in lockstep with original hxcpp's
    // ArrayBase::sArrayFields. Generated code deliberately falls back to
    // Dynamic field calls when an Array is passed through Dynamic.
    inline static const char* const dynamicFieldNames[24]{
        "length", "concat", "insert", "iterator", "keyValueIterator",
        "join", "copy", "pop", "push", "contains", "remove",
        "removeAt", "indexOf", "lastIndexOf", "reverse", "shift",
        "slice", "splice", "sort", "toString", "unshift", "filter",
        "map", "resize"};

    class ElementProxy final {
    public:
        ElementProxy(Array_obj* owner, int index) noexcept
            : owner_(owner), index_(index) {}
        ElementProxy& operator=(const T& value) {
            owner_->setCtx(nullptr, index_, value);
            return *this;
        }
        [[nodiscard]] operator T() const { return owner_->__get(index_); }
        T operator++(int) {
            T previous = owner_->__get(index_);
            owner_->setCtx(nullptr, index_, static_cast<T>(previous + 1));
            return previous;
        }
        T operator--(int) {
            T previous = owner_->__get(index_);
            owner_->setCtx(nullptr, index_, static_cast<T>(previous - 1));
            return previous;
        }
        ElementProxy& operator++() {
            owner_->setCtx(nullptr, index_,
                           static_cast<T>(owner_->__get(index_) + 1));
            return *this;
        }
        ElementProxy& operator--() {
            owner_->setCtx(nullptr, index_,
                           static_cast<T>(owner_->__get(index_) - 1));
            return *this;
        }

    private:
        Array_obj* owner_;
        int index_;
    };

    static const std::uint32_t referenceOffsets[1];
    static const ::hx::gc::TypeDescriptor descriptor;

    static ::Array<T> __new(int initialLength = 0) {
        if (initialLength < 0)
            throw std::length_error("negative Haxe Array length");
        ::Array<T> result = ::hx::allocate<Array_obj<T>>(descriptor);
        ::hx::gc::RefSlot* roots[]{&result.mPtr};
        ::hx::gc::RootScope rootScope(roots, 1);
        if (initialLength != 0) {
            ::hx::ScopedPin pin(result.get());
            auto* self = static_cast<Array_obj<T>*>(pin.get());
            self->ensureCapacityPinned(initialLength);
            self->length = initialLength;
            self->setTraceLengthPinned(initialLength);
        }
        return result;
    }

    static ::Array<T> fromData(const T* values, int count) {
        if (count < 0 || (count != 0 && values == nullptr))
            throw std::invalid_argument("invalid Haxe Array source data");
        ::Array<T> result = __new(count);
        for (int index = 0; index < count; ++index)
            result->setCtx(nullptr, index, values[index]);
        return result;
    }

    ::Array<T> init(int index, const T& value) {
        if constexpr (Traits::Managed) {
            ::hx::RootedValue<T> rootedValue(value);
            return initRooted(index, rootedValue.get());
        }
        return initRooted(index, value);
    }

    int push(const T& value) {
        if constexpr (Traits::Managed) {
            ::hx::RootedValue<T> rootedValue(value);
            return pushRooted(rootedValue.get());
        }
        return pushRooted(value);
    }

    // These names are emitted directly when Array remains statically typed;
    // the descriptor callbacks below cover the same protocol through Dynamic.
    [[nodiscard]] ::Dynamic __GetItem(int index) {
        return ::hx::boxDynamic(__get(index));
    }
    ::Dynamic __SetItem(int index, const ::Dynamic& value) {
        return ::hx::boxDynamic(setCtx(
            nullptr, index, ::hx::unboxDynamic<T>(value)));
    }
    [[nodiscard]] ::Dynamic iterator() {
        return ::hx::makeArrayIterator(this);
    }
    [[nodiscard]] ::Dynamic keyValueIterator() {
        return ::hx::makeArrayKeyValueIterator(this);
    }
    [[nodiscard]] ::Dynamic iterator_dyn() {
        return ::hx::makeArrayIteratorFactory(this);
    }
    [[nodiscard]] ::Dynamic keyValueIterator_dyn() {
        return ::hx::makeArrayKeyValueIteratorFactory(this);
    }
    [[nodiscard]] ::Dynamic push_dyn() {
        return ::hx::CreateMemberFunction1("push", this, &dynamicPush);
    }
    [[nodiscard]] ::Dynamic pop_dyn() {
        return ::hx::CreateMemberFunction0("pop", this, &dynamicPop);
    }

private:
    ::Array<T> initRooted(int index, const T& value) {
        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        self->setCtxPinned(index, value);
        return self;
    }

    int pushRooted(const T& value) {
        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        if constexpr (std::is_same_v<T, ::Dynamic>) {
            const auto* actual = ::hx::gc::Runtime::instance().typeOf(self);
            if (actual != &descriptor)
                return ::hx::virtualArrayPush(self, value);
        }
        self->ensureCapacityPinned(self->length + 1);
        self->storeAtPinned(self->length, value);
        ++self->length;
        self->setTraceLengthPinned(self->length);
        return self->length;
    }

public:

    void resize(int requestedLength) {
        if (requestedLength < 0)
            throw std::length_error("negative Haxe Array length");
        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        const int previousLength = self->length;
        if (requestedLength > previousLength) {
            const bool replacesStorage = requestedLength > self->capacity;
            self->ensureCapacityPinned(requestedLength);
            if constexpr (!Traits::Managed && std::is_arithmetic_v<T>) {
                // A replacement buffer comes from an all-zero NovaGC
                // payload and ensureCapacityPinned copies only the old
                // logical elements. Rewriting its complete new tail here
                // made Bytes.alloc walk image-sized UInt8 buffers through
                // storeAtPinned, keeping a RootScope active long enough to
                // delay a Young-GC handshake.
                if (!replacesStorage) {
                    // Existing capacity can contain values discarded by a
                    // prior scalar removeRange, so an in-place regrow must
                    // still restore Haxe's default-value semantics. A direct
                    // scalar fill needs no reference barrier and compiles to
                    // an efficient bulk clear for byte arrays.
                    T* currentElements = self->elements();
                    std::fill(currentElements + previousLength,
                              currentElements + requestedLength, T{});
                }
            } else {
                for (int index = previousLength; index < requestedLength;
                     ++index) {
                    self->storeAtPinned(index, T{});
                }
            }
        } else {
            if constexpr (!Traits::Managed && std::is_arithmetic_v<T>) {
                if (requestedLength < previousLength) {
                    // Resizing a lent native view is a mutation. Preserve the
                    // Array copy-on-write contract before clearing its tail.
                    if (self->capacity < 0)
                        self->ensureCapacityPinned(previousLength);
                    T* currentElements = self->elements();
                    std::fill(currentElements + requestedLength,
                              currentElements + previousLength, T{});
                }
            } else {
                for (int index = requestedLength; index < previousLength;
                     ++index) {
                    self->storeAtPinned(index, T{});
                }
            }
        }
        self->length = requestedLength;
        self->setTraceLengthPinned(requestedLength);
    }

    void blit(int destinationPosition, const ::Array<T>& source,
              int sourcePosition, int count) {
        if (destinationPosition < 0 || sourcePosition < 0 || count < 0 ||
            !source)
            throw std::out_of_range("invalid Haxe Array.blit range");

        ::hx::ScopedPin destinationArrayPin(this);
        auto* destinationArray = static_cast<Array_obj<T>*>(
            destinationArrayPin.get());
        ::hx::ScopedPin sourceArrayPin(source.get());
        auto* sourceArray = static_cast<Array_obj<T>*>(sourceArrayPin.get());
        if (sourcePosition + count < sourcePosition ||
            sourcePosition + count > sourceArray->length)
            throw std::out_of_range("invalid Haxe Array.blit range");

        const int required = destinationPosition + count;
        if (required < destinationPosition)
            throw std::length_error("Haxe Array.blit overflow");
        if (required > destinationArray->length)
            destinationArray->resize(required);
        if (count == 0)
            return;

        // blit mutates its destination even when it does not grow.  Detach a
        // lent native view before obtaining either storage pointer.
        if (destinationArray->capacity < 0)
            destinationArray->ensureCapacityPinned(destinationArray->length);

        Buffer* destinationBuffer = destinationArray->resolvedBuffer();
        void* sourceStorage = ::hx::gc::Runtime::instance().loadBarrier(sourceArray->buffer);
        if (destinationBuffer == nullptr || sourceStorage == nullptr)
            throw std::runtime_error("Haxe Array.blit missing storage");

        ::hx::ScopedPin destinationBufferPin(
            reinterpret_cast<::hx::Object*>(destinationBuffer));
        destinationBuffer = reinterpret_cast<Buffer*>(
            destinationBufferPin.get());
        ::hx::ScopedPin sourceBufferPin(
            reinterpret_cast<::hx::Object*>(sourceStorage));
        sourceStorage = sourceBufferPin.get();
        T* destinationElements = ::hx::detail::arrayElements(destinationBuffer);
        T* sourceElements = sourceArray->capacity < 0
            ? static_cast<ExternalBuffer*>(sourceStorage)->elements
            : ::hx::detail::arrayElements(static_cast<Buffer*>(sourceStorage));

        if constexpr (!Traits::Managed && std::is_trivially_copyable_v<T>) {
            // Bytes.blit/Image.scroll is a dominant real-world path.  The
            // previous element loop entered setCtx and created a Pinned GC
            // handle once per byte.  Both buffers are pinned here, so one
            // overlap-safe native copy is exact-GC safe.
            std::memmove(destinationElements + destinationPosition,
                         sourceElements + sourcePosition,
                         sizeof(T) * static_cast<std::size_t>(count));
        } else {
            const bool copyBackward =
                destinationElements == sourceElements &&
                destinationPosition > sourcePosition &&
                destinationPosition < sourcePosition + count;
            if (copyBackward) {
                for (int index = count - 1; index >= 0; --index) {
                    storeInto(destinationBuffer,
                              destinationElements[destinationPosition + index],
                              sourceElements[sourcePosition + index]);
                }
            } else {
                for (int index = 0; index < count; ++index) {
                    storeInto(destinationBuffer,
                              destinationElements[destinationPosition + index],
                              sourceElements[sourcePosition + index]);
                }
            }
        }
    }

    using NullableReturn =
        typename ::hx::detail::ArrayNullableReturn<T>::type;

    [[nodiscard]] NullableReturn pop() {
        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        if (self->length == 0)
            return NullableReturn{};
        const int index = self->length - 1;
        T result = self->elements()[index];
        if constexpr (Traits::Managed) {
            ::hx::gc::RefSlot* roots[]{&Traits::slot(result)};
            ::hx::gc::RootScope rootScope(roots, 1);
            self->storeAtPinned(index, T{});
            self->length = index;
            self->setTraceLengthPinned(index);
            return result;
        } else {
            self->storeAtPinned(index, T{});
            self->length = index;
            self->setTraceLengthPinned(index);
            if constexpr (::hx::detail::ArrayNullableReturn<T>::Boxed)
                return ::hx::boxDynamic(result);
            else
                return result;
        }
    }

    [[nodiscard]] NullableReturn shift() {
        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        if (self->length == 0)
            return NullableReturn{};
        T result = self->elements()[0];
        if constexpr (Traits::Managed) {
            ::hx::gc::RefSlot* roots[]{&Traits::slot(result)};
            ::hx::gc::RootScope rootScope(roots, 1);
            self->shiftLeftPinned();
            return result;
        } else {
            self->shiftLeftPinned();
            if constexpr (::hx::detail::ArrayNullableReturn<T>::Boxed)
                return ::hx::boxDynamic(result);
            else
                return result;
        }
    }

    void unshift(const T& value) { insert(0, value); }

    void insert(int position, const T& value) {
        if constexpr (Traits::Managed) {
            ::hx::RootedValue<T> rootedValue(value);
            insertRooted(position, rootedValue.get());
            return;
        }
        insertRooted(position, value);
    }

private:
    void insertRooted(int position, const T& value) {
        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        if (position < 0)
            position = std::max(0, self->length + position);
        if (position > self->length)
            position = self->length;
        self->ensureCapacityPinned(self->length + 1);
        for (int index = self->length; index > position; --index)
            self->storeAtPinned(index, self->elements()[index - 1]);
        self->storeAtPinned(position, value);
        ++self->length;
        self->setTraceLengthPinned(self->length);
    }

public:

    [[nodiscard]] int indexOf(const T& value,
                              ::hx::Null<int> fromIndex = nullptr) {
        int start = fromIndex.Default(0);
        if (start < 0)
            start = std::max(0, length + start);
        for (int index = start; index < length; ++index) {
            if (equalValues(__get(index), value))
                return index;
        }
        return -1;
    }

    [[nodiscard]] int lastIndexOf(const T& value,
                                  ::hx::Null<int> fromIndex = nullptr) {
        int start = fromIndex.isNull() ? length - 1 : fromIndex.Default(0);
        if (start < 0)
            start = length + start;
        start = std::min(start, length - 1);
        for (int index = start; index >= 0; --index) {
            if (equalValues(__get(index), value))
                return index;
        }
        return -1;
    }

    [[nodiscard]] bool contains(const T& value) {
        return indexOf(value) >= 0;
    }

    [[nodiscard]] bool remove(const T& value) {
        const int index = indexOf(value);
        if (index < 0)
            return false;
        (void)splice(index, 1);
        return true;
    }

    [[nodiscard]] bool removeAt(int index) {
        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        if (index < 0)
            index += self->length;
        if (index < 0 || index >= self->length)
            return false;
        // storeAtPinned performs the copy-on-write detach first when this is
        // an external native view, then applies the exact reference barrier.
        for (int cursor = index + 1; cursor < self->length; ++cursor)
            self->storeAtPinned(cursor - 1, self->elements()[cursor]);
        self->storeAtPinned(self->length - 1, T{});
        --self->length;
        self->setTraceLengthPinned(self->length);
        return true;
    }

    [[nodiscard]] ::Array<T> copy() {
        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        ::Array<T> result = __new();
        ::hx::gc::RefSlot* roots[]{&result.mPtr};
        ::hx::gc::RootScope rootScope(roots, 1);
        for (int index = 0; index < self->length; ++index)
            result->push(self->__get(index));
        return result;
    }

    [[nodiscard]] ::Array<T> concat(const ::Array<T>& other) {
        ::Array<T> result = copy();
        ::hx::gc::RefSlot* roots[]{&result.mPtr};
        ::hx::gc::RootScope rootScope(roots, 1);
        if (other) {
            for (int index = 0; index < other->length; ++index)
                result->push(other->__get(index));
        }
        return result;
    }

    [[nodiscard]] ::Array<T> slice(int position = 0,
                                   ::hx::Null<int> end = nullptr) {
        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        int begin = normalizeIndex(position, self->length);
        int finish = end.isNull() ? self->length :
            normalizeIndex(end.Default(0), self->length);
        finish = std::max(begin, finish);
        ::Array<T> result = __new();
        ::hx::gc::RefSlot* roots[]{&result.mPtr};
        ::hx::gc::RootScope rootScope(roots, 1);
        for (int index = begin; index < finish; ++index)
            result->push(self->__get(index));
        return result;
    }

    [[nodiscard]] ::Array<T> splice(int position, int count) {
        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        int begin = normalizeIndex(position, self->length);
        count = std::clamp(count, 0, self->length - begin);
        ::Array<T> removed = __new();
        ::hx::gc::RefSlot* roots[]{&removed.mPtr};
        ::hx::gc::RootScope rootScope(roots, 1);
        for (int index = 0; index < count; ++index)
            removed->push(self->elements()[begin + index]);
        for (int index = begin + count; index < self->length; ++index)
            self->storeAtPinned(index - count, self->elements()[index]);
        for (int index = self->length - count; index < self->length; ++index)
            self->storeAtPinned(index, T{});
        self->length -= count;
        self->setTraceLengthPinned(self->length);
        return removed;
    }

    void removeRange(int position, int count) {
        ::hx::ScopedPin arrayPin(this);
        auto* self = static_cast<Array_obj<T>*>(arrayPin.get());
        const int begin = normalizeIndex(position, self->length);
        count = std::clamp(count, 0, self->length - begin);
        if (count == 0)
            return;

        // Haxe's Array.removeRange discards the removed values.  Routing it
        // through splice allocated and populated a throw-away Array on every
        // call; OpenFL clears several render vectors this way every frame.
        // Scalar storage can be compacted directly, while reference storage
        // still uses the exact generational/concurrent store barrier.
        if constexpr (!Traits::Managed && std::is_trivially_copyable_v<T>) {
            if (self->capacity < 0)
                self->ensureCapacityPinned(self->length);
            Buffer* currentBuffer = self->resolvedBuffer();
            if (currentBuffer == nullptr)
                throw std::runtime_error("Haxe Array.removeRange missing storage");
            ::hx::ScopedPin bufferPin(
                reinterpret_cast<::hx::Object*>(currentBuffer));
            currentBuffer = reinterpret_cast<Buffer*>(bufferPin.get());
            T* data = ::hx::detail::arrayElements(currentBuffer);
            const int tail = self->length - (begin + count);
            if (tail > 0) {
                std::memmove(data + begin, data + begin + count,
                             sizeof(T) * static_cast<std::size_t>(tail));
            }
        } else {
            for (int index = begin + count; index < self->length; ++index)
                self->storeAtPinned(index - count, self->elements()[index]);
            for (int index = self->length - count; index < self->length;
                 ++index)
                self->storeAtPinned(index, T{});
        }
        self->length -= count;
        self->setTraceLengthPinned(self->length);
    }

    void reverse() {
        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        for (int left = 0, right = self->length - 1; left < right;
             ++left, --right) {
            T temporary = self->elements()[left];
            if constexpr (Traits::Managed) {
                ::hx::gc::RefSlot* roots[]{&Traits::slot(temporary)};
                ::hx::gc::RootScope rootScope(roots, 1);
                self->storeAtPinned(left, self->elements()[right]);
                self->storeAtPinned(right, temporary);
            } else {
                self->storeAtPinned(left, self->elements()[right]);
                self->storeAtPinned(right, temporary);
            }
        }
    }

    void sort(const ::Dynamic& comparator) {
        ::hx::ScopedPin arrayPin(this);
        auto* self = static_cast<Array_obj<T>*>(arrayPin.get());
        if (self->capacity < 0)
            self->ensureCapacityPinned(self->length);
        Buffer* currentBuffer = self->resolvedBuffer();
        if (currentBuffer == nullptr)
            return;
        ::hx::ScopedPin bufferPin(reinterpret_cast<::hx::Object*>(currentBuffer));
        currentBuffer = reinterpret_cast<Buffer*>(bufferPin.get());
        T* data = ::hx::detail::arrayElements(currentBuffer);
        for (int index = 1; index < self->length; ++index) {
            int cursor = index;
            while (cursor > 0) {
                ::Dynamic ordering = comparator(
                    ::hx::boxDynamic(data[cursor - 1]),
                    ::hx::boxDynamic(data[cursor]));
                if (ordering.Cast<int>() <= 0)
                    break;
                T temporary = data[cursor - 1];
                if constexpr (Traits::Managed) {
                    ::hx::gc::RefSlot* roots[]{&Traits::slot(temporary)};
                    ::hx::gc::RootScope rootScope(roots, 1);
                    storeInto(currentBuffer, data[cursor - 1], data[cursor]);
                    storeInto(currentBuffer, data[cursor], temporary);
                } else {
                    storeInto(currentBuffer, data[cursor - 1], data[cursor]);
                    storeInto(currentBuffer, data[cursor], temporary);
                }
                --cursor;
            }
        }
    }

    [[nodiscard]] ::Array<T> filter(const ::Dynamic& predicate) {
        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        ::Array<T> result = __new();
        ::hx::gc::RefSlot* roots[]{&result.mPtr};
        ::hx::gc::RootScope rootScope(roots, 1);
        for (int index = 0; index < self->length; ++index) {
            T value = self->__get(index);
            ::Dynamic accepted = predicate(::hx::boxDynamic(value));
            if (accepted.Cast<bool>())
                result->push(value);
        }
        return result;
    }

    [[nodiscard]] ::Array<::Dynamic> map(const ::Dynamic& transform) {
        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        ::Array<::Dynamic> result = ::Array_obj<::Dynamic>::__new();
        ::hx::gc::RefSlot* roots[]{&result.mPtr};
        ::hx::gc::RootScope rootScope(roots, 1);
        for (int index = 0; index < self->length; ++index)
            result->push(transform(::hx::boxDynamic(self->__get(index))));
        return result;
    }

    [[nodiscard]] ::String join(const ::String& separator) {
        if (::hx::detail::arrayStringDepth >= 5)
            return ::String("...");
        ::hx::detail::ArrayStringDepthScope stringDepthScope;
        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        if (self->length == 0)
            return ::String("");

        // Original hxcpp's Array<String>::join first computes/copies into one
        // destination buffer. Repeated managed String operator+ here was
        // quadratic: Serializer's ~133 KiB save string created a ladder of
        // large temporary strings, each in its own region. A native builder
        // retains the same linear-copy property while supporting every Array
        // element specialization through valueString().
        ::hx::RootedValue<::String> rootedSeparator(separator);
        std::string result;
        const std::size_t separatorBytes = static_cast<std::size_t>(
            std::max(0, rootedSeparator.get().length));
        if (self->length > 1 && separatorBytes != 0) {
            const std::size_t separators =
                static_cast<std::size_t>(self->length - 1);
            if (separators <=
                std::numeric_limits<std::size_t>::max() / separatorBytes) {
                result.reserve(separators * separatorBytes);
            }
        }
        for (int index = 0; index < self->length; ++index) {
            // Pure String arrays can otherwise execute this whole native
            // template loop without allocating until the final result.
            if ((index & 63) == 0)
                ::hx::safepoint();
            if (index != 0)
                result.append(rootedSeparator.get().view());
            ::hx::RootedValue<::String> part(valueString(self->__get(index)));
            if (part.get().isNull())
                result.append("null");
            else
                result.append(part.get().view());
        }
        return ::String(result);
    }

    [[nodiscard]] ::String toString() {
        return ::String("[") + join(::String(",")) + ::String("]");
    }

    T setCtx(::hx::Ctx* context, int index, const T& value) {
        (void)context;
        // Match hxcpp's Array_obj::Item contract: a negative indexed write is
        // discarded through a temporary null element.  It must not escape as
        // a native std::exception because Haxe code can legally treat this
        // operation as unspecified (and catch only Dynamic values).
        if (index < 0)
            return value;
        if constexpr (Traits::Managed) {
            auto& runtime = ::hx::gc::Runtime::instance();
            if (!runtime.concurrentRelocationActive()) {
                bool directArray = true;
                if constexpr (std::is_same_v<T, ::Dynamic>)
                    directArray = runtime.typeOf(this) == &descriptor;
                // This update cannot allocate, resize or reach a safepoint.
                // The caller's value therefore remains valid until the exact
                // buffer store barrier publishes it.  Avoid manufacturing a
                // one-slot RootFrame for every FFT/render-buffer write; the
                // slow path below still roots values across virtual dispatch,
                // copy-on-write detach, resize and active relocation.
                if (directArray && capacity >= 0 && index < length) {
                    storeAtPinned(index, value);
                    return value;
                }
            }
            ::hx::RootedValue<T> rootedValue(value);
            return setCtxRooted(index, rootedValue.get());
        }
        return setCtxRooted(index, value);
    }

private:
    T setCtxRooted(int index, const T& value) {
        auto& runtime = ::hx::gc::Runtime::instance();
        if (!runtime.concurrentRelocationActive()) {
            bool directArray = true;
            if constexpr (std::is_same_v<T, ::Dynamic>)
                directArray = runtime.typeOf(this) == &descriptor;
            // An in-bounds write cannot allocate or resize.  The buffer store
            // barrier itself handles the rare race where concurrent
            // relocation begins after the check, so avoid pinning the Array
            // and its region for the overwhelmingly common update-in-place
            // path (FFT and render buffers depend on this).
            if (directArray && capacity >= 0 && index < length) {
                storeAtPinned(index, value);
                return value;
            }
        }

        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        if constexpr (std::is_same_v<T, ::Dynamic>) {
            const auto* actual = ::hx::gc::Runtime::instance().typeOf(self);
            if (actual != &descriptor)
                return ::hx::virtualArraySet(self, index, value);
        }
        self->setCtxPinned(index, value);
        return value;
    }

public:

    [[nodiscard]] T __get(int index) {
        // ObjectPtr::operator-> has already applied the load barrier to the
        // Array itself.  A young collection cannot move it until this mutator
        // reaches a safepoint, and this fast read contains no safepoint.  Do
        // not take a real object/region pin for every ordinary Array read: GL
        // rendering performs millions of these and the pin-count atomics used
        // to dominate the frame.  Concurrent relocation is the exceptional
        // case where a copying worker can run beside the mutator, so retain
        // the full pin path while it is active.
        auto& runtime = ::hx::gc::Runtime::instance();
        if (!runtime.concurrentRelocationActive()) {
            bool directArray = true;
            if constexpr (std::is_same_v<T, ::Dynamic>)
                directArray = runtime.typeOf(this) == &descriptor;
            if (directArray) {
                if (index < 0 || index >= length)
                    return T{};
                T* currentElements = elements();
                return currentElements == nullptr ? T{}
                                                  : currentElements[index];
            }
        }

        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        if constexpr (std::is_same_v<T, ::Dynamic>) {
            const auto* actual = ::hx::gc::Runtime::instance().typeOf(self);
            if (actual != &descriptor)
                return ::hx::virtualArrayGet(self, index);
        }
        if (index < 0 || index >= self->length)
            return T{};
        T* currentElements = self->elements();
        return currentElements == nullptr ? T{} : currentElements[index];
    }

    [[nodiscard]] ElementProxy operator[](int index) {
        return ElementProxy(this, index);
    }
    [[nodiscard]] int get_length() const noexcept { return length; }
    [[nodiscard]] T* getBase() { return elements(); }

    [[nodiscard]] int memcmp(const ::Array<T>& other) {
        if (!other)
            return 1;
        const int common = std::min(length, other->length);
        for (int index = 0; index < common; ++index) {
            const T left = __get(index);
            const T right = other->__get(index);
            if (left < right)
                return -1;
            if (right < left)
                return 1;
        }
        return length < other->length ? -1 : (length > other->length ? 1 : 0);
    }

    void zero(const ::Dynamic& firstValue = ::Dynamic(),
              const ::Dynamic& countValue = ::Dynamic()) {
        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        int first = firstValue.isNull() ? 0 : firstValue.Cast<int>();
        if (first < 0)
            first += self->length;
        if (first < 0 || first >= self->length)
            return;
        int count = countValue.isNull() ? self->length
                                        : countValue.Cast<int>();
        if (count < 0)
            count += self->length;
        if (count < 0)
            return;
        count = std::min(count, self->length - first);
        for (int index = 0; index < count; ++index)
            self->storeAtPinned(first + index, T{});
    }

    void __unsafeStringReference(const ::String& value)
        requires std::is_same_v<T, unsigned char> {
        const auto bytes = value.view();
        resize(static_cast<int>(bytes.size()));
        for (int index = 0; index < length; ++index)
            setCtx(nullptr, index,
                static_cast<unsigned char>(bytes[static_cast<std::size_t>(index)]));
    }

    template<class PointerLike>
    void setUnmanagedData(const PointerLike& source, int count) {
        if (count < 0 || (count != 0 && source.get_raw() == nullptr))
            throw std::invalid_argument("invalid unmanaged Haxe Array data");
        ::hx::ScopedPin pin(this);
        auto* self = static_cast<Array_obj<T>*>(pin.get());
        using External = ::hx::detail::ExternalArrayBuffer<T>;
        auto& runtime = ::hx::gc::Runtime::instance();
        auto* external = static_cast<External*>(runtime.allocate(
            ::hx::detail::ExternalArrayBufferDescriptor<T>));
        external->elements = source.get_raw();
        external->count = static_cast<std::uint32_t>(count);
        runtime.publish(external);

        // A negative capacity is the Array-level external-view tag.  The
        // pointed-to bytes are not owned and are never reclaimed by the GC.
        // The next Haxe-side mutation performs a copy-on-write detach.
        self->capacity = -1;
        self->length = count;
        runtime.storeBarrier(self, self->buffer, external);
    }
    void reserve(int requestedCapacity) {
        if (requestedCapacity < 0)
            throw std::length_error("negative Haxe Array capacity");
        ::hx::ScopedPin pin(this);
        static_cast<Array_obj<T>*>(pin.get())->ensureCapacityPinned(
            requestedCapacity);
    }
    T set(int index, const T& value) { return setCtx(nullptr, index, value); }
    T __unsafe_set(int index, const T& value) {
        return setCtx(nullptr, index, value);
    }

    static bool dynamicIndexGet(void* object, int index, void* output) {
        auto* self = static_cast<Array_obj<T>*>(object);
        *static_cast<::Dynamic*>(output) = ::hx::boxDynamic(self->__get(index));
        return index >= 0 && index < self->length;
    }

    static bool dynamicIndexSet(void* object, int index, const void* input) {
        if (index < 0)
            return false;
        auto* self = static_cast<Array_obj<T>*>(object);
        const auto& value = *static_cast<const ::Dynamic*>(input);
        (void)self->setCtx(nullptr, index, ::hx::unboxDynamic<T>(value));
        return true;
    }

    static bool dynamicFieldGet(void* object, const char* name, int access,
                                void* output) {
        (void)access;
        auto* self = static_cast<Array_obj<T>*>(object);
        auto& result = *static_cast<::Dynamic*>(output);
        if (std::strcmp(name, "length") == 0) {
            result = ::Dynamic(self->length);
            return true;
        }
        if (std::strcmp(name, "iterator") == 0) {
            result = ::hx::makeArrayIteratorFactory(self);
            return true;
        }
        if (std::strcmp(name, "keyValueIterator") == 0) {
            result = ::hx::makeArrayKeyValueIteratorFactory(self);
            return true;
        }
        if (std::strcmp(name, "concat") == 0)
            result = ::hx::CreateMemberFunction1(name, self, &dynamicConcat);
        else if (std::strcmp(name, "insert") == 0)
            result = ::hx::CreateMemberFunction2(name, self, &dynamicInsert);
        else if (std::strcmp(name, "join") == 0)
            result = ::hx::CreateMemberFunction1(name, self, &dynamicJoin);
        else if (std::strcmp(name, "copy") == 0)
            result = ::hx::CreateMemberFunction0(name, self, &dynamicCopy);
        else if (std::strcmp(name, "pop") == 0)
            result = ::hx::CreateMemberFunction0(name, self, &dynamicPop);
        else if (std::strcmp(name, "push") == 0)
            result = ::hx::CreateMemberFunction1(name, self, &dynamicPush);
        else if (std::strcmp(name, "contains") == 0)
            result = ::hx::CreateMemberFunction1(name, self,
                                                  &dynamicContains);
        else if (std::strcmp(name, "remove") == 0)
            result = ::hx::CreateMemberFunction1(name, self, &dynamicRemove);
        else if (std::strcmp(name, "removeAt") == 0)
            result = ::hx::CreateMemberFunction1(name, self,
                                                  &dynamicRemoveAt);
        else if (std::strcmp(name, "indexOf") == 0)
            result = ::hx::CreateMemberFunction2(name, self,
                                                  &dynamicIndexOf);
        else if (std::strcmp(name, "lastIndexOf") == 0)
            result = ::hx::CreateMemberFunction2(name, self,
                                                  &dynamicLastIndexOf);
        else if (std::strcmp(name, "reverse") == 0)
            result = ::hx::CreateMemberFunction0(name, self, &dynamicReverse);
        else if (std::strcmp(name, "shift") == 0)
            result = ::hx::CreateMemberFunction0(name, self, &dynamicShift);
        else if (std::strcmp(name, "slice") == 0)
            result = ::hx::CreateMemberFunction2(name, self, &dynamicSlice);
        else if (std::strcmp(name, "splice") == 0)
            result = ::hx::CreateMemberFunction2(name, self, &dynamicSplice);
        else if (std::strcmp(name, "sort") == 0)
            result = ::hx::CreateMemberFunction1(name, self, &dynamicSort);
        else if (std::strcmp(name, "toString") == 0)
            result = ::hx::CreateMemberFunction0(name, self,
                                                  &dynamicToString);
        else if (std::strcmp(name, "unshift") == 0)
            result = ::hx::CreateMemberFunction1(name, self,
                                                  &dynamicUnshift);
        else if (std::strcmp(name, "filter") == 0)
            result = ::hx::CreateMemberFunction1(name, self, &dynamicFilter);
        else if (std::strcmp(name, "map") == 0)
            result = ::hx::CreateMemberFunction1(name, self, &dynamicMap);
        else if (std::strcmp(name, "resize") == 0 ||
                 std::strcmp(name, "__SetSize") == 0 ||
                 std::strcmp(name, "__SetSizeExact") == 0)
            result = ::hx::CreateMemberFunction1(name, self, &dynamicResize);
        else if (std::strcmp(name, "__unsafe_get") == 0)
            result = ::hx::CreateMemberFunction1(name, self,
                                                  &dynamicUnsafeGet);
        else if (std::strcmp(name, "__unsafe_set") == 0)
            result = ::hx::CreateMemberFunction2(name, self,
                                                  &dynamicUnsafeSet);
        else if (std::strcmp(name, "blit") == 0)
            result = ::hx::CreateMemberFunction4(name, self, &dynamicBlit);
        else if (std::strcmp(name, "zero") == 0)
            result = ::hx::CreateMemberFunction2(name, self, &dynamicZero);
        else if (std::strcmp(name, "memcmp") == 0)
            result = ::hx::CreateMemberFunction1(name, self, &dynamicMemcmp);
        else if (std::strcmp(name, "_hx_storeType") == 0)
            result = ::Dynamic(dynamicStoreType());
        else if (std::strcmp(name, "_hx_elementSize") == 0)
            result = ::Dynamic(static_cast<int>(sizeof(T)));
        else if (std::strcmp(name, "_hx_pointer") == 0)
            result = ::Dynamic::fromNativePointer(self->elements());
        else
            return false;
        return true;
    }

    static ::Dynamic dynamicConcat(::hx::Object* object,
                                   const ::Dynamic& other) {
        return ::hx::boxDynamic(static_cast<Array_obj<T>*>(object)->concat(
            other.Cast<::Array<T>>()));
    }
    static ::Dynamic dynamicInsert(::hx::Object* object,
                                   const ::Dynamic& position,
                                   const ::Dynamic& value) {
        static_cast<Array_obj<T>*>(object)->insert(
            position.Cast<int>(), ::hx::unboxDynamic<T>(value));
        return ::Dynamic();
    }
    static ::Dynamic dynamicJoin(::hx::Object* object,
                                 const ::Dynamic& separator) {
        return ::hx::boxDynamic(static_cast<Array_obj<T>*>(object)->join(
            separator.Cast<::String>()));
    }
    static ::Dynamic dynamicCopy(::hx::Object* object) {
        return ::hx::boxDynamic(static_cast<Array_obj<T>*>(object)->copy());
    }
    static ::Dynamic dynamicPop(::hx::Object* object) {
        return ::hx::boxDynamic(static_cast<Array_obj<T>*>(object)->pop());
    }
    static ::Dynamic dynamicPush(::hx::Object* object,
                                 const ::Dynamic& value) {
        return ::Dynamic(static_cast<Array_obj<T>*>(object)->push(
            ::hx::unboxDynamic<T>(value)));
    }
    static ::Dynamic dynamicContains(::hx::Object* object,
                                     const ::Dynamic& value) {
        return ::Dynamic(static_cast<Array_obj<T>*>(object)->contains(
            ::hx::unboxDynamic<T>(value)));
    }
    static ::Dynamic dynamicRemove(::hx::Object* object,
                                   const ::Dynamic& value) {
        return ::Dynamic(static_cast<Array_obj<T>*>(object)->remove(
            ::hx::unboxDynamic<T>(value)));
    }
    static ::Dynamic dynamicRemoveAt(::hx::Object* object,
                                     const ::Dynamic& index) {
        return ::Dynamic(static_cast<Array_obj<T>*>(object)->removeAt(
            index.Cast<int>()));
    }
    static ::Dynamic dynamicIndexOf(::hx::Object* object,
                                    const ::Dynamic& value,
                                    const ::Dynamic& fromIndex) {
        auto* self = static_cast<Array_obj<T>*>(object);
        const T typed = ::hx::unboxDynamic<T>(value);
        return ::Dynamic(fromIndex.isNull()
            ? self->indexOf(typed)
            : self->indexOf(typed, fromIndex.Cast<int>()));
    }
    static ::Dynamic dynamicLastIndexOf(::hx::Object* object,
                                        const ::Dynamic& value,
                                        const ::Dynamic& fromIndex) {
        auto* self = static_cast<Array_obj<T>*>(object);
        const T typed = ::hx::unboxDynamic<T>(value);
        return ::Dynamic(fromIndex.isNull()
            ? self->lastIndexOf(typed)
            : self->lastIndexOf(typed, fromIndex.Cast<int>()));
    }
    static ::Dynamic dynamicReverse(::hx::Object* object) {
        static_cast<Array_obj<T>*>(object)->reverse();
        return ::Dynamic();
    }
    static ::Dynamic dynamicShift(::hx::Object* object) {
        return ::hx::boxDynamic(static_cast<Array_obj<T>*>(object)->shift());
    }
    static ::Dynamic dynamicSlice(::hx::Object* object,
                                  const ::Dynamic& position,
                                  const ::Dynamic& end) {
        auto* self = static_cast<Array_obj<T>*>(object);
        const int begin = position.isNull() ? 0 : position.Cast<int>();
        return ::hx::boxDynamic(end.isNull()
            ? self->slice(begin)
            : self->slice(begin, end.Cast<int>()));
    }
    static ::Dynamic dynamicSplice(::hx::Object* object,
                                   const ::Dynamic& position,
                                   const ::Dynamic& count) {
        return ::hx::boxDynamic(static_cast<Array_obj<T>*>(object)->splice(
            position.Cast<int>(), count.Cast<int>()));
    }
    static ::Dynamic dynamicSort(::hx::Object* object,
                                 const ::Dynamic& comparator) {
        static_cast<Array_obj<T>*>(object)->sort(comparator);
        return ::Dynamic();
    }
    static ::Dynamic dynamicToString(::hx::Object* object) {
        return ::hx::boxDynamic(static_cast<Array_obj<T>*>(object)->toString());
    }
    static ::Dynamic dynamicUnshift(::hx::Object* object,
                                    const ::Dynamic& value) {
        static_cast<Array_obj<T>*>(object)->unshift(
            ::hx::unboxDynamic<T>(value));
        return ::Dynamic();
    }
    static ::Dynamic dynamicFilter(::hx::Object* object,
                                   const ::Dynamic& predicate) {
        return ::hx::boxDynamic(
            static_cast<Array_obj<T>*>(object)->filter(predicate));
    }
    static ::Dynamic dynamicMap(::hx::Object* object,
                                const ::Dynamic& transform) {
        return ::hx::boxDynamic(
            static_cast<Array_obj<T>*>(object)->map(transform));
    }
    static ::Dynamic dynamicResize(::hx::Object* object,
                                   const ::Dynamic& length) {
        static_cast<Array_obj<T>*>(object)->resize(length.Cast<int>());
        return ::Dynamic();
    }
    static ::Dynamic dynamicUnsafeGet(::hx::Object* object,
                                      const ::Dynamic& index) {
        return ::hx::boxDynamic(static_cast<Array_obj<T>*>(object)->__get(
            index.Cast<int>()));
    }
    static ::Dynamic dynamicUnsafeSet(::hx::Object* object,
                                      const ::Dynamic& index,
                                      const ::Dynamic& value) {
        return ::hx::boxDynamic(static_cast<Array_obj<T>*>(object)->setCtx(
            nullptr, index.Cast<int>(), ::hx::unboxDynamic<T>(value)));
    }
    static ::Dynamic dynamicBlit(::hx::Object* object,
                                 const ::Dynamic& destination,
                                 const ::Dynamic& source,
                                 const ::Dynamic& sourcePosition,
                                 const ::Dynamic& count) {
        static_cast<Array_obj<T>*>(object)->blit(
            destination.Cast<int>(), source.Cast<::Array<T>>(),
            sourcePosition.Cast<int>(), count.Cast<int>());
        return ::Dynamic();
    }
    static ::Dynamic dynamicZero(::hx::Object* object,
                                 const ::Dynamic& first,
                                 const ::Dynamic& count) {
        static_cast<Array_obj<T>*>(object)->zero(first, count);
        return ::Dynamic();
    }
    static ::Dynamic dynamicMemcmp(::hx::Object* object,
                                   const ::Dynamic& other) {
        return ::Dynamic(static_cast<Array_obj<T>*>(object)->memcmp(
            other.Cast<::Array<T>>()));
    }

    static constexpr int dynamicStoreType() noexcept {
        // Original hxcpp's ArrayTraits defaults to arrayObject and specializes
        // exactly these element categories.
        if constexpr (std::is_same_v<T, int>)
            return 4; // arrayInt
        else if constexpr (std::is_same_v<T, float> ||
                           std::is_same_v<T, double>)
            return 5; // arrayFloat
        else if constexpr (std::is_same_v<T, ::String>)
            return 6; // arrayString
        else if constexpr (std::is_same_v<T, std::int64_t>)
            return 8; // arrayInt64
        else
            return 7; // arrayObject
    }

private:
    using Buffer = ::hx::detail::ArrayBuffer<T>;
    using ExternalBuffer = ::hx::detail::ExternalArrayBuffer<T>;
    using Traits = ::hx::detail::ArrayReferenceTraits<T>;

    static int normalizeIndex(int index, int currentLength) {
        if (index < 0)
            return std::max(0, currentLength + index);
        return std::min(index, currentLength);
    }

    static bool equalValues(const T& left, const T& right) {
        if constexpr (std::is_same_v<T, ::Dynamic>)
            return ::hx::IsEq(left, right);
        else
            return left == right;
    }

    static ::String valueString(const T& value) {
        if constexpr (std::is_same_v<T, ::String>)
            return value;
        else if constexpr (std::is_same_v<T, ::Dynamic>)
            return value.toString();
        else if constexpr (std::is_same_v<T, bool>)
            return ::String(value ? "true" : "false");
        else if constexpr (std::is_integral_v<T>)
            return ::String(std::to_string(static_cast<long long>(value)));
        else if constexpr (std::is_floating_point_v<T>)
            return ::String(static_cast<double>(value));
        else
            return ::Dynamic(value).toString();
    }

    [[nodiscard]] Buffer* resolvedBuffer() {
        return static_cast<Buffer*>(::hx::gc::Runtime::instance().loadBarrier(buffer));
    }

    [[nodiscard]] T* elements() {
        void* current = ::hx::gc::Runtime::instance().loadBarrier(buffer);
        if (current == nullptr)
            return nullptr;
        if (capacity < 0)
            return static_cast<ExternalBuffer*>(current)->elements;
        return ::hx::detail::arrayElements(static_cast<Buffer*>(current));
    }

    void ensureCapacityPinned(int required) {
        if (required <= capacity)
            return;
        const int maximum = std::numeric_limits<int>::max() /
                            static_cast<int>(sizeof(T));
        if (required > maximum)
            throw std::length_error("Haxe Array capacity overflow");
        const int nextCapacity = std::max(required, std::max(4, capacity * 2));
        const std::size_t payloadBytes = offsetof(Buffer, elements) +
                                         sizeof(T) *
                                             static_cast<std::size_t>(nextCapacity);

        auto& runtime = ::hx::gc::Runtime::instance();
        auto* replacement = static_cast<Buffer*>(runtime.allocateVariable(
            ::hx::detail::ArrayBufferDescriptor<T>, payloadBytes));
        replacement->capacity = static_cast<std::uint32_t>(nextCapacity);
        ::hx::detail::setArrayTraceLength(replacement, 0);
        T* replacementElements = ::hx::detail::arrayElements(replacement);
        if constexpr (!Traits::Managed && std::is_arithmetic_v<T>) {
            // Runtime::allocateVariable guarantees a fresh all-zero payload:
            // newly committed pages are zero-filled and recycled Regions are
            // cleared before reuse. Placement default-initialization starts
            // each scalar's lifetime without rewriting those bytes. The old
            // value-initialization loop compiled to a full-buffer `rep stos`;
            // synchronous Lime image loads therefore zeroed every UInt8 image
            // buffer twice and could keep the UI mutator away from a Young-GC
            // handshake for tens of milliseconds.
            for (int index = 0; index < nextCapacity; ++index)
                ::new (static_cast<void*>(replacementElements + index)) T;
        } else {
            for (int index = 0; index < nextCapacity; ++index)
                ::new (static_cast<void*>(replacementElements + index)) T();
        }
        runtime.publish(replacement);

        T* previousElements = elements();
        for (int index = 0; index < length; ++index)
            storeInto(replacement, replacementElements[index],
                      previousElements[index]);
        ::hx::detail::setArrayTraceLength(
            replacement, static_cast<std::uint32_t>(length));

        runtime.storeBarrier(this, buffer, replacement);
        capacity = nextCapacity;
    }

    void storeAtPinned(int index, const T& value) {
        if (capacity < 0)
            ensureCapacityPinned(std::max(length, index + 1));
        Buffer* owner = resolvedBuffer();
        storeInto(owner, ::hx::detail::arrayElements(owner)[index], value);
        ::hx::detail::growArrayTraceLength(
            owner, static_cast<std::uint32_t>(index + 1));
    }

    void setCtxPinned(int index, const T& value) {
        ensureCapacityPinned(index + 1);
        storeAtPinned(index, value);
        if (index >= length)
            length = index + 1;
        setTraceLengthPinned(length);
    }

    void shiftLeftPinned() {
        for (int index = 1; index < length; ++index)
            storeAtPinned(index - 1, elements()[index]);
        storeAtPinned(length - 1, T{});
        --length;
        setTraceLengthPinned(length);
    }

    void setTraceLengthPinned(int logicalLength) {
        if (capacity < 0)
            return;
        ::hx::detail::setArrayTraceLength(
            resolvedBuffer(), static_cast<std::uint32_t>(
                                  std::max(logicalLength, 0)));
    }

    static void storeInto(Buffer* owner, T& destination, const T& source) {
        if constexpr (Traits::Managed) {
            auto& sourceSlot = const_cast<::hx::gc::RefSlot&>(Traits::slot(
                const_cast<T&>(source)));
            void* object = ::hx::gc::Runtime::instance().loadBarrier(sourceSlot);
            ::hx::gc::Runtime::instance().storeBarrier(
                owner, Traits::slot(destination), object);
            Traits::copyMetadata(destination, source);
        } else {
            destination = source;
        }
    }
};

template<class T>
const std::uint32_t Array_obj<T>::referenceOffsets[1]{
    ::hx::fieldOffset(&Array_obj<T>::buffer),
};

template<class T>
const ::hx::gc::TypeDescriptor Array_obj<T>::descriptor{
    0x41525241594f424aULL ^ static_cast<std::uint64_t>(sizeof(T)),
    "Array",
    nullptr,
    sizeof(Array_obj<T>),
    alignof(Array_obj<T>),
    0,
    ::hx::gc::TypeContainsReferences | ::hx::gc::TypeArray,
    1,
    Array_obj<T>::referenceOffsets,
    nullptr,
    nullptr,
    &Array_obj<T>::dynamicFieldGet,
    nullptr,
    &Array_obj<T>::dynamicIndexGet,
    &Array_obj<T>::dynamicIndexSet,
    24,
    Array_obj<T>::dynamicFieldNames,
};

template<class T>
[[nodiscard]] inline T _hx_array_unsafe_get(const ::Array<T>& array,
                                             int index) {
    return array->__get(index);
}

template<class T>
inline void _hx_array_set_size_exact(const ::Array<T>& array, int size) {
    if (!array)
        throw std::runtime_error("resize of null Haxe Array");
    array->resize(size);
}

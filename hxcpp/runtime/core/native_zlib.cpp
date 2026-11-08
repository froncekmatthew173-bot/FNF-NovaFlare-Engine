#include <hxcpp.h>

#include <zlib.h>

#include <memory>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct InflateState final {
    z_stream stream{};
    int flush = Z_NO_FLUSH;
    bool ended = false;
    std::mutex mutex;
};

struct DeflateState final {
    z_stream stream{};
    int flush = Z_NO_FLUSH;
    bool ended = false;
    std::mutex mutex;
};

std::mutex inflateStatesMutex;
std::vector<std::unique_ptr<InflateState>> inflateStates;
std::mutex deflateStatesMutex;
std::vector<std::unique_ptr<DeflateState>> deflateStates;

class PinnedByteBuffer final {
public:
    explicit PinnedByteBuffer(const ::Array<unsigned char>& bytes)
        : arrayPin_(bytes ? bytes.get() : nullptr),
          bufferPin_(bufferFor(arrayPin_.get())) {
        if (!bytes)
            throw std::invalid_argument("null byte array passed to zlib");
        auto* array = static_cast<::Array_obj<unsigned char>*>(arrayPin_.get());
        length_ = array->length;
        // zlib only borrows this address for the lifetime of the current
        // inflate/deflate call. A persistent Pinned handle isolates the
        // buffer with a stop-the-world heap remap; doing that for every image
        // chunk caused thousands of pin handshakes while Freeplay loaded.
        // ScopedPin already publishes the address to NovaGC and makes moving
        // collectors retain its containing Region for exactly this scope.
        auto* pinned = static_cast<::hx::detail::ArrayBuffer<unsigned char>*>(
            static_cast<void*>(bufferPin_.get()));
        data_ = pinned == nullptr ? nullptr : ::hx::detail::arrayElements(pinned);
    }

    PinnedByteBuffer(const PinnedByteBuffer&) = delete;
    PinnedByteBuffer& operator=(const PinnedByteBuffer&) = delete;

    [[nodiscard]] unsigned char* data() const noexcept { return data_; }
    [[nodiscard]] int length() const noexcept { return length_; }

private:
    static ::hx::Object* bufferFor(::hx::Object* arrayObject) {
        if (arrayObject == nullptr)
            return nullptr;
        auto* array = static_cast<::Array_obj<unsigned char>*>(arrayObject);
        return static_cast<::hx::Object*>(
            ::hx::gc::Runtime::instance().loadBarrier(array->buffer));
    }

    ::hx::ScopedPin arrayPin_;
    ::hx::ScopedPin bufferPin_;
    unsigned char* data_ = nullptr;
    int length_ = 0;
};

InflateState* stateFrom(const ::Dynamic& value) {
    auto* state = static_cast<InflateState*>(value.nativePointer());
    if (state == nullptr)
        throw std::runtime_error("invalid zlib inflate handle");
    std::scoped_lock guard(inflateStatesMutex);
    for (const auto& candidate : inflateStates) {
        if (candidate.get() == state)
            return state;
    }
    throw std::runtime_error("unknown zlib inflate handle");
}

DeflateState* deflateStateFrom(const ::Dynamic& value) {
    auto* state = static_cast<DeflateState*>(value.nativePointer());
    if (state == nullptr)
        throw std::runtime_error("invalid zlib deflate handle");
    std::scoped_lock guard(deflateStatesMutex);
    for (const auto& candidate : deflateStates) {
        if (candidate.get() == state)
            return state;
    }
    throw std::runtime_error("unknown zlib deflate handle");
}

InflateState* findInflateState(void* pointer) {
    std::scoped_lock guard(inflateStatesMutex);
    for (const auto& candidate : inflateStates)
        if (candidate.get() == pointer)
            return candidate.get();
    return nullptr;
}

DeflateState* findDeflateState(void* pointer) {
    std::scoped_lock guard(deflateStatesMutex);
    for (const auto& candidate : deflateStates)
        if (candidate.get() == pointer)
            return candidate.get();
    return nullptr;
}

[[noreturn]] void zlibFailure(const char* operation, int status,
                              const z_stream& stream) {
    std::string message(operation);
    message += " failed (zlib status ";
    message += std::to_string(status);
    message += ')';
    if (stream.msg != nullptr) {
        message += ": ";
        message += stream.msg;
    }
    throw std::runtime_error(message);
}

void validatePosition(const char* name, int position, int length) {
    if (position < 0 || position > length)
        throw std::out_of_range(std::string("invalid ") + name +
                                " zlib buffer position");
}

} // namespace

::Dynamic _hx_inflate_init(::Dynamic windowBits) {
    auto state = std::make_unique<InflateState>();
    const int bits = windowBits.isNull() ? 15 : static_cast<int>(windowBits);
    const int status = bits == 15
        ? inflateInit(&state->stream)
        : inflateInit2(&state->stream, bits);
    if (status != Z_OK)
        zlibFailure("inflateInit", status, state->stream);

    InflateState* raw = state.get();
    std::scoped_lock guard(inflateStatesMutex);
    inflateStates.push_back(std::move(state));
    return ::Dynamic::fromNativePointer(raw);
}

::Dynamic _hx_inflate_buffer(::Dynamic handle,
    const ::Array<unsigned char>& source, int sourcePosition,
    const ::Array<unsigned char>& destination, int destinationPosition) {
    InflateState* state = stateFrom(handle);
    std::scoped_lock stateGuard(state->mutex);
    if (state->ended)
        throw std::runtime_error("inflate called after close");

    PinnedByteBuffer input(source);
    PinnedByteBuffer output(destination);
    validatePosition("source", sourcePosition, input.length());
    validatePosition("destination", destinationPosition, output.length());

    const int inputAvailable = input.length() - sourcePosition;
    const int outputAvailable = output.length() - destinationPosition;
    state->stream.next_in = inputAvailable == 0
        ? Z_NULL : input.data() + sourcePosition;
    state->stream.avail_in = static_cast<uInt>(inputAvailable);
    state->stream.next_out = outputAvailable == 0
        ? Z_NULL : output.data() + destinationPosition;
    state->stream.avail_out = static_cast<uInt>(outputAvailable);

    const int status = inflate(&state->stream, state->flush);
    if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR)
        zlibFailure("inflate", status, state->stream);

    const int read = inputAvailable - static_cast<int>(state->stream.avail_in);
    const int written = outputAvailable - static_cast<int>(state->stream.avail_out);
    return ::Dynamic(::hx::Anon_obj::Create(3)
        ->setFixed(0, ::String("done"), status == Z_STREAM_END)
        ->setFixed(1, ::String("read"), read)
        ->setFixed(2, ::String("write"), written));
}

void _hx_inflate_end(::Dynamic handle) {
    InflateState* state = stateFrom(handle);
    std::scoped_lock guard(state->mutex);
    if (state->ended)
        return;
    const int status = inflateEnd(&state->stream);
    state->ended = true;
    if (status != Z_OK)
        zlibFailure("inflateEnd", status, state->stream);
}

::Dynamic _hx_deflate_init(int level) {
    auto state = std::make_unique<DeflateState>();
    const int status = deflateInit(&state->stream, level);
    if (status != Z_OK)
        zlibFailure("deflateInit", status, state->stream);

    DeflateState* raw = state.get();
    std::scoped_lock guard(deflateStatesMutex);
    deflateStates.push_back(std::move(state));
    return ::Dynamic::fromNativePointer(raw);
}

::Dynamic _hx_deflate_buffer(::Dynamic handle,
    const ::Array<unsigned char>& source, int sourcePosition,
    const ::Array<unsigned char>& destination, int destinationPosition) {
    DeflateState* state = deflateStateFrom(handle);
    std::scoped_lock stateGuard(state->mutex);
    if (state->ended)
        throw std::runtime_error("deflate called after close");

    PinnedByteBuffer input(source);
    PinnedByteBuffer output(destination);
    validatePosition("source", sourcePosition, input.length());
    validatePosition("destination", destinationPosition, output.length());

    const int inputAvailable = input.length() - sourcePosition;
    const int outputAvailable = output.length() - destinationPosition;
    state->stream.next_in = inputAvailable == 0
        ? Z_NULL : input.data() + sourcePosition;
    state->stream.avail_in = static_cast<uInt>(inputAvailable);
    state->stream.next_out = outputAvailable == 0
        ? Z_NULL : output.data() + destinationPosition;
    state->stream.avail_out = static_cast<uInt>(outputAvailable);

    const int status = deflate(&state->stream, state->flush);
    if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR)
        zlibFailure("deflate", status, state->stream);

    const int read = inputAvailable - static_cast<int>(state->stream.avail_in);
    const int written = outputAvailable - static_cast<int>(state->stream.avail_out);
    return ::Dynamic(::hx::Anon_obj::Create(3)
        ->setFixed(0, ::String("done"), status == Z_STREAM_END)
        ->setFixed(1, ::String("read"), read)
        ->setFixed(2, ::String("write"), written));
}

void _hx_deflate_end(::Dynamic handle) {
    DeflateState* state = deflateStateFrom(handle);
    std::scoped_lock guard(state->mutex);
    if (state->ended)
        return;
    const int status = deflateEnd(&state->stream);
    state->ended = true;
    if (status != Z_OK)
        zlibFailure("deflateEnd", status, state->stream);
}

int _hx_deflate_bound(::Dynamic handle, int sourceLength) {
    if (sourceLength < 0)
        throw std::out_of_range("negative zlib source length");
    DeflateState* state = deflateStateFrom(handle);
    std::scoped_lock guard(state->mutex);
    if (state->ended)
        throw std::runtime_error("deflate bound requested after close");
    const uLong bound = deflateBound(&state->stream,
                                     static_cast<uLong>(sourceLength));
    if (bound > static_cast<uLong>(std::numeric_limits<int>::max()))
        throw std::overflow_error("zlib deflate bound exceeds Haxe Int");
    return static_cast<int>(bound);
}

void _hx_zip_set_flush_mode(::Dynamic handle, ::String mode) {
    const std::string value(mode.view());
    int flush = Z_NO_FLUSH;
    if (value == "NO")
        flush = Z_NO_FLUSH;
    else if (value == "SYNC")
        flush = Z_SYNC_FLUSH;
    else if (value == "FULL")
        flush = Z_FULL_FLUSH;
    else if (value == "FINISH")
        flush = Z_FINISH;
#ifdef Z_BLOCK
    else if (value == "BLOCK")
        flush = Z_BLOCK;
#endif
    else
        throw std::invalid_argument("unknown zlib flush mode: " + value);

    void* pointer = handle.nativePointer();
    if (auto* state = findInflateState(pointer)) {
        std::scoped_lock guard(state->mutex);
        if (state->ended)
            throw std::runtime_error("flush mode changed after inflate close");
        state->flush = flush;
        return;
    }
    if (auto* state = findDeflateState(pointer)) {
        std::scoped_lock guard(state->mutex);
        if (state->ended)
            throw std::runtime_error("flush mode changed after deflate close");
        state->flush = flush;
        return;
    }
    throw std::runtime_error("unknown zlib stream handle");
}

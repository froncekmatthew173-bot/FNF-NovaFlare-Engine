#include <hxcpp.h>

#include <array>
#include <cstdint>
#include <iostream>

class ArrayNode final : public ::hx::Object {
public:
    int value = 0;
    explicit ArrayNode(int inValue) : value(inValue) {}

    static const ::hx::gc::TypeDescriptor descriptor;
};

const ::hx::gc::TypeDescriptor ArrayNode::descriptor{
    0x4152524e4f444531ULL, "ArrayNode", nullptr,
    sizeof(ArrayNode), alignof(ArrayNode), 0,
    ::hx::gc::TypeNone, 0, nullptr, nullptr, nullptr,
};

int main() {
    __hxcpp_nova_initialize();
    {
        HX_NOVAGC_VARI(::Array<int>, integers) = ::Array_obj<int>::__new();
        HX_NOVAGC_VARI(::Array<Dynamic>, objects) =
            ::Array_obj<Dynamic>::__new();
        HX_NOVAGC_VARI(::Array<int>, negativeWrites) =
            ::Array_obj<int>::__new();
        negativeWrites->push(7);
        negativeWrites->setCtx(nullptr, -1, 99);
        if (negativeWrites->length != 1 || negativeWrites->__get(0) != 7) {
            std::cerr << "negative Array write was not discarded\n";
            return 12;
        }
        const double floatingData[]{1.25, 2.5};
        HX_NOVAGC_VARI(::Array<double>, floating) =
            ::Array_obj<double>::fromData(floatingData, 2);
        HX_NOVAGC_VARI(::cpp::VirtualArray, virtualized) = nullptr;
        virtualized = floating;
        if (!virtualized || virtualized->length != 2 ||
            virtualized->__get(0).Cast<double>() != 1.25 ||
            virtualized->__get(1).Cast<double>() != 2.5) {
            std::cerr << "typed Array to VirtualArray conversion failed\n";
            return 1;
        }
        HX_NOVAGC_VARI(::Dynamic, boxedFloating) = Dynamic(floating);
        HX_NOVAGC_VARI(::cpp::VirtualArray, rawVirtualized) =
            boxedFloating.Cast<::cpp::VirtualArray>();
        if (!rawVirtualized || rawVirtualized->length != 2 ||
            rawVirtualized->__get(0).Cast<double>() != 1.25 ||
            rawVirtualized->__get(1).Cast<double>() != 2.5) {
            std::cerr << "raw typed Array VirtualArray access used the wrong stride\n";
            return 1;
        }
        if (rawVirtualized->push(Dynamic(3.75)) != 3 ||
            floating->length != 3 || floating->__get(2) != 3.75) {
            std::cerr << "raw typed Array VirtualArray write used the wrong stride\n";
            return 1;
        }

        // hxvlc lends the LibVLC audio callback memory to BytesData with a
        // logical length measured in sample frames, then asks OpenAL to read
        // length * frameSize bytes.  setUnmanagedData must therefore remain a
        // zero-copy native view; copying only the logical element count makes
        // every multi-byte audio chunk truncated.
        unsigned char nativeAudio[32]{};
        for (int index = 0; index < 32; ++index)
            nativeAudio[index] = static_cast<unsigned char>(index + 1);
        HX_NOVAGC_VARI(::Array<unsigned char>, audioView) =
            ::Array_obj<unsigned char>::__new();
        audioView->setUnmanagedData(
            ::cpp::Pointer<unsigned char>(nativeAudio), 8);
        if (audioView->getBase() != nativeAudio || audioView->length != 8 ||
            audioView->getBase()[31] != 32) {
            std::cerr << "unmanaged audio Array did not retain its native view\n";
            return 2;
        }
        auto audioPointer = ::cpp::Pointer_obj::arrayElem(audioView, 0);
        if (audioPointer.get_raw() != nativeAudio ||
            audioPointer[0] != 1 || audioPointer[7] != 8) {
            std::cerr << "Pointer.arrayElem returned the external descriptor "
                         "instead of its native data\n";
            return 2;
        }
        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        if (audioView->getBase() != nativeAudio ||
            audioView->getBase()[31] != 32 ||
            ::cpp::Pointer_obj::arrayElem(audioView, 0).get_raw() !=
                nativeAudio) {
            std::cerr << "unmanaged audio Array view failed after relocation\n";
            return 2;
        }
        unsigned char nativeMemory[16]{};
        HX_NOVAGC_VARI(::Array<unsigned char>, externalMemory) =
            ::Array_obj<unsigned char>::__new();
        externalMemory->setUnmanagedData(
            ::cpp::Pointer<unsigned char>(nativeMemory), 16);
        __hxcpp_memory_set_float(externalMemory, 4, 3.25F);
        bool nativeMemoryChanged = false;
        for (int index = 4; index < 8; ++index)
            nativeMemoryChanged = nativeMemoryChanged ||
                                  nativeMemory[index] != 0;
        if (__hxcpp_memory_get_float(externalMemory, 4) != 3.25F ||
            !nativeMemoryChanged) {
            std::cerr << "unmanaged byte-memory access used descriptor storage\n";
            return 16;
        }
        HX_NOVAGC_VARI(::Array<unsigned char>, resizedAudioView) =
            ::Array_obj<unsigned char>::__new();
        resizedAudioView->setUnmanagedData(
            ::cpp::Pointer<unsigned char>(nativeAudio), 8);
        resizedAudioView->resize(4);
        if (resizedAudioView->getBase() == nativeAudio ||
            resizedAudioView->length != 4 ||
            resizedAudioView->__get(0) != 1 ||
            resizedAudioView->__get(3) != 4 ||
            nativeAudio[4] != 5 || nativeAudio[7] != 8) {
            std::cerr << "scalar Array resize modified its external view\n";
            return 2;
        }
        // Writes detach into managed storage rather than modifying or
        // retaining an unbarriered native backing store.
        audioView->setCtx(nullptr, 0, static_cast<unsigned char>(99));
        if (audioView->getBase() == nativeAudio || audioView->__get(0) != 99 ||
            nativeAudio[0] != 1) {
            std::cerr << "unmanaged audio Array copy-on-write detach failed\n";
            return 2;
        }

        // Generated static literal arrays are initialized before the GC and
        // therefore use String::literal_ instead of a managed payload.
        ::String preRuntimeLiteral;
        preRuntimeLiteral.literal_ = "story_mode";
        preRuntimeLiteral.length = 10;
        const ::String literalData[]{preRuntimeLiteral};
        HX_NOVAGC_VARI(::Array<::String>, literalValues) =
            ::Array_obj<::String>::fromData(literalData, 1);
        const ::String copiedLiteral = literalValues->__get(0);
        if (copiedLiteral != "story_mode" || copiedLiteral.length != 10 ||
            copiedLiteral.literal_ != preRuntimeLiteral.literal_) {
            std::cerr << "String Array lost pre-runtime literal metadata\n";
            return 2;
        }

        // Serializer/StringBuf joins thousands of fragments when FlxSave is
        // flushed. This must allocate one linear result, not a quadratic
        // ladder of increasingly large managed Strings. The latter also used
        // to starve the collector handshake once fragments crossed the large
        // object threshold.
        HX_NOVAGC_VARI(::Array<::String>, saveFragments) =
            ::Array_obj<::String>::__new();
        const std::string fragment(64, 'x');
        for (int index = 0; index < 4096; ++index)
            saveFragments->push(::String(fragment));
        saveFragments->push(nullptr);
        HX_NOVAGC_VARI(::String, serialized) =
            saveFragments->join(::String("|"));
        const std::size_t expectedSerializedBytes =
            4096U * fragment.size() + 4096U + 4U;
        if (serialized.length !=
                static_cast<int>(expectedSerializedBytes) ||
            !serialized.view().ends_with("|null")) {
            std::cerr << "large String Array join was truncated or malformed\n";
            return 11;
        }

        // A variable-capacity reference buffer can outlive the Array object
        // that most recently owned it for the remainder of a concurrent
        // snapshot. Bytes beyond the Array's logical length are not managed
        // edges even if they still contain old reference-shaped bits. Young
        // tracing must therefore use the buffer's published logical trace
        // length instead of its allocation capacity. The game crash that
        // motivated this regression had exactly 46 live String entries in a
        // capacity-64 buffer and failed on stale slot 46.
        HX_NOVAGC_VARI(::Array<::Dynamic>, boundedReferenceArray) =
            ::Array_obj<::Dynamic>::__new();
        constexpr int LogicalReferenceCount = 46;
        constexpr int PhysicalReferenceCount = 64;
        constexpr int StaleReferenceCount =
            PhysicalReferenceCount - LogicalReferenceCount;
        std::array<std::uint64_t, StaleReferenceCount> staleReferences{};
        std::array<::hx::gc::Handle, StaleReferenceCount> weakTail{};
        for (int index = 0; index < PhysicalReferenceCount; ++index) {
            auto value = ::hx::allocate<ArrayNode>(
                ArrayNode::descriptor, 10000 + index);
            boundedReferenceArray->push(value);
            if (index >= LogicalReferenceCount) {
                const int tail = index - LogicalReferenceCount;
                staleReferences[tail] =
                    boundedReferenceArray->getBase()[index].mPtr.load(
                        std::memory_order_acquire);
                weakTail[tail] =
                    ::hx::gc::Runtime::instance().createHandle(
                        value, ::hx::gc::HandleKind::Weak);
            }
        }
        boundedReferenceArray->resize(LogicalReferenceCount);
        for (int tail = 0; tail < StaleReferenceCount; ++tail) {
            // Model an orphaned/snapshot-retained backing buffer: stale bits
            // outside logical length are deliberately not published through
            // a write barrier because they are not language-level edges.
            boundedReferenceArray
                ->getBase()[LogicalReferenceCount + tail]
                .mPtr.store(staleReferences[tail],
                            std::memory_order_release);
        }
        (void)::hx::gc::Runtime::instance().collectYoung();
        for (int tail = 0; tail < StaleReferenceCount; ++tail) {
            if (::hx::gc::Runtime::instance().resolveHandle(
                    weakTail[tail]) != nullptr) {
                std::cerr << "Array GC traced stale storage beyond logical "
                             "length at tail index "
                          << tail << '\n';
                return 14;
            }
            ::hx::gc::Runtime::instance().releaseHandle(weakTail[tail]);
        }

        // StringBuf stores pending UTF-8 bytes in Array<cpp.Char>. Returning
        // a cpp.Pointer to that buffer used to late-pin it, which relocated
        // the buffer and remapped every edge in a multi-million-object game
        // heap. Replay serialization then appeared permanently frozen on the
        // results screen. Scalar buffers must be stable from allocation and
        // Pointer.arrayElem must not start a pin-isolation cycle.
        HX_NOVAGC_VARI(::Array<char>, replayChars) =
            ::Array_obj<char>::__new(1024 * 1024);
        void* replayBuffer = ::hx::gc::Runtime::instance().loadBarrier(
            replayChars->buffer);
        const auto* replayBufferType =
            ::hx::gc::Runtime::instance().typeOf(replayBuffer);
        if (replayBufferType == nullptr ||
            (replayBufferType->flags & ::hx::gc::TypePinned) == 0) {
            std::cerr << "scalar Array buffer was not pinned from birth\n";
            return 13;
        }
        const std::uint64_t isolatedBefore =
            ::hx::gc::Runtime::instance().heapStats().isolatedPinnedObjects;
        auto replayPointer = ::cpp::Pointer_obj::arrayElem(replayChars, 0);
        const std::uint64_t isolatedAfter =
            ::hx::gc::Runtime::instance().heapStats().isolatedPinnedObjects;
        if (replayPointer.get_raw() == nullptr ||
            isolatedAfter != isolatedBefore) {
            std::cerr << "scalar Pointer.arrayElem started late-pin isolation\n";
            return 13;
        }

        // Scalar Array storage begins in zeroed NovaGC pages. Capacity setup
        // must preserve Haxe value-initialization semantics without writing
        // the complete image-sized buffer a second time.
        constexpr int StartupImageBytes = 1024 * 1024;
        HX_NOVAGC_VARI(::Array<unsigned char>, startupImageBytes) =
            ::Array_obj<unsigned char>::__new(StartupImageBytes);
        for (int index : {0, 1, 4095, StartupImageBytes / 2,
                          StartupImageBytes - 1}) {
            if (startupImageBytes->__get(index) != 0) {
                std::cerr << "fresh scalar Array storage was not zeroed\n";
                return 15;
            }
        }
        __hxcpp_memory_set_double(startupImageBytes, 8, 123.5);
        if (__hxcpp_memory_get_double(startupImageBytes, 8) != 123.5) {
            std::cerr << "managed byte-memory access lost scalar storage\n";
            return 16;
        }
        startupImageBytes->setCtx(
            nullptr, StartupImageBytes - 1,
            static_cast<unsigned char>(0xa5));
        startupImageBytes->resize(StartupImageBytes * 2);
        if (startupImageBytes->__get(StartupImageBytes - 1) !=
                static_cast<unsigned char>(0xa5) ||
            startupImageBytes->__get(StartupImageBytes) != 0 ||
            startupImageBytes->__get(StartupImageBytes * 2 - 1) != 0) {
            std::cerr << "scalar Array growth lost data or zero semantics\n";
            return 15;
        }

        // Scalar removeRange intentionally avoids clearing the unreachable
        // physical tail. Regrowing inside the existing capacity must bulk
        // clear that stale range instead of exposing the removed bytes.
        HX_NOVAGC_VARI(::Array<unsigned char>, reusedImageBytes) =
            ::Array_obj<unsigned char>::__new(16);
        for (int index = 0; index < reusedImageBytes->length; ++index) {
            reusedImageBytes->setCtx(
                nullptr, index, static_cast<unsigned char>(index + 1));
        }
        reusedImageBytes->removeRange(4, 8);
        if (reusedImageBytes->length != 8 ||
            reusedImageBytes->__get(4) != 13 ||
            reusedImageBytes->__get(7) != 16) {
            std::cerr << "scalar Array removeRange setup was incorrect\n";
            return 15;
        }
        reusedImageBytes->resize(16);
        for (int index = 8; index < reusedImageBytes->length; ++index) {
            if (reusedImageBytes->__get(index) != 0) {
                std::cerr << "in-capacity scalar Array regrowth exposed stale "
                             "tail data\n";
                return 15;
            }
        }
        reusedImageBytes->resize(3);
        reusedImageBytes->resize(8);
        for (int index = 3; index < reusedImageBytes->length; ++index) {
            if (reusedImageBytes->__get(index) != 0) {
                std::cerr << "scalar Array shrink/regrow lost zero semantics\n";
                return 15;
            }
        }

        HX_NOVAGC_VARI(::Array<unsigned char>, sourceBytes) =
            ::Array_obj<unsigned char>::__new(4096);
        HX_NOVAGC_VARI(::Array<unsigned char>, destinationBytes) =
            ::Array_obj<unsigned char>::__new();
        for (int index = 0; index < sourceBytes->length; ++index)
            sourceBytes->setCtx(nullptr, index,
                                static_cast<unsigned char>(index & 0xff));
        destinationBytes->blit(0, sourceBytes, 0, sourceBytes->length);
        for (int index : {0, 1, 255, 2047, 4095}) {
            if (destinationBytes->__get(index) !=
                static_cast<unsigned char>(index & 0xff)) {
                std::cerr << "bulk byte Array.blit copied the wrong value\n";
                return 2;
            }
        }
        destinationBytes->blit(37, destinationBytes, 0, 2048);
        for (int index : {0, 1, 255, 1023, 2047}) {
            if (destinationBytes->__get(37 + index) !=
                static_cast<unsigned char>(index & 0xff)) {
                std::cerr << "overlapping byte Array.blit was not memmove-safe\n";
                return 2;
            }
        }

        for (int index = 0; index < 2048; ++index) {
            integers->push(index * 3);
            objects->push(Dynamic(::hx::allocate<ArrayNode>(
                ArrayNode::descriptor, index)));
        }

        __hxcpp_collect(true);
        __hxcpp_gc_compact();

        for (int index : {0, 1, 127, 1023, 2047}) {
            auto node = objects->__get(index).Cast<
                ::hx::ObjectPtr<ArrayNode>>();
            if (integers->__get(index) != index * 3 ||
                !node || node->value != index) {
                std::cerr << "precise Array failed after resize/relocation\n";
                return 3;
            }
        }

        Dynamic iterator = Dynamic(objects)
            .__Field(String("iterator"), ::hx::paccDynamic)();
        for (int index = 0; index < objects->length; ++index) {
            if (!iterator.__Field(String("hasNext"),
                                  ::hx::paccDynamic)().Cast<bool>()) {
                std::cerr << "dynamic Array iterator ended early\n";
                return 4;
            }
            Dynamic next = iterator.__Field(String("next"),
                                            ::hx::paccDynamic)();
            auto node = next.Cast<::hx::ObjectPtr<ArrayNode>>();
            if (!node || node->value != index) {
                std::cerr << "dynamic Array iterator returned wrong value\n";
                return 5;
            }
        }
        if (iterator.__Field(String("hasNext"),
                            ::hx::paccDynamic)().Cast<bool>()) {
            std::cerr << "dynamic Array iterator did not terminate\n";
            return 6;
        }

        // LoadingState.putPreload accepts an Array<String> through Dynamic
        // and invokes `contains` and `push` through __Field. Original hxcpp
        // exposes the complete ArrayBase method table; returning null for
        // either method caused the real Freeplay transition to fail with
        // "Dynamic value is not callable".
        HX_NOVAGC_VARI(::Array<::String>, preload) =
            ::Array_obj<::String>::__new();
        HX_NOVAGC_VARI(::Dynamic, dynamicPreload) = ::Dynamic(preload);
        HX_NOVAGC_VARI(::Dynamic, push) = dynamicPreload.__Field(
            String("push"), ::hx::paccDynamic);
        if (push(String("healthBar")).Cast<int>() != 1 ||
            !dynamicPreload.__Field(String("contains"),
                                    ::hx::paccDynamic)(
                String("healthBar")).Cast<bool>()) {
            std::cerr << "dynamic Array push/contains dispatch failed\n";
            return 7;
        }
        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        if (push(String("ready")).Cast<int>() != 2 ||
            dynamicPreload.__Field(String("indexOf"),
                                   ::hx::paccDynamic)(
                String("ready"), Dynamic()).Cast<int>() != 1 ||
            !dynamicPreload.__Field(String("removeAt"),
                                    ::hx::paccDynamic)(-1).Cast<bool>() ||
            preload->length != 1) {
            std::cerr << "dynamic Array closure failed after relocation\n";
            return 8;
        }

        HX_NOVAGC_VARI(::Dynamic, keyValues) = dynamicPreload
            .__Field(String("keyValueIterator"), ::hx::paccDynamic)();
        if (!keyValues.__Field(String("hasNext"),
                               ::hx::paccDynamic)().Cast<bool>()) {
            std::cerr << "dynamic Array key/value iterator ended early\n";
            return 9;
        }
        HX_NOVAGC_VARI(::Dynamic, pair) = keyValues.__Field(
            String("next"), ::hx::paccDynamic)();
        if (pair.__Field(String("key"), ::hx::paccDynamic).Cast<int>() != 0 ||
            pair.__Field(String("value"),
                         ::hx::paccDynamic).Cast<::String>() != "healthBar") {
            std::cerr << "dynamic Array key/value iterator returned wrong pair\n";
            return 10;
        }
    }
    __hxcpp_nova_shutdown();
    std::cout << "precise variable Array buffer test passed\n";
    return 0;
}

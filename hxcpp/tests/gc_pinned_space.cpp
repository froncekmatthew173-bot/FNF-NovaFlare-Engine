#include <hx/gc/runtime.hpp>

#include <cstdint>
#include <atomic>
#include <iostream>
#include <memory>
#include <new>
#include <thread>
#include <vector>

namespace {

struct Value final {
    std::uint64_t number = 0;
};

const ::hx::gc::TypeDescriptor MovableType{
    0x50494e4d4f564142ULL,
    "test.MovableValue",
    nullptr,
    sizeof(Value),
    alignof(Value),
    0,
    ::hx::gc::TypeNone,
    0,
    nullptr,
    nullptr,
    nullptr,
};

const ::hx::gc::TypeDescriptor PinnedType{
    0x50494e4e45445350ULL,
    "test.PinnedValue",
    nullptr,
    sizeof(Value),
    alignof(Value),
    0,
    ::hx::gc::TypePinned,
    0,
    nullptr,
    nullptr,
    nullptr,
};

[[nodiscard]] Value* allocateValue(const ::hx::gc::TypeDescriptor& type,
                                   std::uint64_t number) {
    auto& runtime = ::hx::gc::Runtime::instance();
    auto* value = ::new (runtime.allocate(type)) Value();
    value->number = number;
    runtime.publish(value);
    return value;
}

} // namespace

int main() {
    auto& runtime = ::hx::gc::Runtime::instance();
    try {
        ::hx::gc::RuntimeConfig config{};
        config.regionBytes = 64U * 1024U;
        config.tlabBytes = 16U * 1024U;
        config.maximumRegions = 16;
        config.initialTenuringThreshold = 1;
        config.maximumTenuringThreshold = 1;
        config.adaptiveTenuring = false;
        runtime.initialize(config);
        runtime.attachCurrentThread();

        Value* pinned = allocateValue(PinnedType, 11);
        Value* movable = allocateValue(MovableType, 22);
        void* pinnedAddress = pinned;
        ::hx::gc::RefSlot pinnedRoot{runtime.makeRef(pinned).bits()};
        ::hx::gc::RefSlot movableRoot{runtime.makeRef(movable).bits()};
        ::hx::gc::RefSlot* roots[]{&pinnedRoot, &movableRoot};
        {
            ::hx::gc::RootScope scope(roots, 2, 0x50494e31U);
            (void)runtime.collectYoung();
            movable = static_cast<Value*>(runtime.loadBarrier(movableRoot));
            void* movableBeforeCompact = movable;
            (void)runtime.collectBootstrap();
            const auto compact = runtime.compactBootstrap();
            pinned = static_cast<Value*>(runtime.loadBarrier(pinnedRoot));
            movable = static_cast<Value*>(runtime.loadBarrier(movableRoot));
            if (pinned != pinnedAddress || pinned->number != 11 ||
                movable == nullptr || movable->number != 22 ||
                movable == movableBeforeCompact ||
                compact.relocatedObjects == 0) {
                std::cerr << "dedicated pinned space moved or blocked compaction"
                          << " pinned_same=" << (pinned == pinnedAddress)
                          << " pinned_value=" << pinned->number
                          << " movable_nonnull=" << (movable != nullptr)
                          << " movable_value="
                          << (movable != nullptr ? movable->number : 0)
                          << " movable_changed="
                          << (movable != movableBeforeCompact)
                          << " relocated=" << compact.relocatedObjects
                          << '\n';
                return 1;
            }
        }

        // Container-owned ScopedPins/RootScopes are not guaranteed to be
        // destroyed in strict reverse order. Removing an older frame must not
        // drop a younger live root or leave the cached snapshot size stale.
        Value* firstOutOfOrder = allocateValue(MovableType, 41);
        Value* secondOutOfOrder = allocateValue(MovableType, 42);
        ::hx::gc::RefSlot firstSlot{
            runtime.makeRef(firstOutOfOrder).bits()};
        ::hx::gc::RefSlot secondSlot{
            runtime.makeRef(secondOutOfOrder).bits()};
        ::hx::gc::RefSlot* firstRoots[]{&firstSlot};
        ::hx::gc::RefSlot* secondRoots[]{&secondSlot};
        {
            ::hx::gc::RefSlot* reserveRoots[]{&firstSlot, &secondSlot};
            ::hx::gc::RootScope reserve(reserveRoots, 2, 0x50494e32U);
        }
        auto firstScope = std::make_unique<::hx::gc::RootScope>(
            firstRoots, 1, 0x50494e33U);
        auto secondScope = std::make_unique<::hx::gc::RootScope>(
            secondRoots, 1, 0x50494e34U);
        firstScope.reset();
        (void)runtime.collectBootstrap();
        secondOutOfOrder = static_cast<Value*>(
            runtime.loadBarrier(secondSlot));
        if (secondOutOfOrder == nullptr || secondOutOfOrder->number != 42) {
            std::cerr << "out-of-order root removal dropped a younger frame\n";
            return 1;
        }
        secondScope.reset();

        // Native async containers can transfer ownership to another attached
        // thread. Their RootScope must be removed from the creating mutator's
        // chain without racing a collector snapshot of that chain.
        struct TransferredRoot final {
            ::hx::gc::RefSlot slot{0};
            ::hx::gc::RefSlot* slots[1]{&slot};
            std::unique_ptr<::hx::gc::RootScope> scope;
        };
        std::vector<std::unique_ptr<TransferredRoot>> transferred;
        transferred.reserve(128);
        for (std::uint64_t index = 0; index < 128; ++index) {
            auto root = std::make_unique<TransferredRoot>();
            Value* value = allocateValue(MovableType, 1000 + index);
            root->slot.store(runtime.makeRef(value).bits(),
                             std::memory_order_release);
            root->scope = std::make_unique<::hx::gc::RootScope>(
                root->slots, 1,
                static_cast<std::uint32_t>(0x50495000U + index));
            transferred.push_back(std::move(root));
        }
        std::atomic<bool> releaseTransferred{false};
        std::thread foreignDestroyer([&] {
            runtime.attachCurrentThread();
            while (!releaseTransferred.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (std::size_t index = 0; index < transferred.size();
                 index += 2) {
                transferred[index]->scope.reset();
                std::this_thread::yield();
            }
            runtime.detachCurrentThread();
        });
        releaseTransferred.store(true, std::memory_order_release);
        for (int iteration = 0; iteration < 8; ++iteration)
            (void)runtime.collectYoung();
        foreignDestroyer.join();
        for (auto& root : transferred)
            root->scope.reset();

        // pinForNative intentionally has no managed handle lifetime. Its
        // counted native pin must therefore be a root in full as well as young
        // marking, not merely a relocation exclusion bit.
        Value* nativePinned = allocateValue(MovableType, 33);
        nativePinned = static_cast<Value*>(
            runtime.pinForNative(nativePinned));
        const auto full = runtime.collectBootstrap();
        if (full.markedObjects == 0 ||
            runtime.typeOf(nativePinned) != &MovableType ||
            nativePinned->number != 33) {
            std::cerr << "native pin was not treated as a full-GC root\n";
            return 1;
        }

        runtime.detachCurrentThread();
        runtime.shutdown();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "hxcpp pinned space and native-pin root test passed\n";
    return 0;
}

#include <hxcpp.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>

namespace {

struct Node final {
    ::hx::gc::RefSlot next{0};
    std::uint64_t value = 0;
};

constexpr std::uint32_t NodeReferences[]{
    static_cast<std::uint32_t>(offsetof(Node, next)),
};

const ::hx::gc::TypeDescriptor NodeType{
    0x594f554e474e4f44ULL,
    "test.YoungNode",
    nullptr,
    sizeof(Node),
    alignof(Node),
    0,
    ::hx::gc::TypeContainsReferences,
    1,
    NodeReferences,
    nullptr,
    nullptr,
};

struct ImplicitOwner final {
    ::hx::gc::RefSlot child{0};
};

constexpr std::uint32_t ImplicitOwnerReferences[]{
    static_cast<std::uint32_t>(offsetof(ImplicitOwner, child)),
};

const ::hx::gc::TypeDescriptor ImplicitOwnerType{
    0x594f554e47494d50ULL,
    "test.ImplicitOwner",
    nullptr,
    sizeof(ImplicitOwner),
    alignof(ImplicitOwner),
    0,
    ::hx::gc::TypeContainsReferences,
    1,
    ImplicitOwnerReferences,
    nullptr,
    nullptr,
};

std::atomic<std::uint64_t> finalized{0};

struct Finalizable final {
    std::uint64_t value = 0;
};

void finalize(void*) {
    finalized.fetch_add(1, std::memory_order_relaxed);
}

const ::hx::gc::TypeDescriptor FinalizableType{
    0x594f554e4746494eULL,
    "test.YoungFinalizable",
    nullptr,
    sizeof(Finalizable),
    alignof(Finalizable),
    0,
    ::hx::gc::TypeFinalizable,
    0,
    nullptr,
    nullptr,
    &finalize,
};

[[nodiscard]] Node* allocateNode(std::uint64_t value) {
    auto& runtime = ::hx::gc::Runtime::instance();
    void* storage = runtime.allocate(NodeType);
    auto* result = ::new (storage) Node();
    result->value = value;
    runtime.publish(result);
    return result;
}

[[nodiscard]] ImplicitOwner* allocateImplicitOwner() {
    auto& runtime = ::hx::gc::Runtime::instance();
    void* storage = runtime.allocate(ImplicitOwnerType);
    auto* result = ::new (storage) ImplicitOwner();
    runtime.publish(result);
    return result;
}

void allocateFinalizable(std::uint64_t value) {
    auto& runtime = ::hx::gc::Runtime::instance();
    void* storage = runtime.allocate(FinalizableType);
    auto* result = ::new (storage) Finalizable();
    result->value = value;
    runtime.publish(result);
}

void disableAutomaticGc() {
#ifdef _WIN32
    _putenv_s("HXCPP_NOVAGC_AUTO_GC", "0");
    _putenv_s("HXCPP_NOVAGC_TENURING_THRESHOLD", "1");
    _putenv_s("HXCPP_NOVAGC_MAX_TENURING_THRESHOLD", "1");
    _putenv_s("HXCPP_NOVAGC_ADAPTIVE_TENURING", "0");
#else
    setenv("HXCPP_NOVAGC_AUTO_GC", "0", 1);
    setenv("HXCPP_NOVAGC_TENURING_THRESHOLD", "1", 1);
    setenv("HXCPP_NOVAGC_MAX_TENURING_THRESHOLD", "1", 1);
    setenv("HXCPP_NOVAGC_ADAPTIVE_TENURING", "0", 1);
#endif
}

} // namespace

int main() {
    disableAutomaticGc();
    __hxcpp_nova_initialize();
    auto& runtime = ::hx::gc::Runtime::instance();

    // Promote the owner first, then create a genuine old-to-young edge.  The
    // following minor collection must discover the child through the dirty
    // old region and heal the owner's slot after evacuation.
    Node* owner = allocateNode(1);
    ::hx::gc::RefSlot ownerRoot{runtime.makeRef(owner).bits()};
    ::hx::gc::RefSlot* ownerRoots[]{&ownerRoot};
    {
        ::hx::gc::RootScope ownerScope(ownerRoots, 1, 0x594f5531U);
        const auto initial = runtime.collectYoung();
        owner = static_cast<Node*>(runtime.loadBarrier(ownerRoot));
        if (owner == nullptr || initial.promotedObjects == 0) {
            std::cerr << "initial nursery promotion lost its exact root\n";
            return 1;
        }

        Node* child = allocateNode(2);
        runtime.storeBarrier(owner, owner->next, child);
        for (std::uint64_t index = 0; index < 100'000; ++index)
            (void)allocateNode(index + 100);

        const auto remembered = runtime.collectYoung();
        owner = static_cast<Node*>(runtime.loadBarrier(ownerRoot));
        auto* retainedChild = static_cast<Node*>(
            runtime.loadBarrier(owner->next));
        if (retainedChild == nullptr || retainedChild->value != 2 ||
            remembered.rememberedRegions == 0 ||
            remembered.reclaimedRegions == 0) {
            std::cerr << "old-to-young remembered edge was not preserved\n";
            return 1;
        }
    }

    // A full collection may retain a mixed old region while reclaiming the
    // target region of a dead object's field.  The exact remembered-owner set
    // must discard that dead owner before its region can become dangling.
    Node* staleOwner = allocateNode(3001);
    Node* liveNeighbour = allocateNode(3002);
    ::hx::gc::RefSlot staleOwnerRoot{runtime.makeRef(staleOwner).bits()};
    ::hx::gc::RefSlot liveNeighbourRoot{
        runtime.makeRef(liveNeighbour).bits()};
    ::hx::gc::RefSlot* mixedRoots[]{&staleOwnerRoot, &liveNeighbourRoot};
    {
        ::hx::gc::RootScope mixedScope(mixedRoots, 2, 0x594f5535U);
        (void)runtime.collectYoung();
        staleOwner = static_cast<Node*>(runtime.loadBarrier(staleOwnerRoot));

        Node* doomedYoungTarget = allocateNode(3003);
        runtime.storeBarrier(staleOwner, staleOwner->next,
                             doomedYoungTarget);
        staleOwnerRoot.store(0, std::memory_order_release);

        const auto full = runtime.collectBootstrap();
        if (full.deadObjects == 0 || full.reclaimedRegions == 0) {
            std::cerr << "full collection did not build stale-card fixture\n";
            return 1;
        }

        Node* postFullYoung = allocateNode(3004);
        ::hx::gc::RefSlot postFullRoot{
            runtime.makeRef(postFullYoung).bits()};
        ::hx::gc::RefSlot* postFullRoots[]{&postFullRoot};
        {
            ::hx::gc::RootScope postFullScope(postFullRoots, 1,
                                               0x594f5536U);
            const auto afterFull = runtime.collectYoung();
            postFullYoung = static_cast<Node*>(
                runtime.loadBarrier(postFullRoot));
            if (postFullYoung == nullptr || postFullYoung->value != 3004 ||
                afterFull.rememberedRegions != 0) {
                std::cerr << "dead exact remembered owner was not pruned\n";
                return 1;
            }
        }
    }

    // Generated ObjectPtr/Dynamic assignment operators pass a null owner.
    // The runtime must infer the containing old region from the destination
    // slot address or a normal generated field update bypasses the card table.
    ImplicitOwner* implicit = allocateImplicitOwner();
    ::hx::gc::RefSlot implicitRoot{runtime.makeRef(implicit).bits()};
    ::hx::gc::RefSlot* implicitRoots[]{&implicitRoot};
    {
        ::hx::gc::RootScope implicitScope(implicitRoots, 1, 0x594f5534U);
        (void)runtime.collectYoung();
        implicit = static_cast<ImplicitOwner*>(
            runtime.loadBarrier(implicitRoot));
        Node* implicitChild = allocateNode(1234);
        runtime.storeBarrier(nullptr, implicit->child, implicitChild);
        const auto inferred = runtime.collectYoung();
        implicit = static_cast<ImplicitOwner*>(
            runtime.loadBarrier(implicitRoot));
        implicitChild = static_cast<Node*>(
            runtime.loadBarrier(implicit->child));
        if (implicitChild == nullptr || implicitChild->value != 1234 ||
            inferred.rememberedRegions == 0) {
            std::cerr << "implicit generated field owner missed its card\n";
            return 1;
        }
    }

    // Full relocation promotes its destination objects to old regions.  If a
    // relocated owner still points into a pinned young region, relocation
    // itself has created an old-to-young edge without executing a generated
    // store barrier.  The relocated owner must be rebuilt into the exact
    // remembered set before the next minor collection.
    Node* relocationOwner = allocateNode(4001);
    ::hx::gc::RefSlot relocationOwnerRoot{
        runtime.makeRef(relocationOwner).bits()};
    ::hx::gc::RefSlot* relocationOwnerRoots[]{&relocationOwnerRoot};
    {
        ::hx::gc::RootScope relocationOwnerScope(
            relocationOwnerRoots, 1, 0x594f5537U);

        // Force the child into a later region than its owner so pinning the
        // child does not also exclude the owner from the relocation set.
        for (std::uint64_t index = 0; index < 150'000; ++index)
            (void)allocateNode(index + 10'000);
        Node* relocationChild = allocateNode(4002);
        runtime.storeBarrier(relocationOwner, relocationOwner->next,
                             relocationChild);

        ::hx::gc::RefSlot relocationChildRoot{
            runtime.makeRef(relocationChild).bits()};
        ::hx::gc::RefSlot* relocationChildRoots[]{&relocationChildRoot};
        {
            ::hx::gc::RootScope relocationChildScope(
                relocationChildRoots, 1, 0x594f5538U);
            void* pinnedChild = runtime.acquireScopedPin(relocationChildRoot);
            const auto full = runtime.collectBootstrap();
            const auto moved = runtime.compactBootstrap();
            relocationOwner = static_cast<Node*>(
                runtime.loadBarrier(relocationOwnerRoot));
            if (full.markedObjects < 2 || moved.relocatedObjects == 0 ||
                relocationOwner == nullptr) {
                runtime.releaseScopedPin(pinnedChild);
                std::cerr << "full relocation did not build remembered-edge fixture\n";
                return 1;
            }
            runtime.releaseScopedPin(pinnedChild);
            relocationChildRoot.store(0, std::memory_order_release);

            const auto afterRelocation = runtime.collectYoung();
            relocationOwner = static_cast<Node*>(
                runtime.loadBarrier(relocationOwnerRoot));
            relocationChild = relocationOwner == nullptr
                ? nullptr
                : static_cast<Node*>(
                      runtime.loadBarrier(relocationOwner->next));
            if (relocationChild == nullptr ||
                relocationChild->value != 4002 ||
                afterRelocation.rememberedRegions == 0) {
                std::cerr << "relocation-created old-to-young edge was lost\n";
                return 1;
            }
        }
    }

    Node* weakObject = allocateNode(77);
    const auto weak = runtime.createHandle(
        weakObject, ::hx::gc::HandleKind::Weak);
    (void)runtime.collectYoung();
    if (runtime.resolveHandle(weak) != nullptr) {
        std::cerr << "minor collection retained an unreachable weak target\n";
        return 1;
    }
    runtime.releaseHandle(weak);

    // A live young chain must be evacuated transitively and every interior
    // reference healed before source regions are returned to the OS.
    Node* head = allocateNode(10);
    Node* tail = allocateNode(11);
    runtime.storeBarrier(head, head->next, tail);
    ::hx::gc::RefSlot chainRoot{runtime.makeRef(head).bits()};
    ::hx::gc::RefSlot* chainRoots[]{&chainRoot};
    {
        ::hx::gc::RootScope chainScope(chainRoots, 1, 0x594f5532U);
        const auto chain = runtime.collectYoung();
        head = static_cast<Node*>(runtime.loadBarrier(chainRoot));
        tail = static_cast<Node*>(runtime.loadBarrier(head->next));
        if (tail == nullptr || tail->value != 11 ||
            chain.promotedObjects < 2) {
            std::cerr << "transitive nursery evacuation failed\n";
            return 1;
        }
    }

    Node* pinned = allocateNode(99);
    ::hx::gc::RefSlot pinRoot{runtime.makeRef(pinned).bits()};
    ::hx::gc::RefSlot* pinRoots[]{&pinRoot};
    {
        ::hx::gc::RootScope pinScope(pinRoots, 1, 0x594f5533U);
        void* pinnedAddress = runtime.acquireScopedPin(pinRoot);
        (void)runtime.collectYoung();
        if (runtime.loadBarrier(pinRoot) != pinnedAddress) {
            std::cerr << "minor collection moved a native-pinned object\n";
            return 1;
        }
        runtime.releaseScopedPin(pinnedAddress);
    }

    constexpr std::uint64_t FinalizerCount = 4096;
    for (std::uint64_t index = 0; index < FinalizerCount; ++index)
        allocateFinalizable(index);
    const auto finalizerCycle = runtime.collectYoung();
    if (finalizerCycle.finalizers.completed != FinalizerCount ||
        finalized.load(std::memory_order_acquire) != FinalizerCount) {
        std::cerr << "minor collection did not commit finalizers before "
                     "resuming mutators\n";
        return 1;
    }
    if (runtime.drainFinalizers().completed != 0) {
        std::cerr << "minor collection left a deferred finalizer race\n";
        return 1;
    }

    __hxcpp_nova_shutdown();
    std::cout << "hxcpp nursery evacuation and remembered-set test passed\n";
    return 0;
}

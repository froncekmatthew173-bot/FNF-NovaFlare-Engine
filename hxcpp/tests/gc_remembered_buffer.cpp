#include <hx/gc/runtime.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

namespace {

struct Owner final {
    ::hx::gc::RefSlot child{0};
    std::uint64_t value = 0;
};

constexpr std::uint64_t DeadOldSentinel = 0xd34d0dd1ULL;
bool rejectDeadOldTrace = false;

void traceOwner(void* payload, std::size_t,
                ::hx::gc::TraceVisitor& visitor) {
    auto* owner = static_cast<Owner*>(payload);
    if (rejectDeadOldTrace && owner->value == DeadOldSentinel)
        throw std::logic_error(
            "remembered overflow scanned an unreachable old object");
    visitor.visit(owner->child);
}

const ::hx::gc::TypeDescriptor OwnerType{
    0x52454d4255464f57ULL,
    "test.RememberedOwner",
    nullptr,
    sizeof(Owner),
    alignof(Owner),
    0,
    ::hx::gc::TypeContainsReferences,
    0,
    nullptr,
    &traceOwner,
    nullptr,
};

struct Child final {
    std::uint64_t value = 0;
};

const ::hx::gc::TypeDescriptor ChildType{
    0x52454d4255464348ULL,
    "test.RememberedChild",
    nullptr,
    sizeof(Child),
    alignof(Child),
    0,
    ::hx::gc::TypeNone,
    0,
    nullptr,
    nullptr,
    nullptr,
};

template<class T>
[[nodiscard]] T* allocate(const ::hx::gc::TypeDescriptor& type,
                          std::uint64_t value) {
    auto& runtime = ::hx::gc::Runtime::instance();
    auto* result = ::new (runtime.allocate(type)) T();
    result->value = value;
    runtime.publish(result);
    return result;
}

} // namespace

int main() {
    auto& runtime = ::hx::gc::Runtime::instance();
    try {
        ::hx::gc::RuntimeConfig config{};
        config.regionBytes = 64U * 1024U;
        config.tlabBytes = 16U * 1024U;
        config.maximumRegions = 128;
        config.initialTenuringThreshold = 1;
        config.maximumTenuringThreshold = 1;
        config.adaptiveTenuring = false;
        config.rememberedSetFailureEvery = 1;
        runtime.initialize(config);
        runtime.attachCurrentThread();

        constexpr std::size_t Count = 256;
        Owner* deadOld = allocate<Owner>(OwnerType, DeadOldSentinel);
        ::hx::gc::RefSlot deadOldRoot{
            runtime.makeRef(deadOld).bits()};
        ::hx::gc::RefSlot* deadOldRoots[]{&deadOldRoot};
        std::vector<Owner*> owners;
        std::vector<::hx::gc::RefSlot> roots(Count);
        std::vector<::hx::gc::RefSlot*> rootPointers(Count);
        owners.reserve(Count);
        for (std::size_t index = 0; index < Count; ++index) {
            Owner* owner = allocate<Owner>(OwnerType, index);
            owners.push_back(owner);
            roots[index].store(runtime.makeRef(owner).bits(),
                               std::memory_order_release);
            rootPointers[index] = &roots[index];
        }
        {
            ::hx::gc::RootScope scope(
                rootPointers.data(), static_cast<std::uint32_t>(Count),
                0x52454d31U);
            {
                ::hx::gc::RootScope deadScope(
                    deadOldRoots, 1, 0x52454d32U);
                (void)runtime.collectYoung();
            }
            deadOldRoot.store(0, std::memory_order_release);
            rejectDeadOldTrace = true;
            for (std::size_t index = 0; index < Count; ++index) {
                owners[index] = static_cast<Owner*>(
                    runtime.loadBarrier(roots[index]));
                Child* child = allocate<Child>(ChildType, index + 1000U);
                runtime.storeBarrier(owners[index], owners[index]->child,
                                     child);
                // Repeated stores to the same exact field must hit the
                // per-Region act-once bit instead of publishing duplicate map
                // entries or consuming fault-injection attempts.
                runtime.storeBarrier(owners[index], owners[index]->child,
                                     child);
                runtime.storeBarrier(owners[index], owners[index]->child,
                                     child);
            }

            const auto recovered = runtime.collectYoung();
            rejectDeadOldTrace = false;
            if (recovered.rememberedRegions == 0 ||
                recovered.rememberedBitmapBytes == 0 ||
                recovered.actOnceRememberedStores < Count * 2U) {
                std::cerr << "remembered bitmap/act-once overflow recovery "
                             "was not exercised\n";
                return 1;
            }
            for (std::size_t index = 0; index < Count; ++index) {
                owners[index] = static_cast<Owner*>(
                    runtime.loadBarrier(roots[index]));
                auto* child = static_cast<Child*>(
                    runtime.loadBarrier(owners[index]->child));
                if (child == nullptr || child->value != index + 1000U) {
                    std::cerr << "overflow recovery lost exact edge "
                              << index << '\n';
                    return 1;
                }
            }

            // Once a full liveness epoch exists, an exact remembered-set
            // publication failure must rescan only full-proven live owners.
            // Promoting the entire nursery in place here caused NovaFlare's
            // first post-loading Young cycle to move roughly two million
            // objects into old generation and pause for more than 100 ms.
            runtime.startConcurrentMark();
            (void)runtime.finishConcurrentMark();
            for (std::size_t index = 0; index < Count; ++index) {
                owners[index] = static_cast<Owner*>(
                    runtime.loadBarrier(roots[index]));
                Child* child = allocate<Child>(ChildType, index + 2000U);
                runtime.storeBarrier(owners[index], owners[index]->child,
                                     child);
            }
            const auto fullEpochRecovered = runtime.collectYoung();
            if (!fullEpochRecovered.metadataDegraded ||
                fullEpochRecovered.inPlacePromotedRegions != 0) {
                std::cerr << "full-epoch remembered recovery promoted the "
                             "complete nursery: degraded="
                          << fullEpochRecovered.metadataDegraded
                          << " in_place="
                          << fullEpochRecovered.inPlacePromotedRegions << '\n';
                return 1;
            }
            for (std::size_t index = 0; index < Count; ++index) {
                owners[index] = static_cast<Owner*>(
                    runtime.loadBarrier(roots[index]));
                auto* child = static_cast<Child*>(
                    runtime.loadBarrier(owners[index]->child));
                if (child == nullptr || child->value != index + 2000U) {
                    std::cerr << "full-epoch overflow recovery lost edge "
                              << index << '\n';
                    return 1;
                }
            }

            // lastFullMarkEpoch only proves that an old owner was live at the
            // preceding Full. It may die before a later Region-wide overflow
            // recovery, while its unobserved young field is reclaimed and the
            // same address becomes the interior of another object. The
            // overflow visitor must reject that non-payload address before
            // calling the forwarding-pointer load barrier.
            Owner* staleAfterFull = owners.back();
            roots.back().store(0, std::memory_order_release);
            Child* interiorFixture =
                allocate<Child>(ChildType, 0x51515151U);
            ::hx::gc::RefSlot interiorFixtureRoot{
                runtime.makeRef(interiorFixture).bits()};
            ::hx::gc::RefSlot* interiorFixtureRoots[]{
                &interiorFixtureRoot};
            {
                ::hx::gc::RootScope interiorScope(
                    interiorFixtureRoots, 1, 0x52454d33U);
                void* interiorAddress =
                    static_cast<void*>(
                        reinterpret_cast<std::byte*>(interiorFixture) +
                        sizeof(std::uint64_t));
                staleAfterFull->child.store(
                    runtime.makeRef(interiorAddress).bits(),
                    std::memory_order_release);

                Owner* liveOverflowOwner = owners.front();
                Child* overflowChild =
                    allocate<Child>(ChildType, 0x61616161U);
                runtime.storeBarrier(
                    liveOverflowOwner, liveOverflowOwner->child,
                    overflowChild);
                const auto staleReferenceRecovered =
                    runtime.collectYoung();
                owners.front() = static_cast<Owner*>(
                    runtime.loadBarrier(roots.front()));
                auto* retainedOverflowChild = static_cast<Child*>(
                    runtime.loadBarrier(owners.front()->child));
                if (retainedOverflowChild == nullptr ||
                    retainedOverflowChild->value != 0x61616161U ||
                    staleReferenceRecovered
                            .recoveredRememberedOverflowRegions == 0) {
                    std::cerr << "overflow recovery did not reject a stale "
                                 "interior reference safely\n";
                    return 1;
                }
            }
        }

        runtime.detachCurrentThread();
        runtime.shutdown();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "hxcpp remembered-buffer overflow recovery test passed\n";
    return 0;
}

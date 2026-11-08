#include <hx/gc/runtime.hpp>

#include <cstdint>
#include <iostream>
#include <new>

namespace {

struct TailValue final {
    std::uint64_t value = 0;
};

const hx::gc::TypeDescriptor TailValueType{
    0x52454d41524b5041ULL,
    "test.ParallelRemarkTail",
    nullptr,
    sizeof(TailValue),
    alignof(TailValue),
    0,
    hx::gc::TypeNone,
    0,
    nullptr,
    nullptr,
    nullptr,
};

struct TailContext final {
    std::uint32_t count = 0;
    bool invoked = false;
};

void populateRemarkTail(void* opaque) {
    auto& context = *static_cast<TailContext*>(opaque);
    auto& runtime = hx::gc::Runtime::instance();
    for (std::uint32_t index = 0; index < context.count; ++index) {
        void* storage = runtime.allocate(TailValueType);
        auto* value = ::new (storage) TailValue();
        value->value = index;
        runtime.publish(value);
    }
    context.invoked = true;
}

} // namespace

int main() {
    auto& runtime = hx::gc::Runtime::instance();
    TailContext context{4096, false};
    hx::gc::RuntimeConfig config{};
    config.markWorkerCount = 4;
    config.remarkTailHook = populateRemarkTail;
    config.remarkTailHookContext = &context;
    runtime.initialize(config);
    runtime.attachCurrentThread();

    runtime.startConcurrentMark();
    const hx::gc::CollectionStats cycle = runtime.finishConcurrentMark();
    if (!context.invoked || cycle.remarkWorkers != 4 ||
        cycle.stolenMarkTasks == 0 || cycle.markedObjects < context.count) {
        std::cerr << "parallel work-stealing remark did not drain the tail: "
                  << "workers=" << cycle.remarkWorkers
                  << " stolen=" << cycle.stolenMarkTasks
                  << " marked=" << cycle.markedObjects << '\n';
        return 1;
    }

    runtime.detachCurrentThread();
    runtime.shutdown();
    std::cout << "hxcpp parallel remark passed: workers="
              << cycle.remarkWorkers << " stolen="
              << cycle.stolenMarkTasks << '\n';
    return 0;
}

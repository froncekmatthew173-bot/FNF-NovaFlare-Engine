#include <hxcpp.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <thread>

namespace {

std::atomic<int> observedValue{0};
std::atomic<int> observedBoundValue{0};

class BoundThreadOwner final : public ::hx::Object {
public:
    explicit BoundThreadOwner(int input) : value(input) {}

    static Dynamic run(::hx::Object* object) {
        auto* self = static_cast<BoundThreadOwner*>(object);
        observedBoundValue.store(self->value, std::memory_order_release);
        return Dynamic();
    }

    int value = 0;
    static const ::hx::gc::TypeDescriptor descriptor;
};

const ::hx::gc::TypeDescriptor BoundThreadOwner::descriptor{
    0x424f554e44544852ULL, "BoundThreadOwner", nullptr,
    sizeof(BoundThreadOwner), alignof(BoundThreadOwner), 0,
    ::hx::gc::TypeNone, 0, nullptr, nullptr, nullptr,
};

Dynamic workerEntry() {
    Dynamic message = __hxcpp_thread_read_message(true);
    observedValue.store(
        message->__Field(String("value"), ::hx::paccDynamic).Cast<int>(),
        std::memory_order_release);
    return Dynamic();
}

} // namespace

int main() {
    __hxcpp_nova_initialize();
    int result = 0;
    try {
        Dynamic worker = ::hx::CreateStaticFunction0("workerEntry", workerEntry);

        HX_BEGIN_LOCAL_FUNC_S1(::hx::LocalFunc, ThreadBootstrap,
                               Dynamic, worker1) HXARGC(0)
            void _hx_run() {
                worker1();
            }
        HX_END_LOCAL_FUNC0((void))

        Dynamic handle = __hxcpp_thread_create(
            Dynamic(new ThreadBootstrap(worker)));
        auto boundOwner = ::hx::allocate<BoundThreadOwner>(
            BoundThreadOwner::descriptor, 91);
        Dynamic boundWorker = ::hx::CreateMemberFunction0(
            "run", boundOwner.get(), &BoundThreadOwner::run);
        if (!boundWorker.isMemberFunction())
            throw std::runtime_error(
                "bound worker was not represented inline");
        (void)__hxcpp_thread_create(boundWorker);
        boundOwner = nullptr;
        boundWorker = nullptr;
        Dynamic message(::hx::Anon_obj::Create(1)->setFixed(
            0, String("value"), Dynamic(73)));
        __hxcpp_thread_send(handle, message);
        __hxcpp_zgc_join_threads();

        if (observedValue.load(std::memory_order_acquire) != 73) {
            std::cerr << "worker did not receive the rooted message\n";
            result = 1;
        }
        if (observedBoundValue.load(std::memory_order_acquire) != 91) {
            std::cerr << "inline bound worker lost its receiver\n";
            result = 4;
        }

        Dynamic semaphore = __hxcpp_semaphore_create(0);
        std::thread semaphoreWorker([semaphore] {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            __hxcpp_semaphore_release(semaphore);
        });
        if (!__hxcpp_semaphore_try_acquire(semaphore, 1.0)) {
            std::cerr << "semaphore timed acquire did not observe release\n";
            result = 5;
        }
        semaphoreWorker.join();

        Dynamic condition = __hxcpp_condition_create();
        __hxcpp_condition_acquire(condition);
        std::thread conditionWorker([condition] {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            __hxcpp_condition_acquire(condition);
            __hxcpp_condition_signal(condition);
            __hxcpp_condition_release(condition);
        });
        if (!__hxcpp_condition_timed_wait(condition, 1.0)) {
            std::cerr << "condition timed wait did not observe signal\n";
            result = 6;
        }
        __hxcpp_condition_release(condition);
        conditionWorker.join();
    } catch (const std::exception& error) {
        std::cerr << "thread worker failed: " << error.what() << '\n';
        result = 2;
    } catch (...) {
        std::cerr << "thread worker failed with a non-standard exception\n";
        result = 3;
    }
    __hxcpp_nova_shutdown();
    if (result == 0)
        std::cout << "precise rooted thread message test passed\n";
    return result;
}

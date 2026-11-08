#include <hxcpp.h>

#include <condition_variable>
#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#include <deque>
#include <unordered_map>

namespace {

struct NativeMessage final {
    Dynamic value;
    ::hx::gc::Handle root;
    std::int64_t payload = 0;
    Dynamic::Tag tag = Dynamic::Tag::Null;
};

struct ThreadControl final {
    std::mutex messageMutex;
    std::condition_variable messageCondition;
    std::condition_variable completionCondition;
    std::vector<NativeMessage> messages;
    std::exception_ptr failure;
    bool finished = false;
    bool waited = false;
};

struct NativeMutex final {
    std::mutex mutex;
};

struct NativeLock final {
    std::mutex mutex;
    std::condition_variable condition;
    std::uint64_t permits = 0;
};

struct NativeSemaphore final {
    explicit NativeSemaphore(std::uint64_t initial) : permits(initial) {}
    std::mutex mutex;
    std::condition_variable condition;
    std::uint64_t permits = 0;
};

struct NativeCondition final {
    std::recursive_mutex mutex;
    std::condition_variable_any condition;
};

struct NativeDeque final {
    std::mutex mutex;
    std::condition_variable condition;
    std::deque<NativeMessage> values;
};

// The public Haxe thread API is detached, just like original hxcpp's
// HxCreateDetachedThread. These registries are process-lifetime storage so a
// control/mutex handle cannot be destroyed by C++ static teardown while a
// detached thread is still inside native I/O during process exit.
auto* controlsMutex = new std::mutex;
auto* controls = new std::vector<std::unique_ptr<ThreadControl>>;
auto* nativeMutexes = new std::vector<std::unique_ptr<NativeMutex>>;
auto* nativeLocks = new std::vector<std::unique_ptr<NativeLock>>;
auto* nativeSemaphores = new std::vector<std::unique_ptr<NativeSemaphore>>;
auto* nativeConditions = new std::vector<std::unique_ptr<NativeCondition>>;
auto* nativeDeques = new std::vector<std::unique_ptr<NativeDeque>>;
ThreadControl mainControl;
thread_local ThreadControl* currentControl = nullptr;
thread_local unsigned foreignCallbackDepth = 0;
thread_local bool foreignCallbackAttached = false;
thread_local std::vector<::hx::gc::Handle> deliveredMessageRoots;

void flushDeliveredMessageRoots(::hx::gc::Runtime& runtime) {
    for (const auto& root : deliveredMessageRoots) {
        if (root.valid())
            runtime.releaseHandle(root);
    }
    deliveredMessageRoots.clear();
}

ThreadControl* ensureCurrentControl() {
    if (currentControl == nullptr)
        currentControl = &mainControl;
    return currentControl;
}

ThreadControl* decodeControl(const Dynamic& value) {
    auto* control = static_cast<ThreadControl*>(value.nativePointer());
    if (control == nullptr)
        throw std::invalid_argument("null native Haxe thread handle");
    return control;
}

template<class T>
T* decodeNative(const Dynamic& value, const char* kind) {
    auto* native = static_cast<T*>(value.nativePointer());
    if (native == nullptr)
        throw std::invalid_argument(std::string("null native ") + kind);
    return native;
}

NativeMessage retainMessage(Dynamic value) {
    NativeMessage message{value, {}, value.integer_, value.tag()};
    if (value.mPtr.load(std::memory_order_acquire) != 0) {
        auto& runtime = ::hx::gc::Runtime::instance();
        void* object = runtime.loadBarrier(value.mPtr);
        message.root = runtime.createHandle(object, ::hx::gc::HandleKind::Strong);
        message.value.mPtr.store(0, std::memory_order_release);
    }
    return message;
}

Dynamic releaseMessage(NativeMessage message) {
    auto& runtime = ::hx::gc::Runtime::instance();
    // The previous delivery has returned to generated code by now, where its
    // HX_NOVAGC_VARI slot is an exact root.  Keep the current message handle
    // until the next delivery so collection cannot win the tiny interval
    // between this return and the caller's rooted assignment.
    flushDeliveredMessageRoots(runtime);
    Dynamic result = std::move(message.value);
    if (message.root.valid()) {
        void* object = runtime.resolveHandle(message.root);
        result.mPtr.store(runtime.makeRef(object).bits(),
                          std::memory_order_release);
        deliveredMessageRoots.push_back(message.root);
    }
    // A Dynamic whose managed slot was cleared is normalized to Null by its
    // move constructor.  Restore both discriminant and scalar/string payload
    // after materializing the strong handle.
    result.integer_ = message.payload;
    result.tag_ = message.tag;
    return result;
}

Dynamic materializeMessage(const NativeMessage& message) {
    Dynamic result = message.value;
    if (message.root.valid()) {
        auto& runtime = ::hx::gc::Runtime::instance();
        void* object = runtime.resolveHandle(message.root);
        result.mPtr.store(runtime.makeRef(object).bits(),
                          std::memory_order_release);
    }
    result.integer_ = message.payload;
    result.tag_ = message.tag;
    return result;
}

struct TlsStorage final {
    std::unordered_map<int, NativeMessage> values;
    ~TlsStorage() {
        auto& runtime = ::hx::gc::Runtime::instance();
        if (!runtime.initialized())
            return;
        for (auto& [slot, message] : values) {
            (void)slot;
            if (message.root.valid())
                runtime.releaseHandle(message.root);
        }
    }
};

thread_local TlsStorage tlsStorage;

} // namespace

namespace hx {

void SetTopOfStack(int* marker, bool force) {
    (void)force;
    auto& runtime = ::hx::gc::Runtime::instance();
    if (marker != nullptr) {
        if (foreignCallbackDepth++ == 0) {
            foreignCallbackAttached = !runtime.currentThreadAttached();
            if (foreignCallbackAttached)
                runtime.attachCurrentThread();
        }
        runtime.safepointPoll();
        return;
    }

    if (foreignCallbackDepth == 0)
        return;
    if (--foreignCallbackDepth == 0 && foreignCallbackAttached) {
        foreignCallbackAttached = false;
        runtime.detachCurrentThread();
    }
}

} // namespace hx

extern "C" Dynamic __hxcpp_thread_current() {
    return Dynamic::fromNativePointer(ensureCurrentControl());
}

extern "C" Dynamic __hxcpp_thread_create(Dynamic job) {
    if (job.tag() != Dynamic::Tag::Object && !job.isMemberFunction())
        throw std::invalid_argument("Haxe thread job must be a closure");
    NativeMessage retainedJob = retainMessage(std::move(job));

    auto control = std::make_unique<ThreadControl>();
    ThreadControl* raw = control.get();
    {
        std::scoped_lock lock(*controlsMutex);
        controls->push_back(std::move(control));
    }
    std::thread([raw, retainedJob = std::move(retainedJob)]() mutable {
        auto& workerRuntime = ::hx::gc::Runtime::instance();
        workerRuntime.attachCurrentThread();
        currentControl = raw;
        try {
            Dynamic callable = releaseMessage(std::move(retainedJob));
            (void)callable();
        } catch (...) {
            std::scoped_lock lock(raw->messageMutex);
            raw->failure = std::current_exception();
        }
        flushDeliveredMessageRoots(workerRuntime);
        workerRuntime.detachCurrentThread();
        {
            std::scoped_lock lock(raw->messageMutex);
            raw->finished = true;
        }
        raw->messageCondition.notify_all();
        raw->completionCondition.notify_all();
    }).detach();
    return Dynamic::fromNativePointer(raw);
}

extern "C" void __hxcpp_thread_send(Dynamic thread, Dynamic message) {
    ThreadControl* control = decodeControl(thread);
    {
        std::scoped_lock lock(control->messageMutex);
        control->messages.push_back(retainMessage(std::move(message)));
    }
    control->messageCondition.notify_one();
}

bool __hxcpp_is_current_thread(::hx::Object* thread) {
    return thread != nullptr &&
           thread == reinterpret_cast<::hx::Object*>(ensureCurrentControl());
}

extern "C" Dynamic __hxcpp_thread_read_message(bool block) {
    ThreadControl* control = ensureCurrentControl();
    auto& runtime = ::hx::gc::Runtime::instance();
    std::unique_lock lock(control->messageMutex);
    if (block && control->messages.empty()) {
        runtime.enterNativeBlocking();
        control->messageCondition.wait(lock, [control] {
            return !control->messages.empty();
        });
        runtime.leaveNativeBlocking();
    }
    if (control->messages.empty())
        return Dynamic();
    NativeMessage message = std::move(control->messages.front());
    control->messages.erase(control->messages.begin());
    lock.unlock();
    return releaseMessage(std::move(message));
}

extern "C" void __hxcpp_zgc_join_threads() {
    std::vector<ThreadControl*> pending;
    {
        std::scoped_lock lock(*controlsMutex);
        for (const auto& control : *controls) {
            if (!control->waited) {
                control->waited = true;
                pending.push_back(control.get());
            }
        }
    }
    if (pending.empty())
        return;

    auto& runtime = ::hx::gc::Runtime::instance();
    runtime.enterNativeBlocking();
    for (ThreadControl* control : pending) {
        std::unique_lock lock(control->messageMutex);
        control->completionCondition.wait(lock, [control] {
            return control->finished;
        });
    }
    runtime.leaveNativeBlocking();

    flushDeliveredMessageRoots(runtime);

    for (ThreadControl* control : pending) {
        if (control->failure)
            std::rethrow_exception(control->failure);
    }
}

extern "C" Dynamic __hxcpp_mutex_create() {
    auto value = std::make_unique<NativeMutex>();
    NativeMutex* raw = value.get();
    std::scoped_lock guard(*controlsMutex);
    nativeMutexes->push_back(std::move(value));
    return Dynamic::fromNativePointer(raw);
}

extern "C" void __hxcpp_mutex_acquire(Dynamic value) {
    NativeMutex* mutex = decodeNative<NativeMutex>(value, "mutex");
    if (mutex->mutex.try_lock())
        return;
    auto& runtime = ::hx::gc::Runtime::instance();
    const bool attached = runtime.currentThreadAttached();
    if (attached)
        runtime.enterNativeBlocking();
    mutex->mutex.lock();
    if (attached)
        runtime.leaveNativeBlocking();
}

extern "C" bool __hxcpp_mutex_try(Dynamic value) {
    return decodeNative<NativeMutex>(value, "mutex")->mutex.try_lock();
}

extern "C" void __hxcpp_mutex_release(Dynamic value) {
    decodeNative<NativeMutex>(value, "mutex")->mutex.unlock();
}

extern "C" Dynamic __hxcpp_lock_create() {
    auto value = std::make_unique<NativeLock>();
    NativeLock* raw = value.get();
    std::scoped_lock guard(*controlsMutex);
    nativeLocks->push_back(std::move(value));
    return Dynamic::fromNativePointer(raw);
}

extern "C" bool __hxcpp_lock_wait(Dynamic value,
                                   ::hx::Null<double> timeout) {
    NativeLock* lock = decodeNative<NativeLock>(value, "lock");
    auto& runtime = ::hx::gc::Runtime::instance();
    const bool attached = runtime.currentThreadAttached();
    std::unique_lock guard(lock->mutex);
    auto ready = [lock] { return lock->permits != 0; };
    bool acquired = false;
    if (timeout.isNull()) {
        if (attached)
            runtime.enterNativeBlocking();
        lock->condition.wait(guard, ready);
        if (attached)
            runtime.leaveNativeBlocking();
        acquired = true;
    } else {
        const double seconds = timeout.Default(0.0);
        if (seconds <= 0.0) {
            acquired = ready();
        } else {
            if (attached)
                runtime.enterNativeBlocking();
            acquired = lock->condition.wait_for(
                guard, std::chrono::duration<double>(seconds), ready);
            if (attached)
                runtime.leaveNativeBlocking();
        }
    }
    if (acquired)
        --lock->permits;
    return acquired;
}

extern "C" void __hxcpp_lock_release(Dynamic value) {
    NativeLock* lock = decodeNative<NativeLock>(value, "lock");
    {
        std::scoped_lock guard(lock->mutex);
        ++lock->permits;
    }
    lock->condition.notify_one();
}

Dynamic __hxcpp_semaphore_create(int initial) {
    if (initial < 0)
        throw std::invalid_argument("negative semaphore count");
    auto value = std::make_unique<NativeSemaphore>(
        static_cast<std::uint64_t>(initial));
    NativeSemaphore* raw = value.get();
    std::scoped_lock guard(*controlsMutex);
    nativeSemaphores->push_back(std::move(value));
    return Dynamic::fromNativePointer(raw);
}

bool __hxcpp_semaphore_try_acquire(Dynamic value, double timeout) {
    NativeSemaphore* semaphore = decodeNative<NativeSemaphore>(
        value, "semaphore");
    std::unique_lock guard(semaphore->mutex);
    const auto ready = [semaphore] { return semaphore->permits != 0; };
    bool acquired = ready();
    if (!acquired && timeout > 0.0) {
        auto& runtime = ::hx::gc::Runtime::instance();
        const bool attached = runtime.currentThreadAttached();
        if (attached)
            runtime.enterNativeBlocking();
        acquired = semaphore->condition.wait_for(
            guard, std::chrono::duration<double>(timeout), ready);
        if (attached)
            runtime.leaveNativeBlocking();
    }
    if (acquired)
        --semaphore->permits;
    return acquired;
}

void __hxcpp_semaphore_acquire(Dynamic value) {
    NativeSemaphore* semaphore = decodeNative<NativeSemaphore>(
        value, "semaphore");
    std::unique_lock guard(semaphore->mutex);
    auto& runtime = ::hx::gc::Runtime::instance();
    const bool attached = runtime.currentThreadAttached();
    if (attached)
        runtime.enterNativeBlocking();
    semaphore->condition.wait(guard, [semaphore] {
        return semaphore->permits != 0;
    });
    if (attached)
        runtime.leaveNativeBlocking();
    --semaphore->permits;
}

void __hxcpp_semaphore_release(Dynamic value) {
    NativeSemaphore* semaphore = decodeNative<NativeSemaphore>(
        value, "semaphore");
    {
        std::scoped_lock guard(semaphore->mutex);
        ++semaphore->permits;
    }
    semaphore->condition.notify_one();
}

Dynamic __hxcpp_condition_create() {
    auto value = std::make_unique<NativeCondition>();
    NativeCondition* raw = value.get();
    std::scoped_lock guard(*controlsMutex);
    nativeConditions->push_back(std::move(value));
    return Dynamic::fromNativePointer(raw);
}

void __hxcpp_condition_acquire(Dynamic value) {
    decodeNative<NativeCondition>(value, "condition")->mutex.lock();
}

bool __hxcpp_condition_try_acquire(Dynamic value) {
    return decodeNative<NativeCondition>(value, "condition")->mutex.try_lock();
}

void __hxcpp_condition_release(Dynamic value) {
    decodeNative<NativeCondition>(value, "condition")->mutex.unlock();
}

void __hxcpp_condition_wait(Dynamic value) {
    NativeCondition* condition = decodeNative<NativeCondition>(
        value, "condition");
    std::unique_lock<std::recursive_mutex> guard(condition->mutex,
                                                 std::adopt_lock);
    auto& runtime = ::hx::gc::Runtime::instance();
    const bool attached = runtime.currentThreadAttached();
    if (attached)
        runtime.enterNativeBlocking();
    condition->condition.wait(guard);
    if (attached)
        runtime.leaveNativeBlocking();
    (void)guard.release();
}

bool __hxcpp_condition_timed_wait(Dynamic value, double timeout) {
    NativeCondition* condition = decodeNative<NativeCondition>(
        value, "condition");
    std::unique_lock<std::recursive_mutex> guard(condition->mutex,
                                                 std::adopt_lock);
    auto& runtime = ::hx::gc::Runtime::instance();
    const bool attached = runtime.currentThreadAttached();
    if (attached)
        runtime.enterNativeBlocking();
    const auto status = condition->condition.wait_for(
        guard, std::chrono::duration<double>(std::max(0.0, timeout)));
    if (attached)
        runtime.leaveNativeBlocking();
    (void)guard.release();
    return status != std::cv_status::timeout;
}

void __hxcpp_condition_signal(Dynamic value) {
    decodeNative<NativeCondition>(value, "condition")->condition.notify_one();
}

void __hxcpp_condition_broadcast(Dynamic value) {
    decodeNative<NativeCondition>(value, "condition")->condition.notify_all();
}

extern "C" Dynamic __hxcpp_tls_get(int slot) {
    auto found = tlsStorage.values.find(slot);
    return found == tlsStorage.values.end()
        ? Dynamic() : materializeMessage(found->second);
}

extern "C" void __hxcpp_tls_set(int slot, Dynamic value) {
    auto found = tlsStorage.values.find(slot);
    if (found != tlsStorage.values.end()) {
        if (found->second.root.valid())
            ::hx::gc::Runtime::instance().releaseHandle(found->second.root);
        found->second = retainMessage(std::move(value));
    } else {
        tlsStorage.values.emplace(slot, retainMessage(std::move(value)));
    }
}

extern "C" Dynamic __hxcpp_deque_create() {
    auto value = std::make_unique<NativeDeque>();
    NativeDeque* raw = value.get();
    std::scoped_lock guard(*controlsMutex);
    nativeDeques->push_back(std::move(value));
    return Dynamic::fromNativePointer(raw);
}

extern "C" void __hxcpp_deque_add(Dynamic value, Dynamic item) {
    NativeDeque* deque = decodeNative<NativeDeque>(value, "deque");
    {
        std::scoped_lock guard(deque->mutex);
        deque->values.push_back(retainMessage(std::move(item)));
    }
    deque->condition.notify_one();
}

extern "C" void __hxcpp_deque_push(Dynamic value, Dynamic item) {
    NativeDeque* deque = decodeNative<NativeDeque>(value, "deque");
    {
        std::scoped_lock guard(deque->mutex);
        deque->values.push_front(retainMessage(std::move(item)));
    }
    deque->condition.notify_one();
}

extern "C" Dynamic __hxcpp_deque_pop(Dynamic value, bool block) {
    NativeDeque* deque = decodeNative<NativeDeque>(value, "deque");
    auto& runtime = ::hx::gc::Runtime::instance();
    std::unique_lock guard(deque->mutex);
    if (block && deque->values.empty()) {
        const bool attached = runtime.currentThreadAttached();
        if (attached)
            runtime.enterNativeBlocking();
        deque->condition.wait(guard, [deque] { return !deque->values.empty(); });
        if (attached)
            runtime.leaveNativeBlocking();
    }
    if (deque->values.empty())
        return Dynamic();
    NativeMessage result = std::move(deque->values.front());
    deque->values.pop_front();
    guard.unlock();
    return releaseMessage(std::move(result));
}

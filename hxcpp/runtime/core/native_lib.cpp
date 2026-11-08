#include <hxcpp.h>
#include <hx/CFFI.h>

#include <filesystem>
#include <bit>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" void* hx_cffi(const char* name) noexcept;

namespace {

class ClassicClosure final : public ::hx::LocalFunc {
public:
    ClassicClosure(void* function, int arity) noexcept
        : function_(function), arity_(arity) {
        ::hx::gc::Runtime::instance().publish(this);
    }

    int argCount() const noexcept override { return arity_; }

    ::Dynamic invokeMany(const ::Dynamic* arguments, int count) override {
        if (count != arity_)
            throw std::runtime_error("classic CFFI primitive arity mismatch");
        ::hx::cffi::CallScope scope;
        value values[5]{};
        for (int index = 0; index < count; ++index)
            values[index] = ::hx::cffi::fromDynamic(arguments[index]);
        value result = nullptr;
        switch (count) {
            case 0:
                result = reinterpret_cast<value (*)()>(function_)();
                break;
            case 1:
                result = reinterpret_cast<value (*)(value)>(function_)(values[0]);
                break;
            case 2:
                result = reinterpret_cast<value (*)(value, value)>(function_)(
                    values[0], values[1]);
                break;
            case 3:
                result = reinterpret_cast<value (*)(value, value, value)>(
                    function_)(values[0], values[1], values[2]);
                break;
            case 4:
                result = reinterpret_cast<value (*)(value, value, value, value)>(
                    function_)(values[0], values[1], values[2], values[3]);
                break;
            case 5:
                result = reinterpret_cast<value (*)(value, value, value, value,
                    value)>(function_)(values[0], values[1], values[2],
                                       values[3], values[4]);
                break;
            default:
                throw std::runtime_error("classic CFFI supports arity 0..5");
        }
        return ::hx::cffi::toDynamic(result);
    }

    static void* operator new(std::size_t size) {
        if (size != sizeof(ClassicClosure))
            throw std::bad_alloc();
        return ::hx::gc::Runtime::instance().allocate(descriptor);
    }
    static void operator delete(void*) noexcept {}
    static void operator delete(void*, std::size_t) noexcept {}

private:
    static const ::hx::gc::TypeDescriptor descriptor;
    void* function_ = nullptr;
    int arity_ = 0;
};

const ::hx::gc::TypeDescriptor ClassicClosure::descriptor{
    0x43464649434c4153ULL,
    "CFFI.ClassicClosure",
    nullptr,
    sizeof(ClassicClosure),
    alignof(ClassicClosure),
    0,
    ::hx::gc::TypeFunction,
    0,
    nullptr,
    nullptr,
    nullptr,
};

// NDLL initializers are allowed to call hx_set_loader and immediately resolve
// or register more primitives.  Keep the loader/cache operation atomic while
// permitting that same-thread callback path; a plain mutex deadlocks during
// __neko_entry_point for several real hxcpp libraries.
std::recursive_mutex librariesMutex;
std::vector<std::filesystem::path> searchPaths;
#ifdef _WIN32
std::vector<HMODULE> libraries;
std::unordered_map<std::string, HMODULE> libraryCache;
#endif
std::string floatFormat = "%g";
std::unordered_map<std::string, void*> registeredPrimitives;

::Dynamic builtinSysString() {
#ifdef _WIN32
    return ::Dynamic(::String("Windows"));
#else
    return ::Dynamic(::String("Unknown"));
#endif
}

#ifdef _WIN32
HMODULE loadModule(const ::String& library) {
    const std::string cacheKey(library.view());
    auto cached = libraryCache.find(cacheKey);
    if (cached != libraryCache.end())
        return cached->second;

    const std::filesystem::path requested(cacheKey);
    std::vector<std::filesystem::path> candidates;
    candidates.push_back(requested);
    if (!requested.has_extension())
        candidates.push_back(requested.string() + ".ndll");
    for (const auto& directory : searchPaths) {
        candidates.push_back(directory / requested);
        if (!requested.has_extension())
            candidates.push_back(directory / (requested.string() + ".ndll"));
    }
    for (const auto& candidate : candidates) {
        HMODULE module = LoadLibraryW(candidate.wstring().c_str());
        if (module != nullptr) {
            using ResolveProc = void* (*)(const char*);
            using SetLoaderProc = void (*)(ResolveProc);
            FARPROC setLoaderAddress = GetProcAddress(module, "hx_set_loader");
            if (setLoaderAddress != nullptr) {
                static_assert(sizeof(setLoaderAddress) ==
                              sizeof(SetLoaderProc));
                auto setLoader = std::bit_cast<SetLoaderProc>(
                    setLoaderAddress);
                setLoader(&hx_cffi);
            }
            FARPROC entryAddress = GetProcAddress(module,
                                                   "__neko_entry_point");
            if (entryAddress != nullptr) {
                using EntryGetter = void* (*)();
                using EntryPoint = void (*)();
                auto entryGetter = std::bit_cast<EntryGetter>(entryAddress);
                void* entry = entryGetter();
                if (entry != nullptr)
                    std::bit_cast<EntryPoint>(entry)();
            }
            libraryCache.emplace(cacheKey, module);
            libraries.push_back(module);
            return module;
        }
    }
    return nullptr;
}
#endif

} // namespace

::Dynamic __loadprim(::String library, ::String primitive, int arity) {
    if (library == "std" && primitive == "sys_string" && arity == 0)
        return ::hx::CreateStaticFunction0("sys_string", &builtinSysString);
    if (arity < 0 || arity > 5)
        throw std::invalid_argument("classic CFFI arity is outside 0..5");
#ifdef _WIN32
    std::scoped_lock lock(librariesMutex);
    HMODULE module = loadModule(library);
    if (module == nullptr)
        throw std::runtime_error("unable to load native library: " +
                                 std::string(library.view()));
    const std::string symbol = std::string(primitive.view()) + "__" +
                               std::to_string(arity);
    FARPROC getterAddress = GetProcAddress(module, symbol.c_str());
    if (getterAddress == nullptr)
        throw std::runtime_error("native primitive not found: " + symbol);
    static_assert(sizeof(getterAddress) == sizeof(void* (*)()));
    auto getter = std::bit_cast<void* (*)()>(getterAddress);
    void* function = getter();
    if (function == nullptr)
        throw std::runtime_error("native primitive getter returned null: " +
                                 symbol);
    return ::Dynamic(new ClassicClosure(function, arity));
#else
    (void)library; (void)primitive; (void)arity;
    throw std::runtime_error("dynamic native libraries are unsupported");
#endif
}

void* __hxcpp_load_native_symbol(::String library, ::String symbol,
                                 bool quietFail) {
#ifdef _WIN32
    std::scoped_lock lock(librariesMutex);
    HMODULE module = loadModule(library);
    if (module == nullptr) {
        if (quietFail)
            return nullptr;
        throw std::runtime_error("unable to load native library: " +
                                 std::string(library.view()));
    }
    void* address = reinterpret_cast<void*>(
        GetProcAddress(module, std::string(symbol.view()).c_str()));
    if (address == nullptr && !quietFail)
        throw std::runtime_error("native symbol not found: " +
                                 std::string(symbol.view()));
    return address;
#else
    (void)library; (void)symbol;
    if (quietFail)
        return nullptr;
    throw std::runtime_error("dynamic native libraries are unsupported");
#endif
}

void* __hxcpp_get_proc_address(::String library, ::String primitive,
                               bool ndllPrimitive, bool quietFail) {
    {
        std::scoped_lock lock(librariesMutex);
        const auto direct = registeredPrimitives.find(
            std::string(primitive.view()));
        if (direct != registeredPrimitives.end())
            return direct->second;
        if (!library.isNull() && library.length != 0) {
            const std::string qualified = std::string(library.view()) + "_" +
                                          std::string(primitive.view());
            const auto found = registeredPrimitives.find(qualified);
            if (found != registeredPrimitives.end())
                return found->second;
        }
    }
    if (library.isNull() || library.length == 0) {
        if (quietFail)
            return nullptr;
        throw std::runtime_error("static native primitive not found: " +
                                 std::string(primitive.view()));
    }
    void* address = __hxcpp_load_native_symbol(library, primitive, quietFail);
    if (address == nullptr || !ndllPrimitive)
        return address;
    auto getter = reinterpret_cast<void* (*)()>(address);
    void* function = getter();
    if (function == nullptr && !quietFail)
        throw std::runtime_error("native primitive getter returned null: " +
                                 std::string(primitive.view()));
    return function;
}

int __hxcpp_register_prim(const char* name, void* function) {
    if (name == nullptr || *name == '\0' || function == nullptr)
        throw std::invalid_argument("invalid static native primitive");
    std::scoped_lock lock(librariesMutex);
    registeredPrimitives[std::string(name)] = function;
    return 0;
}

void __hxcpp_run_dll(::String library, ::String primitive) {
    void* address = __hxcpp_get_proc_address(library, primitive, false, false);
    reinterpret_cast<void (*)()>(address)();
}

int __hxcpp_unload_all_libraries() {
#ifdef _WIN32
    std::scoped_lock lock(librariesMutex);
    int count = 0;
    for (auto iterator = libraries.rbegin(); iterator != libraries.rend();
         ++iterator) {
        if (FreeLibrary(*iterator) != 0)
            ++count;
    }
    libraries.clear();
    libraryCache.clear();
    ::hx::cffi::clearExternalPrimes();
    return count;
#else
    return 0;
#endif
}

void __hxcpp_push_dll_path(::String path) {
    std::scoped_lock lock(librariesMutex);
    searchPaths.emplace_back(std::string(path.view()));
}

::String __hxcpp_get_dll_extension() {
#ifdef _WIN32
    return ::String(".dll");
#else
    return ::String(".so");
#endif
}

::String __hxcpp_get_bin_dir() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length == 0)
        return ::String("");
    buffer.resize(length);
    const auto directory = std::filesystem::path(buffer).parent_path();
    return ::String(directory.string());
#else
    return ::String(std::filesystem::current_path().string());
#endif
}

void __hxcpp_print(::Dynamic value) {
    const ::String text = value.toString();
    std::cout.write(text.view().data(),
                    static_cast<std::streamsize>(text.view().size()));
    std::cout.flush();
}

void __hxcpp_println(::Dynamic value) {
    __hxcpp_print(value);
    std::cout.put('\n');
    std::cout.flush();
}

void __hxcpp_set_float_format(::String format) {
    std::scoped_lock lock(librariesMutex);
    floatFormat.assign(format.view());
}

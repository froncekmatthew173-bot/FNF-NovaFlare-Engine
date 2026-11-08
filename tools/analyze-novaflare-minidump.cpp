#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

#include <algorithm>
#include <cwchar>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct MappedFile final {
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
    const std::byte* bytes = nullptr;
    std::size_t size = 0;

    MappedFile() = default;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept
        : file(std::exchange(other.file, INVALID_HANDLE_VALUE)),
          mapping(std::exchange(other.mapping, nullptr)),
          bytes(std::exchange(other.bytes, nullptr)),
          size(std::exchange(other.size, 0)) {}
    MappedFile& operator=(MappedFile&& other) noexcept {
        if (this == &other)
            return *this;
        if (bytes != nullptr)
            UnmapViewOfFile(bytes);
        if (mapping != nullptr)
            CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
        file = std::exchange(other.file, INVALID_HANDLE_VALUE);
        mapping = std::exchange(other.mapping, nullptr);
        bytes = std::exchange(other.bytes, nullptr);
        size = std::exchange(other.size, 0);
        return *this;
    }

    ~MappedFile() {
        if (bytes != nullptr)
            UnmapViewOfFile(bytes);
        if (mapping != nullptr)
            CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
    }
};

struct Module final {
    std::uint64_t dumpBase = 0;
    std::uint64_t size = 0;
    std::wstring path;
    HMODULE localImage = nullptr;
    std::uint64_t preferredBase = 0;
    RUNTIME_FUNCTION* functions = nullptr;
    std::size_t functionCount = 0;
};

struct Symbol final {
    std::uint64_t address = 0;
    std::uint64_t size = 0;
    std::string name;
    std::string source;
};

struct DumpMemory final {
    struct Segment final {
        std::uint64_t start = 0;
        std::uint64_t size = 0;
        std::uint64_t fileOffset = 0;
    };

    const std::byte* dump = nullptr;
    std::size_t dumpSize = 0;
    std::vector<Segment> segments;

    [[nodiscard]] const std::byte* read(
            std::uint64_t address, std::uint64_t bytes) const {
        for (const Segment& segment : segments) {
            if (address < segment.start ||
                address + bytes < address ||
                address + bytes > segment.start + segment.size) {
                continue;
            }
            const std::uint64_t offset =
                segment.fileOffset + address - segment.start;
            if (offset + bytes > dumpSize)
                return nullptr;
            return dump + offset;
        }
        return nullptr;
    }
};

[[nodiscard]] std::string narrow(const std::wstring& value) {
    if (value.empty())
        return {};
    const int bytes = WideCharToMultiByte(
        CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0,
        nullptr, nullptr);
    if (bytes <= 0)
        return {};
    std::string result(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        result.data(), bytes, nullptr, nullptr);
    return result;
}

[[nodiscard]] std::wstring dumpString(
        const std::byte* dump, std::size_t dumpSize, RVA rva) {
    if (rva == 0 || static_cast<std::size_t>(rva) + sizeof(ULONG32) > dumpSize)
        return {};
    const auto* string = reinterpret_cast<const MINIDUMP_STRING*>(dump + rva);
    const std::size_t bytes = string->Length;
    if (static_cast<std::size_t>(rva) + sizeof(ULONG32) + bytes > dumpSize)
        return {};
    return std::wstring(
        string->Buffer, string->Buffer + bytes / sizeof(wchar_t));
}

[[nodiscard]] std::optional<MappedFile> mapFile(
        const std::filesystem::path& path) {
    MappedFile result;
    result.file = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (result.file == INVALID_HANDLE_VALUE)
        return std::nullopt;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(result.file, &size) || size.QuadPart <= 0)
        return std::nullopt;
    result.size = static_cast<std::size_t>(size.QuadPart);
    result.mapping = CreateFileMappingW(
        result.file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (result.mapping == nullptr)
        return std::nullopt;
    result.bytes = static_cast<const std::byte*>(
        MapViewOfFile(result.mapping, FILE_MAP_READ, 0, 0, 0));
    if (result.bytes == nullptr)
        return std::nullopt;
    return result;
}

[[nodiscard]] bool readPeMetadata(Module& module) {
    {
        std::ifstream input(
            std::filesystem::path(module.path), std::ios::binary);
        IMAGE_DOS_HEADER diskDos{};
        IMAGE_NT_HEADERS64 diskNt{};
        if (input.read(
                reinterpret_cast<char*>(&diskDos), sizeof(diskDos)) &&
            diskDos.e_magic == IMAGE_DOS_SIGNATURE) {
            input.seekg(diskDos.e_lfanew, std::ios::beg);
            if (input.read(
                    reinterpret_cast<char*>(&diskNt), sizeof(diskNt)) &&
                diskNt.Signature == IMAGE_NT_SIGNATURE &&
                diskNt.OptionalHeader.Magic ==
                    IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
                module.preferredBase = diskNt.OptionalHeader.ImageBase;
            }
        }
    }
    module.localImage = LoadLibraryExW(
        module.path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (module.localImage == nullptr)
        return false;
    const auto* image = reinterpret_cast<const std::byte*>(module.localImage);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return false;
    }
    if (module.preferredBase == 0)
        module.preferredBase = nt->OptionalHeader.ImageBase;
    const auto& exceptionDirectory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (exceptionDirectory.VirtualAddress == 0 ||
        exceptionDirectory.Size < sizeof(RUNTIME_FUNCTION)) {
        return true;
    }
    module.functions = reinterpret_cast<RUNTIME_FUNCTION*>(
        const_cast<std::byte*>(image) + exceptionDirectory.VirtualAddress);
    module.functionCount =
        exceptionDirectory.Size / sizeof(RUNTIME_FUNCTION);
    return true;
}

[[nodiscard]] RUNTIME_FUNCTION* findRuntimeFunction(
        Module& module, std::uint32_t rva) {
    std::size_t low = 0;
    std::size_t high = module.functionCount;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        RUNTIME_FUNCTION& entry = module.functions[middle];
        if (rva < entry.BeginAddress) {
            high = middle;
        } else if (rva >= entry.EndAddress) {
            low = middle + 1;
        } else {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] Module* moduleFor(
        std::vector<Module>& modules, std::uint64_t address) {
    for (Module& module : modules) {
        if (address >= module.dumpBase &&
            address < module.dumpBase + module.size) {
            return &module;
        }
    }
    return nullptr;
}

[[nodiscard]] std::vector<Symbol> parseMap(
        const std::filesystem::path& path) {
    std::ifstream input(path);
    std::vector<Symbol> symbols;
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t marker = line.find("0x");
        if (marker == std::string::npos)
            continue;
        const std::size_t addressEnd = line.find_first_of(" \t", marker);
        if (addressEnd == std::string::npos)
            continue;
        std::uint64_t address = 0;
        std::istringstream addressStream(
            line.substr(marker, addressEnd - marker));
        addressStream >> std::hex >> address;
        if (!addressStream || address < 0x10000)
            continue;
        const std::size_t textStart =
            line.find_first_not_of(" \t", addressEnd);
        if (textStart == std::string::npos)
            continue;
        std::string text = line.substr(textStart);
        if (text.starts_with("LOAD ") || text.starts_with("PROVIDE ") ||
            text.starts_with("ASSERT ")) {
            continue;
        }
        if (text.starts_with("0x")) {
            std::istringstream sizeStream(text);
            std::string sizeToken;
            sizeStream >> sizeToken;
            std::uint64_t size = 0;
            std::istringstream valueStream(sizeToken);
            valueStream >> std::hex >> size;
            std::string source;
            std::getline(sizeStream, source);
            const std::size_t sourceStart =
                source.find_first_not_of(" \t");
            if (sourceStart != std::string::npos)
                source.erase(0, sourceStart);
            const std::size_t slash = source.find_last_of("/\\");
            if (slash != std::string::npos)
                source.erase(0, slash + 1);
            if (valueStream && size != 0)
                symbols.push_back(
                    Symbol{address, size, {}, std::move(source)});
            continue;
        }
        if (!symbols.empty() && symbols.back().address == address &&
            symbols.back().name.empty()) {
            symbols.back().name = std::move(text);
        } else {
            symbols.push_back(Symbol{address, 0, std::move(text), {}});
        }
    }
    std::ranges::stable_sort(symbols, {}, &Symbol::address);
    std::vector<Symbol> merged;
    merged.reserve(symbols.size());
    for (Symbol& symbol : symbols) {
        if (merged.empty() || merged.back().address != symbol.address) {
            merged.push_back(std::move(symbol));
            continue;
        }
        Symbol& existing = merged.back();
        existing.size = std::max(existing.size, symbol.size);
        if (existing.name.empty() && !symbol.name.empty())
            existing.name = std::move(symbol.name);
        if (existing.source.empty() && !symbol.source.empty())
            existing.source = std::move(symbol.source);
    }
    return merged;
}

[[nodiscard]] std::string symbolize(
        const Module& module, std::uint64_t dumpAddress,
        const std::vector<Symbol>& symbols) {
    const std::filesystem::path path(module.path);
    std::ostringstream output;
    output << narrow(path.filename().wstring()) << "+0x" << std::hex
           << (dumpAddress - module.dumpBase);
    if (symbols.empty() || module.preferredBase == 0)
        return output.str();
    const std::uint64_t linkAddress =
        module.preferredBase + dumpAddress - module.dumpBase;
    const auto after = std::upper_bound(
        symbols.begin(), symbols.end(), linkAddress,
        [](std::uint64_t address, const Symbol& symbol) {
            return address < symbol.address;
        });
    if (after == symbols.begin())
        return output.str();
    const Symbol& symbol = *std::prev(after);
    const std::uint64_t displacement = linkAddress - symbol.address;
    const bool insideSymbol =
        symbol.size != 0 ? displacement < symbol.size : displacement == 0;
    if (insideSymbol && !symbol.name.empty()) {
        output << " [" << symbol.name << "+0x" << displacement << "]";
    } else if (insideSymbol && !symbol.source.empty()) {
        output << " [object:" << symbol.source << "+0x" << displacement << "]";
    }
    return output.str();
}

void printContext(std::ostream& output, const CONTEXT& context) {
    output << "  registers:"
           << " rip=0x" << std::hex << context.Rip
           << " rsp=0x" << context.Rsp
           << " rbp=0x" << context.Rbp
           << " rax=0x" << context.Rax
           << " rbx=0x" << context.Rbx
           << " rcx=0x" << context.Rcx
           << " rdx=0x" << context.Rdx
           << " rsi=0x" << context.Rsi
           << " rdi=0x" << context.Rdi
           << " r8=0x" << context.R8
           << " r9=0x" << context.R9
           << " r10=0x" << context.R10
           << " r11=0x" << context.R11
           << " r12=0x" << context.R12
           << " r13=0x" << context.R13
           << " r14=0x" << context.R14
           << " r15=0x" << context.R15 << std::dec << '\n';
}

void unwindThread(
        std::ostream& output, const MINIDUMP_THREAD& thread,
        const std::byte* dump, std::size_t dumpSize,
        const DumpMemory& memory,
        std::vector<Module>& modules, const std::vector<Symbol>& symbols,
        bool exceptionThread,
        const MINIDUMP_LOCATION_DESCRIPTOR* exceptionContext) {
    output << "\nthread 0x" << std::hex << thread.ThreadId << std::dec;
    if (exceptionThread)
        output << " [exception]";
    output << '\n';
    const MINIDUMP_LOCATION_DESCRIPTOR& contextLocation =
        exceptionContext != nullptr ? *exceptionContext
                                    : thread.ThreadContext;
    if (contextLocation.DataSize < sizeof(CONTEXT) ||
        static_cast<std::uint64_t>(contextLocation.Rva) +
                sizeof(CONTEXT) >
            dumpSize) {
        output << "  no AMD64 CONTEXT in dump\n";
        return;
    }
    CONTEXT context{};
    std::memcpy(
        &context, dump + contextLocation.Rva, sizeof(CONTEXT));
    printContext(output, context);

    const std::uint64_t dumpStackStart = thread.Stack.StartOfMemoryRange;
    const std::uint64_t stackBytes = thread.Stack.Memory.DataSize;
    if (dumpStackStart == 0 || stackBytes == 0) {
        output << "  no stack memory in dump\n";
        return;
    }
    const std::byte* stackSource = memory.read(dumpStackStart, stackBytes);
    if (stackSource == nullptr && thread.Stack.Memory.Rva != 0 &&
        static_cast<std::uint64_t>(thread.Stack.Memory.Rva) + stackBytes <=
            dumpSize) {
        stackSource = dump + thread.Stack.Memory.Rva;
    }
    if (stackSource == nullptr) {
        output << "  stack range is absent from dump memory streams\n";
        return;
    }
    std::vector<std::byte> localStack(static_cast<std::size_t>(stackBytes));
    std::memcpy(
        localStack.data(), stackSource, static_cast<std::size_t>(stackBytes));
    const std::uint64_t localStackStart =
        reinterpret_cast<std::uint64_t>(localStack.data());
    const std::uint64_t localStackEnd = localStackStart + stackBytes;
    const std::uint64_t dumpStackEnd = dumpStackStart + stackBytes;
    auto toLocalStack = [&](std::uint64_t& value) {
        if (value >= dumpStackStart && value < dumpStackEnd)
            value = localStackStart + value - dumpStackStart;
    };
    auto translateStackRegisters = [&] {
        toLocalStack(context.Rsp);
        toLocalStack(context.Rbp);
        toLocalStack(context.Rax);
        toLocalStack(context.Rbx);
        toLocalStack(context.Rcx);
        toLocalStack(context.Rdx);
        toLocalStack(context.Rsi);
        toLocalStack(context.Rdi);
        toLocalStack(context.R8);
        toLocalStack(context.R9);
        toLocalStack(context.R10);
        toLocalStack(context.R11);
        toLocalStack(context.R12);
        toLocalStack(context.R13);
        toLocalStack(context.R14);
        toLocalStack(context.R15);
    };
    auto reportedStackAddress = [&](std::uint64_t value) {
        return value >= localStackStart && value < localStackEnd
            ? dumpStackStart + value - localStackStart
            : value;
    };
    translateStackRegisters();

    std::set<std::pair<std::uint64_t, std::uint64_t>> seen;
    bool exactUnwindFailed = false;
    for (std::size_t frame = 0; frame < 128 && context.Rip != 0; ++frame) {
        const auto state = std::pair{context.Rip, context.Rsp};
        if (!seen.insert(state).second)
            break;
        Module* module = moduleFor(modules, context.Rip);
        output << "  #" << frame << " rip=0x" << std::hex << context.Rip
               << " rsp=0x" << reportedStackAddress(context.Rsp)
               << std::dec << ' ';
        if (module == nullptr) {
            output << "<unknown module>\n";
            exactUnwindFailed = true;
            break;
        }
        output << symbolize(*module, context.Rip, symbols) << '\n';
        if (module->localImage == nullptr) {
            exactUnwindFailed = true;
            break;
        }

        const std::uint32_t rva =
            static_cast<std::uint32_t>(context.Rip - module->dumpBase);
        auto* function = findRuntimeFunction(*module, rva);
        const std::uint64_t oldRsp = context.Rsp;
        if (function == nullptr) {
            if (context.Rsp < localStackStart ||
                context.Rsp + sizeof(std::uint64_t) > localStackEnd) {
                exactUnwindFailed = true;
                break;
            }
            context.Rip =
                *reinterpret_cast<const std::uint64_t*>(context.Rsp);
            context.Rsp += sizeof(std::uint64_t);
        } else {
            const std::uint64_t localBase =
                reinterpret_cast<std::uint64_t>(module->localImage);
            context.Rip = localBase + rva;
            void* handlerData = nullptr;
            std::uint64_t establisherFrame = 0;
            RtlVirtualUnwind(
                UNW_FLAG_NHANDLER, localBase, context.Rip, function, &context,
                &handlerData, &establisherFrame, nullptr);
            translateStackRegisters();
        }
        if (context.Rsp <= oldRsp) {
            exactUnwindFailed = true;
            break;
        }
    }

    if (!exactUnwindFailed)
        return;
    const std::uint64_t stackStart =
        std::max(context.Rsp, localStackStart);
    const std::uint64_t stackEnd = localStackEnd;
    if (stackStart >= stackEnd)
        return;
    output << "  fallback return-address candidates"
              " (module-range scan, not exact unwind):\n";
    std::size_t candidates = 0;
    for (std::uint64_t address = (stackStart + 7) & ~std::uint64_t{7};
         address + sizeof(std::uint64_t) <= stackEnd && candidates < 64;
         address += sizeof(std::uint64_t)) {
        const std::uint64_t value =
            *reinterpret_cast<const std::uint64_t*>(address);
        Module* module = moduleFor(modules, value);
        if (module == nullptr)
            continue;
        output << "    stack+0x" << std::hex
               << (address - localStackStart)
               << " -> 0x" << value << std::dec << ' '
               << symbolize(*module, value, symbols) << '\n';
        ++candidates;
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::wcerr
            << L"usage: analyze-novaflare-minidump.exe <dump> [map] [output]"
               L" [address[:bytes] ...]\n";
        return 2;
    }
    const std::filesystem::path dumpPath(argv[1]);
    const std::optional<std::filesystem::path> mapPath =
        argc >= 3 && std::wstring_view(argv[2]) != L"-"
            ? std::optional<std::filesystem::path>(argv[2])
            : std::nullopt;
    const std::optional<std::filesystem::path> outputPath =
        argc >= 4 ? std::optional<std::filesystem::path>(argv[3])
                  : std::nullopt;

    auto mapped = mapFile(dumpPath);
    if (!mapped) {
        std::wcerr << L"failed to map dump: " << dumpPath << L'\n';
        return 1;
    }
    MINIDUMP_DIRECTORY* directory = nullptr;
    void* stream = nullptr;
    ULONG streamBytes = 0;
    if (!MiniDumpReadDumpStream(
            const_cast<std::byte*>(mapped->bytes), ModuleListStream,
            &directory, &stream, &streamBytes)) {
        std::cerr << "dump has no module list\n";
        return 1;
    }
    const auto* moduleList =
        static_cast<const MINIDUMP_MODULE_LIST*>(stream);
    std::vector<Module> modules;
    modules.reserve(moduleList->NumberOfModules);
    for (ULONG index = 0; index < moduleList->NumberOfModules; ++index) {
        const MINIDUMP_MODULE& source = moduleList->Modules[index];
        Module module;
        module.dumpBase = source.BaseOfImage;
        module.size = source.SizeOfImage;
        module.path =
            dumpString(mapped->bytes, mapped->size, source.ModuleNameRva);
        (void)readPeMetadata(module);
        modules.push_back(std::move(module));
    }

    if (!MiniDumpReadDumpStream(
            const_cast<std::byte*>(mapped->bytes), ThreadListStream,
            &directory, &stream, &streamBytes)) {
        std::cerr << "dump has no thread list\n";
        return 1;
    }
    const auto* threadList =
        static_cast<const MINIDUMP_THREAD_LIST*>(stream);

    DumpMemory memory;
    memory.dump = mapped->bytes;
    memory.dumpSize = mapped->size;
    if (MiniDumpReadDumpStream(
            const_cast<std::byte*>(mapped->bytes), Memory64ListStream,
            &directory, &stream, &streamBytes)) {
        const auto* list =
            static_cast<const MINIDUMP_MEMORY64_LIST*>(stream);
        std::uint64_t fileOffset = list->BaseRva;
        memory.segments.reserve(
            static_cast<std::size_t>(list->NumberOfMemoryRanges));
        for (std::uint64_t index = 0;
             index < list->NumberOfMemoryRanges; ++index) {
            const MINIDUMP_MEMORY_DESCRIPTOR64& range =
                list->MemoryRanges[index];
            memory.segments.push_back(DumpMemory::Segment{
                range.StartOfMemoryRange, range.DataSize, fileOffset});
            fileOffset += range.DataSize;
        }
    }

    std::uint32_t exceptionThreadId = 0;
    std::uint32_t exceptionCode = 0;
    std::uint64_t exceptionAddress = 0;
    MINIDUMP_LOCATION_DESCRIPTOR exceptionContext{};
    bool hasExceptionContext = false;
    if (MiniDumpReadDumpStream(
            const_cast<std::byte*>(mapped->bytes), ExceptionStream,
            &directory, &stream, &streamBytes)) {
        const auto* exception =
            static_cast<const MINIDUMP_EXCEPTION_STREAM*>(stream);
        exceptionThreadId = exception->ThreadId;
        exceptionCode = exception->ExceptionRecord.ExceptionCode;
        exceptionAddress = exception->ExceptionRecord.ExceptionAddress;
        exceptionContext = exception->ThreadContext;
        hasExceptionContext =
            exceptionContext.DataSize >= sizeof(CONTEXT) &&
            static_cast<std::uint64_t>(exceptionContext.Rva) +
                    sizeof(CONTEXT) <=
                mapped->size;
    }

    std::vector<Symbol> symbols;
    if (mapPath)
        symbols = parseMap(*mapPath);

    std::ofstream outputFile;
    std::ostream* output = &std::cout;
    if (outputPath) {
        outputFile.open(*outputPath, std::ios::binary);
        if (!outputFile) {
            std::wcerr << L"failed to open output: " << *outputPath << L'\n';
            return 1;
        }
        output = &outputFile;
    }
    *output << "NovaFlare native minidump thread-stack report\n"
            << "dump=" << narrow(dumpPath.wstring()) << '\n'
            << "threads=" << threadList->NumberOfThreads << '\n'
            << "modules=" << modules.size() << '\n'
            << "map_symbols=" << symbols.size() << '\n'
            << "exception_thread=0x" << std::hex << exceptionThreadId << '\n'
            << "exception_code=0x" << exceptionCode << '\n'
            << "exception_address=0x" << exceptionAddress << std::dec << '\n';
    for (const Module& module : modules) {
        if (std::filesystem::path(module.path).filename() ==
            L"NovaFlare Engine.exe") {
            *output << "main_dump_base=0x" << std::hex << module.dumpBase
                    << '\n'
                    << "main_preferred_base=0x" << module.preferredBase << '\n'
                    << "main_local_image=0x"
                    << reinterpret_cast<std::uint64_t>(module.localImage)
                    << '\n'
                    << "main_runtime_functions=0x"
                    << module.functionCount << std::dec << '\n';
            break;
        }
    }

    for (ULONG index = 0; index < threadList->NumberOfThreads; ++index) {
        const MINIDUMP_THREAD& thread = threadList->Threads[index];
        const bool isExceptionThread = thread.ThreadId == exceptionThreadId;
        unwindThread(
            *output, thread, mapped->bytes, mapped->size, memory, modules,
            symbols, isExceptionThread,
            isExceptionThread && hasExceptionContext
                ? &exceptionContext
                : nullptr);
    }

    for (int argument = 4; argument < argc; ++argument) {
        std::wstring_view specification(argv[argument]);
        const std::size_t separator = specification.find(L':');
        const std::wstring addressText(
            specification.substr(0, separator));
        const std::wstring bytesText =
            separator == std::wstring_view::npos
                ? std::wstring{}
                : std::wstring(specification.substr(separator + 1));
        wchar_t* addressEnd = nullptr;
        wchar_t* bytesEnd = nullptr;
        const std::uint64_t address =
            std::wcstoull(addressText.c_str(), &addressEnd, 0);
        std::uint64_t requestedBytes = bytesText.empty()
            ? 128
            : std::wcstoull(bytesText.c_str(), &bytesEnd, 0);
        const bool invalidAddress =
            addressEnd == addressText.c_str() || *addressEnd != L'\0';
        const bool invalidBytes =
            (!bytesText.empty() &&
             (bytesEnd == bytesText.c_str() || *bytesEnd != L'\0')) ||
            requestedBytes == 0 || requestedBytes > 64 * 1024;
        *output << "\nmemory address=" << narrow(addressText)
                << " bytes=" << requestedBytes << '\n';
        if (invalidAddress || invalidBytes) {
            *output << "  invalid memory specification\n";
            continue;
        }
        const std::byte* source = memory.read(address, requestedBytes);
        if (source == nullptr) {
            *output << "  range is absent from dump memory streams\n";
            continue;
        }
        for (std::uint64_t offset = 0; offset < requestedBytes; offset += 16) {
            const std::uint64_t rowBytes =
                std::min<std::uint64_t>(16, requestedBytes - offset);
            *output << "  0x" << std::hex << std::setw(16)
                    << std::setfill('0') << (address + offset) << "  ";
            for (std::uint64_t index = 0; index < 16; ++index) {
                if (index < rowBytes) {
                    *output << std::setw(2)
                            << std::to_integer<unsigned int>(
                                   source[offset + index])
                            << ' ';
                } else {
                    *output << "   ";
                }
            }
            *output << " |";
            for (std::uint64_t index = 0; index < rowBytes; ++index) {
                const unsigned int value =
                    std::to_integer<unsigned int>(source[offset + index]);
                *output << static_cast<char>(
                    value >= 0x20 && value <= 0x7e ? value : '.');
            }
            *output << "|\n";
        }
        *output << std::setfill(' ') << std::dec;
    }
    return 0;
}

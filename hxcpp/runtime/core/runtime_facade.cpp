#include <hxcpp.h>

#include <climits>
#include <clocale>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#endif

extern "C" bool __hxcpp_zgc_concurrent_mark_worker_idle() noexcept;
extern "C" bool __hxcpp_zgc_concurrent_relocation_worker_done() noexcept;
extern "C" std::uint64_t __hxcpp_zgc_last_initial_mark_pause_us() noexcept;
extern "C" std::uint64_t __hxcpp_zgc_last_remark_pause_us() noexcept;
extern "C" std::uint64_t
__hxcpp_zgc_last_relocation_select_pause_us() noexcept;
extern "C" std::uint64_t
__hxcpp_zgc_last_relocation_remap_pause_us() noexcept;
extern "C" const char* __hxcpp_zgc_last_dead_type_census() noexcept;
extern "C" const char* __hxcpp_zgc_last_handshake_diagnostic() noexcept;
extern "C" void __hxcpp_zgc_set_selective_relocation(bool enabled) noexcept;

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::uint64_t MiB = 1024U * 1024U;

std::atomic<std::uint64_t> lastRelocatedObjects{0};
::Dynamic criticalErrorHandler;
std::mutex criticalErrorMutex;
std::once_flag criticalErrorRoot;
std::mutex traceMutex;
std::mutex immortalMutex;
std::vector<::hx::gc::Handle> immortalHandles;

std::mutex gcCycleMutex;
std::mutex automaticGcWaitMutex;
std::condition_variable automaticGcCondition;
std::thread automaticGcThread;
std::atomic<bool> automaticGcStop{false};
std::atomic<bool> automaticGcExited{true};
std::atomic<bool> automaticFullGcRequested{false};
std::atomic<bool> automaticGcSuppressed{false};
std::atomic<std::uint64_t> automaticGcSuppressedAtAllocation{0};
std::atomic<std::uint64_t> automaticGcResumeAfterNanos{0};
std::atomic<std::uint64_t> nextAutomaticGcAllocation{0};
std::atomic<std::uint64_t> currentAutomaticYoungHeadroom{16U * MiB};
std::atomic<std::uint64_t> automaticGcCycles{0};
bool automaticGcEnabled = true;
bool automaticGcTelemetry = false;
bool automaticYoungGcEnabled = true;
std::string jsonTelemetryPath;
std::string csvTelemetryPath;
std::string diagnosticTelemetryPath;
std::vector<std::uint64_t> pauseHistoryMicros;
std::uint64_t pauseP99LimitMicros = 0;
std::uint64_t automaticGcInitialBytes = 16U * MiB;
std::uint64_t automaticYoungHeadroom = 16U * MiB;
std::uint64_t automaticYoungMinimumHeadroom = 16U * MiB;
std::uint64_t automaticYoungMaximumHeadroom = 64U * MiB;
std::uint64_t automaticYoungPauseTargetMicros = 10000;
std::uint64_t automaticGcResumeGraceMilliseconds = 250;
std::uint64_t automaticSoftMaxBytes = 768U * MiB;
std::uint64_t automaticFullPromotionBytes = 512U * MiB;
std::uint64_t automaticFullYoungCycles = 128;
std::uint64_t automaticFullMinimumMajorGrowthBytes = 128U * MiB;
std::uint64_t automaticGcMinimumHeadroom = 64U * MiB;
std::uint64_t automaticGcMaximumHeadroom = 256U * MiB;

#if defined(_WIN32)
std::wstring nativeDumpDirectory;
std::atomic_flag nativeDumpInProgress = ATOMIC_FLAG_INIT;
LPTOP_LEVEL_EXCEPTION_FILTER previousUnhandledExceptionFilter = nullptr;

[[nodiscard]] std::wstring utf8ToWide(const char* value) {
    if (value == nullptr || *value == '\0')
        return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          value, -1, nullptr, 0);
    if (count <= 1)
        return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
                            result.data(), count) == 0)
        return {};
    result.resize(static_cast<std::size_t>(count - 1));
    return result;
}

LONG WINAPI writeNativeCrashDump(EXCEPTION_POINTERS* exceptionPointers) {
    if (nativeDumpDirectory.empty() ||
        nativeDumpInProgress.test_and_set(std::memory_order_acq_rel)) {
        return previousUnhandledExceptionFilter != nullptr
            ? previousUnhandledExceptionFilter(exceptionPointers)
            : EXCEPTION_CONTINUE_SEARCH;
    }

    SYSTEMTIME timestamp{};
    GetLocalTime(&timestamp);
    const DWORD processId = GetCurrentProcessId();
    const DWORD threadId = GetCurrentThreadId();
    const DWORD exceptionCode =
        exceptionPointers != nullptr &&
        exceptionPointers->ExceptionRecord != nullptr
            ? exceptionPointers->ExceptionRecord->ExceptionCode : 0;
    const void* exceptionAddress =
        exceptionPointers != nullptr &&
        exceptionPointers->ExceptionRecord != nullptr
            ? exceptionPointers->ExceptionRecord->ExceptionAddress : nullptr;

    wchar_t stem[192]{};
    std::swprintf(
        stem, sizeof(stem) / sizeof(stem[0]),
        L"native-crash-%04u%02u%02u-%02u%02u%02u-p%lu-t%lu",
        timestamp.wYear, timestamp.wMonth, timestamp.wDay,
        timestamp.wHour, timestamp.wMinute, timestamp.wSecond,
        static_cast<unsigned long>(processId),
        static_cast<unsigned long>(threadId));
    const std::filesystem::path base =
        std::filesystem::path(nativeDumpDirectory) / stem;
    const std::filesystem::path dumpPath =
        std::filesystem::path(base.wstring() + L".dmp");
    const std::filesystem::path textPath =
        std::filesystem::path(base.wstring() + L".txt");

    BOOL dumpWritten = FALSE;
    DWORD dumpError = ERROR_SUCCESS;
    HMODULE dbgHelp = LoadLibraryW(L"Dbghelp.dll");
    HANDLE dumpFile = CreateFileW(
        dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dbgHelp != nullptr && dumpFile != INVALID_HANDLE_VALUE) {
        using MiniDumpWriteDumpFunction = BOOL(WINAPI*)(
            HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
            PMINIDUMP_EXCEPTION_INFORMATION,
            PMINIDUMP_USER_STREAM_INFORMATION,
            PMINIDUMP_CALLBACK_INFORMATION);
        const FARPROC procedure =
            GetProcAddress(dbgHelp, "MiniDumpWriteDump");
        MiniDumpWriteDumpFunction writeDump = nullptr;
        static_assert(sizeof(writeDump) == sizeof(procedure));
        std::memcpy(&writeDump, &procedure, sizeof(writeDump));
        if (writeDump != nullptr) {
            MINIDUMP_EXCEPTION_INFORMATION exceptionInformation{};
            exceptionInformation.ThreadId = threadId;
            exceptionInformation.ExceptionPointers = exceptionPointers;
            exceptionInformation.ClientPointers = FALSE;
            const auto dumpType = static_cast<MINIDUMP_TYPE>(
                MiniDumpWithFullMemory |
                MiniDumpWithHandleData |
                MiniDumpWithUnloadedModules |
                MiniDumpWithFullMemoryInfo |
                MiniDumpWithThreadInfo);
            dumpWritten = writeDump(
                GetCurrentProcess(), processId, dumpFile, dumpType,
                exceptionPointers == nullptr ? nullptr : &exceptionInformation,
                nullptr, nullptr);
            if (!dumpWritten)
                dumpError = GetLastError();
        } else {
            dumpError = GetLastError();
        }
    } else {
        dumpError = GetLastError();
    }
    if (dumpFile != INVALID_HANDLE_VALUE)
        CloseHandle(dumpFile);
    if (dbgHelp != nullptr)
        FreeLibrary(dbgHelp);

    char diagnostic[512]{};
    const int diagnosticLength = std::snprintf(
        diagnostic, sizeof(diagnostic),
        "process_id=%lu\r\nthread_id=%lu\r\n"
        "exception_code=0x%08lx\r\nexception_address=%p\r\n"
        "dump_written=%d\r\ndump_error=%lu\r\n",
        static_cast<unsigned long>(processId),
        static_cast<unsigned long>(threadId),
        static_cast<unsigned long>(exceptionCode), exceptionAddress,
        dumpWritten ? 1 : 0, static_cast<unsigned long>(dumpError));
    HANDLE textFile = CreateFileW(
        textPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (textFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        const DWORD bytes = diagnosticLength > 0
            ? static_cast<DWORD>(diagnosticLength) : 0;
        if (bytes != 0)
            WriteFile(textFile, diagnostic, bytes, &written, nullptr);
        CloseHandle(textFile);
    }

    return previousUnhandledExceptionFilter != nullptr &&
            previousUnhandledExceptionFilter != &writeNativeCrashDump
        ? previousUnhandledExceptionFilter(exceptionPointers)
        : EXCEPTION_CONTINUE_SEARCH;
}

void installNativeCrashDumpHandler() {
    const std::wstring directory =
        utf8ToWide(std::getenv("NOVAFLARE_NATIVE_DUMP_DIR"));
    if (directory.empty())
        return;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
        return;
    nativeDumpDirectory = directory;
    previousUnhandledExceptionFilter =
        SetUnhandledExceptionFilter(&writeNativeCrashDump);
}
#endif

[[nodiscard]] bool environmentFlag(const char* name, bool fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
        return fallback;
    return std::strcmp(value, "0") != 0 &&
           std::strcmp(value, "false") != 0 &&
           std::strcmp(value, "off") != 0;
}

[[nodiscard]] std::uint64_t environmentMiB(const char* name,
                                           std::uint64_t fallback,
                                           std::uint64_t minimum,
                                           std::uint64_t maximum) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
        return std::clamp(fallback, minimum, maximum) * MiB;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if (end == value || *end != '\0')
        return std::clamp(fallback, minimum, maximum) * MiB;
    return std::clamp<std::uint64_t>(parsed, minimum, maximum) * MiB;
}

[[nodiscard]] std::uint64_t environmentCount(const char* name,
                                              std::uint64_t fallback,
                                              std::uint64_t minimum,
                                              std::uint64_t maximum) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
        return std::clamp(fallback, minimum, maximum);
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if (end == value || *end != '\0')
        return std::clamp(fallback, minimum, maximum);
    return std::clamp<std::uint64_t>(parsed, minimum, maximum);
}

[[nodiscard]] std::string environmentString(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
}

[[nodiscard]] std::uint64_t wallTimestampMicros() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

[[nodiscard]] std::uint64_t steadyTimestampNanos() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch()).count());
}

struct PausePercentiles final {
    std::uint64_t p50 = 0;
    std::uint64_t p95 = 0;
    std::uint64_t p99 = 0;
};

// A Major/Full cycle owns normal Old regions, isolated Pinned regions and
// Large regions that are no longer part of the Young generation. Counting all
// three prevents the cadence gate from overlooking native buffers or promoted
// large objects while still excluding Eden/Survivor churn handled by Young.
[[nodiscard]] std::uint64_t majorHeapBytes(
        const ::hx::gc::HeapStats& heap) noexcept {
    const std::uint64_t oldLargeBytes =
        heap.largeBytes > heap.youngLargeBytes
            ? heap.largeBytes - heap.youngLargeBytes : 0;
    return heap.oldBytes + heap.pinnedBytes + oldLargeBytes;
}

struct MajorDecision final {
    const char* triggerReason = "none";
    std::uint64_t youngCycles = 0;
    std::uint64_t promotedDebtBytes = 0;
    std::uint64_t majorBytes = 0;
    std::uint64_t baselineBytes = 0;
    std::uint64_t growthBytes = 0;
    std::uint64_t minimumGrowthBytes = 0;
    std::uint64_t softMaxBytes = 0;
    bool softMaxExceeded = false;
    bool softMaxRateReady = false;
    bool softMaxDue = false;
    bool promotionDue = false;
    bool cycleDue = false;
    bool growthGatePassed = false;
    bool fullDue = false;
};

[[nodiscard]] PausePercentiles recordPauseLocked(std::uint64_t pauseMicros) {
    constexpr std::size_t MaximumSamples = 1024;
    if (pauseHistoryMicros.size() == MaximumSamples)
        pauseHistoryMicros.erase(pauseHistoryMicros.begin());
    pauseHistoryMicros.push_back(pauseMicros);
    std::vector<std::uint64_t> ordered = pauseHistoryMicros;
    std::sort(ordered.begin(), ordered.end());
    const auto percentile = [&](std::size_t numerator) {
        const std::size_t index = std::min(
            ordered.size() - 1U,
            (ordered.size() * numerator + 99U) / 100U - 1U);
        return ordered[index];
    };
    return {percentile(50), percentile(95), percentile(99)};
}

void appendStructuredTelemetry(
        const char* kind, std::uint64_t cycle, std::uint64_t epoch,
        std::uint64_t allocatedBytes, std::uint64_t usedBefore,
        std::uint64_t usedAfter, std::uint64_t roots,
        std::uint64_t markedObjects, std::uint64_t markedBytes,
        std::uint64_t deadObjects, std::uint64_t reclaimedRegions,
        std::uint64_t reclaimedBytes, std::uint64_t promotedObjects,
        std::uint64_t promotedBytes, std::uint64_t survivorObjects,
        std::uint64_t survivorBytes, std::uint64_t relocatedObjects,
        std::uint64_t relocatedBytes, std::uint64_t inPlaceRegions,
        std::uint64_t initialPauseMicros,
        std::uint64_t concurrentMicros, std::uint64_t finalPauseMicros,
        std::uint64_t totalPauseMicros, std::uint32_t workers,
        std::uint64_t initialRememberedOverflowRegions,
        std::uint64_t finalRememberedOverflowRegions,
        std::uint64_t recoveredRememberedOverflowRegions,
        std::uint64_t pinnedSourceRegions,
        std::uint64_t evacuationFallbackRegions,
        std::uint32_t forceInPlaceReasonMask,
        bool youngMarkMetadataOverflow, bool forcedInPlace,
        bool noMoveRequested,
        const PausePercentiles& percentiles,
        const MajorDecision& majorDecision) {
    const std::uint64_t timestamp = wallTimestampMicros();
    const auto allocation = ::hx::gc::Runtime::instance().allocationStats();
    const auto heap = ::hx::gc::Runtime::instance().heapStats();
    if (!jsonTelemetryPath.empty()) {
        std::ofstream json(jsonTelemetryPath,
                           std::ios::out | std::ios::app);
        if (json) {
            json << '{'
                 << "\"timestamp_us\":" << timestamp
                 << ",\"kind\":\"" << kind << '\"'
                 << ",\"cycle\":" << cycle
                 << ",\"epoch\":" << epoch
                 << ",\"allocated_bytes\":" << allocatedBytes
                 << ",\"used_before_bytes\":" << usedBefore
                 << ",\"used_after_bytes\":" << usedAfter
                 << ",\"roots\":" << roots
                 << ",\"marked_objects\":" << markedObjects
                 << ",\"marked_bytes\":" << markedBytes
                 << ",\"dead_objects\":" << deadObjects
                 << ",\"reclaimed_regions\":" << reclaimedRegions
                 << ",\"reclaimed_bytes\":" << reclaimedBytes
                 << ",\"promoted_objects\":" << promotedObjects
                 << ",\"promoted_bytes\":" << promotedBytes
                 << ",\"survivor_objects\":" << survivorObjects
                 << ",\"survivor_bytes\":" << survivorBytes
                 << ",\"relocated_objects\":" << relocatedObjects
                 << ",\"relocated_bytes\":" << relocatedBytes
                 << ",\"in_place_regions\":" << inPlaceRegions
                 << ",\"initial_pause_us\":" << initialPauseMicros
                 << ",\"concurrent_us\":" << concurrentMicros
                 << ",\"final_pause_us\":" << finalPauseMicros
                 << ",\"pause_us\":" << totalPauseMicros
                 << ",\"pause_p50_us\":" << percentiles.p50
                 << ",\"pause_p95_us\":" << percentiles.p95
                 << ",\"pause_p99_us\":" << percentiles.p99
                 << ",\"workers\":" << workers
                 << ",\"initial_remembered_overflow_regions\":"
                 << initialRememberedOverflowRegions
                 << ",\"final_remembered_overflow_regions\":"
                 << finalRememberedOverflowRegions
                 << ",\"recovered_remembered_overflow_regions\":"
                 << recoveredRememberedOverflowRegions
                 << ",\"pinned_source_regions\":" << pinnedSourceRegions
                 << ",\"evacuation_fallback_regions\":"
                 << evacuationFallbackRegions
                 << ",\"force_in_place_reason_mask\":"
                 << forceInPlaceReasonMask
                 << ",\"young_mark_metadata_overflow\":"
                 << (youngMarkMetadataOverflow ? 1 : 0)
                 << ",\"forced_in_place\":" << (forcedInPlace ? 1 : 0)
                 << ",\"no_move_requested\":"
                 << (noMoveRequested ? 1 : 0)
                 << ",\"allocation_stalls\":"
                 << allocation.allocationStalls
                 << ",\"allocation_stall_us\":"
                 << allocation.allocationStallMicros
                 << ",\"allocation_stall_timeouts\":"
                 << allocation.allocationStallTimeouts
                 << ",\"emergency_collections\":"
                 << allocation.emergencyCollections
                 << ",\"created_regions\":"
                 << allocation.createdRegions
                 << ",\"active_regions\":" << heap.activeRegions
                 << ",\"reusable_regions\":" << heap.reusableRegions
                 << ",\"address_space_reserved_bytes\":"
                 << heap.addressSpaceReservedBytes
                 << ",\"committed_bytes\":" << heap.committedBytes
                 << ",\"remembered_bitmap_bytes\":"
                 << heap.rememberedBitmapBytes
                 << ",\"young_mark_bitmap_bytes\":"
                 << heap.youngMarkBitmapBytes
                 << ",\"memory_policy_fallbacks\":"
                 << heap.memoryPolicyFallbacks
                 << ",\"eden_bytes\":" << heap.edenBytes
                 << ",\"survivor_heap_bytes\":" << heap.survivorBytes
                 << ",\"old_bytes\":" << heap.oldBytes
                 << ",\"pinned_bytes\":" << heap.pinnedBytes
                 << ",\"large_bytes\":" << heap.largeBytes
                 << ",\"young_large_bytes\":" << heap.youngLargeBytes
                 << ",\"current_tenuring_threshold\":"
                 << heap.tenuringThreshold
                  << ",\"adaptive_mark_workers\":"
                  << heap.adaptiveMarkWorkers
                  << ",\"adaptive_relocation_workers\":"
                  << heap.adaptiveRelocationWorkers
                  << ",\"major_trigger_reason\":\""
                  << majorDecision.triggerReason << '\"'
                  << ",\"major_full_due\":"
                  << (majorDecision.fullDue ? 1 : 0)
                  << ",\"major_soft_max_bytes\":"
                  << majorDecision.softMaxBytes
                  << ",\"major_soft_max_exceeded\":"
                  << (majorDecision.softMaxExceeded ? 1 : 0)
                  << ",\"major_soft_max_rate_ready\":"
                  << (majorDecision.softMaxRateReady ? 1 : 0)
                  << ",\"major_soft_max_due\":"
                  << (majorDecision.softMaxDue ? 1 : 0)
                  << ",\"major_promotion_due\":"
                  << (majorDecision.promotionDue ? 1 : 0)
                  << ",\"major_cycle_due\":"
                  << (majorDecision.cycleDue ? 1 : 0)
                  << ",\"major_growth_gate_passed\":"
                  << (majorDecision.growthGatePassed ? 1 : 0)
                  << ",\"major_young_cycles\":"
                  << majorDecision.youngCycles
                  << ",\"major_promoted_debt_bytes\":"
                  << majorDecision.promotedDebtBytes
                  << ",\"major_bytes\":" << majorDecision.majorBytes
                  << ",\"major_baseline_bytes\":"
                  << majorDecision.baselineBytes
                  << ",\"major_growth_bytes\":"
                  << majorDecision.growthBytes
                  << ",\"major_min_growth_bytes\":"
                  << majorDecision.minimumGrowthBytes << "}\n";
        }
    }
    if (!csvTelemetryPath.empty()) {
        std::error_code sizeError;
        const bool needsHeader = !std::filesystem::exists(
            csvTelemetryPath, sizeError) ||
            (!sizeError && std::filesystem::file_size(
                csvTelemetryPath, sizeError) == 0);
        std::ofstream csv(csvTelemetryPath,
                          std::ios::out | std::ios::app);
        if (csv) {
            if (needsHeader) {
                csv << "timestamp_us,kind,cycle,epoch,allocated_bytes,"
                       "used_before_bytes,used_after_bytes,roots,"
                       "marked_objects,marked_bytes,dead_objects,"
                       "reclaimed_regions,reclaimed_bytes,promoted_objects,"
                       "promoted_bytes,survivor_objects,survivor_bytes,"
                       "relocated_objects,relocated_bytes,in_place_regions,"
                       "initial_pause_us,concurrent_us,final_pause_us,"
                       "pause_us,pause_p50_us,pause_p95_us,pause_p99_us,"
                       "workers,initial_remembered_overflow_regions,"
                       "final_remembered_overflow_regions,"
                       "recovered_remembered_overflow_regions,"
                       "pinned_source_regions,evacuation_fallback_regions,"
                       "force_in_place_reason_mask,"
                       "young_mark_metadata_overflow,forced_in_place,"
                       "no_move_requested,allocation_stalls,"
                       "allocation_stall_us,"
                       "allocation_stall_timeouts,emergency_collections,"
                       "created_regions,active_regions,reusable_regions,"
                       "address_space_reserved_bytes,committed_bytes,"
                       "remembered_bitmap_bytes,young_mark_bitmap_bytes,"
                       "memory_policy_fallbacks,eden_bytes,"
                       "survivor_heap_bytes,old_bytes,pinned_bytes,"
                        "large_bytes,young_large_bytes,"
                        "current_tenuring_threshold,major_trigger_reason,"
                        "major_full_due,major_soft_max_bytes,"
                        "major_soft_max_exceeded,"
                        "major_soft_max_rate_ready,major_soft_max_due,"
                        "major_promotion_due,major_cycle_due,"
                        "major_growth_gate_passed,major_young_cycles,"
                        "major_promoted_debt_bytes,major_bytes,"
                        "major_baseline_bytes,major_growth_bytes,"
                        "major_min_growth_bytes\n";
            }
            csv << timestamp << ',' << kind << ',' << cycle << ',' << epoch
                << ',' << allocatedBytes << ',' << usedBefore << ','
                << usedAfter << ',' << roots << ',' << markedObjects << ','
                << markedBytes << ',' << deadObjects << ','
                << reclaimedRegions << ',' << reclaimedBytes << ','
                << promotedObjects << ',' << promotedBytes << ','
                << survivorObjects << ',' << survivorBytes << ','
                << relocatedObjects << ',' << relocatedBytes << ','
                << inPlaceRegions << ',' << initialPauseMicros << ','
                << concurrentMicros << ',' << finalPauseMicros << ','
                << totalPauseMicros << ',' << percentiles.p50 << ','
                << percentiles.p95 << ',' << percentiles.p99 << ','
                << workers << ',' << initialRememberedOverflowRegions << ','
                << finalRememberedOverflowRegions << ','
                << recoveredRememberedOverflowRegions << ','
                << pinnedSourceRegions << ',' << evacuationFallbackRegions
                << ',' << forceInPlaceReasonMask << ','
                << (youngMarkMetadataOverflow ? 1 : 0) << ','
                << (forcedInPlace ? 1 : 0) << ','
                << (noMoveRequested ? 1 : 0) << ','
                << allocation.allocationStalls << ','
                << allocation.allocationStallMicros << ','
                << allocation.allocationStallTimeouts << ','
                << allocation.emergencyCollections << ','
                << allocation.createdRegions << ',' << heap.activeRegions
                << ',' << heap.reusableRegions << ','
                << heap.addressSpaceReservedBytes << ',' << heap.committedBytes
                << ',' << heap.rememberedBitmapBytes << ','
                << heap.youngMarkBitmapBytes << ','
                << heap.memoryPolicyFallbacks << ',' << heap.edenBytes << ','
                << heap.survivorBytes << ',' << heap.oldBytes << ','
                 << heap.pinnedBytes << ',' << heap.largeBytes << ','
                 << heap.youngLargeBytes << ',' << heap.tenuringThreshold
                 << ',' << majorDecision.triggerReason << ','
                 << (majorDecision.fullDue ? 1 : 0) << ','
                 << majorDecision.softMaxBytes << ','
                 << (majorDecision.softMaxExceeded ? 1 : 0) << ','
                 << (majorDecision.softMaxRateReady ? 1 : 0) << ','
                 << (majorDecision.softMaxDue ? 1 : 0) << ','
                 << (majorDecision.promotionDue ? 1 : 0) << ','
                 << (majorDecision.cycleDue ? 1 : 0) << ','
                 << (majorDecision.growthGatePassed ? 1 : 0) << ','
                 << majorDecision.youngCycles << ','
                 << majorDecision.promotedDebtBytes << ','
                 << majorDecision.majorBytes << ','
                 << majorDecision.baselineBytes << ','
                 << majorDecision.growthBytes << ','
                 << majorDecision.minimumGrowthBytes
                 << '\n';
        }
    }
}

void appendDiagnosticTelemetry(const char* kind, std::uint64_t cycle,
                               const char* census, const char* handshake) {
    if (diagnosticTelemetryPath.empty())
        return;
    if ((census == nullptr || *census == '\0') &&
        (handshake == nullptr || *handshake == '\0'))
        return;
    std::ofstream diagnostic(diagnosticTelemetryPath,
                             std::ios::out | std::ios::app);
    if (!diagnostic)
        return;
    const std::uint64_t timestamp = wallTimestampMicros();
    if (census != nullptr && *census != '\0') {
        diagnostic << "timestamp_us=" << timestamp
                   << " kind=" << kind << " cycle=" << cycle
                   << " channel=type_census " << census << '\n';
    }
    if (handshake != nullptr && *handshake != '\0') {
        diagnostic << "timestamp_us=" << timestamp
                   << " kind=" << kind << " cycle=" << cycle
                   << " channel=handshake " << handshake << '\n';
    }
}

[[nodiscard]] std::unique_lock<std::mutex> acquireGcCycle() {
    std::unique_lock lock(gcCycleMutex, std::defer_lock);
    auto& runtime = ::hx::gc::Runtime::instance();
    while (!lock.try_lock()) {
        if (runtime.currentThreadAttached())
            runtime.safepointPoll();
        std::this_thread::yield();
    }
    return lock;
}

template<class Predicate>
void waitForConcurrentWorker(Predicate&& complete,
                             std::chrono::milliseconds maximumWait) {
    const auto deadline = Clock::now() + maximumWait;
    unsigned stableSamples = 0;
    while (Clock::now() < deadline) {
        if (complete()) {
            if (++stableSamples >= 2)
                return;
        } else {
            stableSamples = 0;
        }
        if (automaticGcStop.load(std::memory_order_acquire))
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

struct CycleResult final {
    ::hx::gc::CollectionStats mark;
    ::hx::gc::RelocationStats relocation;
    ::hx::gc::FinalizerStats finalizers;
    std::uint64_t allocatedBytes = 0;
    std::uint64_t usedBefore = 0;
    std::uint64_t usedAfterMark = 0;
    std::uint64_t usedAfterRelocation = 0;
    std::uint64_t markMilliseconds = 0;
    std::uint64_t relocationMilliseconds = 0;
    std::uint64_t finalizerMilliseconds = 0;
    std::uint64_t initialMarkPauseMicros = 0;
    std::uint64_t remarkPauseMicros = 0;
    std::uint64_t relocationSelectPauseMicros = 0;
    std::uint64_t relocationRemapPauseMicros = 0;
};

struct YoungCycleResult final {
    ::hx::gc::YoungCollectionStats collection;
    ::hx::gc::FinalizerStats finalizers;
    std::uint64_t allocatedBytes = 0;
    std::uint64_t usedBefore = 0;
    std::uint64_t usedAfter = 0;
    std::uint64_t wallMicros = 0;
    std::uint64_t finalizerMicros = 0;
};

[[nodiscard]] YoungCycleResult runAutomaticYoungCycleLocked() {
    auto& runtime = ::hx::gc::Runtime::instance();
    YoungCycleResult result{};
    result.allocatedBytes = runtime.allocationStats().allocatedBytes;
    result.usedBefore = runtime.usedBytes();
    const auto started = Clock::now();
    result.collection = runtime.collectYoung();
    result.wallMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - started).count());
    result.finalizers = result.collection.finalizers;
    result.finalizerMicros = result.collection.finalizerMicros;
    result.usedAfter = runtime.usedBytes();
    return result;
}

[[nodiscard]] CycleResult runAutomaticCycleLocked() {
    auto& runtime = ::hx::gc::Runtime::instance();
    CycleResult result{};
    result.allocatedBytes = runtime.allocationStats().allocatedBytes;
    result.usedBefore = runtime.usedBytes();

    const auto markStart = Clock::now();
    runtime.startConcurrentMark();
    waitForConcurrentWorker(&__hxcpp_zgc_concurrent_mark_worker_idle,
                            std::chrono::milliseconds(1000));
    result.mark = runtime.finishConcurrentMark();
    result.initialMarkPauseMicros =
        __hxcpp_zgc_last_initial_mark_pause_us();
    result.remarkPauseMicros = __hxcpp_zgc_last_remark_pause_us();
    result.markMilliseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - markStart).count());

    // finishConcurrentMark commits the pending finalizer batch before its
    // safepoint handshake resumes mutators.  Keep telemetry separate even
    // though that work is now correctly part of the remark pause.
    result.finalizers = result.mark.finalizers;
    result.finalizerMilliseconds = result.mark.finalizerMicros / 1000U;
    result.usedAfterMark = runtime.usedBytes();

    const std::uint64_t fragmentedBytes =
        result.usedAfterMark > result.mark.markedBytes
            ? result.usedAfterMark - result.mark.markedBytes : 0;
    const bool worthwhileRelocation =
        result.mark.deadObjects != 0 &&
        fragmentedBytes >= automaticGcMinimumHeadroom &&
        fragmentedBytes >= result.mark.markedBytes / 3U;
    if (worthwhileRelocation) {
        const auto relocationStart = Clock::now();
        __hxcpp_zgc_set_selective_relocation(true);
        try {
            runtime.beginConcurrentRelocation();
        } catch (...) {
            __hxcpp_zgc_set_selective_relocation(false);
            throw;
        }
        __hxcpp_zgc_set_selective_relocation(false);
        waitForConcurrentWorker(
            &__hxcpp_zgc_concurrent_relocation_worker_done,
            std::chrono::milliseconds(2000));
        result.relocation = runtime.finishConcurrentRelocation();
        if (result.relocation.degraded) {
            // Destination exhaustion is recoverable without extra heap
            // Regions: the safepoint compactor slides each remaining source
            // downward in place and uses its external forwarding table to
            // heal roots/heap fields. This path is exceptional and trades a
            // longer pause for guaranteed progress at the heap limit.
            const auto fallback = runtime.compactBootstrap();
            result.relocation.relocatedObjects += fallback.relocatedObjects;
            result.relocation.relocatedBytes += fallback.relocatedBytes;
            result.relocation.sourceRegions += fallback.sourceRegions;
            result.relocation.destinationRegions +=
                fallback.destinationRegions;
            result.relocation.inPlaceRegions += fallback.inPlaceRegions;
            result.relocation.failedRegions = 0;
        }
        result.relocationSelectPauseMicros =
            __hxcpp_zgc_last_relocation_select_pause_us();
        result.relocationRemapPauseMicros =
            __hxcpp_zgc_last_relocation_remap_pause_us();
        result.relocationMilliseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - relocationStart).count());
        lastRelocatedObjects.store(result.relocation.relocatedObjects,
                                   std::memory_order_release);
    }
    result.usedAfterRelocation = runtime.usedBytes();
    return result;
}

void writeAutomaticTelemetry(std::uint64_t cycle,
                              const CycleResult& result,
                              std::uint64_t headroom,
                              const MajorDecision& majorDecision) {
    if (!automaticGcTelemetry && jsonTelemetryPath.empty() &&
        csvTelemetryPath.empty())
        return;
    const char* census = __hxcpp_zgc_last_dead_type_census();
    const char* handshake = __hxcpp_zgc_last_handshake_diagnostic();
    std::scoped_lock lock(traceMutex);
    const std::uint64_t totalPause = result.initialMarkPauseMicros +
        result.remarkPauseMicros + result.relocationSelectPauseMicros +
        result.relocationRemapPauseMicros;
    const auto percentiles = recordPauseLocked(totalPause);
    appendDiagnosticTelemetry("full", cycle, census, handshake);
    const std::uint64_t markWallMicros = result.markMilliseconds * 1000U;
    const std::uint64_t concurrentMicros = markWallMicros >
            result.initialMarkPauseMicros + result.remarkPauseMicros
        ? markWallMicros - result.initialMarkPauseMicros -
              result.remarkPauseMicros
        : 0;
    const std::uint64_t reclaimedBytes =
        result.usedBefore > result.usedAfterRelocation
            ? result.usedBefore - result.usedAfterRelocation : 0;
    appendStructuredTelemetry(
        "full", cycle, result.mark.epoch, result.allocatedBytes,
        result.usedBefore, result.usedAfterRelocation, result.mark.roots,
        result.mark.markedObjects, result.mark.markedBytes,
        result.mark.deadObjects, result.mark.reclaimedRegions, reclaimedBytes,
        0, 0, 0, 0,
        result.relocation.relocatedObjects, result.relocation.relocatedBytes,
        result.relocation.inPlaceRegions, result.initialMarkPauseMicros,
        concurrentMicros, result.remarkPauseMicros, totalPause,
        result.mark.remarkWorkers,
        0, 0, 0, 0, 0, 0, false, false, false, percentiles,
        majorDecision);
    if (automaticGcTelemetry) {
        std::cerr
        << "novagc:auto cycle=" << cycle
        << " allocated_mb=" << result.allocatedBytes / MiB
        << " used_before_mb=" << result.usedBefore / MiB
        << " used_after_mark_mb=" << result.usedAfterMark / MiB
        << " used_after_relocate_mb=" << result.usedAfterRelocation / MiB
        << " roots=" << result.mark.roots
        << " marked_objects=" << result.mark.markedObjects
        << " marked_mb=" << result.mark.markedBytes / MiB
        << " dead_objects=" << result.mark.deadObjects
        << " reclaimed_regions=" << result.mark.reclaimedRegions
        << " reclaimed_mb=" << reclaimedBytes / MiB
        << " relocated_objects=" << result.relocation.relocatedObjects
        << " relocated_mb=" << result.relocation.relocatedBytes / MiB
        << " in_place_regions=" << result.relocation.inPlaceRegions
        << " mark_wall_ms=" << result.markMilliseconds
        << " finalized=" << result.finalizers.completed
        << " finalizer_failures=" << result.finalizers.failed
        << " finalizer_wall_ms=" << result.finalizerMilliseconds
        << " relocate_wall_ms=" << result.relocationMilliseconds
        << " initial_pause_us=" << result.initialMarkPauseMicros
        << " remark_pause_us=" << result.remarkPauseMicros
        << " relocate_select_pause_us="
        << result.relocationSelectPauseMicros
        << " relocate_remap_pause_us="
        << result.relocationRemapPauseMicros
        << " pause_p99_us=" << percentiles.p99
        << " trigger_reason=" << majorDecision.triggerReason
        << " major_growth_mb=" << majorDecision.growthBytes / MiB
        << " major_min_growth_mb="
        << majorDecision.minimumGrowthBytes / MiB
        << " young_cycles_since_full=" << majorDecision.youngCycles
        << " promoted_debt_mb=" << majorDecision.promotedDebtBytes / MiB
        << " next_headroom_mb=" << headroom / MiB << '\n';
        if (census != nullptr && *census != '\0')
            std::cerr << census << '\n';
        if (handshake != nullptr && *handshake != '\0')
            std::cerr << handshake << '\n';
        if (pauseP99LimitMicros != 0 &&
            percentiles.p99 > pauseP99LimitMicros) {
            std::cerr << "novagc:pause_gate exceeded=1 p99_us="
                      << percentiles.p99 << " limit_us="
                      << pauseP99LimitMicros << '\n';
        }
        std::cerr.flush();
    }
}

void writeYoungTelemetry(std::uint64_t cycle,
                         const YoungCycleResult& result,
                         std::uint64_t promotedDebt,
                         std::uint64_t nextYoungHeadroom,
                         const MajorDecision& majorDecision) {
    if (!automaticGcTelemetry && jsonTelemetryPath.empty() &&
        csvTelemetryPath.empty())
        return;
    const char* census = __hxcpp_zgc_last_dead_type_census();
    const char* handshake = __hxcpp_zgc_last_handshake_diagnostic();
    std::scoped_lock lock(traceMutex);
    const auto percentiles = recordPauseLocked(
        result.collection.pauseMicros);
    appendDiagnosticTelemetry("young", cycle, census, handshake);
    appendStructuredTelemetry(
        "young", cycle, result.collection.epoch, result.allocatedBytes,
        result.usedBefore, result.usedAfter, result.collection.roots,
        result.collection.markedObjects, result.collection.markedBytes,
        result.collection.deadObjects, result.collection.reclaimedRegions,
        result.collection.reclaimedBytes, result.collection.promotedObjects,
        result.collection.promotedBytes, result.collection.survivorObjects,
        result.collection.survivorBytes, 0, 0,
        result.collection.inPlacePromotedRegions,
        result.collection.initialPauseMicros,
        result.collection.concurrentMarkMicros,
        result.collection.finalPauseMicros, result.collection.pauseMicros,
        result.collection.traceWorkers,
        result.collection.initialRememberedOverflowRegions,
        result.collection.finalRememberedOverflowRegions,
        result.collection.recoveredRememberedOverflowRegions,
        result.collection.pinnedSourceRegions,
        result.collection.evacuationFallbackRegions,
        result.collection.forceInPlaceReasonMask,
        result.collection.youngMarkMetadataOverflow,
        result.collection.forcedInPlace,
        result.collection.noMoveRequested, percentiles, majorDecision);
    if (automaticGcTelemetry) {
        std::cerr
        << "novagc:young cycle=" << cycle
        << " epoch=" << result.collection.epoch
        << " allocated_mb=" << result.allocatedBytes / MiB
        << " used_before_mb=" << result.usedBefore / MiB
        << " used_after_mb=" << result.usedAfter / MiB
        << " roots=" << result.collection.roots
        << " remembered_regions=" << result.collection.rememberedRegions
        << " marked_objects=" << result.collection.markedObjects
        << " marked_mb=" << result.collection.markedBytes / MiB
        << " dead_objects=" << result.collection.deadObjects
        << " reclaimed_regions=" << result.collection.reclaimedRegions
        << " reclaimed_mb=" << result.collection.reclaimedBytes / MiB
        << " promoted_regions=" << result.collection.promotedRegions
        << " promoted_objects=" << result.collection.promotedObjects
        << " promoted_mb=" << result.collection.promotedBytes / MiB
        << " survivor_regions=" << result.collection.survivorRegions
        << " survivor_objects=" << result.collection.survivorObjects
        << " survivor_mb=" << result.collection.survivorBytes / MiB
        << " tenuring=" << result.collection.tenuringThreshold
        << " next_tenuring=" << result.collection.nextTenuringThreshold
        << " trace_workers=" << result.collection.traceWorkers
        << " in_place_promoted_regions="
        << result.collection.inPlacePromotedRegions
        << " initial_remembered_overflow_regions="
        << result.collection.initialRememberedOverflowRegions
        << " final_remembered_overflow_regions="
        << result.collection.finalRememberedOverflowRegions
        << " recovered_remembered_overflow_regions="
        << result.collection.recoveredRememberedOverflowRegions
        << " pinned_source_regions="
        << result.collection.pinnedSourceRegions
        << " evacuation_fallback_regions="
        << result.collection.evacuationFallbackRegions
        << " force_in_place_reason_mask="
        << result.collection.forceInPlaceReasonMask
        << " young_mark_metadata_overflow="
        << result.collection.youngMarkMetadataOverflow
        << " forced_in_place=" << result.collection.forcedInPlace
        << " no_move_requested=" << result.collection.noMoveRequested
        << " handshake_us=" << result.collection.handshakeMicros
        << " initial_pause_us=" << result.collection.initialPauseMicros
        << " concurrent_mark_us=" << result.collection.concurrentMarkMicros
        << " final_pause_us=" << result.collection.finalPauseMicros
        << " trace_us=" << result.collection.traceMicros
        << " evacuate_us=" << result.collection.evacuateMicros
        << " remap_us=" << result.collection.remapMicros
        << " pause_us=" << result.collection.pauseMicros
        << " wall_us=" << result.wallMicros
        << " finalized=" << result.finalizers.completed
        << " finalizer_failures=" << result.finalizers.failed
        << " finalizer_us=" << result.finalizerMicros
        << " pause_p99_us=" << percentiles.p99
        << " promoted_debt_mb=" << promotedDebt / MiB
        << " major_trigger_reason=" << majorDecision.triggerReason
        << " major_full_due=" << majorDecision.fullDue
        << " major_growth_mb=" << majorDecision.growthBytes / MiB
        << " major_min_growth_mb="
        << majorDecision.minimumGrowthBytes / MiB
        << " young_cycles_since_full=" << majorDecision.youngCycles
        << " next_young_headroom_mb=" << nextYoungHeadroom / MiB << '\n';
        if (census != nullptr && *census != '\0')
            std::cerr << census << '\n';
        if (handshake != nullptr && *handshake != '\0')
            std::cerr << handshake << '\n';
        if (pauseP99LimitMicros != 0 &&
            percentiles.p99 > pauseP99LimitMicros) {
            std::cerr << "novagc:pause_gate exceeded=1 p99_us="
                      << percentiles.p99 << " limit_us="
                      << pauseP99LimitMicros << '\n';
        }
        std::cerr.flush();
    }
}

void startAutomaticGc() {
    automaticGcEnabled = environmentFlag("HXCPP_NOVAGC_AUTO_GC", true);
    automaticGcTelemetry = environmentFlag("HXCPP_NOVAGC_TELEMETRY", false);
    jsonTelemetryPath = environmentString("HXCPP_NOVAGC_JSON_LOG");
    csvTelemetryPath = environmentString("HXCPP_NOVAGC_CSV_LOG");
    diagnosticTelemetryPath =
        environmentString("HXCPP_NOVAGC_DIAGNOSTIC_LOG");
    pauseHistoryMicros.clear();
    pauseP99LimitMicros = environmentCount(
        "HXCPP_NOVAGC_PAUSE_P99_LIMIT_US", 0, 0, 10000000);
    automaticYoungGcEnabled = environmentFlag(
        "HXCPP_NOVAGC_YOUNG_GC", true);
    automaticGcInitialBytes = environmentMiB(
        "HXCPP_NOVAGC_INITIAL_TRIGGER_MB", 16, 4, 4096);
    automaticYoungHeadroom = environmentMiB(
        "HXCPP_NOVAGC_YOUNG_TRIGGER_MB", 16, 4, 1024);
    automaticYoungMinimumHeadroom = environmentMiB(
        "HXCPP_NOVAGC_MIN_YOUNG_MB", 16, 4, 1024);
    automaticYoungMaximumHeadroom = environmentMiB(
        "HXCPP_NOVAGC_MAX_YOUNG_MB", 64,
        automaticYoungMinimumHeadroom / MiB, 4096);
    automaticYoungPauseTargetMicros = environmentCount(
        "HXCPP_NOVAGC_YOUNG_PAUSE_TARGET_US", 10000, 250, 100000);
    automaticGcResumeGraceMilliseconds = environmentCount(
        "HXCPP_NOVAGC_RESUME_GRACE_MS", 250, 0, 10000);
    automaticSoftMaxBytes = environmentCount(
        "HXCPP_NOVAGC_SOFT_MAX_HEAP_MB", 768, 0, 1048576) * MiB;
    automaticFullPromotionBytes = environmentMiB(
        "HXCPP_NOVAGC_FULL_PROMOTION_MB", 512, 64, 4096);
    automaticFullYoungCycles = environmentCount(
        "HXCPP_NOVAGC_FULL_YOUNG_CYCLES", 128, 1, 1024);
    // Gameplay measurements showed that a cadence Full at 69.55 MiB of
    // Major growth reclaimed only 40.38 MiB while spending 207.915 ms in
    // concurrent mark/relocation. Young collections reclaim comparable
    // batches far more cheaply, so do not start a proactive Major at that
    // growth level. Explicit requests and promotion debt still bypass this
    // profitability floor, and SoftMax remains an independent pressure input.
    automaticFullMinimumMajorGrowthBytes = environmentCount(
        "HXCPP_NOVAGC_FULL_MIN_MAJOR_GROWTH_MB", 128, 0, 1048576) * MiB;
    automaticGcMinimumHeadroom = environmentMiB(
        "HXCPP_NOVAGC_MIN_HEADROOM_MB", 64, 16, 4096);
    automaticGcMaximumHeadroom = environmentMiB(
        "HXCPP_NOVAGC_MAX_HEADROOM_MB", 256,
        automaticGcMinimumHeadroom / MiB, 8192);
    automaticGcStop.store(false, std::memory_order_release);
    automaticGcSuppressed.store(false, std::memory_order_release);
    automaticGcSuppressedAtAllocation.store(0, std::memory_order_release);
    automaticGcResumeAfterNanos.store(0, std::memory_order_release);
    automaticFullGcRequested.store(false, std::memory_order_release);
    automaticGcCycles.store(0, std::memory_order_release);
    currentAutomaticYoungHeadroom.store(automaticYoungHeadroom,
                                        std::memory_order_release);
    nextAutomaticGcAllocation.store(automaticGcInitialBytes,
                                    std::memory_order_release);
    if (!automaticGcEnabled)
        return;

    automaticGcExited.store(false, std::memory_order_release);
    automaticGcThread = std::thread([] {
        auto& runtime = ::hx::gc::Runtime::instance();
        bool attached = false;
        std::uint64_t promotedDebt = 0;
        std::uint64_t youngCyclesSinceFull = 0;
        std::uint64_t nextSoftMaxFullAllocation = 0;
        std::uint64_t majorBytesAfterFull = 0;
        std::uint64_t youngHeadroom =
            currentAutomaticYoungHeadroom.load(std::memory_order_acquire);
        try {
            // Finalizers are allowed to execute Haxe/runtime code.  Give the
            // collector thread a real mutator state so exact RootScopes and
            // safepoints used by callbacks remain valid.
            runtime.attachCurrentThread();
            attached = true;
            majorBytesAfterFull = majorHeapBytes(runtime.heapStats());
        } catch (const std::exception& error) {
            std::scoped_lock lock(traceMutex);
            std::cerr << "novagc:auto attach_error=" << error.what() << '\n';
            std::cerr.flush();
        }
        while (!automaticGcStop.load(std::memory_order_acquire)) {
            if (!attached)
                break;
            runtime.safepointPoll();
            const std::uint64_t resumeAfter =
                automaticGcResumeAfterNanos.load(std::memory_order_acquire);
            const bool resumeGraceActive = resumeAfter != 0 &&
                steadyTimestampNanos() < resumeAfter;
            if (automaticGcSuppressed.load(std::memory_order_acquire) ||
                resumeGraceActive) {
                // LoadingState uses cpp.vm.Gc.enable(false) while worker
                // threads decode assets and the render thread uploads them.
                // Treat that interval as an automatic-collection suppression
                // window, otherwise every nursery threshold starts a global
                // handshake against threads intentionally inside long native
                // work. Emergency allocation recovery remains available in
                // Runtime::allocate when the reserved heap is exhausted.
                runtime.enterNativeBlocking();
                std::unique_lock waitLock(automaticGcWaitMutex);
                automaticGcCondition.wait_for(
                    waitLock, std::chrono::milliseconds(10), [] {
                        return automaticGcStop.load(std::memory_order_acquire);
                    });
                waitLock.unlock();
                runtime.leaveNativeBlocking();
                continue;
            }
            const std::uint64_t allocated =
                runtime.allocationStats().allocatedBytes;
            const std::uint64_t trigger =
                nextAutomaticGcAllocation.load(std::memory_order_acquire);
            const bool fullRequested =
                automaticFullGcRequested.load(std::memory_order_acquire);
            if (allocated < trigger && !fullRequested) {
                // The uncommit delay can expire while the application is
                // idle and performs no further allocations/collections.
                runtime.uncommitUnusedMemory(false);
                // The attached collector is dormant here.  Publish that fact
                // so a synchronous collection initiated by another mutator
                // does not wait for this condition-variable sleeper.
                runtime.enterNativeBlocking();
                std::unique_lock waitLock(automaticGcWaitMutex);
                    automaticGcCondition.wait_for(
                    waitLock, std::chrono::milliseconds(10), [] {
                        return automaticGcStop.load(std::memory_order_acquire) ||
                            automaticFullGcRequested.load(
                                std::memory_order_acquire);
                    });
                waitLock.unlock();
                runtime.leaveNativeBlocking();
                continue;
            }

            try {
                auto cycleLock = acquireGcCycle();
                if (automaticGcStop.load(std::memory_order_acquire))
                    break;
                const std::uint64_t cycle =
                    automaticGcCycles.fetch_add(1,
                                                std::memory_order_acq_rel) + 1;
                if (automaticFullGcRequested.exchange(
                        false, std::memory_order_acq_rel)) {
                    const std::uint64_t majorBytes =
                        majorHeapBytes(runtime.heapStats());
                    MajorDecision majorDecision{};
                    majorDecision.triggerReason = "explicit_request";
                    majorDecision.youngCycles = youngCyclesSinceFull;
                    majorDecision.promotedDebtBytes = promotedDebt;
                    majorDecision.majorBytes = majorBytes;
                    majorDecision.baselineBytes = majorBytesAfterFull;
                    majorDecision.growthBytes =
                        majorBytes > majorBytesAfterFull
                            ? majorBytes - majorBytesAfterFull : 0;
                    majorDecision.minimumGrowthBytes =
                        automaticFullMinimumMajorGrowthBytes;
                    majorDecision.softMaxBytes = automaticSoftMaxBytes;
                    majorDecision.cycleDue = youngCyclesSinceFull >=
                        automaticFullYoungCycles;
                    majorDecision.growthGatePassed =
                        automaticFullMinimumMajorGrowthBytes == 0 ||
                        majorDecision.growthBytes >=
                            automaticFullMinimumMajorGrowthBytes;
                    majorDecision.fullDue = true;
                    const CycleResult full = runAutomaticCycleLocked();
                    const std::uint64_t headroom =
                        std::clamp<std::uint64_t>(
                            full.mark.markedBytes / 2U,
                            automaticGcMinimumHeadroom,
                            automaticGcMaximumHeadroom);
                    nextAutomaticGcAllocation.store(
                        runtime.allocationStats().allocatedBytes + headroom,
                        std::memory_order_release);
                    writeAutomaticTelemetry(cycle, full, headroom,
                                            majorDecision);
                    promotedDebt = 0;
                    youngCyclesSinceFull = 0;
                    majorBytesAfterFull = majorHeapBytes(runtime.heapStats());
                    if (automaticSoftMaxBytes != 0 &&
                        full.usedAfterRelocation > automaticSoftMaxBytes) {
                        nextSoftMaxFullAllocation = full.allocatedBytes +
                            automaticFullPromotionBytes;
                    } else {
                        nextSoftMaxFullAllocation = 0;
                    }
                    runtime.uncommitUnusedMemory(false);
                    continue;
                }
                if (automaticYoungGcEnabled) {
                    const YoungCycleResult young =
                        runAutomaticYoungCycleLocked();
                    promotedDebt += young.collection.promotedBytes;
                    ++youngCyclesSinceFull;

                    const std::uint64_t survivedBytes =
                        young.collection.survivorBytes +
                        young.collection.promotedBytes;
                    const std::uint64_t cohortBytes = std::max<std::uint64_t>(
                        1U, survivedBytes + young.collection.reclaimedBytes);
                    const std::uint64_t survivalPercent =
                        survivedBytes * 100U / cohortBytes;
                    if (young.collection.pauseMicros >
                        automaticYoungPauseTargetMicros) {
                        // Latency is the primary contract. Large mostly-dead
                        // cohorts still make final tracing/finalization scale
                        // with the number of objects (observed 267 ms at
                        // 512 MiB), so every target miss must reduce the next
                        // batch instead of trying to amortize root scanning.
                        youngHeadroom = std::max(
                            automaticYoungMinimumHeadroom,
                            youngHeadroom * 3U / 4U);
                    } else if (young.collection.pauseMicros <
                                   automaticYoungPauseTargetMicros / 2U &&
                               survivalPercent < 25U) {
                        youngHeadroom = std::min(
                            automaticYoungMaximumHeadroom,
                            youngHeadroom * 5U / 4U);
                    } else if (survivalPercent > 60U) {
                        youngHeadroom = std::max(
                            automaticYoungMinimumHeadroom,
                            youngHeadroom * 7U / 8U);
                    }
                    currentAutomaticYoungHeadroom.store(
                        youngHeadroom, std::memory_order_release);

                    const bool softMaxExceeded = automaticSoftMaxBytes != 0 &&
                        young.usedAfter > automaticSoftMaxBytes;
                    if (!softMaxExceeded)
                        nextSoftMaxFullAllocation = 0;
                    // Committed regions are reusable capacity, not live heap.
                    // Soft max is an advisory pressure signal, as it is in
                    // JDK ZGC. Rate-limit repeated attempts and require actual
                    // Major growth below before paying for a Full cycle.
                    const bool softMaxRateReady = softMaxExceeded &&
                        (nextSoftMaxFullAllocation == 0 ||
                         young.allocatedBytes >= nextSoftMaxFullAllocation);
                    const bool promotionDue =
                        promotedDebt >= automaticFullPromotionBytes;
                    const std::uint64_t majorBytes =
                        majorHeapBytes(runtime.heapStats());
                    MajorDecision majorDecision{};
                    majorDecision.youngCycles = youngCyclesSinceFull;
                    majorDecision.promotedDebtBytes = promotedDebt;
                    majorDecision.majorBytes = majorBytes;
                    majorDecision.baselineBytes = majorBytesAfterFull;
                    majorDecision.growthBytes =
                        majorBytes > majorBytesAfterFull
                            ? majorBytes - majorBytesAfterFull : 0;
                    majorDecision.minimumGrowthBytes =
                        automaticFullMinimumMajorGrowthBytes;
                    majorDecision.softMaxBytes = automaticSoftMaxBytes;
                    majorDecision.softMaxExceeded = softMaxExceeded;
                    majorDecision.softMaxRateReady = softMaxRateReady;
                    majorDecision.promotionDue = promotionDue;
                    majorDecision.cycleDue = youngCyclesSinceFull >=
                        automaticFullYoungCycles;
                    majorDecision.growthGatePassed =
                        automaticFullMinimumMajorGrowthBytes == 0 ||
                        majorDecision.growthBytes >=
                            automaticFullMinimumMajorGrowthBytes;
                    const bool cycleGrowthDue = majorDecision.cycleDue &&
                        majorDecision.growthGatePassed;
                    const bool softMaxDue = softMaxExceeded &&
                        softMaxRateReady &&
                        majorDecision.growthGatePassed;
                    majorDecision.softMaxDue = softMaxDue;
                    majorDecision.fullDue = softMaxDue || promotionDue ||
                        cycleGrowthDue;
                    if (promotionDue)
                        majorDecision.triggerReason = "promotion_debt";
                    else if (softMaxDue)
                        majorDecision.triggerReason = "soft_max";
                    else if (cycleGrowthDue)
                        majorDecision.triggerReason = "young_cycle_growth";
                    else if (softMaxExceeded && !softMaxRateReady)
                        majorDecision.triggerReason = "soft_max_rate_wait";
                    else if (softMaxExceeded)
                        majorDecision.triggerReason = "soft_max_growth_wait";
                    else if (majorDecision.cycleDue)
                        majorDecision.triggerReason = "cycle_growth_wait";
                    writeYoungTelemetry(cycle, young, promotedDebt,
                                        youngHeadroom, majorDecision);
                    const bool fullDue = majorDecision.fullDue;
                    if (fullDue &&
                        !automaticGcStop.load(std::memory_order_acquire)) {
                        if (softMaxExceeded)
                            runtime.requestSoftReferenceClearing();
                        const CycleResult full = runAutomaticCycleLocked();
                        const std::uint64_t headroom =
                            std::clamp<std::uint64_t>(
                                full.mark.markedBytes / 2U,
                                automaticGcMinimumHeadroom,
                                automaticGcMaximumHeadroom);
                        writeAutomaticTelemetry(cycle, full, headroom,
                                                majorDecision);
                        promotedDebt = 0;
                        youngCyclesSinceFull = 0;
                        majorBytesAfterFull =
                            majorHeapBytes(runtime.heapStats());
                        if (automaticSoftMaxBytes != 0 &&
                            full.usedAfterRelocation > automaticSoftMaxBytes) {
                            nextSoftMaxFullAllocation = full.allocatedBytes +
                                automaticFullPromotionBytes;
                        } else {
                            nextSoftMaxFullAllocation = 0;
                        }
                    }
                    // Advance from the allocation snapshot taken at the
                    // beginning of the minor cycle. A fast mutator that
                    // allocates another nursery while collection runs keeps
                    // that debt and immediately triggers a catch-up cycle.
                    nextAutomaticGcAllocation.store(
                        young.allocatedBytes + youngHeadroom,
                        std::memory_order_release);
                    runtime.uncommitUnusedMemory(false);
                } else {
                    const std::uint64_t majorBytes =
                        majorHeapBytes(runtime.heapStats());
                    MajorDecision majorDecision{};
                    majorDecision.triggerReason = "full_only_mode";
                    majorDecision.majorBytes = majorBytes;
                    majorDecision.baselineBytes = majorBytesAfterFull;
                    majorDecision.growthBytes =
                        majorBytes > majorBytesAfterFull
                            ? majorBytes - majorBytesAfterFull : 0;
                    majorDecision.minimumGrowthBytes =
                        automaticFullMinimumMajorGrowthBytes;
                    majorDecision.softMaxBytes = automaticSoftMaxBytes;
                    majorDecision.growthGatePassed = true;
                    majorDecision.fullDue = true;
                    const CycleResult result = runAutomaticCycleLocked();
                    const std::uint64_t headroom =
                        std::clamp<std::uint64_t>(
                            result.mark.markedBytes / 2U,
                            automaticGcMinimumHeadroom,
                            automaticGcMaximumHeadroom);
                    nextAutomaticGcAllocation.store(
                        result.allocatedBytes + headroom,
                        std::memory_order_release);
                    writeAutomaticTelemetry(cycle, result, headroom,
                                            majorDecision);
                    majorBytesAfterFull = majorHeapBytes(runtime.heapStats());
                }
            } catch (const std::exception& error) {
                const std::uint64_t allocatedAfter =
                    runtime.allocationStats().allocatedBytes;
                nextAutomaticGcAllocation.store(
                    allocatedAfter + (automaticYoungGcEnabled
                        ? currentAutomaticYoungHeadroom.load(
                              std::memory_order_acquire)
                        : automaticGcMinimumHeadroom),
                    std::memory_order_release);
                std::scoped_lock lock(traceMutex);
                std::cerr << "novagc:auto error=" << error.what() << '\n';
                const char* handshake =
                    __hxcpp_zgc_last_handshake_diagnostic();
                if (handshake != nullptr && *handshake != '\0')
                    std::cerr << handshake << '\n';
                std::cerr.flush();
            }
        }
        if (attached) {
            try {
                runtime.detachCurrentThread();
            } catch (const std::exception& error) {
                std::scoped_lock lock(traceMutex);
                std::cerr << "novagc:auto detach_error=" << error.what()
                          << '\n';
                std::cerr.flush();
            }
        }
        automaticGcExited.store(true, std::memory_order_release);
        automaticGcCondition.notify_all();
    });
}

void stopAutomaticGc() noexcept {
    automaticGcStop.store(true, std::memory_order_release);
    automaticGcCondition.notify_all();
    auto& runtime = ::hx::gc::Runtime::instance();
    while (!automaticGcExited.load(std::memory_order_acquire)) {
        try {
            if (runtime.currentThreadAttached())
                runtime.safepointPoll();
        } catch (...) {
        }
        std::this_thread::yield();
    }
    if (automaticGcThread.joinable())
        automaticGcThread.join();
}
}

void __trace(::Dynamic value, ::Dynamic infos) {
    (void)infos;
    const ::String message = value.toString();
    std::scoped_lock lock(traceMutex);
    std::cout.write(message.view().data(),
                    static_cast<std::streamsize>(message.view().size()));
    std::cout.put('\n');
    std::cout.flush();
}

extern "C" void __hxcpp_zgc_join_threads();

extern "C" void __hxcpp_nova_initialize() {
    auto& runtime = ::hx::gc::Runtime::instance();
    ::hx::gc::RuntimeConfig config{};
    config.regionBytes = static_cast<std::size_t>(environmentMiB(
        "HXCPP_NOVAGC_REGION_MB", 2, 1, 1024));
    config.tlabBytes = static_cast<std::size_t>(environmentCount(
        "HXCPP_NOVAGC_TLAB_KB", 64, 4, 65536) * 1024U);
    config.largeObjectThreshold = static_cast<std::size_t>(environmentCount(
        "HXCPP_NOVAGC_LARGE_THRESHOLD_KB", 128, 16, 1048576) * 1024U);
    config.maximumRegions = static_cast<std::uint32_t>(environmentCount(
        "HXCPP_NOVAGC_MAX_REGIONS", 4096, 1, 1048576));
    config.maximumAddressSpaceBytes = static_cast<std::size_t>(
        environmentCount("HXCPP_NOVAGC_ADDRESS_SPACE_MB", 0, 0,
                         16777216) * MiB);
    config.handshakeTimeoutMilliseconds = static_cast<std::uint32_t>(
        environmentCount("HXCPP_NOVAGC_HANDSHAKE_TIMEOUT_MS", 5000,
                         100, 600000));
    config.allocationStallTimeoutMilliseconds = static_cast<std::uint32_t>(
        environmentCount("HXCPP_NOVAGC_STALL_TIMEOUT_MS", 5000,
                         1, 600000));
    config.markWorkerCount = static_cast<std::uint32_t>(environmentCount(
        "HXCPP_NOVAGC_MARK_WORKERS", 2, 0, 1024));
    config.relocationWorkerCount = static_cast<std::uint32_t>(environmentCount(
        "HXCPP_NOVAGC_RELOCATION_WORKERS", 2, 0, 1024));
    config.maximumPauseMilliseconds = static_cast<std::uint32_t>(
        environmentCount("HXCPP_NOVAGC_MAX_PAUSE_MS", 10, 1, 10000));
    config.initialTenuringThreshold = static_cast<std::uint16_t>(
        environmentCount("HXCPP_NOVAGC_TENURING_THRESHOLD", 2, 1, 255));
    config.maximumTenuringThreshold = static_cast<std::uint16_t>(
        environmentCount("HXCPP_NOVAGC_MAX_TENURING_THRESHOLD", 3,
                         config.initialTenuringThreshold, 255));
    config.survivorTargetPercent = static_cast<std::uint32_t>(
        environmentCount("HXCPP_NOVAGC_SURVIVOR_TARGET_PERCENT", 50,
                         1, 100));
    config.adaptiveTenuring = environmentFlag(
        "HXCPP_NOVAGC_ADAPTIVE_TENURING", true);
    config.uncommitDelayMilliseconds = static_cast<std::uint32_t>(
        environmentCount("HXCPP_NOVAGC_UNCOMMIT_DELAY_SECONDS", 30,
                         0, 86400) * 1000U);
    const std::string allocationService = environmentString(
        "HXCPP_NOVAGC_ALLOCATION_SERVICE");
    if (allocationService == "latency") {
        config.allocationServiceLevel =
            ::hx::gc::AllocationServiceLevel::Latency;
    } else if (allocationService == "throughput") {
        config.allocationServiceLevel =
            ::hx::gc::AllocationServiceLevel::Throughput;
    }
    const std::string numaPolicy = environmentString(
        "HXCPP_NOVAGC_NUMA_POLICY");
    if (numaPolicy == "disabled") {
        config.numaPolicy = ::hx::gc::NumaPolicy::Disabled;
    } else if (numaPolicy == "interleave") {
        config.numaPolicy = ::hx::gc::NumaPolicy::Interleave;
    }
    const std::string largePages = environmentString(
        "HXCPP_NOVAGC_LARGE_PAGES");
    if (largePages == "disabled") {
        config.largePagePolicy = ::hx::gc::LargePagePolicy::Disabled;
    } else if (largePages == "explicit") {
        config.largePagePolicy = ::hx::gc::LargePagePolicy::Explicit;
    }
    runtime.initialize(config);
#if defined(_WIN32)
    installNativeCrashDumpHandler();
#endif
    runtime.attachCurrentThread();
    try {
        startAutomaticGc();
    } catch (...) {
        runtime.detachCurrentThread();
        runtime.shutdown();
        throw;
    }
}

extern "C" void __hxcpp_nova_shutdown() {
    auto& runtime = ::hx::gc::Runtime::instance();
    stopAutomaticGc();
    // sys.thread.Thread is detached in original hxcpp. Application services
    // such as TraceServer may legitimately remain blocked in Socket.accept
    // until process termination, so shutdown must not join them. Keep the GC
    // heap alive when another mutator is still attached; ExitProcess will end
    // those detached threads, while a quiescent test/embedder still receives
    // complete deterministic cleanup below.
    if (runtime.attachedThreadCount() > 1) {
        runtime.detachCurrentThread();
        return;
    }
    // The legacy global-memory view owns an exact pinned handle for as long
    // as it is selected.  Drop it before releasing the remaining immortal
    // roots so deterministic embedders/tests never leave a stale handle in a
    // runtime which is about to shut down.
    __hxcpp_memory_clear();
    {
        std::scoped_lock lock(immortalMutex);
        for (const auto handle : immortalHandles)
            runtime.releaseHandle(handle);
        immortalHandles.clear();
    }
    runtime.detachCurrentThread();
    runtime.shutdown();
}

extern "C" void __hxcpp_gc_safe_point() {
    ::hx::gc::Runtime::instance().safepointPoll();
}

extern "C" void __hxcpp_collect(bool major) {
    auto& runtime = ::hx::gc::Runtime::instance();
    auto cycleLock = acquireGcCycle();
    if (!major && automaticYoungGcEnabled) {
        (void)runtime.collectYoung();
        (void)runtime.drainFinalizers();
        return;
    }
    runtime.startConcurrentMark();
    waitForConcurrentWorker(&__hxcpp_zgc_concurrent_mark_worker_idle,
                            std::chrono::milliseconds(1000));
    (void)runtime.finishConcurrentMark();
}

extern "C" void __hxcpp_gc_request_full() {
    if (!automaticGcEnabled || automaticGcExited.load(std::memory_order_acquire))
        return;
    // Coalesce page-transition requests.  The automatic collector performs
    // concurrent marking off the UI thread; callers never wait for the mark.
    automaticFullGcRequested.store(true, std::memory_order_release);
    automaticGcCondition.notify_all();
}

extern "C" void __hxcpp_gc_compact() {
    auto& runtime = ::hx::gc::Runtime::instance();
    auto cycleLock = acquireGcCycle();
    runtime.beginConcurrentRelocation();
    waitForConcurrentWorker(
        &__hxcpp_zgc_concurrent_relocation_worker_done,
        std::chrono::milliseconds(2000));
    const auto stats = runtime.finishConcurrentRelocation();
    auto completed = stats;
    if (completed.degraded) {
        const auto fallback = runtime.compactBootstrap();
        completed.relocatedObjects += fallback.relocatedObjects;
        completed.relocatedBytes += fallback.relocatedBytes;
        completed.sourceRegions += fallback.sourceRegions;
        completed.destinationRegions += fallback.destinationRegions;
        completed.inPlaceRegions += fallback.inPlaceRegions;
        completed.failedRegions = 0;
    }
    lastRelocatedObjects.store(completed.relocatedObjects,
                               std::memory_order_release);
}

extern "C" void __hxcpp_enable(bool enabled) {
    auto& runtime = ::hx::gc::Runtime::instance();
    if (!enabled) {
        const bool wasSuppressed = automaticGcSuppressed.exchange(
            true, std::memory_order_acq_rel);
        if (!wasSuppressed && runtime.initialized()) {
            automaticGcSuppressedAtAllocation.store(
                runtime.allocationStats().allocatedBytes,
                std::memory_order_release);
        }
        automaticGcResumeAfterNanos.store(0, std::memory_order_release);
        automaticGcCondition.notify_all();
        return;
    }

    const bool wasSuppressed = automaticGcSuppressed.load(
        std::memory_order_acquire);
    if (runtime.initialized()) {
        const std::uint64_t allocated =
            runtime.allocationStats().allocatedBytes;
        const std::uint64_t suppressedAt =
            automaticGcSuppressedAtAllocation.load(
                std::memory_order_acquire);
        const std::uint64_t suppressedDebt = allocated >= suppressedAt
            ? allocated - suppressedAt : 0;

        // A loading window often creates hundreds of MiB of temporary and
        // long-lived objects.  Let a concurrent full cycle classify that
        // mixed cohort after the destination state has reached its first
        // stable frames; a single huge young evacuation causes exactly the
        // latency spike suppression was intended to avoid.
        if (wasSuppressed &&
            suppressedDebt >= automaticYoungMinimumHeadroom) {
            automaticFullGcRequested.store(true,
                                            std::memory_order_release);
        }
        // A small loading window does not request a Full. Resuming it with the
        // Full collector's maximum headroom used to skip up to 256 MiB of
        // ordinary nursery work and create one giant Young pause. Preserve
        // the adaptive Young cadence instead. A requested Full still runs
        // immediately because fullRequested bypasses this allocation gate.
        const std::uint64_t resumeHeadroom = automaticYoungGcEnabled
            ? currentAutomaticYoungHeadroom.load(std::memory_order_acquire)
            : automaticGcMaximumHeadroom;
        nextAutomaticGcAllocation.store(
            allocated + resumeHeadroom, std::memory_order_release);
        automaticGcResumeAfterNanos.store(
            steadyTimestampNanos() +
                automaticGcResumeGraceMilliseconds * 1'000'000U,
            std::memory_order_release);
    }
    // Publish the resume only after the request, threshold and grace deadline
    // are visible, so the collector cannot race into a catch-up young cycle.
    automaticGcSuppressed.store(false, std::memory_order_release);
    automaticGcCondition.notify_all();
}

extern "C" std::uint64_t __hxcpp_gc_last_relocated_objects() {
    return lastRelocatedObjects.load(std::memory_order_acquire);
}

double __hxcpp_gc_used_bytes() {
    return static_cast<double>(::hx::gc::Runtime::instance().usedBytes());
}

int __hxcpp_gc_reserved_bytes() {
    const std::uint64_t bytes =
        ::hx::gc::Runtime::instance().reservedBytes();
    return bytes > static_cast<std::uint64_t>(INT_MAX)
        ? INT_MAX : static_cast<int>(bytes);
}

void __hxcpp_stdlibs_boot() {
    (void)std::setlocale(LC_ALL, "");
    (void)std::setlocale(LC_NUMERIC, "C");
}

void __hxcpp_boot_std_classes() {}

double __hxcpp_gc_mem_info(int kind) {
#if defined(_WIN32)
    if (kind == 8) { // NovaFlare process private bytes
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(
                GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                sizeof(counters)) != FALSE) {
            return static_cast<double>(counters.PrivateUsage);
        }
        return 0.0;
    }
#endif
    const auto stats = ::hx::gc::Runtime::instance().heapStats();
    switch (kind) {
    case 0: // MEM_INFO_USAGE
    case 2: // MEM_INFO_CURRENT
        return static_cast<double>(stats.usedBytes);
    case 1: // MEM_INFO_RESERVED
        return static_cast<double>(stats.reservedBytes);
    case 3: // MEM_INFO_LARGE
        return static_cast<double>(stats.largeBytes);
    case 4: // NovaGC committed physical heap pages
        return static_cast<double>(stats.committedBytes);
    case 5: // NovaGC reusable virtual reservations
        return static_cast<double>(stats.reusableBytes);
    case 6: // NovaGC survivor occupancy
        return static_cast<double>(stats.survivorBytes);
    case 7: // NovaGC old-generation occupancy
        return static_cast<double>(stats.oldBytes);
    default:
        return 0.0;
    }
}

int __hxcpp_gc_trace(::hx::Class sought, bool printInstances) {
    (void)sought;
    (void)printInstances;
    return 0;
}

void __hxcpp_gc_do_not_kill(::Dynamic object) {
    void* managed = object.managedPointer();
    if (managed == nullptr)
        return;
    const auto handle = ::hx::gc::Runtime::instance().createHandle(
        managed, ::hx::gc::HandleKind::Strong);
    std::scoped_lock lock(immortalMutex);
    immortalHandles.push_back(handle);
}

::Dynamic __hxcpp_get_next_zombie() { return nullptr; }

void __hxcpp_enter_gc_free_zone() {
    auto& runtime = ::hx::gc::Runtime::instance();
    if (runtime.currentThreadAttached())
        runtime.enterNativeBlocking();
}

void __hxcpp_exit_gc_free_zone() {
    auto& runtime = ::hx::gc::Runtime::instance();
    if (runtime.currentThreadAttached())
        runtime.leaveNativeBlocking();
}

void __hxcpp_set_minimum_free_space(int bytes) { (void)bytes; }
void __hxcpp_set_target_free_space_percentage(int percentage) {
    (void)percentage;
}
void __hxcpp_set_minimum_working_memory(int bytes) { (void)bytes; }

extern "C" void __hxcpp_set_critical_error_handler(::Dynamic handler) {
    std::call_once(criticalErrorRoot, [] {
        ::hx::gc::Runtime::instance().registerStaticRoot(
            criticalErrorHandler.mPtr);
    });
    std::scoped_lock lock(criticalErrorMutex);
    criticalErrorHandler = handler;
}

extern "C" bool __hxcpp_invoke_critical_error_handler(::String message) {
    ::Dynamic handler;
    {
        std::scoped_lock lock(criticalErrorMutex);
        handler = criticalErrorHandler;
    }
    if (handler.isNull())
        return false;
    (void)handler(::Dynamic(message));
    return true;
}

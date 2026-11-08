#include <hxcpp.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <string>
#include <thread>

namespace {

struct Blob final {
    std::byte data[64U * 1024U]{};
};

const hx::gc::TypeDescriptor BlobType{
    0x54454c454d455452ULL,
    "test.TelemetryBlob",
    nullptr,
    sizeof(Blob),
    alignof(Blob),
    0,
    hx::gc::TypeNone,
    0,
    nullptr,
    nullptr,
    nullptr,
};

void setEnvironment(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

[[nodiscard]] std::string readAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const auto base = std::filesystem::temp_directory_path() /
        ("novagc-telemetry-" + std::to_string(nonce));
    const auto jsonPath = base.string() + ".jsonl";
    const auto csvPath = base.string() + ".csv";
    setEnvironment("HXCPP_NOVAGC_JSON_LOG", jsonPath);
    setEnvironment("HXCPP_NOVAGC_CSV_LOG", csvPath);
    setEnvironment("HXCPP_NOVAGC_INITIAL_TRIGGER_MB", "16");
    setEnvironment("HXCPP_NOVAGC_YOUNG_TRIGGER_MB", "16");
    setEnvironment("HXCPP_NOVAGC_MIN_YOUNG_MB", "16");
    setEnvironment("HXCPP_NOVAGC_MAX_YOUNG_MB", "16");
    setEnvironment("HXCPP_NOVAGC_SOFT_MAX_HEAP_MB", "4096");
    setEnvironment("HXCPP_NOVAGC_FULL_PROMOTION_MB", "4096");
    setEnvironment("HXCPP_NOVAGC_FULL_YOUNG_CYCLES", "1");
    setEnvironment("HXCPP_NOVAGC_FULL_MIN_MAJOR_GROWTH_MB", "128");
    setEnvironment("HXCPP_NOVAGC_UNCOMMIT_DELAY_SECONDS", "0");

    __hxcpp_nova_initialize();
    auto& runtime = hx::gc::Runtime::instance();
    for (int index = 0; index < 400; ++index) {
        auto* blob = ::new (runtime.allocate(BlobType)) Blob();
        runtime.publish(blob);
        if ((index & 15) == 0)
            runtime.safepointPoll();
    }
    bool cycleGateHeld = false;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        runtime.safepointPoll();
        std::error_code error;
        if (std::filesystem::exists(jsonPath, error) && !error &&
            std::filesystem::file_size(jsonPath, error) != 0 && !error &&
            std::filesystem::exists(csvPath, error) && !error &&
            std::filesystem::file_size(csvPath, error) != 0 && !error) {
            const std::string partialJson = readAll(jsonPath);
            cycleGateHeld =
                partialJson.find("\"major_trigger_reason\":"
                                 "\"cycle_growth_wait\"") !=
                    std::string::npos &&
                partialJson.find("\"kind\":\"full\"") ==
                    std::string::npos;
            if (cycleGateHeld)
                break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Explicit requests are correctness/safety signals and must bypass the
    // cadence growth gate. They also provide a Full row that verifies the
    // trigger-reason and reclaimed-byte telemetry added for Major tuning.
    __hxcpp_gc_request_full();
    bool explicitFullObserved = false;
    const auto fullDeadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < fullDeadline) {
        runtime.safepointPoll();
        const std::string partialJson = readAll(jsonPath);
        explicitFullObserved =
            partialJson.find("\"kind\":\"full\"") !=
                std::string::npos &&
            partialJson.find("\"major_trigger_reason\":"
                             "\"explicit_request\"") !=
                std::string::npos;
        if (explicitFullObserved)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    __hxcpp_nova_shutdown();

    const std::string json = readAll(jsonPath);
    const std::string csv = readAll(csvPath);
    std::error_code ignored;
    std::filesystem::remove(jsonPath, ignored);
    std::filesystem::remove(csvPath, ignored);
    if (json.find("\"kind\":\"young\"") == std::string::npos ||
        json.find("\"pause_p99_us\"") == std::string::npos ||
        json.find("\"force_in_place_reason_mask\"") ==
            std::string::npos ||
        json.find("\"initial_remembered_overflow_regions\"") ==
            std::string::npos ||
        json.find("\"eden_bytes\"") == std::string::npos ||
        json.find("\"current_tenuring_threshold\"") ==
            std::string::npos ||
        json.find("\"address_space_reserved_bytes\"") ==
            std::string::npos ||
        json.find("\"major_growth_gate_passed\":0") ==
            std::string::npos ||
        json.find("\"major_min_growth_bytes\":134217728") ==
            std::string::npos ||
        json.find("\"reclaimed_bytes\"") == std::string::npos ||
        csv.find("timestamp_us,kind,cycle") != 0 ||
        csv.find("force_in_place_reason_mask") == std::string::npos ||
        csv.find("major_trigger_reason") == std::string::npos ||
        csv.find("major_growth_bytes") == std::string::npos ||
        csv.find("eden_bytes") == std::string::npos ||
        csv.find(",young,") == std::string::npos ||
        !cycleGateHeld || !explicitFullObserved) {
        std::cerr << "structured telemetry pipeline failed"
                  << " json_bytes=" << json.size()
                  << " csv_bytes=" << csv.size()
                  << " cycle_gate_held=" << cycleGateHeld
                  << " explicit_full_observed=" << explicitFullObserved
                  << '\n';
        return 1;
    }
    std::cout << "hxcpp JSONL/CSV GC telemetry passed\n";
    return 0;
}

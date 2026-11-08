#include <hxcpp.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct Blob final {
    std::byte data[64U * 1024U]{};
};

const hx::gc::TypeDescriptor BlobType{
    0x534f46544d415847ULL,
    "test.SoftMaxBlob",
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

[[nodiscard]] std::size_t csvColumns(std::string_view row) {
    return 1U + static_cast<std::size_t>(
        std::count(row.begin(), row.end(), ','));
}

} // namespace

int main() {
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const auto base = std::filesystem::temp_directory_path() /
        ("novagc-softmax-" + std::to_string(nonce));
    const auto jsonPath = base.string() + ".jsonl";
    const auto csvPath = base.string() + ".csv";
    setEnvironment("HXCPP_NOVAGC_JSON_LOG", jsonPath);
    setEnvironment("HXCPP_NOVAGC_CSV_LOG", csvPath);
    setEnvironment("HXCPP_NOVAGC_INITIAL_TRIGGER_MB", "4");
    setEnvironment("HXCPP_NOVAGC_YOUNG_TRIGGER_MB", "4");
    setEnvironment("HXCPP_NOVAGC_MIN_YOUNG_MB", "4");
    setEnvironment("HXCPP_NOVAGC_MAX_YOUNG_MB", "4");
    setEnvironment("HXCPP_NOVAGC_SOFT_MAX_HEAP_MB", "1");
    setEnvironment("HXCPP_NOVAGC_FULL_PROMOTION_MB", "4096");
    setEnvironment("HXCPP_NOVAGC_FULL_YOUNG_CYCLES", "1024");
    setEnvironment("HXCPP_NOVAGC_FULL_MIN_MAJOR_GROWTH_MB", "64");
    setEnvironment("HXCPP_NOVAGC_UNCOMMIT_DELAY_SECONDS", "0");

    __hxcpp_nova_initialize();
    auto& runtime = hx::gc::Runtime::instance();
    std::vector<hx::gc::Handle> roots;
    roots.reserve(128);
    for (int index = 0; index < 128; ++index) {
        auto* blob = ::new (runtime.allocate(BlobType)) Blob();
        runtime.publish(blob);
        roots.push_back(runtime.createHandle(
            blob, hx::gc::HandleKind::Strong));
        if ((index & 7) == 0)
            runtime.safepointPoll();
    }

    bool softMaxGateHeld = false;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        runtime.safepointPoll();
        const std::string json = readAll(jsonPath);
        softMaxGateHeld =
            json.find("\"major_trigger_reason\":"
                      "\"soft_max_growth_wait\"") != std::string::npos &&
            json.find("\"major_soft_max_bytes\":1048576") !=
                std::string::npos &&
            json.find("\"major_soft_max_exceeded\":1") !=
                std::string::npos &&
            json.find("\"major_soft_max_rate_ready\":1") !=
                std::string::npos &&
            json.find("\"major_soft_max_due\":0") !=
                std::string::npos &&
            json.find("\"kind\":\"full\"") == std::string::npos;
        if (softMaxGateHeld)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    for (const auto handle : roots)
        runtime.releaseHandle(handle);
    __hxcpp_nova_shutdown();

    const std::string json = readAll(jsonPath);
    const std::string csv = readAll(csvPath);
    const std::size_t headerEnd = csv.find('\n');
    const std::size_t rowEnd = headerEnd == std::string::npos
        ? std::string::npos : csv.find('\n', headerEnd + 1U);
    const bool csvShapeValid =
        headerEnd != std::string::npos &&
        rowEnd != std::string::npos &&
        csvColumns(std::string_view(csv).substr(0, headerEnd)) ==
            csvColumns(std::string_view(csv).substr(
                headerEnd + 1U, rowEnd - headerEnd - 1U));
    std::error_code ignored;
    std::filesystem::remove(jsonPath, ignored);
    std::filesystem::remove(csvPath, ignored);
    if (!softMaxGateHeld ||
        csv.find("major_soft_max_bytes") == std::string::npos ||
        csv.find("major_soft_max_exceeded") == std::string::npos ||
        csv.find("major_soft_max_rate_ready") == std::string::npos ||
        !csvShapeValid) {
        std::cerr << "soft-max Major growth gate failed"
                  << " json_bytes=" << json.size()
                  << " csv_bytes=" << csv.size()
                  << " gate_held=" << softMaxGateHeld
                  << " csv_shape_valid=" << csvShapeValid << '\n';
        return 1;
    }
    std::cout << "hxcpp soft-max Major growth gate passed\n";
    return 0;
}

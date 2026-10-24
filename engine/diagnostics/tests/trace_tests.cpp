#include "raiders/TraceWriter.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#if RA_ENABLE_PROFILING
#include <nlohmann/json.hpp>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <fstream>
#include <limits>
#include <thread>

namespace {
using Json = nlohmann::json;
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
raiders::TraceConfig Config(const std::filesystem::path& directory) {
    raiders::TraceConfig config;
    config.directory = directory;
    config.session_id = "test.capture"; config.process_instance_id = "test.process";
    config.clock_domain_id = "test.steady"; config.build_id = "test.build";
    config.process_name = "Game \"quoted\"\n\xc3\xa7";
    config.events_per_chunk = 4;
    return config;
}
Json Read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    Require(stream.good(), "Missing trace output");
    return Json::parse(stream);
}
raiders::ProfileEvent Scope(std::string_view name = "work", std::uint64_t start = 100, std::uint64_t duration = 10) {
    raiders::ProfileEvent event;
    std::copy(name.begin(), name.end(), event.name.begin());
    std::string category = "runtime";
    std::copy(category.begin(), category.end(), event.category.begin());
    event.thread_id = 1; event.timestamp_us = start; event.duration_us = duration;
    return event;
}
bool Partial(const Json& trace) {
    for (const auto& collector : trace.at("collectors"))
        if (collector.at("coverage").at("status") == "partial") return true;
    return false;
}
void CrashChild(const std::filesystem::path& directory) {
    raiders::TraceWriter writer(Config(directory));
    writer.AppendScope(Scope("committed"));
    writer.AsyncBegin(1, "tasks", "unfinished", "operation.1");
    writer.Checkpoint();
    writer.AppendScope(Scope("not-checkpointed", 120));
    // Simulate a torn, unpublished chunk: recovery must never scan for it.
    std::ofstream(directory / "chunk-999.json.tmp") << "{unfinished";
    TerminateProcess(GetCurrentProcess(), 87);
    std::abort();
}
void RunCrashChild(const std::filesystem::path& directory) {
    wchar_t executable[32768];
    Require(GetModuleFileNameW(nullptr, executable, 32768) != 0, "Cannot locate test executable");
    auto command = std::wstring(L"\"") + executable + L"\" --crash \"" + directory.native() + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    Require(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process), "Cannot start crash test child");
    auto wait = WaitForSingleObject(process.hProcess, 10000);
    if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 88);
    DWORD exitCode = 0; GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    Require(wait == WAIT_OBJECT_0 && exitCode == 87, "Crash child did not reach checkpoint");
}
}
#endif

int wmain(int argc, wchar_t** argv) {
    try {
#if RA_ENABLE_PROFILING
        if (argc == 3 && std::wstring_view(argv[1]) == L"--crash") { CrashChild(argv[2]); return EXIT_FAILURE; }
        auto root = std::filesystem::current_path() / ("trace-test-" + std::to_string(raiders::ProfileCapture::ClockNowUs()));
        std::filesystem::create_directories(root);
        {
            raiders::ProfileCapture capture(32);
            {
                RA_PROFILE_SCOPE(capture, "runtime", "outer\"\n\xc3\xa7");
                { RA_PROFILE_SCOPE(capture, "physics", "inner", (raiders::ProfileContext{"command.1", "asset.1"})); }
            }
            auto config = Config(root / "normal");
            raiders::TraceWriter writer(config);
            for (const auto& event : capture.Drain()) Require(writer.AppendScope(event), "Scope export failed");
            capture.Stop();
            Require(writer.ObserveProfile(capture.Stats()), "Profile coverage rejected");
            Require(writer.RegisterThread(1, "Main \"thread\""), "Thread metadata failed");
            Require(writer.Counter(1, "queue", "pending", "queue.count", 7), "Counter export failed");
            Require(writer.AsyncBegin(1, "assets", "load \"bundle\"", "load.1", {"command.1"}), "Async begin failed");
            std::thread other([&] { Require(writer.AsyncEnd(2, "load.1"), "Cross-thread async end failed"); });
            other.join();
            Require(!raiders::TraceWriter::Recover(config.directory), "Recovery allowed an active capture");
            Require(writer.Finalize() && writer.Finalize(), "Finalization failed or not idempotent");
            auto trace = Read(config.directory / "trace.json");
            Require(!Partial(trace), "Loss-free session incorrectly marked partial");
            Require(trace["processes"][0]["name"] == config.process_name, "Escaped process name corrupted");
            int durations = 0, counters = 0, begins = 0, ends = 0, metadata = 0;
            for (const auto& event : trace["traceEvents"]) {
                if (event["ph"] == "X") ++durations;
                if (event["ph"] == "C") { ++counters; Require(event["args"]["queue.count"] == 7, "Counter lost value"); }
                if (event["ph"] == "b") ++begins;
                if (event["ph"] == "e") ++ends;
                if (event["ph"] == "M") ++metadata;
            }
            Require(durations == 2 && counters == 1 && begins == 1 && ends == 1 && metadata >= 3, "Expected trace phases missing");
            Require(!writer.Counter(1, "closed", "closed", "queue.count", 1), "Finalized writer accepted event");
        }
        {
            raiders::ProfileCapture capture(1);
            { RA_PROFILE_SCOPE(capture, "bounded", "kept"); }
            { RA_PROFILE_SCOPE(capture, "bounded", "dropped"); }
            raiders::TraceWriter writer(Config(root / "drops"));
            for (const auto& event : capture.Drain()) writer.AppendScope(event);
            capture.Stop(); writer.ObserveProfile(capture.Stats());
            Require(writer.Finalize(), "Drop capture failed");
            auto trace = Read(root / "drops/trace.json");
            Require(Partial(trace) && trace["collectors"][0]["coverage"]["dropped_records"] == 1, "Producer drops lost");
        }
        {
            auto config = Config(root / "limits"); config.events_per_chunk = 1; config.max_chunks = 3;
            raiders::TraceWriter writer(config);
            Require(writer.AppendScope(Scope()), "First scope rejected within budget");
            Require(!writer.AppendScope(Scope("overflow", 120)), "Disk event budget exceeded");
            Require(writer.Stats().dropped > 0 && writer.Finalize(), "Writer loss not reported");
            Require(Partial(Read(config.directory / "trace.json")), "Writer drops not reflected in coverage");
        }
        {
            auto config = Config(root / "validation"); config.max_threads = 1; config.max_async = 1;
            raiders::TraceWriter writer(config);
            Require(!writer.Counter(1, "queue", "bad", "missing-unit", 1), "Unit-free counter accepted");
            Require(!writer.Counter(1, "queue", "bad", "queue.count", std::numeric_limits<double>::infinity()), "Nonfinite counter accepted");
            Require(!writer.AsyncEnd(1, "unknown"), "Unpaired async end accepted");
            Require(writer.AsyncBegin(1, "tasks", "unfinished", "a.1"), "Async begin rejected");
            Require(!writer.AsyncBegin(1, "tasks", "duplicate", "a.1"), "Duplicate async ID accepted");
            Require(!writer.AsyncBegin(1, "tasks", "capacity", "a.2"), "Async budget exceeded");
            Require(!writer.RegisterThread(2, "too many"), "Thread budget exceeded");
            auto invalid = Scope(); invalid.name.fill('x');
            Require(!writer.AppendScope(invalid), "Unterminated scope field accepted");
            Require(writer.Finalize(), "Partial export failed");
            auto trace = Read(config.directory / "trace.json");
            for (const auto& event : trace["traceEvents"]) Require(event["ph"] != "b", "Unfinished async event retained");
            Require(Partial(trace), "Invalid/unfinished capture reported complete");
        }
        {
            auto directory = root / L"crash-\u00e7";
            RunCrashChild(directory);
            Require(!std::filesystem::exists(directory / "trace.json"), "Crash finalized output unexpectedly");
            Require(raiders::TraceWriter::Recover(directory), "Interrupted capture not recoverable");
            auto trace = Read(directory / "recovered.json");
            Require(Partial(trace), "Recovery incorrectly reported complete coverage");
            int scopes = 0;
            for (const auto& event : trace["traceEvents"]) if (event["ph"] == "X") {
                ++scopes; Require(event["name"] == "committed", "Uncommitted scope recovered");
            }
            Require(scopes == 1, "Committed scope missing from recovery");
            Require(!raiders::TraceWriter::Recover(directory), "Recovery overwrote existing output");
            std::filesystem::copy_file(directory / "recovered.json", root / "recovered-validation.json");
            std::filesystem::remove(directory / "recovered.json");
            std::ofstream(directory / "chunk-0.json", std::ios::app) << ' ';
            Require(!raiders::TraceWriter::Recover(directory), "Corrupt indexed chunk accepted");
        }
        {
            raiders::TraceWriter writer(Config(root / "overlap"));
            Require(writer.AppendScope(Scope("first", 100, 20)), "First overlap scope rejected");
            Require(writer.AppendScope(Scope("crossing", 110, 30)), "Second overlap scope rejected");
            Require(writer.Finalize(), "Overlap export failed");
            auto trace = Read(root / "overlap/trace.json");
            int scopes = 0;
            for (const auto& event : trace["traceEvents"]) if (event["ph"] == "X") ++scopes;
            Require(scopes == 1 && Partial(trace), "Overlapping nonnested scopes exported as valid nesting");
        }
        {
            auto directory = root / "io-error";
            raiders::TraceWriter writer(Config(directory));
            writer.AppendScope(Scope());
            // Force index replacement failure without requiring disk-full privileges.
            auto index = directory / "recovery-index.json";
            std::filesystem::remove(index); std::filesystem::create_directory(index);
            Require(!writer.Checkpoint() && writer.Stats().io_errors == 1, "I/O failure not surfaced");
            Require(!writer.Finalize(), "Failed writer reported success");
        }
        {
            bool rejected = false;
            try { raiders::TraceWriter writer(Config(root / "normal")); } catch (const std::exception&) { rejected = true; }
            Require(rejected, "Existing capture directory reused");
        }
        std::cout << "Trace export contracts passed; captures: " << root.string() << '\n';
#else
        (void)argc; (void)argv;
        raiders::TraceConfig config; config.directory = "disabled-trace-must-not-exist";
        if (std::filesystem::exists(config.directory)) return EXIT_FAILURE;
        raiders::TraceWriter writer(config);
        if (writer.AppendScope({}) || writer.Counter(1, "off", "off", "value.count", 1) ||
            writer.AsyncBegin(1, "off", "off", "id.1") || writer.Checkpoint() || writer.Finalize() ||
            raiders::TraceWriter::Recover(config.directory) || std::filesystem::exists(config.directory)) return EXIT_FAILURE;
#endif
        return EXIT_SUCCESS;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
}

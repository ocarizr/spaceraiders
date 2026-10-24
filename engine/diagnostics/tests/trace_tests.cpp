#include "raiders/TraceWriter.h"

#include <cstdlib>
#include <gtest/gtest.h>
#include <stdexcept>

#if RA_ENABLE_PROFILING
#include <nlohmann/json.hpp>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <fstream>
#include <limits>
#include <thread>

namespace
{
using Json = nlohmann::json;

raiders::TraceConfig Config(const std::filesystem::path& directory)
{
    raiders::TraceConfig config;
    config.directory = directory;
    config.session_id = "test.capture";
    config.process_instance_id = "test.process";
    config.clock_domain_id = "test.steady";
    config.build_id = "test.build";
    config.process_name = "Game \"quoted\"\n\xc3\xa7";
    config.events_per_chunk = 4;
    return config;
}

Json Read(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    EXPECT_TRUE(stream.good()) << "Missing trace output";
    return Json::parse(stream);
}

raiders::ProfileEvent Scope(std::string_view name = "work", std::uint64_t start = 100,
                            std::uint64_t duration = 10)
{
    raiders::ProfileEvent event;
    std::copy(name.begin(), name.end(), event.name.begin());
    std::string category = "runtime";
    std::copy(category.begin(), category.end(), event.category.begin());
    event.thread_id = 1;
    event.timestamp_us = start;
    event.duration_us = duration;
    return event;
}

bool Partial(const Json& trace)
{
    for (const auto& collector : trace.at("collectors"))
    {
        if (collector.at("coverage").at("status") == "partial")
        {
            return true;
        }
    }

    return false;
}

void CrashChild(const std::filesystem::path& directory)
{
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

void RunCrashChild(const std::filesystem::path& directory)
{
    wchar_t executable[32768];
    ASSERT_NE(GetModuleFileNameW(nullptr, executable, 32768), 0) << "Cannot locate test executable";
    auto command = std::wstring(L"\"") + executable + L"\" --crash \"" + directory.native() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    ASSERT_TRUE(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        << "Cannot start crash test child";
    auto wait = WaitForSingleObject(process.hProcess, 10000);
    if (wait != WAIT_OBJECT_0)
    {
        TerminateProcess(process.hProcess, 88);
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    ASSERT_TRUE(wait == WAIT_OBJECT_0 && exitCode == 87) << "Crash child did not reach checkpoint";
}
} // namespace
#endif

#if RA_ENABLE_PROFILING
class TraceWriterTest : public testing::Test
{
  protected:
    std::filesystem::path root;

    void SetUp() override
    {
        root = std::filesystem::current_path() /
               ("TraceWriterTest-" + std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(raiders::ProfileCapture::ClockNowUs()));
        std::filesystem::create_directories(root);
    }

    void TearDown() override
    {
        if (!HasFailure())
        {
            std::error_code error;
            std::filesystem::remove_all(root, error);
            EXPECT_FALSE(error) << error.message();
        }
    }
};

TEST_F(TraceWriterTest, CompleteSession)
{
    raiders::ProfileCapture capture(32);
    {
        RA_PROFILE_SCOPE(capture, "runtime", "outer\"\n\xc3\xa7");
        {
            RA_PROFILE_SCOPE(capture, "physics", "inner",
                             (raiders::ProfileContext{"command.1", "asset.1"}));
        }
    }
    auto config = Config(root / "normal");
    raiders::TraceWriter writer(config);
    for (const auto& event : capture.Drain())
    {
        ASSERT_TRUE(writer.AppendScope(event)) << "Scope export failed";
    }

    capture.Stop();
    ASSERT_TRUE(writer.ObserveProfile(capture.Stats())) << "Profile coverage rejected";
    ASSERT_TRUE(writer.RegisterThread(1, "Main \"thread\"")) << "Thread metadata failed";
    ASSERT_TRUE(writer.Counter(1, "queue", "pending", "queue.count", 7)) << "Counter export failed";
    ASSERT_TRUE(writer.AsyncBegin(1, "assets", "load \"bundle\"", "load.1", {"command.1"}))
        << "Async begin failed";
    std::thread other(
        [&]
        {
            ASSERT_TRUE(writer.AsyncEnd(2, "load.1")) << "Cross-thread async end failed";
        });

    other.join();
    ASSERT_FALSE(raiders::TraceWriter::Recover(config.directory))
        << "Recovery allowed an active capture";
    ASSERT_TRUE(writer.Finalize() && writer.Finalize()) << "Finalization failed or not idempotent";
    auto trace = Read(config.directory / "trace.json");
    ASSERT_FALSE(Partial(trace)) << "Loss-free session incorrectly marked partial";
    ASSERT_EQ(trace["processes"][0]["name"], config.process_name)
        << "Escaped process name corrupted";
    int durations = 0, counters = 0, begins = 0, ends = 0, metadata = 0;
    for (const auto& event : trace["traceEvents"])
    {
        if (event["ph"] == "X")
        {
            ++durations;
        }

        if (event["ph"] == "C")
        {
            ++counters;
            ASSERT_EQ(event["args"]["queue.count"], 7) << "Counter lost value";
        }

        if (event["ph"] == "b")
        {
            ++begins;
        }

        if (event["ph"] == "e")
        {
            ++ends;
        }

        if (event["ph"] == "M")
        {
            ++metadata;
        }
    }

    ASSERT_TRUE(durations == 2 && counters == 1 && begins == 1 && ends == 1 && metadata >= 3)
        << "Expected trace phases missing";
    ASSERT_FALSE(writer.Counter(1, "closed", "closed", "queue.count", 1))
        << "Finalized writer accepted event";
}

TEST_F(TraceWriterTest, ProducerLoss)
{
    raiders::ProfileCapture capture(1);
    {
        RA_PROFILE_SCOPE(capture, "bounded", "kept");
    }
    {
        RA_PROFILE_SCOPE(capture, "bounded", "dropped");
    }
    raiders::TraceWriter writer(Config(root / "drops"));
    for (const auto& event : capture.Drain())
    {
        writer.AppendScope(event);
    }

    capture.Stop();
    writer.ObserveProfile(capture.Stats());
    ASSERT_TRUE(writer.Finalize()) << "Drop capture failed";
    auto trace = Read(root / "drops/trace.json");
    ASSERT_TRUE(Partial(trace) && trace["collectors"][0]["coverage"]["dropped_records"] == 1)
        << "Producer drops lost";
}

TEST_F(TraceWriterTest, WriterBudget)
{
    auto config = Config(root / "limits");
    config.events_per_chunk = 1;
    config.max_chunks = 3;
    raiders::TraceWriter writer(config);
    ASSERT_TRUE(writer.AppendScope(Scope())) << "First scope rejected within budget";
    ASSERT_FALSE(writer.AppendScope(Scope("overflow", 120))) << "Disk event budget exceeded";
    ASSERT_TRUE(writer.Stats().dropped > 0 && writer.Finalize()) << "Writer loss not reported";
    ASSERT_TRUE(Partial(Read(config.directory / "trace.json")))
        << "Writer drops not reflected in coverage";
}

TEST_F(TraceWriterTest, ValidationAndUnfinishedAsync)
{
    auto config = Config(root / "validation");
    config.max_threads = 1;
    config.max_async = 1;
    raiders::TraceWriter writer(config);
    ASSERT_FALSE(writer.Counter(1, "queue", "bad", "missing-unit", 1))
        << "Unit-free counter accepted";
    ASSERT_FALSE(
        writer.Counter(1, "queue", "bad", "queue.count", std::numeric_limits<double>::infinity()))
        << "Nonfinite counter accepted";
    ASSERT_FALSE(writer.AsyncEnd(1, "unknown")) << "Unpaired async end accepted";
    ASSERT_TRUE(writer.AsyncBegin(1, "tasks", "unfinished", "a.1")) << "Async begin rejected";
    ASSERT_FALSE(writer.AsyncBegin(1, "tasks", "duplicate", "a.1"))
        << "Duplicate async ID accepted";
    ASSERT_FALSE(writer.AsyncBegin(1, "tasks", "capacity", "a.2")) << "Async budget exceeded";
    ASSERT_FALSE(writer.RegisterThread(2, "too many")) << "Thread budget exceeded";
    auto invalid = Scope();
    invalid.name.fill('x');
    ASSERT_FALSE(writer.AppendScope(invalid)) << "Unterminated scope field accepted";
    ASSERT_TRUE(writer.Finalize()) << "Partial export failed";
    auto trace = Read(config.directory / "trace.json");
    for (const auto& event : trace["traceEvents"])
    {
        ASSERT_NE(event["ph"], "b") << "Unfinished async event retained";
    }

    ASSERT_TRUE(Partial(trace)) << "Invalid/unfinished capture reported complete";
}

TEST_F(TraceWriterTest, CrashRecovery)
{
    auto directory = root / L"crash-\u00e7";
    RunCrashChild(directory);
    ASSERT_FALSE(std::filesystem::exists(directory / "trace.json"))
        << "Crash finalized output unexpectedly";
    ASSERT_TRUE(raiders::TraceWriter::Recover(directory)) << "Interrupted capture not recoverable";
    auto trace = Read(directory / "recovered.json");
    ASSERT_TRUE(Partial(trace)) << "Recovery incorrectly reported complete coverage";
    int scopes = 0;
    for (const auto& event : trace["traceEvents"])
    {
        if (event["ph"] == "X")
        {
            ++scopes;
            ASSERT_EQ(event["name"], "committed") << "Uncommitted scope recovered";
        }
    }

    ASSERT_EQ(scopes, 1) << "Committed scope missing from recovery";
    ASSERT_FALSE(raiders::TraceWriter::Recover(directory)) << "Recovery overwrote existing output";
    std::filesystem::copy_file(directory / "recovered.json", root / "recovered-validation.json");
    std::filesystem::remove(directory / "recovered.json");
    std::ofstream(directory / "chunk-0.json", std::ios::app) << ' ';
    ASSERT_FALSE(raiders::TraceWriter::Recover(directory)) << "Corrupt indexed chunk accepted";
}

TEST_F(TraceWriterTest, OverlappingScopes)
{
    raiders::TraceWriter writer(Config(root / "overlap"));
    ASSERT_TRUE(writer.AppendScope(Scope("first", 100, 20))) << "First overlap scope rejected";
    ASSERT_TRUE(writer.AppendScope(Scope("crossing", 110, 30))) << "Second overlap scope rejected";
    ASSERT_TRUE(writer.Finalize()) << "Overlap export failed";
    auto trace = Read(root / "overlap/trace.json");
    int scopes = 0;
    for (const auto& event : trace["traceEvents"])
    {
        if (event["ph"] == "X")
        {
            ++scopes;
        }
    }

    ASSERT_TRUE(scopes == 1 && Partial(trace))
        << "Overlapping nonnested scopes exported as valid nesting";
}

TEST_F(TraceWriterTest, CheckpointIoFailure)
{
    auto directory = root / "io-error";
    raiders::TraceWriter writer(Config(directory));
    writer.AppendScope(Scope());
    // Force index replacement failure without requiring disk-full privileges.
    auto index = directory / "recovery-index.json";
    std::filesystem::remove(index);
    std::filesystem::create_directory(index);
    ASSERT_TRUE(!writer.Checkpoint() && writer.Stats().io_errors == 1)
        << "I/O failure not surfaced";
    ASSERT_FALSE(writer.Finalize()) << "Failed writer reported success";
}

TEST_F(TraceWriterTest, RejectExistingDirectory)
{
    std::filesystem::create_directory(root / "normal");
    ASSERT_THROW({ raiders::TraceWriter writer(Config(root / "normal")); }, std::exception);
}

#else
TEST(TraceWriterDisabled, DoesNotWriteFiles)
{
    raiders::TraceConfig config;
    config.directory = "disabled-trace-must-not-exist";
    ASSERT_FALSE(std::filesystem::exists(config.directory));
    raiders::TraceWriter writer(config);
    EXPECT_FALSE(writer.AppendScope({}));
    EXPECT_FALSE(writer.Counter(1, "off", "off", "value.count", 1));
    EXPECT_FALSE(writer.AsyncBegin(1, "off", "off", "id.1"));
    EXPECT_FALSE(writer.Checkpoint());
    EXPECT_FALSE(writer.Finalize());
    EXPECT_FALSE(raiders::TraceWriter::Recover(config.directory));
    EXPECT_FALSE(std::filesystem::exists(config.directory));
}
#endif
int wmain(int argc, wchar_t** argv)
{
#if RA_ENABLE_PROFILING
    if (argc == 3 && std::wstring_view(argv[1]) == L"--crash")
    {
        CrashChild(argv[2]);
        return EXIT_FAILURE;
    }
#endif
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

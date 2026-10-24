#include "raiders/Log.h"
#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdlib>
#include <barrier>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <thread>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
raiders::LogConfig Config(const std::filesystem::path& path) {
    raiders::LogConfig config;
    config.path = path;
    config.session_id = "test.capture";
    config.process_instance_id = "test.process";
    config.clock_domain_id = "test.steady";
    return config;
}
std::vector<nlohmann::json> Read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    Require(stream.good(), "Log file not readable");
    std::vector<nlohmann::json> result;
    std::string line;
    while (std::getline(stream, line)) result.push_back(nlohmann::json::parse(line));
    return result;
}
}

int main() {
    try {
        // All captures live in the ignored build tree, including on failure.
        const auto root = std::filesystem::current_path() /
            ("logging-test-" + std::to_string(raiders::Log::ClockNowUs()));
        std::filesystem::create_directories(root);
        raiders::LogContext context;
        context.thread_id = "main.1";
        context.entity_id = "player.1";
        context.correlation_id = "command.1";
        {
            raiders::Log log(Config(root / L"structured-\u00e7.jsonl"));
            const auto before = raiders::Log::ClockNowUs();
            Require(log.Write(raiders::LogLevel::Info, "assets", context, "quote\" slash\\ newline\n tab\t \x01 \xc3\xa7",
                {{"cost.bytes", std::int64_t{42}}, {"ready", true}, {"label", std::string("value\n")}, {"unknown", nullptr}}), "Valid record rejected");
            Require(log.Flush(), "Flush failed");
            const auto after = raiders::Log::ClockNowUs();
            const auto records = Read(root / L"structured-\u00e7.jsonl");
            Require(records.size() == 1, "Expected one JSONL record");
            const auto& event = records.front();
            Require(event["schema_version"] == 1 && event["severity"] == "info", "Incorrect envelope");
            Require(event["context"]["session_id"] == "test.capture" && event["context"]["entity_id"] == "player.1", "Context lost");
            Require(event["message"] == "quote\" slash\\ newline\n tab\t \x01 \xc3\xa7", "JSON escaping corrupted text");
            Require(event["timestamp_us"] >= before && event["timestamp_us"] <= after, "Timestamp not captured at producer");
            Require(event["attributes"]["cost.bytes"] == 42 && event["attributes"]["ready"] == true, "Attribute types lost");
            Require(!log.Write(raiders::LogLevel::Debug, "test", context, "filtered"), "Runtime level ignored");
            Require(!log.Write(raiders::LogLevel::Info, "test", context, std::string(20000, 'x')), "Oversized input accepted");
            Require(!log.Write(raiders::LogLevel::Info, "test", context, "bad", {{"nan", std::numeric_limits<double>::quiet_NaN()}}), "NaN accepted");
            Require(!log.Write(raiders::LogLevel::Info, "test", context, "bad", {{"duplicate", true}, {"duplicate", false}}), "Duplicate key accepted");
            Require(!log.Write(raiders::LogLevel::Info, "test", context, std::string(1, '\xff')), "Invalid UTF-8 accepted");
            Require(!log.Write(raiders::LogLevel::Info, "test", context, std::string(9000, '\x01')), "Oversized escaped JSON accepted");
            Require(!log.Write(raiders::LogLevel::Info, "test", context, "bad", {{"large", std::int64_t{9007199254740992LL}}}), "Inexact JSON integer accepted");
            auto invalid = context; invalid.asset_id = "invalid id";
            Require(!log.Write(raiders::LogLevel::Info, "test", invalid, "bad"), "Invalid ID accepted");
            log.Shutdown(); log.Shutdown();
            Require(!log.Write(raiders::LogLevel::Error, "test", context, "closed"), "Write after shutdown accepted");
            const auto stats = log.Stats();
            Require(stats.accepted == 1 && stats.processed == 1 && stats.rejected == 8 && !stats.write_errors, "Incorrect counters");
        }
        {
            auto config = Config(root / "drain.jsonl");
            config.queue_capacity = 4096;
            raiders::Log log(config);
            std::vector<std::thread> producers;
            for (int thread = 0; thread < 4; ++thread) {
                producers.emplace_back([&, thread] {
                    raiders::LogContext ownContext;
                    ownContext.thread_id = "worker." + std::to_string(thread);
                    for (int i = 0; i < 100; ++i) log.Write(raiders::LogLevel::Info, "thread", ownContext, "record");
                });
            }
            for (auto& producer : producers) producer.join();
            log.Shutdown();
            const auto records = Read(config.path);
            std::set<std::string> ids;
            for (const auto& record : records) ids.insert(record["event_id"]);
            Require(records.size() == 400 && ids.size() == 400, "Shutdown lost or duplicated records");
            Require(log.Stats().processed == log.Stats().accepted && !log.Stats().write_errors, "Shutdown did not drain queue");
        }
        {
            raiders::Log log(Config(root / "concurrent-stop.jsonl"));
            std::barrier start(3);
            std::thread producer([&] {
                start.arrive_and_wait();
                for (int i = 0; i < 1000; ++i) log.Write(raiders::LogLevel::Info, "shutdown", context, "record");
            });
            std::thread flusher([&] {
                start.arrive_and_wait();
                for (int i = 0; i < 10; ++i) log.Flush();
            });
            start.arrive_and_wait();
            log.Shutdown();
            producer.join(); flusher.join();
            const auto stats = log.Stats();
            Require(stats.accepted == stats.processed && !stats.write_errors, "Concurrent shutdown lost accepted records");
            Require(stats.accepted + stats.rejected + stats.dropped_full == 1000, "Concurrent shutdown accounting mismatch");
        }
        {
            auto config = Config(root / "rotation.jsonl");
            config.max_record_bytes = 1024; config.max_file_bytes = 2048; config.retained_files = 2;
            raiders::Log log(config);
            for (int i = 0; i < 30; ++i) {
                Require(log.Write(raiders::LogLevel::Info, "rotation", context, std::string(200, 'x')), "Rotation record rejected");
                Require(log.Flush(), "Rotation flush failed");
            }
            log.Shutdown();
            Require(std::filesystem::exists(root / "rotation.1.jsonl") && std::filesystem::exists(root / "rotation.2.jsonl"), "Backups missing");
            Require(!std::filesystem::exists(root / "rotation.3.jsonl"), "Retention limit exceeded");
            for (const auto& name : {"rotation.jsonl", "rotation.1.jsonl", "rotation.2.jsonl"}) {
                Require(std::filesystem::file_size(root / name) <= config.max_file_bytes, "Rotated file exceeds limit");
                Require(!Read(root / name).empty(), "Rotation produced invalid JSONL");
            }
        }
        {
            auto config = Config(root / "locked.jsonl");
            config.queue_capacity = 1; config.max_record_bytes = 1024; config.max_file_bytes = 2048;
            raiders::Log log(config);
            // Deny deletion/rename while allowing the backend's active writer.
            HANDLE handle = CreateFileW(config.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Require(handle != INVALID_HANDLE_VALUE, "Failed to lock rotation file");
            for (int i = 0; i < 10; ++i) {
                log.Write(raiders::LogLevel::Info, "failure", context, std::string(200, 'x'));
                log.Flush();
            }
            std::vector<std::thread> producers;
            for (int thread = 0; thread < 4; ++thread) producers.emplace_back([&] {
                for (int i = 0; i < 1000; ++i) log.Write(raiders::LogLevel::Info, "saturation", context, "record");
            });
            for (auto& producer : producers) producer.join();
            log.Shutdown();
            CloseHandle(handle);
            const auto stats = log.Stats();
            Require(stats.write_errors > 0 && stats.dropped_full > 0, "Sink failure or saturation not observed");
            Require(stats.processed == stats.accepted, "Error path did not drain");
            Require(!log.Flush(), "Sink failure not reported");
        }
        {
            auto config = Config(root / "not-a-directory" / "log.jsonl");
            std::ofstream(root / "not-a-directory") << "file";
            bool failed = false;
            try { raiders::Log log(config); } catch (const std::exception&) { failed = true; }
            Require(failed, "File-open failure not reported to caller");
        }
        std::cout << "Logging contracts passed; captures: " << root.string() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

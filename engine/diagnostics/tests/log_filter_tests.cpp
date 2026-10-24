// Compile the consumer header with a stricter level and with logging disabled.
// The library itself retains the configuration selected by CMake.
#if !RA_ENABLE_LOGGING
#define RA_TEST_LIBRARY_OFF 1
#endif
#undef RA_LOG_ACTIVE_LEVEL
#define RA_LOG_ACTIVE_LEVEL 4
#ifdef RA_TEST_DISABLED
#undef RA_ENABLE_LOGGING
#define RA_ENABLE_LOGGING 0
#endif
#include "raiders/Log.h"

#include <gtest/gtest.h>

TEST(LoggingMacros, FilteredArgumentsAreNotEvaluated)
{
    int evaluations = 0;
    auto logger = [&]() -> raiders::Log&
    {
        ++evaluations;
        throw 1;
    };

    auto text = [&]()
    {
        ++evaluations;
        return "unexpected";
    };

    auto context = [&]()
    {
        ++evaluations;
        return raiders::LogContext{};
    };

    auto attributes = [&]()
    {
        ++evaluations;
        return raiders::LogAttributes{};
    };

    RA_LOG_TRACE(logger(), text(), context(), text(), attributes());
    RA_LOG_DEBUG(logger(), text(), context(), text(), attributes());
    RA_LOG_INFO(logger(), text(), context(), text(), attributes());
    RA_LOG_WARNING(logger(), text(), context(), text(), attributes());
#ifdef RA_TEST_DISABLED
    RA_LOG_ERROR(logger(), text(), context(), text(), attributes());
    RA_LOG_CRITICAL(logger(), text(), context(), text(), attributes());
#endif
#ifdef RA_TEST_LIBRARY_OFF
    raiders::LogConfig config;
    config.path = "disabled-logging-must-not-exist.jsonl";
    ASSERT_FALSE(std::filesystem::exists(config.path));
    raiders::Log disabled(config); // OFF does not validate IDs, open files or start a worker.
    EXPECT_FALSE(disabled.Write(raiders::LogLevel::Error, "off", {}, "discarded"));
    disabled.Shutdown();
    disabled.Shutdown();
    EXPECT_TRUE(disabled.Flush());
    EXPECT_EQ(disabled.Stats().accepted, 0);
    EXPECT_FALSE(std::filesystem::exists(config.path));
#endif
    EXPECT_EQ(evaluations, 0);
}

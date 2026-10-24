#ifdef RA_TEST_PROFILE_DISABLED
#undef RA_ENABLE_PROFILING
#define RA_ENABLE_PROFILING 0
#endif
#include "raiders/Profile.h"

#include <cstdlib>
#include <gtest/gtest.h>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

#if RA_ENABLE_PROFILING
TEST(ProfilingTest, NestedScopeTimingAndContext)
{
    raiders::ProfileCapture capture(4);
    const auto before = raiders::ProfileCapture::ClockNowUs();
    {
        // Nested scopes in the same block need distinct names.
        RA_PROFILE_SCOPE(capture, "runtime", "outer");
        RA_PROFILE_SCOPE(capture, "runtime", "inner",
                         (raiders::ProfileContext{"command.1", "asset.1"}));
        ASSERT_EQ(capture.Stats().in_flight, 2) << "Nested scopes not reserved";
        ASSERT_TRUE(capture.Drain().empty()) << "Active scope drained prematurely";
    }
    const auto after = raiders::ProfileCapture::ClockNowUs();
    auto events = capture.Drain();
    ASSERT_EQ(events.size(), 2) << "Nested scope events missing";
    const auto& inner = events[0];
    const auto& outer = events[1];
    ASSERT_TRUE(std::string(inner.name.data()) == "inner" &&
                std::string(outer.name.data()) == "outer")
        << "Completion order incorrect";
    ASSERT_TRUE(outer.timestamp_us >= before && inner.timestamp_us >= outer.timestamp_us)
        << "Start times not captured at producer";
    ASSERT_TRUE(inner.timestamp_us + inner.duration_us <= outer.timestamp_us + outer.duration_us &&
                outer.timestamp_us + outer.duration_us <= after)
        << "Nested duration escaped parent interval";
    ASSERT_TRUE(inner.thread_id == outer.thread_id && inner.thread_id != 0)
        << "Thread identity incorrect";
    ASSERT_TRUE(std::string(inner.correlation_id.data()) == "command.1" &&
                std::string(inner.asset_id.data()) == "asset.1")
        << "Scope context lost";
    ASSERT_TRUE(capture.Drain().empty() && capture.Stats().in_flight == 0)
        << "Drain duplicated events";
}

TEST(ProfilingTest, BoundedBufferReuse)
{
    raiders::ProfileCapture capture(2);
    {
        RA_PROFILE_SCOPE(capture, "bounded", "outer");
        {
            RA_PROFILE_SCOPE(capture, "bounded", "inner");
        }
        {
            RA_PROFILE_SCOPE(capture, "bounded", "dropped");
        }
        ASSERT_EQ(capture.Stats().dropped_full, 1) << "Buffer overflow not counted";
        ASSERT_EQ(capture.Drain().size(), 1) << "In-flight reservation overwritten";
        {
            RA_PROFILE_SCOPE(capture, "bounded", "reused");
        }
    }
    ASSERT_TRUE(capture.Drain().size() == 2 && capture.Stats().completed == 3)
        << "Freed slot not reused";
}

TEST(ProfilingTest, UnwindAndStop)
{
    raiders::ProfileCapture capture;
    try
    {
        RA_PROFILE_SCOPE(capture, "exceptions", "unwind");
        throw std::runtime_error("test");
    }
    catch (const std::runtime_error&)
    {
    }

    ASSERT_EQ(capture.Drain().size(), 1) << "Unwinding lost scope";
    {
        RA_PROFILE_SCOPE(capture, "capture", "finishes-after-stop");
        capture.Stop();
        capture.Stop();
        RA_PROFILE_SCOPE(capture, "capture", "rejected-after-stop");
    }
    ASSERT_TRUE(capture.Drain().size() == 1 && capture.Stats().rejected == 1 &&
                !capture.Stats().accepting)
        << "Stop invalidated active scope or accepted new scope";
}

TEST(ProfilingTest, ScopeOutlivesCapture)
{
    auto capture = std::make_unique<raiders::ProfileCapture>(1);
    auto scope = std::make_unique<raiders::ProfileScope>(*capture, "lifetime", "survives-owner");
    capture.reset();
    scope.reset(); // Must not access a destroyed capture object.
}

TEST(ProfilingTest, ConcurrentProducers)
{
    raiders::ProfileCapture capture(1024);
    std::vector<std::thread> producers;
    for (int i = 0; i < 4; ++i)
    {
        producers.emplace_back(
            [&]
            {
                for (int n = 0; n < 100; ++n)
                {
                    RA_PROFILE_SCOPE(capture, "thread", "outer");
                    RA_PROFILE_SCOPE(capture, "thread", "inner");
                }
            });
    }

    for (auto& thread : producers)
    {
        thread.join();
    }

    const auto events = capture.Drain();
    std::set<std::uint64_t> threads;
    for (const auto& event : events)
    {
        threads.insert(event.thread_id);
    }

    const auto stats = capture.Stats();
    ASSERT_TRUE(events.size() == 800 && threads.size() == 4)
        << "Concurrent scopes missing or thread IDs reused";
    ASSERT_TRUE(stats.accepted == 800 && stats.completed == 800 && !stats.dropped_full &&
                !stats.rejected)
        << "Concurrent accounting mismatch";
}

TEST(ProfilingTest, MalformedInput)
{
    raiders::ProfileCapture capture(4);
    {
        RA_PROFILE_SCOPE(capture, "validation", std::string(129, 'x'));
    }
    {
        RA_PROFILE_SCOPE(capture, "validation", std::string(1, '\xff'));
    }
    {
        RA_PROFILE_SCOPE(capture, "validation", "bad-id", (raiders::ProfileContext{"invalid id"}));
    }
    {
        RA_PROFILE_SCOPE(capture, "validation", std::string("embedded\0null", 13));
    }
    ASSERT_TRUE(capture.Stats().rejected == 4 && capture.Drain().empty())
        << "Malformed scope text accepted";
}

TEST(ProfilingTest, CrossThreadDestruction)
{
    raiders::ProfileCapture capture(1);
    auto* scope = new raiders::ProfileScope(capture, "thread", "wrong-destruction-thread");
    std::thread other(
        [&]
        {
            delete scope;
        });

    other.join();
    ASSERT_TRUE(capture.Drain().empty() && capture.Stats().rejected == 1 &&
                !capture.Stats().in_flight)
        << "Cross-thread destruction not rejected";
}

#else
TEST(ProfilingDisabled, DoesNotEvaluateArgumentsOrCollect)
{

    int evaluations = 0;
    auto capture = [&]() -> raiders::ProfileCapture&
    {
        ++evaluations;
        throw 1;
    };

    auto text = [&]
    {
        ++evaluations;
        return std::string("must-not-run");
    };

    auto context = [&]
    {
        ++evaluations;
        return raiders::ProfileContext{};
    };

    RA_PROFILE_SCOPE(capture(), text(), text(), context());
    // Even nonexistent types/functions must disappear from a disabled call.
    RA_PROFILE_SCOPE(nonexistent_capture(), NonexistentType{}, unregistered_scope());
    ASSERT_EQ(evaluations, 0) << "Disabled macro evaluated arguments";
#ifndef RA_TEST_PROFILE_DISABLED
    raiders::ProfileCapture disabled(0); // OFF ignores capacity and allocates no collector.
    {
        raiders::ProfileScope direct(disabled, "off", "direct");
    }
    disabled.Stop();
    const auto stats = disabled.Stats();
    ASSERT_TRUE(disabled.Drain().empty() && stats.capacity == 0 && stats.accepted == 0 &&
                !stats.accepting)
        << "Disabled build registered a scope";
#endif
}
#endif

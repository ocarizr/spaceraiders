"""Acceptance tests against the installed diagnostics wheel on Windows."""

import asyncio
from concurrent.futures import ThreadPoolExecutor
from contextvars import copy_context
from dataclasses import replace
import io
import json
from pathlib import Path
import subprocess
import sys

import pytest

from raiders_diagnostics import logging, profiling
from raiders_diagnostics.logging import Log
from raiders_diagnostics.profiling import Profiler
from raiders_diagnostics.session import TelemetrySession


def forbidden():
    raise AssertionError("Disabled or rejected instrumentation evaluated a factory")


def cli(*arguments, session=None):
    environment = (session or TelemetrySession()).child_environment()
    return subprocess.run(
        [str(Path(sys.executable).with_name("raiders-telemetry.exe")), *arguments],
        capture_output=True,
        text=True,
        encoding="utf-8",
        env=environment,
        timeout=15,
    )


def test_import_comes_from_installed_wheel():
    assert Path(logging.__file__).resolve().is_relative_to(Path(sys.prefix).resolve())


def test_log_envelope_and_stderr_preserve_stdout(capsys):
    session = TelemetrySession("test.session")
    with session.bind(asset_id="asset.ship", correlation_id="request.1"):
        assert Log(session).write("info", "pipeline", "Unicode \u2603\nnext", {"bytes": 12})

    output = capsys.readouterr()
    assert output.out == ""
    assert len(output.err.splitlines()) == 1
    record = json.loads(output.err)
    assert set(record) == {
        "schema_version",
        "event_id",
        "timestamp_us",
        "context",
        "severity",
        "category",
        "message",
        "attributes",
    }
    assert record["schema_version"] == 1
    assert record["message"] == "Unicode \u2603\nnext"
    assert record["attributes"] == {"bytes": 12}
    assert record["timestamp_us"] >= 0
    assert record["context"]["asset_id"] == "asset.ship"
    assert record["context"]["correlation_id"] == "request.1"


def test_context_restores_after_exception_and_keeps_sessions_separate():
    session = TelemetrySession(initial_context={"asset_id": "asset.initial"})
    other = TelemetrySession()
    with session.bind(correlation_id="outer"):
        with pytest.raises(RuntimeError), session.bind(asset_id=None, correlation_id="inner"):
            assert "asset_id" not in session.context()
            assert "correlation_id" not in other.context()
            raise RuntimeError("test")

        assert session.context()["correlation_id"] == "outer"
        assert session.context()["asset_id"] == "asset.initial"

    assert "correlation_id" not in session.context()


def test_async_and_explicit_thread_context_propagation():
    session = TelemetrySession()

    async def collect():
        entered = [asyncio.Event(), asyncio.Event()]

        async def worker(index):
            with session.bind(correlation_id=f"task.{index}"):
                entered[index].set()
                await entered[1 - index].wait()
                return session.context()["correlation_id"]

        return await asyncio.gather(worker(0), worker(1))

    assert asyncio.run(collect()) == ["task.0", "task.1"]
    with session.bind(correlation_id="thread.parent"), ThreadPoolExecutor(max_workers=1) as pool:
        captured = copy_context()
        inherited = pool.submit(captured.run, session.context).result()
        fresh = pool.submit(session.context).result()
        assert inherited["correlation_id"] == "thread.parent"
        assert "correlation_id" not in fresh
        assert inherited["thread_id"] != session.context()["thread_id"]


@pytest.mark.parametrize(
    "fields", [{"asset_id": "bad space"}, {"unknown": "value"}, {"entity_id": "x" * 129}]
)
def test_invalid_context_rejected(fields):
    with pytest.raises(ValueError), TelemetrySession().bind(**fields):
        pass


def test_environment_roundtrip_clears_stale_context_and_propagates_switches():
    session = TelemetrySession("session.environment", False, True, {"entity_id": "entity.ship"})
    base = {"CUSTOM": "keep", "RA_TELEMETRY_ASSET_ID": "stale"}
    environment = session.child_environment(base)
    child = TelemetrySession.from_environment(environment)
    assert environment["CUSTOM"] == "keep"
    assert "RA_TELEMETRY_ASSET_ID" not in environment
    assert base["RA_TELEMETRY_ASSET_ID"] == "stale"
    assert child.session_id == session.session_id
    assert not child.logging_enabled and child.profiling_enabled
    assert child.context()["entity_id"] == "entity.ship"
    with pytest.raises(ValueError):
        TelemetrySession.from_environment({"RA_ENABLE_LOGGING": "yes"})


def test_disabled_filtered_and_exhausted_logs_skip_factories():
    sink = io.StringIO()
    session = TelemetrySession()
    disabled = Log(replace(session, logging_enabled=False), sink)
    assert not disabled.write("info", "pipeline", forbidden, forbidden)
    log = Log(session, sink, max_records=1)
    assert not log.write("debug", "pipeline", forbidden, forbidden)
    assert log.write("info", "pipeline", "one")
    assert not log.write("info", "pipeline", forbidden, forbidden)
    assert log.stats.written == 1 and log.stats.rejected == 2


@pytest.mark.parametrize(
    "attributes",
    [
        {"value": float("nan")},
        {"value": float("inf")},
        {"nested": [9007199254740992]},
        {"value": object()},
    ],
)
def test_invalid_json_payload_never_reaches_sink(attributes):
    sink = io.StringIO()
    log = Log(TelemetrySession(), sink)
    assert not log.write("info", "pipeline", "invalid", attributes)
    assert sink.getvalue() == ""
    assert log.stats.rejected + log.stats.failures == 1
    assert log.write("info", "pipeline", "recovered")


def test_record_size_and_total_output_are_bounded():
    sink = io.StringIO()
    log = Log(TelemetrySession(), sink, max_records=3, max_record_bytes=1024)
    assert not log.write("info", "pipeline", "x" * 1024)
    for _ in range(10):
        log.write("info", "pipeline", "bounded")

    assert log.stats.written == 3
    assert len(sink.getvalue().encode("utf-8")) <= 3 * 1024


@pytest.mark.parametrize("budget", [0, -1, True, 1.5, float("nan"), float("inf")])
def test_collection_budgets_require_positive_integers(budget):
    session = TelemetrySession()
    with pytest.raises(ValueError):
        Log(session, max_records=budget)

    with pytest.raises(ValueError):
        Log(session, max_record_bytes=budget)

    with pytest.raises(ValueError):
        Profiler(session, capacity=budget)


def test_callback_and_sink_failures_are_contained():
    log = Log(TelemetrySession(), io.StringIO())
    assert not log.write("info", "pipeline", forbidden)
    assert log.stats.failures == 1
    assert log.write("info", "pipeline", "recovered")

    class BrokenSink:
        def write(self, value):
            return 1

    broken = Log(TelemetrySession(), BrokenSink())
    assert not broken.write("info", "pipeline", "partial")
    assert broken.stats.faulted and broken.stats.failures == 1
    assert not broken.write("info", "pipeline", forbidden)


def test_concurrent_logs_are_complete_unique_and_bounded():
    sink = io.StringIO()
    log = Log(TelemetrySession(), sink, max_records=31)
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(lambda index: log.write("info", "pipeline", str(index)), range(100)))

    records = [json.loads(line) for line in sink.getvalue().splitlines()]
    assert len(records) == log.stats.written == 31
    assert len({record["event_id"] for record in records}) == 31


def test_nested_elapsed_scopes_and_context(monkeypatch):
    ticks = iter([100, 110, 120, 130])
    monkeypatch.setattr(profiling, "clock_now_us", lambda: next(ticks))
    session = TelemetrySession()
    profiler = Profiler(session)
    with session.bind(asset_id="asset.ship"), profiler.stage("outer"):
        with profiler.stage("inner"):
            pass

    events = profiler.drain()
    assert [(event["name"], event["duration_us"]) for event in events] == [
        ("inner", 10),
        ("outer", 30),
    ]
    assert all(event["context"]["asset_id"] == "asset.ship" for event in events)
    assert profiler.stats.in_flight == profiler.stats.buffered == 0


def test_profile_failure_and_timeout_release_reservations():
    profiler = Profiler(TelemetrySession())
    with pytest.raises(RuntimeError), profiler.stage("failure"):
        raise RuntimeError("work failed")

    with pytest.raises(subprocess.TimeoutExpired), profiler.stage("timeout"):
        raise subprocess.TimeoutExpired("child", 1)

    assert [event["outcome"] for event in profiler.drain()] == ["error", "timeout"]
    assert profiler.stats.in_flight == 0


def test_profile_capacity_reserves_inflight_and_stop_allows_completion():
    profiler = Profiler(TelemetrySession(), capacity=1)
    with profiler.stage("accepted"):
        with profiler.stage(forbidden):
            pass

        profiler.stop()
        with profiler.stage(forbidden):
            pass

    assert profiler.stats.completed == profiler.stats.buffered == 1
    assert profiler.stats.dropped == profiler.stats.rejected == 1
    assert not profiler.stats.accepting
    assert len(profiler.drain()) == 1


def test_drain_reuses_capacity_and_invalid_stage_does_not_break_work():
    profiler = Profiler(TelemetrySession(), capacity=1)
    with profiler.stage(forbidden):
        work_ran = True

    assert work_ran and profiler.stats.rejected == 1
    for _ in range(2):
        with profiler.stage("stage"):
            pass

        assert len(profiler.drain()) == 1

    assert profiler.stats.completed == 2


def test_disabled_profiling_skips_names_clocks_and_buffers(monkeypatch):
    monkeypatch.setattr(profiling, "clock_now_us", forbidden)
    profiler = Profiler(TelemetrySession(profiling_enabled=False))
    with profiler.stage(forbidden):
        pass

    assert profiler.drain() == []
    assert profiler.stats.capacity == profiler.stats.completed == profiler.stats.in_flight == 0
    assert not profiler.stats.accepting


def test_profile_concurrent_collection():
    profiler = Profiler(TelemetrySession(), capacity=100)

    def collect(index):
        with profiler.stage(f"stage.{index}"):
            pass

    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(collect, range(100)))

    assert len(profiler.drain()) == profiler.stats.completed == 100
    assert profiler.stats.in_flight == profiler.stats.dropped == 0


def test_cli_preserves_protocol_and_propagates_context():
    session = TelemetrySession("test.cli", initial_context={"correlation_id": "request.cli"})
    code = "import json; from raiders_diagnostics.session import TelemetrySession; print(json.dumps(TelemetrySession.from_environment().context()))"
    result = cli("--", sys.executable, "-c", code, session=session)
    assert result.returncode == 0, result.stderr
    child = json.loads(result.stdout)
    records = [json.loads(line) for line in result.stderr.splitlines()]
    assert child["session_id"] == "test.cli"
    assert child["correlation_id"] == "request.cli"
    assert all(record["context"]["session_id"] == "test.cli" for record in records)
    assert child["process_instance_id"] != records[0]["context"]["process_instance_id"]
    assert child["clock_domain_id"] != records[0]["context"]["clock_domain_id"]
    stages = [
        record["attributes"] for record in records if record["category"] == "pipeline.profile"
    ]
    assert {stage["stage.category"] for stage in stages} == {"pipeline.child", "pipeline.stage"}
    assert all(stage["duration.us"] >= 0 and stage["outcome"] == "ok" for stage in stages)


def test_cli_disabled_does_not_create_output_or_evaluate_collection(tmp_path):
    destination = tmp_path / "disabled.jsonl"
    result = cli(
        "--disabled",
        "--log-file",
        str(destination),
        "--",
        sys.executable,
        "-c",
        "import os; print(os.environ['RA_ENABLE_LOGGING'] + os.environ['RA_ENABLE_PROFILING'])",
    )
    assert result.returncode == 0
    assert result.stdout.strip() == "00"
    assert result.stderr == ""
    assert not destination.exists()


def test_cli_file_sink_uses_jsonl_and_refuses_overwrite(tmp_path):
    destination = tmp_path / "events.jsonl"
    arguments = ("--log-file", str(destination), "--", sys.executable, "-c", "print('{}')")
    result = cli(*arguments)
    assert result.returncode == 0
    assert result.stdout.strip() == "{}" and result.stderr == ""
    original = destination.read_bytes()
    assert b"\r\n" not in original
    assert len([json.loads(line) for line in original.splitlines()]) == 4
    assert cli(*arguments).returncode == 2
    assert destination.read_bytes() == original


@pytest.mark.parametrize(
    "arguments, code, outcome",
    [
        (("--", sys.executable, "-c", "import sys; sys.exit(7)"), 7, "error"),
        (
            ("--timeout", "0.05", "--", sys.executable, "-c", "import time; time.sleep(30)"),
            124,
            "timeout",
        ),
    ],
)
def test_cli_child_failure_and_timeout(arguments, code, outcome):
    result = cli(*arguments)
    assert result.returncode == code
    stages = [json.loads(line) for line in result.stderr.splitlines()]
    assert result.stdout == ""
    assert [
        stage["attributes"]["outcome"]
        for stage in stages
        if stage["category"] == "pipeline.profile"
    ] == [outcome, outcome]


@pytest.mark.parametrize("timeout", [0, -1, float("nan"), float("inf")])
def test_invalid_child_timeout_rejected(timeout):
    with pytest.raises(ValueError):
        Profiler(TelemetrySession()).run([sys.executable], timeout=timeout)

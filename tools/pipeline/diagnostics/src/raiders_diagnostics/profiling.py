"""Elapsed stage/child-process collection and a protocol-preserving CLI wrapper."""

import argparse
from contextlib import ExitStack, contextmanager
from dataclasses import asdict, dataclass, replace
from pathlib import Path
import math
import subprocess
import sys
from threading import Lock

from .logging import Log
from .session import TelemetrySession, clock_now_us


@dataclass(frozen=True)
class ProfileStats:
    capacity: int
    buffered: int
    in_flight: int
    completed: int
    dropped: int
    rejected: int
    accepting: bool


class Profiler:
    def __init__(self, session, *, capacity=1024, log=None):
        if type(capacity) is not int or capacity <= 0:
            raise ValueError("Profile capacity must be positive")

        self.session, self.capacity, self.log = session, capacity, log
        self._lock = Lock()
        self._events = []
        self._in_flight = self._completed = self._dropped = self._rejected = 0
        self._accepting = True

    @property
    def stats(self):
        with self._lock:
            enabled = self.session.profiling_enabled
            return ProfileStats(
                self.capacity if enabled else 0,
                len(self._events),
                self._in_flight,
                self._completed,
                self._dropped,
                self._rejected,
                enabled and self._accepting,
            )

    def stop(self):
        with self._lock:
            self._accepting = False

    def drain(self):
        with self._lock:
            events, self._events = self._events, []
            return events

    @contextmanager
    def stage(self, name, *, category="pipeline.stage"):
        accepted = False
        if self.session.profiling_enabled:
            with self._lock:
                if not self._accepting:
                    self._rejected += 1
                elif len(self._events) + self._in_flight >= self.capacity:
                    self._dropped += 1
                else:
                    self._in_flight += 1
                    accepted = True

        if not accepted:
            yield
            return

        try:
            label = name() if callable(name) else name
            if not isinstance(label, str) or not label.strip() or len(label) > 128:
                raise ValueError("Invalid stage name")

            if not isinstance(category, str) or not category.strip() or len(category) > 128:
                raise ValueError("Invalid stage category")

            context, start = self.session.context(), clock_now_us()
        except Exception:
            with self._lock:
                self._in_flight -= 1
                self._rejected += 1

            yield
            return

        outcome = "ok"
        try:
            yield
        except BaseException as error:
            outcome = "timeout" if isinstance(error, subprocess.TimeoutExpired) else "error"
            raise
        finally:
            event = {
                "name": label,
                "category": category,
                "timestamp_us": start,
                "duration_us": clock_now_us() - start,
                "context": context,
                "outcome": outcome,
            }
            with self._lock:
                self._in_flight -= 1
                self._events.append(event)
                self._completed += 1

            if self.log is not None:
                self.log.write(
                    "info",
                    "pipeline.profile",
                    "Stage completed",
                    lambda: {
                        "stage.name": label,
                        "stage.category": category,
                        "duration.us": event["duration_us"],
                        "outcome": outcome,
                    },
                )

    def run(self, command, *, name="child-process", cwd=None, timeout=None, environment=None):
        """No shell or output buffering; propagate child errors and direct-child timeout."""
        if isinstance(command, (str, bytes)) or not command:
            raise ValueError("Expected a nonempty argument sequence")

        if timeout is not None and (not math.isfinite(timeout) or timeout <= 0):
            raise ValueError("Timeout must be positive and finite")

        with self.stage(name, category="pipeline.child"):
            return subprocess.run(
                command,
                shell=False,
                check=True,
                cwd=cwd,
                timeout=timeout,
                env=self.session.child_environment(environment),
            )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disabled", action="store_true")
    parser.add_argument("--log-file", type=Path)
    parser.add_argument("--timeout", type=float)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    options = parser.parse_args()
    command = options.command[1:] if options.command[:1] == ["--"] else options.command
    if not command:
        parser.error("Provide a child command after --")

    try:
        session = TelemetrySession.from_environment()
        if options.disabled:
            session = replace(session, logging_enabled=False, profiling_enabled=False)

        with ExitStack() as stack:
            sink = None
            if options.log_file and session.logging_enabled:
                sink = stack.enter_context(
                    options.log_file.open("x", encoding="utf-8", newline="\n")
                )

            log = Log(session, sink)
            profiler = Profiler(session, log=log)
            log.write("info", "pipeline", "Pipeline command started")
            with profiler.stage("cli.stage"):
                profiler.run(command, timeout=options.timeout)

            log.write(
                "info", "pipeline", "Pipeline command finished", lambda: asdict(profiler.stats)
            )

        return 0
    except subprocess.CalledProcessError as error:
        return error.returncode
    except subprocess.TimeoutExpired:
        return 124
    except (OSError, ValueError) as error:
        print(f"Telemetry command failed: {type(error).__name__}", file=sys.stderr)
        return 2

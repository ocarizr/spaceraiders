"""Bounded, synchronous v1 JSONL logs; stdout is never selected implicitly."""

from dataclasses import dataclass
import json
import math
import sys
from threading import RLock
from uuid import uuid4

from .session import MAX_INTEGER, clock_now_us


LEVELS = {
    name: index
    for index, name in enumerate(("trace", "debug", "info", "warning", "error", "critical", "off"))
}


@dataclass(frozen=True)
class LogStats:
    written: int
    rejected: int
    failures: int
    faulted: bool


def _safe_numbers(value):
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return math.isfinite(value) and abs(value) <= MAX_INTEGER

    if isinstance(value, dict):
        return all(_safe_numbers(item) for item in value.values())

    if isinstance(value, list):
        return all(_safe_numbers(item) for item in value)

    return True


class Log:
    def __init__(
        self, session, sink=None, *, minimum="info", max_records=10000, max_record_bytes=16384
    ):
        if (
            not isinstance(minimum, str)
            or minimum not in LEVELS
            or type(max_records) is not int
            or max_records <= 0
            or type(max_record_bytes) is not int
            or max_record_bytes <= 0
        ):
            raise ValueError("Invalid log configuration")

        self.session = session
        self.sink = sys.stderr if sink is None else sink
        self.minimum = LEVELS[minimum]
        self.max_records = max_records
        self.max_record_bytes = max_record_bytes
        self._lock = RLock()
        self._written = self._rejected = self._failures = 0
        self._faulted = self._writing = False

    @property
    def stats(self):
        with self._lock:
            return LogStats(self._written, self._rejected, self._failures, self._faulted)

    def write(self, level, category, message, attributes=None):
        """Factories are optional; only invoked after enabled/level/budget checks."""
        with self._lock:
            if (
                not self.session.logging_enabled
                or self._faulted
                or self._writing
                or self._written >= self.max_records
                or not isinstance(level, str)
                or LEVELS.get(level, -1) < self.minimum
                or level == "off"
            ):
                self._rejected += 1
                return False

            self._writing = True
            try:
                if not isinstance(category, str) or not category.strip():
                    raise ValueError("Invalid log category")

                context, timestamp = self.session.context(), clock_now_us()
                text = message() if callable(message) else message
                fields = attributes() if callable(attributes) else attributes
                if not isinstance(text, str) or (
                    fields is not None and not isinstance(fields, dict)
                ):
                    raise ValueError("Expected string message and object attributes")

                record = {
                    "schema_version": 1,
                    "event_id": "event." + uuid4().hex,
                    "timestamp_us": timestamp,
                    "context": context,
                    "severity": level,
                    "category": category,
                    "message": text,
                    "attributes": {} if fields is None else fields,
                }
                line = (
                    json.dumps(record, ensure_ascii=True, allow_nan=False, separators=(",", ":"))
                    + "\n"
                )
                if len(line.encode("utf-8")) > self.max_record_bytes or not _safe_numbers(
                    json.loads(line)
                ):
                    self._rejected += 1
                    return False

                try:
                    if self.sink.write(line) != len(line):
                        raise OSError("Partial telemetry write")

                    self.sink.flush()
                except Exception:
                    self._faulted = True
                    self._failures += 1
                    return False

                self._written += 1
                return True
            except Exception:
                self._failures += 1
                return False
            finally:
                self._writing = False

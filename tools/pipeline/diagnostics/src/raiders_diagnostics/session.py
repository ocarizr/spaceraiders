"""Process identities, monotonic microseconds and explicitly propagated context."""

from contextlib import contextmanager
from contextvars import ContextVar
from dataclasses import dataclass, field
import os
import re
from threading import local
from time import perf_counter_ns
from types import MappingProxyType
from uuid import uuid4


MAX_INTEGER = 9007199254740991
_ORIGIN = perf_counter_ns()
_PROCESS = "process." + uuid4().hex
_CLOCK = "clock." + uuid4().hex
_THREAD = local()
_CONTEXT = ContextVar("raiders_telemetry_context", default={})
_FIELDS = ("correlation_id", "asset_id", "entity_id", "widget_id", "vm_instance_id")
_PREFIX = "RA_TELEMETRY_"


def clock_now_us():
    return (perf_counter_ns() - _ORIGIN) // 1000


def validate_id(value):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._:/-]{0,127}", value):
        raise ValueError("Invalid telemetry identity")


@dataclass(frozen=True)
class TelemetrySession:
    session_id: str = field(default_factory=lambda: "session." + uuid4().hex)
    logging_enabled: bool = True
    profiling_enabled: bool = True
    initial_context: object = field(default_factory=dict, repr=False, compare=False)
    _key: object = field(default_factory=object, init=False, repr=False, compare=False)

    def __post_init__(self):
        validate_id(self.session_id)
        if type(self.logging_enabled) is not bool or type(self.profiling_enabled) is not bool:
            raise ValueError("Telemetry switches must be booleans")

        values = dict(self.initial_context)
        for key, value in values.items():
            if key not in _FIELDS:
                raise ValueError("Unknown telemetry context field")

            validate_id(value)

        object.__setattr__(self, "initial_context", MappingProxyType(values))

    def context(self):
        if not hasattr(_THREAD, "identity"):
            _THREAD.identity = "thread." + uuid4().hex

        return {
            "session_id": self.session_id,
            "process_instance_id": _PROCESS,
            "thread_id": _THREAD.identity,
            "clock_domain_id": _CLOCK,
            **_CONTEXT.get().get(self._key, self.initial_context),
        }

    @contextmanager
    def bind(self, **fields):
        values = dict(_CONTEXT.get().get(self._key, self.initial_context))
        for key, value in fields.items():
            if key not in _FIELDS:
                raise ValueError("Unknown telemetry context field")

            if value is None:
                values.pop(key, None)
            else:
                validate_id(value)
                values[key] = value

        token = _CONTEXT.set({**_CONTEXT.get(), self._key: values})
        try:
            yield
        finally:
            _CONTEXT.reset(token)

    def child_environment(self, base=None):
        environment = dict(os.environ if base is None else base)
        context = self.context()
        for key in _FIELDS:
            environment.pop(_PREFIX + key.upper(), None)
            if key in context:
                environment[_PREFIX + key.upper()] = context[key]

        environment[_PREFIX + "SESSION_ID"] = self.session_id
        environment["RA_ENABLE_LOGGING"] = str(int(self.logging_enabled))
        environment["RA_ENABLE_PROFILING"] = str(int(self.profiling_enabled))
        return environment

    @classmethod
    def from_environment(cls, environment=None):
        environment = os.environ if environment is None else environment
        switches = [
            environment.get(name, "1") for name in ("RA_ENABLE_LOGGING", "RA_ENABLE_PROFILING")
        ]
        if any(value not in {"0", "1"} for value in switches):
            raise ValueError("Telemetry environment switches must be 0 or 1")

        values = {
            key: environment[_PREFIX + key.upper()]
            for key in _FIELDS
            if _PREFIX + key.upper() in environment
        }
        return cls(
            environment.get(_PREFIX + "SESSION_ID", "session." + uuid4().hex),
            switches[0] == "1",
            switches[1] == "1",
            values,
        )

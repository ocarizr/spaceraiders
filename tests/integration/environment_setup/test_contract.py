"""Offline schema/fixture conformance; no workstation probes or installers run here."""

from copy import deepcopy
from datetime import datetime
import hashlib
import json
from pathlib import Path
import tomllib

from jsonschema import Draft202012Validator, FormatChecker, ValidationError
import pytest
from referencing import Registry, Resource


ROOT = Path(__file__).resolve().parents[3]
FIXTURES = Path(__file__).with_name("fixtures")
SCHEMAS = {
    name: json.loads((ROOT / "schemas" / f"environment-{name}.schema.json").read_text())
    for name in ("profile", "health")
}
REGISTRY = Registry().with_resources(
    (schema["$id"], Resource.from_contents(schema)) for schema in SCHEMAS.values()
)
FORMATS = FormatChecker()


@FORMATS.checks("date-time", raises=ValueError)
def utc(value):
    if not isinstance(value, str):
        return True

    return (
        value.endswith("Z")
        and datetime.fromisoformat(value[:-1] + "+00:00").utcoffset().total_seconds() == 0
    )


def validate(kind, document):
    Draft202012Validator(SCHEMAS[kind], registry=REGISTRY, format_checker=FORMATS).validate(
        document
    )


def load(name):
    return json.loads((FIXTURES / f"environment-{name}.json").read_text(encoding="utf-8"))


def unique(items, key):
    result = {item[key]: item for item in items}
    if len(result) != len(items):
        raise ValueError(f"Duplicate {key}")

    return result


def closure(graph, selected):
    visited, active = set(), set()

    def visit(key):
        if key not in graph or key in active:
            raise ValueError("Unknown prerequisite or cycle")

        if key in visited:
            return

        active.add(key)
        for dependency in graph[key]["depends_on"]:
            visit(dependency)

        active.remove(key)
        visited.add(key)

    for key in selected:
        visit(key)

    return visited


def catalog_invariants(catalog):
    profiles = unique(catalog["profiles"], "id")
    closure(profiles, profiles)
    for profile in profiles.values():
        requirements = unique(profile["requirements"], "id")
        closure(requirements, requirements)
        for requirement in requirements.values():
            if requirement["required"] and any(
                not requirements[key]["required"]
                for key in closure(requirements, [requirement["id"]])
            ):
                raise ValueError("Required prerequisites cannot be optional")

        if "designer" in profile["roles"]:
            for key in closure(profiles, [profile["id"]]):
                if any(
                    item["required"]
                    and item["kind"] not in {"artifact-bundle", "code-editor", "lua-toolchain"}
                    for item in profiles[key]["requirements"]
                ):
                    raise ValueError("Designer closure must not require developer SDKs")


def aggregate(states):
    priority = ["Unsupported", "Needs repair", "Missing", "Checking", "Unknown", "Ready"]
    return min(states, key=priority.index)


def health_invariants(catalog, report):
    policies = unique(catalog["profiles"], "id")
    profiles = unique(report["profiles"], "id")
    resolved = closure(policies, report["selected_profiles"])
    if resolved != set(report["resolved_profiles"]) or resolved != profiles.keys():
        raise ValueError("Resolved closure and reported inventory must match")

    text = (
        (FIXTURES / "environment-profile.json")
        .read_bytes()
        .decode("utf-8-sig")
        .replace("\r\n", "\n")
        .replace("\r", "\n")
    )
    if (
        report["catalog_id"] != catalog["catalog_id"]
        or report["catalog_sha256"] != hashlib.sha256(text.encode()).hexdigest()
    ):
        raise ValueError("Catalog identity/fingerprint mismatch")

    if report["completed_at"] and datetime.fromisoformat(
        report["completed_at"]
    ) < datetime.fromisoformat(report["started_at"]):
        raise ValueError("Scan completion precedes start")

    for key, profile in profiles.items():
        policy = policies[key]
        requirements = unique(profile["requirements"], "id")
        expected = unique(policy["requirements"], "id")
        if requirements.keys() != expected.keys():
            raise ValueError(
                "Every requirement, including unchecked/optional ones, must be reported"
            )

        dependencies = unique(profile["dependencies"], "profile_id")
        if dependencies.keys() != set(policy["depends_on"]) or any(
            item["state"] != profiles[dep]["state"] for dep, item in dependencies.items()
        ):
            raise ValueError("Dependency health must match the resolved prerequisite")

        for identifier, item in requirements.items():
            rule = expected[identifier]
            if item["required"] != rule["required"]:
                raise ValueError("Required flag cannot be downgraded")

            if any(evidence["probe_id"] != rule["probe_id"] for evidence in item["evidence"]):
                raise ValueError("Probe identity does not match requirement")

            if item["state"] == "Ready":
                if any(
                    requirements[dependency]["state"] != "Ready"
                    for dependency in rule["depends_on"]
                ):
                    raise ValueError("Ready requirement has an unmet prerequisite")

                for evidence in item["evidence"]:
                    if (
                        rule["architecture"] != "any"
                        and evidence["observed_architecture"] != rule["architecture"]
                    ):
                        raise ValueError("Ready evidence must match architecture")

                    constraint = rule["constraint"]
                    if (
                        constraint["kind"] == "one-of"
                        and evidence["observed_version"] not in constraint["values"]
                    ):
                        raise ValueError("Ready evidence violates allowed versions")

                    if (
                        constraint["kind"] in {"configuration", "manifest"}
                        and evidence["configuration_sha256"] is None
                    ):
                        raise ValueError("Ready artifact/configuration requires a fingerprint")

        states = [item["state"] for item in requirements.values() if item["required"]] + [
            item["state"] for item in dependencies.values()
        ]
        state = aggregate(states)
        if report["machine"]["operating_system"] != "windows" or report["machine"][
            "architecture"
        ] in {"x86", "arm64"}:
            state = "Unsupported"
        elif report["machine"]["architecture"] == "unknown":
            state = "Unknown" if report["scan_complete"] else "Checking"

        if state != profile["state"]:
            raise ValueError("Profile health does not match required inputs")

    if aggregate([item["state"] for item in profiles.values()]) != report["state"]:
        raise ValueError("Report health does not match resolved profiles")


def test_schemas_and_synthetic_fixtures():
    for schema in SCHEMAS.values():
        Draft202012Validator.check_schema(schema)

    catalog, report = load("profile"), load("health")
    validate("profile", catalog)
    validate("health", report)
    catalog_invariants(catalog)
    health_invariants(catalog, report)


@pytest.mark.parametrize(
    "state,reason,outcome,complete",
    [
        ("Ready", None, "satisfied", True),
        ("Missing", "NotFound", "absent", True),
        ("Needs repair", "FingerprintMismatch", "mismatch", True),
        ("Unsupported", "UnsupportedArchitecture", "unsupported", True),
        ("Checking", "ScanInProgress", "running", False),
        ("Unknown", "ProbeFailed", "failed", False),
    ],
)
def test_all_health_states_have_coherent_examples(state, reason, outcome, complete):
    report = load("health")
    report["state"] = state
    report["reason_codes"] = [] if reason is None else [reason]
    report["scan_complete"] = state != "Checking"
    report["completed_at"] = None if state == "Checking" else report["completed_at"]
    if state == "Unsupported":
        report["machine"]["architecture"] = "arm64"

    profile = report["profiles"][0]
    profile["state"], profile["reason_codes"] = state, report["reason_codes"]
    for requirement in profile["requirements"]:
        requirement["state"], requirement["reason_codes"] = state, report["reason_codes"]
        evidence = requirement["evidence"][0]
        evidence["outcome"], evidence["complete"] = outcome, complete

    validate("health", report)
    health_invariants(load("profile"), report)


@pytest.mark.parametrize(
    "field,value",
    [("requires_python", True), ("requires_game", True), ("requires_installed_sdk", True)],
)
def test_setup_launch_cannot_require_developer_environments(field, value):
    catalog = load("profile")
    catalog["setup_host"][field] = value
    with pytest.raises(ValidationError):
        validate("profile", catalog)


@pytest.mark.parametrize("mutation", ["linux", "arm64", "floating", "sdk-major", "extra-field"])
def test_invalid_profile_policy_is_rejected(mutation):
    catalog = load("profile")
    if mutation == "linux":
        catalog["profiles"][0]["target"]["operating_system"] = "linux"
    elif mutation == "arm64":
        catalog["profiles"][0]["target"]["architecture"] = "arm64"
    elif mutation == "floating":
        catalog["profiles"][0]["requirements"][0]["constraint"]["value"] = "latest"
    elif mutation == "sdk-major":
        catalog["profiles"][1]["requirements"][0]["constraint"]["value"] = "11.0.402"
    else:
        catalog["profiles"][0]["command"] = "unapproved installer"

    with pytest.raises(ValidationError):
        validate("profile", catalog)


@pytest.mark.parametrize("mutation", ["duplicate", "unknown-dependency", "cycle", "designer-sdk"])
def test_catalog_semantic_errors_are_rejected(mutation):
    catalog = load("profile")
    if mutation == "duplicate":
        catalog["profiles"].append(deepcopy(catalog["profiles"][0]))
    elif mutation == "unknown-dependency":
        catalog["profiles"][0]["depends_on"] = ["not-defined"]
    elif mutation == "cycle":
        catalog["profiles"][0]["depends_on"] = ["native-development"]
    else:
        catalog["profiles"][-1]["depends_on"] = ["managed-tooling"]

    with pytest.raises(ValueError):
        catalog_invariants(catalog)


@pytest.mark.parametrize(
    "mutation",
    [
        "no-evidence",
        "incomplete",
        "failed",
        "local-path",
        "secret-field",
        "date",
        "unfinished",
        "reason",
        "version",
    ],
)
def test_invalid_ready_or_shareable_report_is_rejected(mutation):
    report = load("health")
    item = report["profiles"][0]["requirements"][0]
    if mutation == "no-evidence":
        item["evidence"] = []
    elif mutation == "incomplete":
        item["evidence"][0]["complete"] = False
    elif mutation == "failed":
        item["evidence"][0]["outcome"] = "failed"
    elif mutation == "local-path":
        item["evidence"][0]["location"] = {
            "kind": "local-path",
            "value": "C:/Users/example/Python/python.exe",
        }
    elif mutation == "secret-field":
        item["evidence"][0]["environment"] = {"TOKEN": "must-not-be-exported"}
    elif mutation == "date":
        report["started_at"] = "2026-99-99T99:99:99Z"
    elif mutation == "unfinished":
        report["scan_complete"] = False
    elif mutation == "reason":
        report["reason_codes"] = ["MissingEvidence"]
    else:
        report["schema_version"] = 2

    with pytest.raises(ValidationError):
        validate("health", report)


@pytest.mark.parametrize(
    "mutation",
    [
        "omitted",
        "duplicate",
        "flag",
        "closure",
        "probe",
        "aggregate",
        "fingerprint",
        "architecture",
    ],
)
def test_cross_document_health_errors_are_rejected(mutation):
    report = load("health")
    items = report["profiles"][0]["requirements"]
    if mutation == "omitted":
        items.pop()
    elif mutation == "duplicate":
        items.append(deepcopy(items[0]))
    elif mutation == "flag":
        items[0]["required"] = False
    elif mutation == "closure":
        report["resolved_profiles"].append("designer-tools")
    elif mutation == "probe":
        items[0]["evidence"][0]["probe_id"] = "other.probe"
    elif mutation == "aggregate":
        report["state"] = "Unknown"
    elif mutation == "fingerprint":
        report["catalog_sha256"] = "f" * 64
    else:
        items[0]["evidence"][0]["observed_architecture"] = "x86"

    with pytest.raises(ValueError):
        health_invariants(load("profile"), report)


def test_local_paths_are_local_only_and_redacted_export_is_valid():
    report = load("health")
    report["visibility"] = "local"
    location = report["profiles"][0]["requirements"][0]["evidence"][0]["location"]
    location.update(kind="local-path", value="C:/Tools/Python/python.exe")
    validate("health", report)
    report["visibility"] = "shareable"
    location.update(kind="redacted", value=None)
    validate("health", report)


def test_optional_unchecked_editor_does_not_block_designer_readiness():
    catalog, report = load("profile"), load("health")
    report["selected_profiles"] = report["resolved_profiles"] = ["designer-tools"]
    profile = report["profiles"][0]
    profile["id"] = "designer-tools"
    item = deepcopy(profile["requirements"][0])
    item["id"] = "designer.sdk"
    item["evidence"][0].update(
        probe_id="designer.sdk", observed_version="1.0.0", configuration_sha256="0" * 64
    )
    profile["requirements"] = [
        item,
        {
            "id": "designer.lua-editor",
            "required": False,
            "state": "Unknown",
            "reason_codes": ["NotChecked"],
            "evidence": [],
        },
    ]
    validate("health", report)
    health_invariants(catalog, report)


def test_failed_search_cannot_claim_conclusive_absence():
    report = load("health")
    report["state"] = report["profiles"][0]["state"] = "Missing"
    report["reason_codes"] = report["profiles"][0]["reason_codes"] = ["NotFound"]
    item = report["profiles"][0]["requirements"][0]
    item.update(state="Missing", reason_codes=["NotFound"])
    item["evidence"][0].update(outcome="failed", complete=False)
    with pytest.raises(ValidationError):
        validate("health", report)


def test_catalog_agrees_with_existing_managed_and_python_pins():
    catalog = load("profile")
    sdk = json.loads((ROOT / "global.json").read_text())["sdk"]
    constraint = catalog["profiles"][1]["requirements"][0]["constraint"]
    assert (
        constraint["value"] == sdk["version"] and constraint["roll_forward"] == sdk["rollForward"]
    )
    recipe = json.loads(
        (
            ROOT / "tools/pipeline/wheel_build/src/raiders_pipeline/environments/python-build.lock"
        ).read_text()
    )
    assert (
        catalog["profiles"][2]["requirements"][0]["constraint"]["values"]
        == recipe["interpreter"]["versions"]
    )
    native = tomllib.loads((ROOT / "pyproject.toml").read_text())["tool"]["raiders"]["environment"][
        "native"
    ]
    expected = [
        native[key]
        for key in (
            "compiler-version",
            "toolset-version",
            "windows-sdk-version",
            "cmake-version",
            "ninja-version",
        )
    ]
    assert [
        item["constraint"]["value"] for item in catalog["profiles"][0]["requirements"]
    ] == expected

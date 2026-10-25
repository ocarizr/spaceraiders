"""Offline creation, drift detection and repair against the installed recipe runner."""

from copy import deepcopy
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

from raiders_pipeline import create_env


@pytest.fixture(scope="session")
def artifacts():
    cache = os.environ.get("RAIDERS_TEST_WHEELHOUSE")
    wheel = os.environ.get("RAIDERS_TEST_PIPELINE_WHEEL")
    if not cache or not wheel:
        pytest.fail(
            "Set RAIDERS_TEST_WHEELHOUSE and RAIDERS_TEST_PIPELINE_WHEEL to the locked wheel cache and tested pipeline wheel"
        )

    wheel = Path(wheel).resolve()
    return {
        "python": sys._base_executable,
        "wheelhouse": Path(cache).resolve(),
        "local_wheel": wheel,
        "local_sha256": hashlib.sha256(wheel.read_bytes()).hexdigest(),
    }


def create(profile, root, artifacts):
    options = dict(artifacts)
    if profile == "build":
        options.pop("local_wheel")
        options.pop("local_sha256")

    return create_env.create_environment(profile, root, **options)


@pytest.mark.parametrize("profile", ["build", "tools"])
def test_installed_recipes_have_a_complete_hashed_wheel_graph(profile):
    recipe = create_env.load_recipe(profile)
    assert recipe["profile"] == profile
    assert recipe["interpreter"]["versions"] == ["3.12.14", "3.12.10"]
    bootstrap = recipe["interpreter"]["bootstrap"]
    assert bootstrap["version"] in recipe["interpreter"]["versions"]
    assert bootstrap["url"].startswith("https://www.python.org/ftp/python/")
    assert len(bootstrap["sha256"]) == 64
    assert "InstallAllUsers=0" in bootstrap["arguments"]
    assert {package["name"] for package in recipe["packages"]} >= {"pip", "packaging"}
    assert all(
        len(package["sha256"]) == 64 and package["filename"].endswith(".whl")
        for package in recipe["packages"]
    )
    assert not Path(create_env.__file__).resolve().is_relative_to(Path.cwd() / "tools" / "pipeline")


def test_discover_matching_interpreter_and_reject_unavailable_one(artifacts):
    recipe = create_env.load_recipe("build")
    python, identity = create_env.discover_interpreter(recipe)
    assert python.is_file()
    assert identity["version"] in recipe["interpreter"]["versions"]
    with pytest.raises(ValueError, match="No compatible installed"):
        create_env.discover_interpreter(recipe, "Z:/no-such-python-q07.exe")


def test_missing_environment_is_reported_without_creating_it(tmp_path):
    status = create_env.check_environment("build", tmp_path / "not-created")
    assert status["status"] == "Missing"
    assert not (tmp_path / "not-created").exists()


def test_build_profile_is_idempotent_detects_drift_and_repairs(tmp_path, artifacts, monkeypatch):
    root = tmp_path / "managed environments"
    assert create("build", root, artifacts)["status"] == "Ready"
    receipt = root / "build" / ".raiders-environment.json"
    original = receipt.read_bytes()
    assert create("build", root, artifacts)["status"] == "Ready"
    assert receipt.read_bytes() == original
    assert (
        create_env.create_environment(
            "build", root, python=artifacts["python"], wheelhouse=tmp_path / "missing-cache"
        )["status"]
        == "Ready"
    )
    assert not (tmp_path / "missing-cache").exists()
    installed = root / "build" / "Lib" / "site-packages" / "packaging" / "__init__.py"
    installed.write_text(installed.read_text() + "\n# package drift\n")
    assert create_env.check_environment("build", root)["status"] == "Needs repair"
    assert create("build", root, artifacts)["status"] == "Ready"
    assert "package drift" not in installed.read_text()

    changed = deepcopy(create_env.load_recipe("build"))
    changed["fingerprint"] = "0" * 64
    monkeypatch.setattr(create_env, "load_recipe", lambda _: changed)
    assert create_env.check_environment("build", root)["status"] == "Needs repair"


def test_tools_profile_installs_locked_local_wheel_and_exposes_json_cli(tmp_path, artifacts):
    root = tmp_path / "runtime"
    assert create("tools", root, artifacts)["status"] == "Ready"
    assert (
        create_env.check_environment("tools", root, local_sha256=artifacts["local_sha256"])[
            "status"
        ]
        == "Ready"
    )
    assert (
        create_env.check_environment("tools", root, local_sha256="0" * 64)["status"]
        == "Needs repair"
    )
    outside = tmp_path / "outside"
    outside.mkdir()
    result = subprocess.run(
        [
            str(root / "tools" / "Scripts" / "python.exe"),
            "-I",
            "-m",
            "raiders_pipeline",
            "env",
            "check",
            "--profile",
            "tools",
            "--root",
            str(root),
            "--local-sha256",
            artifacts["local_sha256"],
        ],
        cwd=outside,
        capture_output=True,
        text=True,
        timeout=45,
    )
    assert result.returncode == 0, result.stderr
    assert json.loads(result.stdout)["status"] == "Ready"
    # Metadata changes and undeclared packages are detected even when versions look valid.
    metadata = root / "tools" / "Lib" / "site-packages" / "unexpected-1.0.dist-info"
    metadata.mkdir()
    (metadata / "METADATA").write_text("Metadata-Version: 2.1\nName: unexpected\nVersion: 1.0\n")
    (metadata / "RECORD").write_text("unexpected-1.0.dist-info/METADATA,,\n")
    assert (
        create_env.check_environment("tools", root, local_sha256=artifacts["local_sha256"])[
            "status"
        ]
        == "Needs repair"
    )


def test_bad_locked_artifact_does_not_create_an_environment(tmp_path, artifacts):
    cache = tmp_path / "corrupt-cache"
    shutil.copytree(artifacts["wheelhouse"], cache)
    package = create_env.load_recipe("build")["packages"][0]
    (cache / package["filename"]).write_bytes(b"corrupt")
    with pytest.raises(ValueError, match="Missing or corrupt locked artifact"):
        create_env.create_environment(
            "build", tmp_path / "env", python=artifacts["python"], wheelhouse=cache
        )

    assert not (tmp_path / "env" / "build").exists()


def test_unowned_directory_is_never_deleted(tmp_path, artifacts):
    target = tmp_path / "build"
    target.mkdir()
    sentinel = target / "keep.txt"
    sentinel.write_text("developer-owned content")
    assert create_env.check_environment("build", tmp_path)["status"] == "Needs repair"
    with pytest.raises(OSError):
        create("build", tmp_path, artifacts)

    assert sentinel.read_text() == "developer-owned content"


def test_local_artifact_hash_and_version_are_required(tmp_path, artifacts):
    options = dict(artifacts)
    options["local_sha256"] = "0" * 64
    with pytest.raises(ValueError, match="SHA-256 mismatch"):
        create_env.create_environment("tools", tmp_path, **options)

    assert not (tmp_path / "tools").exists()
    recipe = create_env.load_recipe("tools")
    recipe["local_package"]["version"] = "0.0.0"
    with pytest.raises(ValueError, match="identity does not match"):
        create_env._local_artifact(recipe, artifacts["local_wheel"], artifacts["local_sha256"])


def test_active_environment_cannot_be_recreated(tmp_path, artifacts, monkeypatch):
    assert create("build", tmp_path, artifacts)["status"] == "Ready"
    target = tmp_path / "build"
    receipt = target / ".raiders-environment.json"
    state = json.loads(receipt.read_text())
    state["complete"] = False
    receipt.write_text(json.dumps(state))
    monkeypatch.setattr(sys, "prefix", str(target))
    with pytest.raises(ValueError, match="Cannot recreate an active"):
        create("build", tmp_path, artifacts)

    assert (target / "Scripts" / "python.exe").is_file()


def test_concurrent_creator_is_rejected_before_touching_environment(tmp_path, artifacts):
    with create_env._environment_lock(tmp_path / "build"):
        result = subprocess.run(
            [
                artifacts["python"],
                "-I",
                str(Path(create_env.__file__)),
                "create",
                "--profile",
                "build",
                "--root",
                str(tmp_path),
                "--wheelhouse",
                str(artifacts["wheelhouse"]),
            ],
            capture_output=True,
            text=True,
            timeout=30,
        )

    assert result.returncode != 0
    assert "Another process" in json.loads(result.stdout)["reasons"][0]
    assert not (tmp_path / "build").exists()


def test_download_is_checked_against_locked_hash_before_caching(tmp_path, artifacts, monkeypatch):
    recipe = deepcopy(create_env.load_recipe("build"))
    recipe["packages"] = recipe["packages"][:1]
    package = recipe["packages"][0]
    payload = (artifacts["wheelhouse"] / package["filename"]).read_bytes()
    monkeypatch.setattr(create_env, "urlopen", lambda *args, **kwargs: io.BytesIO(payload))
    result = create_env._artifacts(recipe, tmp_path / "cache", True)
    assert result[0][2].read_bytes() == payload
    monkeypatch.setattr(create_env, "urlopen", lambda *args, **kwargs: io.BytesIO(b"wrong bytes"))
    with pytest.raises(ValueError, match="Downloaded artifact SHA-256 mismatch"):
        create_env._artifacts(recipe, tmp_path / "bad-cache", True)

    assert list((tmp_path / "bad-cache").iterdir()) == []

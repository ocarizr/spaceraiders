"""Exercise the installed API/CLI and wheels built with a separate prepared venv."""

import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

import pytest

import raiders_pipeline
from raiders_pipeline import build_wheel, load_policy


@pytest.fixture(scope="session")
def build_python():
    configured = os.environ.get("RAIDERS_TEST_BUILD_PYTHON")
    if not configured:
        pytest.fail(
            "Set RAIDERS_TEST_BUILD_PYTHON to the separate prepared build venv's Python executable"
        )

    return Path(configured).resolve()


def run(*arguments, cwd, environment=None):
    return subprocess.run(
        arguments, cwd=cwd, env=environment, capture_output=True, text=True, timeout=90
    )


def test_installed_distribution_namespace_data_and_console_registration():
    assert Path(raiders_pipeline.__file__).resolve().is_relative_to(Path(sys.prefix).resolve())
    distribution = importlib.metadata.distribution("raiders-pipeline")
    assert raiders_pipeline.__version__ == distribution.version
    assert load_policy()["build_requirements"]["setuptools"] == "84.2.1"
    assert any(
        item.name == "raiders-pipeline" and item.group == "console_scripts"
        for item in distribution.entry_points
    )
    assert any(
        str(item).endswith("raiders_pipeline/wheel_policy.json") for item in distribution.files
    )


@pytest.mark.parametrize("entry", ["module", "console"])
def test_entry_points_work_from_unrelated_directory(tmp_path, entry):
    command = (
        [sys.executable, "-I", "-m", "raiders_pipeline"]
        if entry == "module"
        else [str(Path(sys.executable).with_name("raiders-pipeline.exe"))]
    )
    result = run(*command, "policy", cwd=tmp_path)
    assert result.returncode == 0, result.stderr
    assert json.loads(result.stdout) == load_policy()
    assert result.stderr == ""
    version = run(*command, "--version", cwd=tmp_path)
    assert version.returncode == 0 and version.stdout.strip() == raiders_pipeline.__version__


@pytest.fixture
def project(tmp_path):
    project = tmp_path / "project with spaces"
    package = project / "src" / "wheel_fixture"
    package.mkdir(parents=True)
    requirements = ", ".join(
        json.dumps(f"{name}=={version}")
        for name, version in load_policy()["build_requirements"].items()
    )
    (project / "pyproject.toml").write_text(
        f'[build-system]\nrequires = [{requirements}]\nbuild-backend = "setuptools.build_meta"\n'
        '[project]\nname = "raiders-wheel-fixture"\nversion = "1.2.3"\nrequires-python = ">=3.12,<3.13"\n'
        '[project.scripts]\nraiders-wheel-fixture = "wheel_fixture.__main__:main"\n'
        '[tool.setuptools.packages.find]\nwhere = ["src"]\n'
        '[tool.setuptools.package-data]\nwheel_fixture = ["message.json"]\n',
        encoding="utf-8",
    )
    (package / "__init__.py").write_text("", encoding="utf-8")
    (package / "message.json").write_text('{"message":"installed data"}', encoding="utf-8")
    (package / "__main__.py").write_text(
        "from importlib.resources import files\n"
        'def main():\n    print(files("wheel_fixture").joinpath("message.json").read_text())\n'
        'if __name__ == "__main__":\n    main()\n',
        encoding="utf-8",
    )
    return project


def test_actual_wheel_installs_and_runs_without_source_checkout(project, build_python, tmp_path):
    outside = tmp_path / "unrelated working directory"
    outside.mkdir()
    output = tmp_path / "artifacts"
    # The installed coordinator runs outside the checkout; only the selected source path is explicit.
    result = run(
        sys.executable,
        "-I",
        "-m",
        "raiders_pipeline",
        "build-wheel",
        "--project",
        str(project),
        "--build-python",
        str(build_python),
        "--wheel-dir",
        str(output),
        cwd=outside,
    )
    assert result.returncode == 0, result.stderr
    artifact = json.loads(result.stdout)
    wheel = Path(artifact["wheel"])
    assert wheel.parent == output and wheel.name == "raiders_wheel_fixture-1.2.3-py3-none-any.whl"
    assert artifact["sha256"] == hashlib.sha256(wheel.read_bytes()).hexdigest()
    with zipfile.ZipFile(wheel) as archive:
        assert "wheel_fixture/message.json" in archive.namelist()
        assert "wheel_fixture/__main__.py" in archive.namelist()

    runtime = tmp_path / "fixture-runtime"
    created = run(sys.executable, "-I", "-m", "venv", str(runtime), cwd=outside)
    assert created.returncode == 0, created.stderr
    python = runtime / "Scripts" / "python.exe"
    installed = run(
        str(python),
        "-I",
        "-m",
        "pip",
        "--isolated",
        "install",
        "--no-index",
        "--no-deps",
        str(wheel),
        cwd=outside,
    )
    assert installed.returncode == 0, installed.stderr
    # Relocation makes accidental source imports fail rather than silently passing.
    relocated = (tmp_path / "source removed from original path").resolve()
    assert project.resolve().is_relative_to(tmp_path.resolve())
    assert relocated.is_relative_to(tmp_path.resolve())
    shutil.move(str(project), str(relocated))
    environment = {
        key: value for key, value in os.environ.items() if key not in {"PYTHONPATH", "PYTHONHOME"}
    }
    for command in (
        [str(python), "-I", "-m", "wheel_fixture"],
        [str(runtime / "Scripts" / "raiders-wheel-fixture.exe")],
    ):
        executed = run(*command, cwd=outside, environment=environment)
        assert executed.returncode == 0, executed.stderr
        assert json.loads(executed.stdout) == {"message": "installed data"}


def test_api_builds_only_selected_project(project, build_python, tmp_path):
    unrelated = tmp_path / "unrelated"
    unrelated.mkdir()
    (unrelated / "pyproject.toml").write_text("invalid TOML!", encoding="utf-8")
    result = build_wheel(project, build_python, tmp_path / "wheels")
    assert Path(result["wheel"]).is_file()
    assert len(list((tmp_path / "wheels").glob("*.whl"))) == 1


def test_project_must_have_metadata(tmp_path, build_python):
    with pytest.raises(ValueError, match="pyproject.toml"):
        build_wheel(tmp_path, build_python, tmp_path / "output")


def test_missing_build_interpreter_rejected(project, tmp_path):
    with pytest.raises(ValueError, match="existing build-venv"):
        build_wheel(project, tmp_path / "missing.exe", tmp_path / "output")


def test_execution_venv_cannot_double_as_build_venv(project, tmp_path):
    with pytest.raises(ValueError, match="separate virtual environments"):
        build_wheel(project, sys.executable, tmp_path / "output")


def test_global_interpreter_is_not_a_prepared_build_venv(project, tmp_path):
    with pytest.raises(ValueError, match="separate virtual environments"):
        build_wheel(project, sys._base_executable, tmp_path / "output")


def test_unprepared_build_venv_is_rejected(project, tmp_path):
    empty = tmp_path / "unprepared"
    created = run(sys.executable, "-I", "-m", "venv", "--without-pip", str(empty), cwd=tmp_path)
    assert created.returncode == 0, created.stderr
    with pytest.raises(ValueError, match="missing the pinned"):
        build_wheel(project, empty / "Scripts" / "python.exe", tmp_path / "output")


@pytest.mark.parametrize(
    "backend",
    ['build-backend = "other.backend"', 'backend-path = ["."]', 'requires = ["setuptools"]'],
)
def test_unsupported_build_configuration_rejected(project, build_python, tmp_path, backend):
    (project / "pyproject.toml").write_text(f"[build-system]\n{backend}\n", encoding="utf-8")
    with pytest.raises(ValueError, match="supported pinned"):
        build_wheel(project, build_python, tmp_path / "output")


def test_build_backend_failure_is_reported_without_success_protocol(
    project, build_python, tmp_path
):
    with (project / "pyproject.toml").open("a", encoding="utf-8") as stream:
        stream.write("\n[tool.setuptools]\nthis-option-does-not-exist = true\n")

    result = run(
        sys.executable,
        "-I",
        "-m",
        "raiders_pipeline",
        "build-wheel",
        "--project",
        str(project),
        "--build-python",
        str(build_python),
        "--wheel-dir",
        str(tmp_path / "output"),
        cwd=tmp_path,
    )
    assert result.returncode != 0
    assert result.stdout == ""
    assert "Wheel command failed" in result.stderr
    assert not list((tmp_path / "output").glob("*.whl"))

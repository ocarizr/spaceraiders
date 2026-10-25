"""Build one selected setuptools project using an explicitly prepared build venv."""

import hashlib
from importlib.resources import files
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
import tomllib


def load_policy():
    return json.loads(
        files("raiders_pipeline").joinpath("wheel_policy.json").read_text(encoding="utf-8")
    )


def build_wheel(project, build_python, wheel_directory):
    """Return artifact identity/hash; pip output goes to stderr, never protocol stdout."""
    project, build_python, wheel_directory = (
        Path(path).resolve() for path in (project, build_python, wheel_directory)
    )
    if os.name != "nt":
        raise ValueError("Wheel builds support Windows only")

    if not project.is_dir() or not (project / "pyproject.toml").is_file():
        raise ValueError("Select a project directory containing pyproject.toml")

    if not build_python.is_file():
        raise ValueError("Select an existing build-venv Python executable")

    policy = load_policy()
    metadata = tomllib.loads((project / "pyproject.toml").read_text(encoding="utf-8"))
    backend = metadata.get("build-system", {})
    requirements = {f"{name}=={version}" for name, version in policy["build_requirements"].items()}
    if (
        backend.get("build-backend") != policy["backend"]
        or set(backend.get("requires", [])) != requirements
        or "backend-path" in backend
    ):
        raise ValueError("Project must declare the supported pinned setuptools/wheel backend")

    probe = (
        "import json, sys, os, struct; from importlib.metadata import distributions; "
        "print(json.dumps({'prefix':sys.prefix,'base':sys.base_prefix,'version':list(sys.version_info[:2]),"
        "'platform':os.name,'bits':struct.calcsize('P')*8,"
        "'packages':{d.metadata['Name'].lower():d.version for d in distributions()}}))"
    )
    result = subprocess.run(
        [str(build_python), "-I", "-c", probe],
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    environment = json.loads(result.stdout)
    prefix = Path(environment["prefix"]).resolve()
    if (
        environment["version"] != [3, 12]
        or environment["platform"] != "nt"
        or environment["bits"] != 64
    ):
        raise ValueError("Build environment must use Windows x64 Python 3.12")

    if prefix == Path(environment["base"]).resolve() or prefix == Path(sys.prefix).resolve():
        raise ValueError("Build and execution must use separate virtual environments")

    if any(
        environment["packages"].get(name) != version
        for name, version in policy["build_requirements"].items()
    ):
        raise ValueError("Build venv is missing the pinned setuptools/wheel requirements")

    wheel_directory.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(prefix="raiders-wheel-") as temporary:
        subprocess.run(
            [
                str(build_python),
                "-I",
                "-m",
                "pip",
                "--isolated",
                "--disable-pip-version-check",
                "wheel",
                "--no-deps",
                "--no-build-isolation",
                "--no-index",
                "--wheel-dir",
                temporary,
                str(project),
            ],
            check=True,
            stdout=sys.stderr,
            stderr=sys.stderr,
        )
        wheels = list(Path(temporary).glob("*.whl"))
        if len(wheels) != 1:
            raise ValueError("Selected project must produce exactly one wheel")

        source = wheels[0]
        artifact = wheel_directory / source.name
        shutil.copyfile(source, artifact)

    with artifact.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()

    return {"schema_version": 1, "wheel": str(artifact), "sha256": digest}

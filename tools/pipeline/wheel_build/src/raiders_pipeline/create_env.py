"""Stdlib-only bootstrap, recreation and health checks for managed Python venvs."""

import argparse
from contextlib import contextmanager
import hashlib
import json
import msvcrt
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
from urllib.request import urlopen
from uuid import uuid4
import zipfile
from email.parser import BytesParser


_RECEIPT = ".raiders-environment.json"
_OWNER = "raiders-python-environment-v1"
_IDENTITY = """
import json, os, struct, sys
print(json.dumps({"implementation":sys.implementation.name,"version":".".join(map(str,sys.version_info[:3])),
"platform":sys.platform,"bits":struct.calcsize("P")*8,"base_executable":sys._base_executable}))
"""
_SNAPSHOT = """
import hashlib, json, re, sys
from importlib.metadata import distributions
from pathlib import Path
prefix = Path(sys.prefix).resolve()
packages = {}
for distribution in distributions():
    name = re.sub(r"[-_.]+", "-", distribution.metadata["Name"]).lower()
    if name in packages or distribution.files is None:
        raise ValueError("Missing or duplicate distribution inventory")
    entries = []
    for item in distribution.files:
        if str(item).endswith(".pyc"):
            continue
        path = distribution.locate_file(item).resolve()
        if not path.is_relative_to(prefix):
            raise ValueError("Distribution file escapes managed environment")
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        entries.append((path.relative_to(prefix).as_posix(), digest))
    fingerprint = hashlib.sha256(json.dumps(sorted(entries)).encode()).hexdigest()
    packages[name] = {"version":distribution.version,"fingerprint":fingerprint}
configuration = (prefix / "pyvenv.cfg").read_text()
print(json.dumps({"prefix":str(prefix),"isolated":"include-system-site-packages = false" in configuration,
"packages":packages}))
"""


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def load_recipe(profile):
    if profile not in {"build", "tools"}:
        raise ValueError("Unknown environment profile")

    raw = (Path(__file__).parent / "environments" / f"python-{profile}.lock").read_bytes()
    recipe = json.loads(raw)
    if recipe["schema_version"] != 1 or recipe["profile"] != profile:
        raise ValueError("Unsupported environment recipe")

    seen = set()
    for package in recipe["packages"]:
        if (
            not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", package["name"])
            or package["name"] in seen
            or not re.fullmatch(r"[0-9]+(?:\.[0-9]+)+", package["version"])
            or not re.fullmatch(r"[a-f0-9]{64}", package["sha256"])
            or Path(package["filename"]).name != package["filename"]
            or not package["filename"].endswith(".whl")
            or not package["url"].startswith("https://files.pythonhosted.org/")
        ):
            raise ValueError("Invalid locked artifact")

        seen.add(package["name"])

    recipe["fingerprint"] = hashlib.sha256(raw).hexdigest()
    return recipe


def _probe(python, code):
    try:
        result = subprocess.run(
            [str(python), "-I", "-c", code], check=True, capture_output=True, text=True, timeout=30
        )
    except subprocess.CalledProcessError as error:
        detail = error.stderr.strip().splitlines()[-1] if error.stderr else "no diagnostic output"
        raise ValueError(f"Environment probe failed: {detail}") from error

    return json.loads(result.stdout)


def discover_interpreter(recipe, python=None):
    candidates = (
        [python]
        if python
        else [
            getattr(sys, "_base_executable", sys.executable),
            shutil.which("python"),
            shutil.which("python3"),
        ]
    )
    for candidate in dict.fromkeys(str(path) for path in candidates if path):
        if "windowsapps" in candidate.lower():
            continue

        try:
            identity = _probe(candidate, _IDENTITY)
            selected = recipe["interpreter"]
            if (
                identity["implementation"] == selected["implementation"]
                and identity["version"] in selected["versions"]
                and identity["platform"] == selected["platform"]
                and identity["bits"] == selected["bits"]
            ):
                return Path(identity["base_executable"]).resolve(), identity
        except (OSError, ValueError, subprocess.SubprocessError):
            continue

    raise ValueError("No compatible installed Windows x64 CPython found for the locked versions")


def _target(root, profile):
    root = Path(root).resolve()
    target = root / profile
    if target.is_symlink() or target.is_junction() or target.resolve().parent != root:
        raise ValueError(
            "Environment target must be a direct, non-linked child of its managed root"
        )

    return target


def _receipt(target):
    receipt = json.loads((target / _RECEIPT).read_text(encoding="utf-8"))
    if (
        receipt.get("owner") != _OWNER
        or receipt.get("target") != str(target)
        or receipt.get("profile") != target.name
    ):
        raise ValueError("Directory is not owned by this environment recipe")

    return receipt


def check_environment(profile, root, *, local_sha256=None):
    recipe = load_recipe(profile)
    target = _target(root, profile)
    result = {"profile": profile, "path": str(target), "status": "Missing", "reasons": []}
    if not target.exists():
        result["reasons"].append("Environment directory is missing")
        return result

    result["status"] = "Needs repair"
    try:
        receipt = _receipt(target)
        if receipt.get("recipe") != recipe["fingerprint"] or not receipt.get("complete"):
            raise ValueError("Recipe changed or environment creation was incomplete")

        if recipe["local_package"] and (
            not local_sha256 or receipt.get("local_sha256") != local_sha256
        ):
            raise ValueError("Expected local package SHA-256 is missing or changed")

        python = target / "Scripts" / "python.exe"
        identity = _probe(python, _IDENTITY)
        if identity != receipt["interpreter"]:
            raise ValueError("Interpreter identity changed")

        snapshot = _probe(python, _SNAPSHOT)
        if (
            not snapshot["isolated"]
            or snapshot["prefix"] != str(target)
            or snapshot["packages"] != receipt["packages"]
        ):
            raise ValueError("Installed package inventory or file fingerprints changed")

        result["status"] = "Ready"
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        result["reasons"].append(str(error))

    return result


def _artifacts(recipe, wheelhouse, download):
    wheelhouse = Path(wheelhouse).resolve()
    wheelhouse.mkdir(parents=True, exist_ok=True)
    paths = []
    for package in recipe["packages"]:
        path = wheelhouse / package["filename"]
        if not path.exists() and download:
            temporary = path.with_name(path.name + "." + uuid4().hex + ".tmp")
            try:
                with (
                    urlopen(package["url"], timeout=30) as response,
                    temporary.open("xb") as output,
                ):
                    shutil.copyfileobj(response, output)

                if digest(temporary) != package["sha256"]:
                    raise ValueError("Downloaded artifact SHA-256 mismatch")

                temporary.replace(path)
            finally:
                temporary.unlink(missing_ok=True)

        if not path.is_file() or digest(path) != package["sha256"]:
            raise ValueError(f"Missing or corrupt locked artifact: {package['filename']}")

        paths.append((package["name"], package["version"], path, package["sha256"]))

    return paths


def _local_artifact(recipe, wheel, sha256):
    if not wheel or not sha256 or not re.fullmatch(r"[a-f0-9]{64}", sha256):
        raise ValueError("Tools profile requires a local wheel and its expected SHA-256")

    path = Path(wheel).resolve()
    if path.suffix != ".whl" or digest(path) != sha256:
        raise ValueError("Local package SHA-256 mismatch")

    with zipfile.ZipFile(path) as archive:
        entries = [entry for entry in archive.namelist() if entry.endswith(".dist-info/METADATA")]
        if len(entries) != 1:
            raise ValueError("Expected exactly one local distribution")

        metadata = BytesParser().parsebytes(archive.read(entries[0]))

    expected = recipe["local_package"]
    if metadata["Name"] != expected["name"] or metadata["Version"] != expected["version"]:
        raise ValueError("Local wheel identity does not match recipe")

    return expected["name"], expected["version"], path, sha256


@contextmanager
def _environment_lock(target):
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.with_name(f".raiders-{target.name}.lock").open("a+b") as stream:
        if stream.tell() == 0:
            stream.write(b"0")
            stream.flush()

        stream.seek(0)
        try:
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as error:
            raise ValueError("Another process is creating this environment") from error

        try:
            yield
        finally:
            stream.seek(0)
            msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)


def create_environment(
    profile, root, *, python=None, wheelhouse, download=False, local_wheel=None, local_sha256=None
):
    load_recipe(profile)
    with _environment_lock(_target(root, profile)):
        return _create_environment(
            profile,
            root,
            python=python,
            wheelhouse=wheelhouse,
            download=download,
            local_wheel=local_wheel,
            local_sha256=local_sha256,
        )


def _create_environment(
    profile, root, *, python=None, wheelhouse, download=False, local_wheel=None, local_sha256=None
):
    recipe = load_recipe(profile)
    interpreter, identity = discover_interpreter(recipe, python)
    target = _target(root, profile)
    if target.exists():
        _receipt(target)
        health = check_environment(profile, root, local_sha256=local_sha256)
        if health["status"] == "Ready":
            return health

        active = Path(sys.prefix).resolve()
        if active.is_relative_to(target) or interpreter.is_relative_to(target):
            raise ValueError("Cannot recreate an active or bootstrap interpreter environment")

    # Healthy environments need no cache; validate all inputs before any recreation.
    artifacts = _artifacts(recipe, wheelhouse, download)
    if recipe["local_package"]:
        artifacts.append(_local_artifact(recipe, local_wheel, local_sha256))

    if any(path.is_relative_to(target) for _, _, path, _ in artifacts):
        raise ValueError("Artifact inputs must live outside the environment being recreated")

    if target.exists():
        # _target checks the absolute direct-child path; _receipt proves recipe ownership.
        shutil.rmtree(target)

    target.mkdir(parents=True)
    receipt = {
        "owner": _OWNER,
        "profile": profile,
        "target": str(target),
        "recipe": recipe["fingerprint"],
        "local_sha256": local_sha256,
        "complete": False,
    }
    receipt_path = target / _RECEIPT
    receipt_path.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    subprocess.run(
        [str(interpreter), "-I", "-m", "venv", str(target)],
        check=True,
        stdout=sys.stderr,
        stderr=sys.stderr,
    )
    target_python = target / "Scripts" / "python.exe"
    with TemporaryDirectory(prefix="raiders-requirements-") as temporary:
        requirements = Path(temporary) / "requirements.txt"
        requirements.write_text(
            "\n".join(f"{path.as_uri()} --hash=sha256:{sha}" for _, _, path, sha in artifacts),
            encoding="utf-8",
        )
        subprocess.run(
            [
                str(target_python),
                "-I",
                "-m",
                "pip",
                "--isolated",
                "--disable-pip-version-check",
                "install",
                "--no-index",
                "--no-deps",
                "--only-binary=:all:",
                "--require-hashes",
                "--force-reinstall",
                "-r",
                str(requirements),
            ],
            check=True,
            stdout=sys.stderr,
            stderr=sys.stderr,
        )

    subprocess.run(
        [str(target_python), "-I", "-m", "pip", "--isolated", "check"],
        check=True,
        stdout=sys.stderr,
        stderr=sys.stderr,
    )
    snapshot = _probe(target_python, _SNAPSHOT)
    versions = {name: item["version"] for name, item in snapshot["packages"].items()}
    if versions != {name: version for name, version, _, _ in artifacts} or not snapshot["isolated"]:
        raise ValueError("Installed environment does not match the full locked graph")

    receipt.update(complete=True, interpreter=identity, packages=snapshot["packages"])
    receipt_path.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    return check_environment(profile, root, local_sha256=local_sha256)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["create", "check"])
    parser.add_argument("--profile", choices=["build", "tools"], required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--python", type=Path)
    parser.add_argument("--wheelhouse", type=Path)
    parser.add_argument("--download", action="store_true")
    parser.add_argument("--local-wheel", type=Path)
    parser.add_argument("--local-sha256")
    options = parser.parse_args(argv)
    try:
        if options.action == "check":
            result = check_environment(
                options.profile, options.root, local_sha256=options.local_sha256
            )
        else:
            if options.wheelhouse is None:
                parser.error("create requires --wheelhouse")

            result = create_environment(
                options.profile,
                options.root,
                python=options.python,
                wheelhouse=options.wheelhouse,
                download=options.download,
                local_wheel=options.local_wheel,
                local_sha256=options.local_sha256,
            )
    except (OSError, ValueError, KeyError, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        result = {"profile": options.profile, "status": "Needs repair", "reasons": [str(error)]}

    print(json.dumps(result))
    return 0 if result["status"] == "Ready" else 1


if __name__ == "__main__":
    raise SystemExit(main())

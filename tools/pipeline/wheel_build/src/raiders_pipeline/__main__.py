"""Installed entry point for the Raiders Engine Python wheel builder."""

import argparse
import json
from pathlib import Path
import subprocess
import sys

from . import __version__, build_wheel, load_policy
from .create_env import main as environment_main


def main():
    if sys.argv[1:2] == ["env"]:
        return environment_main(sys.argv[2:])

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", action="version", version=__version__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("env", help="Create or check locked Python environments (use env --help)")
    commands.add_parser("policy", help="Print the installed package's backend policy as JSON")
    wheel = commands.add_parser(
        "build-wheel", help="Build one project with a separate prepared build venv"
    )
    wheel.add_argument("--project", type=Path, required=True)
    wheel.add_argument("--build-python", type=Path, required=True)
    wheel.add_argument("--wheel-dir", type=Path, required=True)
    options = parser.parse_args()
    try:
        result = (
            load_policy()
            if options.command == "policy"
            else build_wheel(options.project, options.build_python, options.wheel_dir)
        )
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Wheel command failed: {error}", file=sys.stderr)
        return 1

    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

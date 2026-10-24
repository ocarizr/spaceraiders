"""Developer-facing formatter with syntax-aware block separation, Windows only."""

import argparse
import hashlib
import io
import os
from pathlib import Path
import subprocess
import sys
import urllib.request
import zipfile

from tree_sitter_language_pack import get_parser


LANGUAGES = {
    ".cpp": "cpp",
    ".h": "cpp",
    ".hpp": "cpp",
    ".cs": "csharp",
    ".py": "python",
    ".lua": "lua",
    ".cmake": "cmake",
}
BLOCKS = {
    "function_definition",
    "function_declaration",
    "method_declaration",
    "constructor_declaration",
    "destructor_declaration",
    "local_function_statement",
    "operator_declaration",
    "accessor_declaration",
    "if_statement",
    "for_statement",
    "for_range_loop",
    "foreach_statement",
    "for_each_statement",
    "while_statement",
    "do_statement",
    "repeat_statement",
    "try_statement",
    "switch_statement",
    "with_statement",
    "match_statement",
    "using_statement",
    "lock_statement",
    "fixed_statement",
    "function_def",
    "macro_def",
    "if_condition",
    "foreach_loop",
    "while_loop",
    "block_def",
}
STYLUA_URL = (
    "https://github.com/JohnnyMorganz/StyLua/releases/download/v2.5.2/stylua-windows-x86_64.zip"
)
STYLUA_SHA256 = "e77d0ea1226b8b389b43f702240091249a96eea25857281f90ea24d0eb9eb969"


def git(*arguments):
    return subprocess.check_output(["git", *arguments]).decode("utf-8")


def repository():
    return Path(git("rev-parse", "--show-toplevel").strip()).resolve()


def bootstrap_lua(root):
    """Explicit bootstrap; extracts only the verified executable into local tools."""
    destination = root / ".tools/code-style/stylua.exe"
    with urllib.request.urlopen(STYLUA_URL, timeout=60) as response:
        archive = response.read(32 * 1024 * 1024)

    if hashlib.sha256(archive).hexdigest() != STYLUA_SHA256:
        raise ValueError("StyLua archive checksum mismatch")

    with zipfile.ZipFile(io.BytesIO(archive)) as bundle:
        binary = bundle.read("stylua.exe")

    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(binary)
    return destination


def walk(node):
    yield node

    for child in node.named_children:
        yield from walk(child)


def separate_blocks(source, language):
    """Only insert whitespace; comments/strings and branch chains are parsed."""
    data = source.encode("utf-8")
    tree = get_parser(language).parse(data)

    if tree.root_node.has_error:
        pending = [tree.root_node]

        while pending:
            candidate = pending.pop()
            # The pinned C++ grammar misreads an empty default initializer as
            # a compound literal with a missing type. Accept only that shape.
            known_default = (
                language == "cpp"
                and candidate.is_missing
                and candidate.type == "type_identifier"
                and candidate.parent.type == "compound_literal_expression"
                and candidate.parent.text.strip() == b"{}"
                and candidate.parent.parent.type == "optional_parameter_declaration"
            )

            if (candidate.is_missing or candidate.type == "ERROR") and not known_default:
                raise ValueError(f"Cannot safely format {language}: syntax/parser error")

            pending.extend(candidate.children)

    lines = source.splitlines()
    gaps = set()

    for node in walk(tree.root_node):
        if language == "csharp":
            for field in ("body", "consequence", "alternative"):
                body = node.child_by_field_name(field)

                if body and node.type in BLOCKS and body.type not in {"block", "switch_body"}:
                    if field == "alternative" and body.type == "if_statement":
                        continue

                    raise ValueError(f"C# braces required at line {node.start_point.row + 1}")

        if node.type not in BLOCKS and node.type != "lambda_expression":
            continue

        owner = node

        if node.type == "lambda_expression":
            while owner.parent and owner.type not in {"declaration", "expression_statement"}:
                owner = owner.parent

            if owner.type not in {"declaration", "expression_statement"}:
                continue

        if owner.parent and owner.parent.type == "decorated_definition":
            owner = owner.parent

        following = owner.next_named_sibling

        while (
            following
            and following.type == "comment"
            and following.start_point.row == owner.end_point.row
        ):
            following = following.next_named_sibling

        if not following or following.type in {
            "else_clause",
            "elif_clause",
            "catch_clause",
            "finally_clause",
        }:
            continue

        end = owner.end_point.row

        if following.start_point.row <= end:
            # Base formatters must expand blocks first; never split inline syntax ourselves.
            raise ValueError(f"Separate statements after block at line {end + 1}")

        if not any(not line.strip() for line in lines[end + 1 : following.start_point.row]):
            gaps.add(end + 1)

    for row in sorted(gaps, reverse=True):
        lines.insert(row, "")

    return "\n".join(lines) + "\n"


def canonical(source, path, root):
    language = "cmake" if path.name == "CMakeLists.txt" else LANGUAGES[path.suffix.lower()]
    binaries = Path(sys.executable).parent

    if language in {"cpp", "csharp"}:
        command = [str(binaries / "clang-format.exe"), "--style=file", f"--assume-filename={path}"]
    elif language == "python":
        command = [
            str(binaries / "ruff.exe"),
            "format",
            "--isolated",
            "--line-length",
            "100",
            "--stdin-filename",
            str(path),
            "-",
        ]
    elif language == "cmake":
        command = [str(binaries / "gersemi.exe"), "--line-length", "100", "--indent", "4", "-"]
    else:
        executable = root / ".tools/code-style/stylua.exe"

        if not executable.is_file():
            raise ValueError("StyLua missing: run raiders-style --bootstrap-lua")

        version = subprocess.check_output([str(executable), "--version"], text=True).strip()

        if version != "stylua 2.5.2":
            raise ValueError(f"Expected stylua 2.5.2, got {version}")

        command = [
            str(executable),
            "--syntax",
            "Lua51",
            "--column-width",
            "100",
            "--indent-type",
            "Spaces",
            "--indent-width",
            "4",
            "--collapse-simple-statement",
            "Never",
            "-",
        ]

    formatted = subprocess.run(
        command,
        input=source,
        text=True,
        encoding="utf-8",
        capture_output=True,
        check=True,
        cwd=root,
    ).stdout
    return separate_blocks(formatted.replace("\r\n", "\n"), language)


def selected_files(root, base):
    arguments = ["diff", "--name-only", "-z", "--diff-filter=ACMR"]

    if base:
        merge_base = git("merge-base", base, "HEAD").strip()
        arguments.append(merge_base)
    else:
        arguments.append("HEAD")

    changed = git(*arguments).split("\0")
    untracked = git("ls-files", "--others", "--exclude-standard", "-z").split("\0")
    return sorted({root / path for path in changed + untracked if path})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=Path)
    parser.add_argument("--fix", action="store_true")
    parser.add_argument("--base", help="Compare against the merge base of this Git ref and HEAD")
    parser.add_argument(
        "--bootstrap-lua", action="store_true", help="Download the pinned Windows StyLua"
    )
    options = parser.parse_args()

    if os.name != "nt":
        parser.error("Only Windows is supported")

    root = repository()

    if options.bootstrap_lua:
        bootstrap_lua(root)

    paths = options.paths or selected_files(root, options.base)
    failed = False

    for path in paths:
        path = path.resolve()

        if path.name != "CMakeLists.txt" and path.suffix.lower() not in LANGUAGES:
            continue

        if not path.is_file():
            continue

        if not path.is_relative_to(root) or any(
            part in {"out", "build", ".tools", ".venv", "CMakeFiles", "_deps", "bin", "obj"}
            for part in path.relative_to(root).parts
        ):
            raise ValueError(f"Refusing generated or external path: {path}")

        source = path.read_text(encoding="utf-8")
        try:
            expected = canonical(source, path, root)
        except (ValueError, subprocess.CalledProcessError) as error:
            raise ValueError(f"{path.relative_to(root)}: {error}") from error

        if source != expected:
            if options.fix:
                path.write_text(expected, encoding="utf-8", newline="\n")
                print(f"Formatted {path.relative_to(root)}")
            else:
                print(f"Format required: {path.relative_to(root)}")
                failed = True

    return int(failed)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Style check failed: {error}", file=sys.stderr)
        raise SystemExit(2)

"""Contract fixtures for syntax-aware spacing in every repository language."""

import re

import pytest

from raiders_code_style.__main__ import canonical, repository, separate_blocks


FIXTURES = {
    "cpp": ("fixture.cpp", "void f(){if(x){g();}h();}\nvoid k(){}\n"),
    "csharp": ("fixture.cs", "class C { void F(){if(x){G();}H();} void K(){} }\n"),
    "python": ("fixture.py", "def f():\n    if x:\n        g()\n    h()\ndef k():\n    pass\n"),
    "cmake": (
        "fixture.cmake",
        "function(f)\nif(X)\ng()\nendif()\nh()\nendfunction()\nfunction(k)\nendfunction()\n",
    ),
    "lua": ("fixture.lua", "function f()\nif x then\ng()\nend\nh()\nend\nfunction k() end\n"),
}


@pytest.fixture(scope="session")
def root():
    return repository()


@pytest.mark.parametrize("filename,source", FIXTURES.values(), ids=FIXTURES.keys())
def test_real_formatters_and_idempotence_for_all_languages(root, filename, source):
    result = canonical(source, root / filename, root)
    assert result != source
    assert canonical(result, root / filename, root) == result
    assert re.search(r"(?:}|endif\(\)|end|g\(\))\n\s*\n\s*[hH]\(", result)


def test_comments_and_strings_are_not_control_blocks():
    source = 'def f():\n    text = "if end }"\n    # if end }\n    return text\n'
    assert separate_blocks(source, "python") == source


def test_cpp_else_and_catch_chains_remain_connected(root):
    source = "void f(){if(x){a();}else{b();}try{c();}catch(...){d();}e();}\n"
    result = canonical(source, root / "fixture.cpp", root)
    assert not re.search(r"}\n\s*\n\s*(else|catch)", result)
    assert re.search(r"}\n\s*\n\s*e\(\)", result)


def test_python_else_and_finally_chains_remain_connected(root):
    source = "def f():\n    try:\n        a()\n    except Exception:\n        b()\n    finally:\n        c()\n    d()\n"
    result = canonical(source, root / "fixture.py", root)
    assert not re.search(r"b\(\)\n\s*\n\s*finally", result)
    assert re.search(r"c\(\)\n\s*\n\s*d\(\)", result)


def test_multiline_literals_are_preserved():
    source = 'def f():\n    text = """for x:\nend\n}\n"""\n    return text\n'
    assert separate_blocks(source, "python") == source


def test_parser_errors_fail_closed():
    with pytest.raises(ValueError):
        separate_blocks("void f( {", "cpp")


def test_cpp_empty_default_initializer_is_valid(root):
    source = "void f(Context context = {});\n"
    assert "context = {}" in canonical(source, root / "fixture.cpp", root)


def test_csharp_braceless_control_body_is_rejected(root):
    with pytest.raises(ValueError, match="braces required"):
        canonical("class C { void F(){if(x) G();} }", root / "fixture.cs", root)


def test_csharp_switch_body_is_valid(root):
    source = "class C { void F(){switch(x){case 1: G(); break;} H();} }"
    result = canonical(source, root / "fixture.cs", root)
    assert canonical(result, root / "fixture.cs", root) == result


def test_check_does_not_write_files(root, tmp_path):
    path = tmp_path / "fixture.py"
    original = FIXTURES["python"][1]
    path.write_text(original, encoding="utf-8")
    canonical(original, root / "fixture.py", root)
    assert path.read_text(encoding="utf-8") == original

"""Contract fixtures for syntax-aware spacing in every repository language."""

from pathlib import Path
import tempfile
import unittest

from raiders_code_style.__main__ import canonical, repository, separate_blocks


ROOT = repository()
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


class StyleTests(unittest.TestCase):
    def test_real_formatters_and_idempotence_for_all_languages(self):
        for language, (filename, source) in FIXTURES.items():
            with self.subTest(language=language):
                result = canonical(source, ROOT / filename, ROOT)
                self.assertNotEqual(result, source)
                self.assertEqual(canonical(result, ROOT / filename, ROOT), result)
                self.assertRegex(result, r"(?:}|endif\(\)|end|g\(\))\n\s*\n\s*[hH]\(")

    def test_comments_and_strings_are_not_control_blocks(self):
        source = 'def f():\n    text = "if end }"\n    # if end }\n    return text\n'
        self.assertEqual(separate_blocks(source, "python"), source)

    def test_cpp_else_and_catch_chains_remain_connected(self):
        source = "void f(){if(x){a();}else{b();}try{c();}catch(...){d();}e();}\n"
        result = canonical(source, ROOT / "fixture.cpp", ROOT)
        self.assertNotRegex(result, r"}\n\s*\n\s*(else|catch)")
        self.assertRegex(result, r"}\n\s*\n\s*e\(\)")

    def test_python_else_and_finally_chains_remain_connected(self):
        source = "def f():\n    try:\n        a()\n    except Exception:\n        b()\n    finally:\n        c()\n    d()\n"
        result = canonical(source, ROOT / "fixture.py", ROOT)
        self.assertNotRegex(result, r"b\(\)\n\s*\n\s*finally")
        self.assertRegex(result, r"c\(\)\n\s*\n\s*d\(\)")

    def test_multiline_literals_are_preserved(self):
        source = 'def f():\n    text = """for x:\nend\n}\n"""\n    return text\n'
        self.assertEqual(separate_blocks(source, "python"), source)

    def test_parser_errors_fail_closed(self):
        with self.assertRaises(ValueError):
            separate_blocks("void f( {", "cpp")

    def test_cpp_empty_default_initializer_is_valid(self):
        source = "void f(Context context = {});\n"
        self.assertIn("context = {}", canonical(source, ROOT / "fixture.cpp", ROOT))

    def test_csharp_braceless_control_body_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "braces required"):
            canonical("class C { void F(){if(x) G();} }", ROOT / "fixture.cs", ROOT)

    def test_csharp_switch_body_is_valid(self):
        source = "class C { void F(){switch(x){case 1: G(); break;} H();} }"
        result = canonical(source, ROOT / "fixture.cs", ROOT)
        self.assertEqual(canonical(result, ROOT / "fixture.cs", ROOT), result)

    def test_check_does_not_write_files(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.py"
            original = FIXTURES["python"][1]
            path.write_text(original, encoding="utf-8")
            canonical(original, ROOT / "fixture.py", ROOT)
            self.assertEqual(path.read_text(encoding="utf-8"), original)


if __name__ == "__main__":
    unittest.main()

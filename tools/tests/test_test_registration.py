from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_test_registration


class RegistrationTests(unittest.TestCase):
    def registered(self, content):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "CTestTestfile.cmake").write_text(content, encoding="utf-8")
            return check_test_registration.registered(build)

    def test_test_name_does_not_register_executable(self):
        names = self.registered('add_test("missing_tests" "/usr/bin/python3" "unrelated.py")\n')
        self.assertNotIn("missing_tests", names)

    def test_direct_executable(self):
        names = self.registered('add_test("bounds" "/build/tests/buffer_bounds_tests")\n')
        self.assertIn("buffer_bounds_tests", names)
        self.assertNotIn("bounds", names)

    def test_wrapped_executable(self):
        names = self.registered('add_test("bounds" "/usr/bin/python3" "runner.py" "/build/tests/buffer_bounds_tests")\n')
        self.assertIn("buffer_bounds_tests", names)

    def test_windows_executable_and_test_name(self):
        names = self.registered('add_test("missing_tests.exe" "C:/Program Files/Python/python.exe" "runner.py" "C:/build/tests/buffer_bounds_tests.exe")\n')
        self.assertIn("buffer_bounds_tests", names)
        self.assertNotIn("missing_tests", names)

    def test_hyphenated_executable_is_discovered(self):
        output = "tests/buffer-bounds_tests: CXX_EXECUTABLE_LINKER\n"
        with patch.object(check_test_registration.subprocess, "run", return_value=SimpleNamespace(stdout=output)):
            self.assertEqual(check_test_registration.built("build"), {"buffer-bounds_tests"})

    def test_nested_test_output_is_discovered(self):
        output = "tests/gpu/buffer_bounds_tests: CXX_EXECUTABLE_LINKER\n"
        with patch.object(check_test_registration.subprocess, "run", return_value=SimpleNamespace(stdout=output)):
            self.assertEqual(check_test_registration.built("build"), {"buffer_bounds_tests"})

    def test_quoted_cmake_path_is_unescaped(self):
        names = self.registered(r'add_test("bounds" "/build/tests/a\"b_tests")' + '\n')
        self.assertIn('a"b_tests', names)

    def test_bracket_quoted_command_is_recognized(self):
        names = self.registered('add_test([=[bounds]=] [=[/build/tests/buffer_bounds_tests]=])\n')
        self.assertIn("buffer_bounds_tests", names)

    def test_multiline_test_command_is_recognized(self):
        names = self.registered('add_test("bounds"\n    "/build/tests/buffer_bounds_tests"\n)\n')
        self.assertIn("buffer_bounds_tests", names)

    def test_disabled_test_is_not_counted_as_run(self):
        names = self.registered('add_test("bounds" "/build/tests/buffer_bounds_tests")\nset_tests_properties("bounds" PROPERTIES DISABLED TRUE)\n')
        self.assertNotIn("buffer_bounds_tests", names)

    def test_option_value_does_not_register_executable(self):
        names = self.registered('add_test("bounds" "/usr/bin/python3" "runner.py" "--report=/build/tests/missing_tests")\n')
        self.assertNotIn("missing_tests", names)

    def test_reenabled_test_is_counted_as_run(self):
        names = self.registered('add_test("bounds" "/build/tests/buffer_bounds_tests")\nset_tests_properties("bounds" PROPERTIES DISABLED TRUE)\nset_tests_properties("bounds" PROPERTIES DISABLED OFF)\n')
        self.assertIn("buffer_bounds_tests", names)

    def test_disabled_wrapper_does_not_register_its_argument(self):
        names = self.registered('add_test("bounds" "/usr/bin/python3" "runner.py" "/build/tests/buffer_bounds_tests")\nset_tests_properties([=[bounds]=] PROPERTIES DISABLED "ON")\n')
        self.assertNotIn("buffer_bounds_tests", names)

    def test_command_text_inside_an_argument_is_not_another_test(self):
        names = self.registered('add_test("bounds" "/usr/bin/python3" [=[\nadd_test("fake" "/build/tests/missing_tests")\n]=])\n')
        self.assertNotIn("missing_tests", names)

    def test_windows_escaped_path_is_decoded(self):
        names = self.registered(r'add_test("bounds" "C:\\build\\tests\\buffer_bounds_tests.exe")' + '\n')
        self.assertIn("buffer_bounds_tests", names)

    def test_unreferenced_registration_directory_does_not_count(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "CTestTestfile.cmake").write_text("", encoding="utf-8")
            stale = build / "removed"
            stale.mkdir()
            (stale / "CTestTestfile.cmake").write_text(
                'add_test("bounds" "/build/tests/buffer_bounds_tests")\n', encoding="utf-8")
            self.assertNotIn("buffer_bounds_tests", check_test_registration.registered(build))
            (build / "CTestTestfile.cmake").unlink()
            self.assertNotIn("buffer_bounds_tests", check_test_registration.registered(build))

    def test_only_reachable_nested_directories_are_registered(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            active = build / "active tests"
            nested = active / "nested"
            removed = active / "removed"
            nested.mkdir(parents=True)
            removed.mkdir()
            (build / "CTestTestfile.cmake").write_text('subdirs("active tests")\n', encoding="utf-8")
            (active / "CTestTestfile.cmake").write_text('subdirs([=[nested]=])\n', encoding="utf-8")
            for path, name in ((nested, "active_tests"), (removed, "removed_tests")):
                (path / "CTestTestfile.cmake").write_text(
                    f'add_test("check" "/build/tests/{name}")\n', encoding="utf-8")
            self.assertEqual(check_test_registration.registered(build), {"active_tests"})

    def test_external_registration_directory_is_followed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build, external = root / "build", root / "external"
            build.mkdir()
            external.mkdir()
            (build / "CTestTestfile.cmake").write_text(
                f'subdirs("{external.as_posix()}")\n', encoding="utf-8")
            (external / "CTestTestfile.cmake").write_text(
                'add_test("bounds" "/build/tests/buffer_bounds_tests")\n', encoding="utf-8")
            self.assertEqual(check_test_registration.registered(build), {"buffer_bounds_tests"})


if __name__ == "__main__":
    unittest.main()

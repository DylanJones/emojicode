"""Regression tests for the test discovery and scheduling in tests.py, run against isolated fixture directories so
they do not depend on, or affect, the real tests/ tree. Run with `python3 test_tests.py`."""
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tests as tests_py


def write(directory, name, extension, content=""):
    with open(os.path.join(directory, name + extension), "w", encoding="utf-8") as f:
        f.write(content)


class DiscoverCompilationTestsTests(unittest.TestCase):
    def test_full_run_selects_a_stress_test_and_all_its_tasks(self):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "heavy", ".emojic", "💭 test: stress unoptimized\n")
            write(directory, "heavy", ".txt")
            write(directory, "heavy", ".ir")
            write(directory, "heavy", ".specializations")

            discovered = tests_py.discover_compilation_tests(directory, set(), quick=False, valgrind=False)

            self.assertEqual(discovered["compilation_tests"], ["heavy"])
            self.assertEqual(discovered["stress_tests"], ["heavy"])
            self.assertEqual(discovered["unoptimized_tests"], ["heavy"])
            self.assertEqual(discovered["specialization_tests"], ["heavy"])
            self.assertEqual(discovered["ir_tests"], ["heavy"])

    def test_leak_check_directive_is_parsed(self):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "checked", ".emojic", "💭 test: leak_check\n")
            write(directory, "checked", ".txt")
            write(directory, "plain", ".emojic")
            write(directory, "plain", ".txt")

            discovered = tests_py.discover_compilation_tests(directory, set(), quick=False, valgrind=False)

            self.assertEqual(discovered["leak_check_tests"], ["checked"])

    def test_quick_and_valgrind_runs_exclude_a_stress_test_from_every_derived_task_list(self):
        # Regression test for the bug fixed alongside this test: unoptimized_tests, specialization_tests and
        # ir_tests used to be derived from compilation_tests before the "stress" filter was applied, so a
        # "stress unoptimized" test with .ir/.specializations sidecars still scheduled those three tasks in
        # quick and valgrind runs.
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "heavy", ".emojic", "💭 test: stress unoptimized\n")
            write(directory, "heavy", ".txt")
            write(directory, "heavy", ".ir")
            write(directory, "heavy", ".specializations")

            for quick, valgrind in [(True, False), (False, True)]:
                with self.subTest(quick=quick, valgrind=valgrind):
                    discovered = tests_py.discover_compilation_tests(directory, set(), quick=quick,
                                                                       valgrind=valgrind)
                    self.assertEqual(discovered["compilation_tests"], [])
                    self.assertEqual(discovered["unoptimized_tests"], [])
                    self.assertEqual(discovered["specialization_tests"], [])
                    self.assertEqual(discovered["ir_tests"], [])

    def test_non_stress_test_is_selected_in_every_mode(self):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "plain", ".emojic")
            write(directory, "plain", ".txt")

            for quick, valgrind in [(False, False), (True, False), (False, True)]:
                with self.subTest(quick=quick, valgrind=valgrind):
                    discovered = tests_py.discover_compilation_tests(directory, set(), quick=quick,
                                                                       valgrind=valgrind)
                    self.assertEqual(discovered["compilation_tests"], ["plain"])

    def test_include_only_fragment_is_skipped_and_needs_no_expected_output(self):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "includer", ".emojic")
            write(directory, "includer", ".txt")
            write(directory, "included", ".emojic")  # No included.txt: it is not a test of its own.

            discovered = tests_py.discover_compilation_tests(directory, {"included"}, quick=False, valgrind=False)

            self.assertEqual(discovered["compilation_tests"], ["includer"])

    def test_unknown_directive_fails_the_suite(self):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "bad", ".emojic", "💭 test: bogus\n")
            write(directory, "bad", ".txt")

            with self.assertRaises(SystemExit):
                tests_py.discover_compilation_tests(directory, set(), quick=False, valgrind=False)

    def test_missing_expected_output_fails_the_suite(self):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "bad", ".emojic")

            with self.assertRaises(SystemExit):
                tests_py.discover_compilation_tests(directory, set(), quick=False, valgrind=False)


class DiscoverLibraryTestsTests(unittest.TestCase):
    def test_slow_directive_is_parsed_and_does_not_exclude_the_test(self):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "heavy", ".emojic", "💭 test: slow\n")
            write(directory, "light", ".emojic")

            library_tests, directives = tests_py.discover_library_tests(directory)

            self.assertEqual(library_tests, ["heavy", "light"])
            self.assertEqual(directives["heavy"], {"slow"})
            self.assertEqual(directives["light"], set())

    def test_unknown_directive_fails_the_suite(self):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "bad", ".emojic", "💭 test: bogus\n")

            with self.assertRaises(SystemExit):
                tests_py.discover_library_tests(directory)


class DiscoverHostTestsTests(unittest.TestCase):
    def test_missing_companion_files_fail_the_suite(self):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "host1", ".emojic")

            with self.assertRaises(SystemExit):
                tests_py.discover_host_tests(directory)

    def test_complete_fixture_is_discovered(self):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "host1", ".emojic")
            write(directory, "host1", ".c")
            write(directory, "host1", ".txt")

            self.assertEqual(tests_py.discover_host_tests(directory), ["host1"])


class DiscoverImportingTestsTests(unittest.TestCase):
    def test_missing_companion_files_fail_the_suite(self):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "importer", ".emojic")

            with self.assertRaises(SystemExit):
                tests_py.discover_importing_tests(directory)

    def test_complete_fixture_is_discovered(self):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "importer", ".emojic")
            write(directory, "importerPackage", ".🍇")
            write(directory, "importer", ".txt")

            self.assertEqual(tests_py.discover_importing_tests(directory), ["importer"])


class FailureReportTests(unittest.TestCase):
    def setUp(self):
        self.original_run = tests_py.run
        self.original_failed = list(tests_py.failed_tests)
        tests_py.failed_tests.clear()

    def tearDown(self):
        tests_py.run = self.original_run
        tests_py.failed_tests[:] = self.original_failed

    def test_failing_library_test_with_invalid_utf8_output_is_logged_and_counted_once(self):
        def fake_run(args, check=False, **kwargs):
            return subprocess.CompletedProcess(args, 1, stdout=b"caf\xc3")
        tests_py.run = fake_run

        lines, _, failed = tests_py.perform("broken", tests_py.library_test, "broken")

        self.assertTrue(failed)
        self.assertEqual(tests_py.failed_tests, ["broken"])
        self.assertIn("caf\\xc3", "".join(lines))
        self.assertNotIn("Traceback", "".join(lines))

    def test_fail_test_reports_a_test_once(self):
        tests_py.report.lines = []
        tests_py.report.failed = False
        tests_py.fail_test("twice")
        tests_py.fail_test("twice")
        self.assertEqual(tests_py.failed_tests, ["twice"])


if __name__ == "__main__":
    unittest.main()

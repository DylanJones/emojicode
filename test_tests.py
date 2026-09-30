"""Regression tests for the test discovery and scheduling in tests.py, run against isolated fixture directories so
they do not depend on, or affect, the real tests/ tree. Run with `python3 test_tests.py`."""
import os
import re
import subprocess
import sys
import tempfile
import time
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


class DirectiveTests(unittest.TestCase):
    def read(self, content, allowed=None):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "t", ".emojic", content)
            return tests_py.read_directives(os.path.join(directory, "t.emojic"),
                                            allowed or tests_py.COMPILATION_DIRECTIVES)

    def test_directive_after_code_is_not_ignored(self):
        self.assertEqual(self.read("📦 s\n\n💭 test: panic\n"), {"panic"})

    def test_later_directive_with_unknown_token_fails(self):
        with self.assertRaises(SystemExit):
            self.read("💭 test: panic\n💭 test: typo\n")

    def test_directives_are_merged(self):
        self.assertEqual(self.read("💭 test: panic\n💭 test: stress\n"), {"panic", "stress"})


class OrphanTests(unittest.TestCase):
    def test_compilation_companions_without_source_fail(self):
        for extension in [".txt", ".ir", ".specializations"]:
            with self.subTest(extension=extension), tempfile.TemporaryDirectory() as directory:
                write(directory, "typo", extension)
                with self.assertRaises(SystemExit):
                    tests_py.discover_compilation_tests(directory, set(), quick=False, valgrind=False)

    def test_host_companions_without_source_fail(self):
        for extension in [".c", ".txt"]:
            with self.subTest(extension=extension), tempfile.TemporaryDirectory() as directory:
                write(directory, "typo", extension)
                with self.assertRaises(SystemExit):
                    tests_py.discover_host_tests(directory)

    def test_importing_package_without_importer_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "typoPackage", ".🍇")
            with self.assertRaises(SystemExit):
                tests_py.discover_importing_tests(directory)


class BuildConfigurationTests(unittest.TestCase):
    """The build and CI configuration must keep building what tests.py runs and running every kind of test."""
    ROOT = os.path.dirname(os.path.abspath(__file__))

    def read(self, *path):
        with open(os.path.join(self.ROOT, *path), encoding="utf-8") as f:
            return f.read()

    def test_tests_target_builds_what_it_runs(self):
        match = re.search(r"add_dependencies\(tests ([^)]*)\)", self.read("CMakeLists.txt"))
        self.assertIsNotNone(match, "the tests target has no dependencies")
        for target in ["emojicodec", "runtime", "s", "c"]:
            self.assertIn(target, match.group(1).split())

    def test_ci_runs_every_test_suite_and_a_release_build(self):
        ci = self.read(".github", "workflows", "ci.yml")
        for command in ["ninja -C build tests", "testspy", "lsptests", "treesittertests", "grammar", "npm test"]:
            self.assertIn(command, ci)
        self.assertIn("Release", ci)
        self.assertIn("CMAKE_BUILD_TYPE", ci)


class RejectTests(unittest.TestCase):
    def test_missing_or_empty_expected_message_fails_the_suite(self):
        for content in [None, "", " \n"]:
            with self.subTest(content=content), tempfile.TemporaryDirectory() as directory:
                write(directory, "r", ".emojic")
                if content is not None:
                    write(directory, "r", ".txt", content)
                with self.assertRaises(SystemExit):
                    tests_py.discover_reject_tests(directory)

    def test_orphan_companion_fails_the_suite(self):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "typo", ".txt", "message")
            with self.assertRaises(SystemExit):
                tests_py.discover_reject_tests(directory)

    def test_complete_fixture_is_discovered(self):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "r", ".emojic")
            write(directory, "r", ".txt", "message\n")
            self.assertEqual(tests_py.discover_reject_tests(directory), [os.path.join(directory, "r.emojic")])

    def test_empty_expectation_never_matches(self):
        """reject_test itself also refuses an empty expectation, which would be contained in any output."""
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "r", ".emojic")
            write(directory, "r", ".txt", "\n")
            original = tests_py.run
            tests_py.run = lambda *a, **k: subprocess.CompletedProcess(a, 1, b"", b"\xf0\x9f\x9a\xa8 error: x\n")
            try:
                tests_py.report.lines, tests_py.report.stderr, tests_py.report.failed = [], [], False
                tests_py.failed_tests.clear()
                tests_py.reject_test(os.path.join(directory, "r.emojic"))
                self.assertTrue(tests_py.report.failed)
            finally:
                tests_py.run = original
                tests_py.failed_tests.clear()


class CheckIRTests(unittest.TestCase):
    IR = 'define void @"f"() {\nentry:\n  ret void\n}\n'

    def check(self, checks):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "t", ".ir", checks)
            tests_py.report.lines = []
            tests_py.report.stderr = []
            tests_py.report.failed = False
            tests_py.check_ir("t", self.IR, os.path.join(directory, "t.ir"))
            return tests_py.report.failed

    def test_line_without_selected_function_fails(self):
        self.assertTrue(self.check("+ impossible\n"))
        self.assertTrue(self.check("- ret\n"))

    def test_selected_function_is_checked(self):
        self.assertFalse(self.check("@ ^f$\n+ ret void\n"))
        self.assertTrue(self.check("@ ^f$\n+ impossible\n"))


class RunTests(unittest.TestCase):
    @staticmethod
    def running(pid):
        """Whether the process exists and is not a zombie that nothing has reaped yet."""
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return False
        try:
            with open("/proc/{0}/stat".format(pid)) as f:
                return f.read().rsplit(")", 1)[1].split()[0] != "Z"
        except OSError:
            return True

    def test_timeout_kills_child_processes(self):
        with tempfile.TemporaryDirectory() as directory:
            pid_file = os.path.join(directory, "pid")
            script = "import subprocess,sys,time;" \
                     "p=subprocess.Popen(['sleep','30']);open(sys.argv[1],'w').write(str(p.pid));time.sleep(30)"
            tests_py.report.stderr = []
            with self.assertRaises(subprocess.TimeoutExpired):
                tests_py.run([sys.executable, "-c", script, pid_file], timeout=1)
            child = int(open(pid_file).read())
            for _ in range(50):
                if not self.running(child):
                    return
                time.sleep(0.1)
            os.kill(child, 9)
            self.fail("the child of a timed-out command is still running")

    def test_compiler_command_with_arguments_is_split(self):
        self.assertEqual(tests_py.shlex.split("cc -O0 -w"), ["cc", "-O0", "-w"])


class WarningTests(unittest.TestCase):
    STDERR = ("f.emojic:13:13: ⚠️  warning: Literal 300 does not fit.\n    code\n        ⬆️\n\n"
              "\x1b[1mg.emojic:2:3: \x1b[33m⚠️  warning: \x1b[0m\x1b[1mTwice.\n\x1b[0m"
              "⚠️  warning: No position.\n🚨 error: Not a warning.\n")

    def check(self, expected):
        with tempfile.TemporaryDirectory() as directory:
            write(directory, "t", ".warnings", expected)
            tests_py.report.lines = []
            tests_py.report.stderr = []
            tests_py.report.failed = False
            tests_py.check_warnings("t", self.STDERR, os.path.join(directory, "t.warnings"))
            return tests_py.report.failed

    def test_warnings_are_parsed_in_order(self):
        self.assertEqual(tests_py.parse_warnings(self.STDERR),
                         ["13:13: Literal 300 does not fit.", "2:3: Twice.", "No position."])

    def test_exact_list_passes(self):
        self.assertFalse(self.check("13:13: Literal 300 does not fit.\n2:3: Twice.\nNo position.\n"))

    def test_missing_extra_or_reordered_warnings_fail(self):
        self.assertTrue(self.check("13:13: Literal 300 does not fit.\n2:3: Twice.\n"))
        self.assertTrue(self.check("13:13: Literal 300 does not fit.\n2:3: Twice.\nNo position.\nNo position.\n"))
        self.assertTrue(self.check("2:3: Twice.\n13:13: Literal 300 does not fit.\nNo position.\n"))
        self.assertTrue(self.check(""))


class LibraryFixtureTests(unittest.TestCase):
    def test_fixtures_are_copied_but_sources_are_not(self):
        with tempfile.TemporaryDirectory() as source, tempfile.TemporaryDirectory() as destination:
            write(source, "t", ".emojic")
            write(source, "t_data", ".txt", "x")

            tests_py.copy_library_fixtures(source, destination)

            self.assertEqual(os.listdir(destination), ["t_data.txt"])

    def test_generated_files_are_ignored_by_git(self):
        root = os.path.dirname(os.path.abspath(__file__))
        for path in ["tests/compilation/ffiStructByValue_trampolines.c", "tests/s/fileTest_writeTest.txt"]:
            with self.subTest(path=path):
                try:
                    result = subprocess.run(["git", "-c", "safe.directory=*", "check-ignore", "-q", path], cwd=root)
                except FileNotFoundError:
                    self.skipTest("git is not installed")
                self.assertEqual(result.returncode, 0, path + " must be ignored, not tracked")


class FailureReportTests(unittest.TestCase):
    def setUp(self):
        self.original_run = tests_py.run
        self.original_failed = list(tests_py.failed_tests)
        tests_py.failed_tests.clear()

    def tearDown(self):
        for attribute in ("lines", "stderr", "failed"):
            if hasattr(tests_py.report, attribute):
                delattr(tests_py.report, attribute)
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

    def test_test_that_fails_and_then_raises_is_reported_once(self):
        def fails_then_raises():
            tests_py.fail_test("both")
            raise ValueError("after the failure")

        _, _, failed = tests_py.perform("both", fails_then_raises)
        self.assertTrue(failed)
        self.assertEqual(tests_py.failed_tests, ["both"])

    def test_test_that_raises_is_reported(self):
        def raises():
            raise ValueError("boom")

        tests_py.perform("raises", raises)
        self.assertEqual(tests_py.failed_tests, ["raises"])

    def test_distinct_failures_in_one_task_are_all_reported(self):
        def fake_run(args, check=False, **kwargs):
            return subprocess.CompletedProcess(args, 99, stdout=b"", stderr=b"")
        tests_py.run = fake_run

        _, _, failed = tests_py.perform("command line", tests_py.command_line_test, None)

        self.assertTrue(failed)
        # Every case of command_line_test fails; the locked-source case only exists when not running as root.
        expected = 10 + (1 if hasattr(os, "geteuid") and os.geteuid() != 0 else 0)
        self.assertEqual(len(tests_py.failed_tests), expected)
        self.assertEqual(len(set(tests_py.failed_tests)), expected)


if __name__ == "__main__":
    unittest.main()

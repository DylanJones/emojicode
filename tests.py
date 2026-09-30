from concurrent.futures import ThreadPoolExecutor, as_completed
from subprocess import PIPE, CalledProcessError, TimeoutExpired
import glob
import shutil
import os
import dist
import subprocess
import sys
import re
import shlex
import shutil
import signal
import tempfile
import threading
import traceback

quick = len(sys.argv) > 1 and sys.argv[1] == 'quick'
valgrind = len(sys.argv) > 1 and sys.argv[1] == 'valgrind'

# Test discovery
#
# Compilation, library, host and importing tests are found on disk, so adding one only adds files (see
# CONTRIBUTING.md). A test may have a directive comment, on a line of its own anywhere in the file, of the form
#
#   💭 test: TOKEN TOKEN ...
#
# A compilation test's tokens may be:
# - "unoptimized": also compiled and run without optimizations, which inline code that tests otherwise only test
#   inlined.
# - "panic": its program prints what NAME.txt says and then panics, which aborts it.
# - "stress": it takes seconds to run, so it is excluded from quick and valgrind runs and starts first, along with
#   the other slow tests, so that it does not end up running alone at the end.
# - "leak_check": also run unoptimized. Every program of a compilation test runs with EMOJICODE_CHECK_LEAKS set,
#   optimized and (with "unoptimized" or this token) unoptimized.
#   The runtime then counts calls to ejcAllocDescription/ejcFreeDescription (the malloc/free pair generated
#   exclusively for dynamic class generic-argument type descriptions, see TypeDescriptionGenerator) and the blocks
#   ejcAlloc allocates and the release functions free, and aborts at exit if they are unbalanced. Unlike an IR check, which inspects unoptimized IR text and so cannot see calls an
#   optimizer's tail-merging collapses together, this observes actual executed allocation/deallocation counts and
#   so still catches an ownership bug (a missing or duplicated free) at -O.
# A library test's tokens may be "slow": like "stress", it takes seconds to run and so starts first, but unlike
# "stress" it is not excluded from quick runs (valgrind runs do not include library tests at all).
#
# NAME.warnings, optional next to a compilation or reject test, lists the warnings the compiler must print, in order
# and with multiplicity, one per line as "LINE:COLUMN: MESSAGE" (only "MESSAGE" for one without a position). An empty
# file asserts that there are none; without one, warnings are not checked.
#
# A file that looks like a test but is missing a file its category requires (e.g. NAME.txt), a companion file
# (NAME.txt, NAME.ir, ...) without its source, or an unrecognized directive token, fails the suite instead of being
# silently skipped.
COMPILATION_DIRECTIVES = {"unoptimized", "panic", "stress", "leak_check"}
LIBRARY_DIRECTIVES = {"slow"}

DIRECTIVE_RE = re.compile(r'^💭\s*test:\s*(.*)$')


def read_directives(path, allowed):
    """Returns the tokens of the "💭 test: ..." directive lines of path, wherever they are in the file. Fails the suite
    if an unrecognized token is used, so that a directive can neither be silently ignored nor mistyped."""
    tokens = set()
    with open(path, "r", encoding='utf-8') as f:
        for line in f:
            match = DIRECTIVE_RE.match(line.strip())
            if match:
                tokens |= set(match.group(1).split())
    unknown = tokens - allowed
    if unknown:
        sys.exit("🛑 {0}: unknown test directive(s): {1}".format(path, ", ".join(sorted(unknown))))
    return tokens


def reject_orphans(directory, companion_extensions, source_extension, description):
    """Fails the suite if directory has a file with one of companion_extensions whose source_extension file is
    missing, which is a typo that would otherwise make the test silently not run."""
    for extension in companion_extensions:
        for name in names_with_extension(directory, extension):
            if not os.path.exists(os.path.join(directory, name + source_extension)):
                sys.exit("🛑 {0} has {1}, but no {2} ({3}).".format(directory, name + extension,
                                                                  name + source_extension, description))


def require(path, description):
    if not os.path.exists(path):
        sys.exit("🛑 {0} is missing.".format(description))


def names_with_extension(directory, extension):
    return sorted(os.path.splitext(os.path.basename(p))[0]
                  for p in glob.glob(os.path.join(directory, "*" + extension)))


def discover_compilation_tests(directory, include_fragments, quick, valgrind):
    """Finds the compilation tests in directory and returns a dict of the lists tests.py schedules from them:
    compilation_tests, stress_tests, unoptimized_tests, panic_tests, leak_check_tests, specialization_tests and
    ir_tests (see the module docstring above for directive semantics).

    quick and valgrind runs exclude the "stress" tests, which take seconds to run; that exclusion is applied to
    compilation_tests before unoptimized_tests, specialization_tests and ir_tests are derived from it, so a stress
    test's other tasks are excluded consistently with its own compilation task."""
    reject_orphans(directory, [".txt", ".ir", ".specializations", ".warnings"], ".emojic", "its compilation test")
    compilation_test_directives = {}
    for name in names_with_extension(directory, ".emojic"):
        if name in include_fragments:
            continue
        require(os.path.join(directory, name + ".txt"),
                "tests/compilation/{0}.txt, the expected output of {0}.emojic (or {0} must be listed in "
                "formatted_includes if it is an include-only fragment)".format(name))
        compilation_test_directives[name] = read_directives(os.path.join(directory, name + ".emojic"),
                                                             COMPILATION_DIRECTIVES)

    compilation_tests = sorted(compilation_test_directives)
    stress_tests = [name for name in compilation_tests if "stress" in compilation_test_directives[name]]
    if quick or valgrind:
        compilation_tests = [name for name in compilation_tests if name not in stress_tests]

    # Compilation tests that are also compiled and run without optimizations, which inline code that tests otherwise
    # only test inlined.
    unoptimized_tests = [name for name in compilation_tests if "unoptimized" in compilation_test_directives[name]]
    # Compilation tests whose programs print what NAME.txt says and then panic, which aborts them.
    panic_tests = [name for name in compilation_tests if "panic" in compilation_test_directives[name]]
    # Compilation tests also run with EMOJICODE_CHECK_DESCRIPTION_LEAKS set (see the "leak_check" directive above).
    leak_check_tests = [name for name in compilation_tests if "leak_check" in compilation_test_directives[name]]
    # Compilation tests whose specializations, functions whose symbol contains $s<, are compared with the names in
    # NAME.specializations. A function that is not specialized, but called generically, does not change what a
    # program prints.
    specialization_tests = [name for name in compilation_tests
                             if os.path.exists(os.path.join(directory, name + ".specializations"))]
    # Programs whose unoptimized LLVM IR is checked against NAME.ir. In NAME.ir, a line "@ REGEX" selects the
    # functions whose names match, and the lines "+ REGEX" and "- REGEX" after it must and must not match their
    # bodies. No function whose name matches the REGEX of a line "! REGEX" may be defined. Lines starting with # are
    # comments.
    ir_tests = [name for name in compilation_tests
                if os.path.exists(os.path.join(directory, name + ".ir"))]

    return {
        "compilation_tests": compilation_tests,
        "stress_tests": stress_tests,
        "unoptimized_tests": unoptimized_tests,
        "panic_tests": panic_tests,
        "leak_check_tests": leak_check_tests,
        "specialization_tests": specialization_tests,
        "ir_tests": ir_tests,
    }


def discover_library_tests(directory):
    """Finds the library tests in directory and returns (library_tests, library_test_directives)."""
    library_test_directives = {name: read_directives(os.path.join(directory, name + ".emojic"), LIBRARY_DIRECTIVES)
                               for name in names_with_extension(directory, ".emojic")}
    return sorted(library_test_directives), library_test_directives


def discover_host_tests(directory):
    """Finds the host tests in directory: Emojicode packages whose C functions (🎍🌊) are called by a C program of
    the same name, which also provides main. Requires each to have a NAME.c and NAME.txt."""
    reject_orphans(directory, [".c", ".txt"], ".emojic", "its host test")
    host_tests = names_with_extension(directory, ".emojic")
    for name in host_tests:
        require(os.path.join(directory, name + ".c"), "tests/host/{0}.c".format(name))
        require(os.path.join(directory, name + ".txt"), "tests/host/{0}.txt".format(name))
    return host_tests


def discover_reject_tests(directory):
    """Finds the reject tests in directory and returns the paths of their sources. Requires each to have a NAME.txt
    with the text its one error must contain, so that it is the error the test is meant to provoke and not an
    unrelated one, e.g. from syntax that no longer exists."""
    reject_orphans(directory, [".txt", ".warnings"], ".emojic", "its reject test")
    tests = names_with_extension(directory, ".emojic")
    for name in tests:
        path = os.path.join(directory, name + ".txt")
        require(path, "tests/reject/{0}.txt, the text of the error of {0}.emojic".format(name))
        if not open(path, encoding='utf-8').read().strip():
            sys.exit("🛑 tests/reject/{0}.txt is empty, so it would match any error.".format(name))
    return [os.path.join(directory, name + ".emojic") for name in tests]


def discover_importing_tests(directory):
    """Finds the importing tests in directory: programs that import a package of the same name with "Package"
    appended, which is compiled first. They test code that is only generated in importers, like the bodies of
    inlined methods. Requires each to have a NAMEPackage.🍇 and NAME.txt. Their IR is checked against NAME.ir and
    NAME.specializations, if present."""
    reject_orphans(directory, [".txt", ".ir", ".specializations"], ".emojic", "its importing test")
    for name in names_with_extension(directory, ".🍇"):
        if name.endswith("Package") and not os.path.exists(os.path.join(directory, name[:-len("Package")] + ".emojic")):
            sys.exit("🛑 {0}/{1}.🍇 has no {2}.emojic importing it.".format(directory, name, name[:-len("Package")]))
    importing_tests = names_with_extension(directory, ".emojic")
    for name in importing_tests:
        require(os.path.join(directory, name + "Package.🍇"), "tests/importing/{0}Package.🍇".format(name))
        require(os.path.join(directory, name + ".txt"), "tests/importing/{0}.txt".format(name))
    return importing_tests


package_ir_tests = names_with_extension(os.path.join(dist.source, "tests", "packageIR"), ".ir")

compilation_directory = os.path.join(dist.source, "tests", "compilation")
# Formatting a file also formats the files it includes, which only it includes, so they are covered by its lock.
# A file listed here is an include-only fragment, not a test of its own, so it needs no NAME.txt.
formatted_includes = {
    "includer": ["included"],
}
include_fragments = {fragment for fragments in formatted_includes.values() for fragment in fragments}

discovered_compilation_tests = discover_compilation_tests(compilation_directory, include_fragments, quick, valgrind)
compilation_tests = discovered_compilation_tests["compilation_tests"]
stress_tests = discovered_compilation_tests["stress_tests"]
unoptimized_tests = discovered_compilation_tests["unoptimized_tests"]
panic_tests = discovered_compilation_tests["panic_tests"]
leak_check_tests = discovered_compilation_tests["leak_check_tests"]
specialization_tests = discovered_compilation_tests["specialization_tests"]
ir_tests = discovered_compilation_tests["ir_tests"]

library_directory = os.path.join(dist.source, "tests", "s")
library_tests, library_test_directives = discover_library_tests(library_directory)

host_directory = os.path.join(dist.source, "tests", "host")
host_tests = discover_host_tests(host_directory)

importing_directory = os.path.join(dist.source, "tests", "importing")
importing_tests = discover_importing_tests(importing_directory)

reject_tests = discover_reject_tests(os.path.join(dist.source, "tests", "reject"))
format_tests = glob.glob(os.path.join(dist.source, "tests", "format", "*.emojic"))
parse_tests = glob.glob(os.path.join(dist.source, "tests", "parse",
                                     "*.emojic"))
test_packages = os.path.join(dist.source, "tests", "packages")

failed_tests = []
failed_tests_lock = threading.Lock()
# The output of the test running on the current thread, which is printed when it finishes.
report = threading.local()

emojicodec = os.path.abspath("Compiler/emojicodec")
os.environ["EMOJICODE_PACKAGES_PATH"] = os.path.abspath(".")
os.environ["TEST_ENV_1"] = "The day starts like the rest I've seen"
# The number of tests run at once, one per core unless EMOJICODE_TEST_JOBS says otherwise.
jobs = int(os.environ.get("EMOJICODE_TEST_JOBS", os.cpu_count() or 1))
# The seconds a command, e.g. the compiler or a test program, may run. One that hangs fails its test instead of the
# whole suite.
command_timeout = 300


source_locks = {}
source_locks_lock = threading.Lock()


def source_lock(path):
    """Returns the lock held while the compiler reads the source file at path, or while formatting rewrites it."""
    with source_locks_lock:
        return source_locks.setdefault(path, threading.Lock())


def log(text):
    report.lines.append(text)


def fail_test(name):
    log("🛑 {0} failed".format(name))
    report.failed = True
    with failed_tests_lock:
        failed_tests.append(name)


def run_process(args, timeout, **kwargs):
    """Like subprocess.run, but a command that times out is killed along with the processes it started."""
    with subprocess.Popen(args, start_new_session=True, **kwargs) as process:
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except BaseException:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            raise
        # Processes that outlive the command must not keep running (or hold its pipes) either.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    return subprocess.CompletedProcess(args, process.returncode, stdout, stderr)


def run(args, check=False, **kwargs):
    """Runs a command like subprocess.run. Its standard error is kept for the report of a test that fails, unless
    the caller handles it."""
    keep_stderr = 'stderr' not in kwargs
    if keep_stderr:
        kwargs['stderr'] = PIPE
    completed = run_process(args, kwargs.pop('timeout', command_timeout), **kwargs)
    if keep_stderr and completed.stderr:
        report.stderr.append(completed.stderr.decode('utf-8', 'replace'))
    if check and completed.returncode != 0:
        raise CalledProcessError(completed.returncode, args)
    return completed


def test_paths(name, kind):
    return (os.path.join(dist.source, "tests", kind, name + ".emojic"),
            os.path.join(dist.source, "tests", kind, name))


def copy_library_fixtures(source_directory, destination):
    """Copies the files a library test reads or writes, relative to its working directory, to destination."""
    for path in glob.glob(os.path.join(source_directory, "*")):
        if os.path.isfile(path) and not path.endswith(".emojic"):
            shutil.copy(path, destination)


def library_test(name):
    """Compiles and runs the library test name optimized and unoptimized, one after the other."""
    source_path = test_paths(name, 's')[0]
    for optimize in (True, False):
        with tempfile.TemporaryDirectory() as directory:
            binary_path = os.path.join(directory, name)
            run([emojicodec, source_path, '-o', binary_path] + (['-O'] if optimize else []), check=True)
            # The tests read files relative to their directory and may write there, so they run in a copy of it
            # instead of modifying the source tree.
            working_directory = os.path.join(directory, "cwd")
            os.mkdir(working_directory)
            copy_library_fixtures(os.path.join(dist.source, "tests", "s"), working_directory)
            completed = run([binary_path], stdout=PIPE, cwd=working_directory)
        if completed.returncode != 0:
            fail_test(name)
            log(completed.stdout.decode('utf-8', 'backslashreplace'))
            return


def check_output(name, binary_path):
    """Runs the program of the compilation test name, with the leak check of the runtime (see the "leak_check"
    directive above), and checks its output."""
    completed = run([binary_path], stdout=PIPE, env=dict(os.environ, EMOJICODE_CHECK_LEAKS='1'))
    exp_path = os.path.join(dist.source, "tests", "compilation", name + ".txt")
    output = completed.stdout.decode('utf-8', 'backslashreplace')
    expected_returncode = -signal.SIGABRT if name in panic_tests else 0
    if output != open(exp_path, "r", encoding='utf-8').read() or completed.returncode != expected_returncode:
        log(output)
        fail_test(name)


WARNING_RE = re.compile(r'^(?:.*?:(\d+):(\d+): )?⚠️  warning: (.*)$')
ANSI_RE = re.compile(r'\x1b\[[0-9;]*m')


def parse_warnings(stderr):
    """Returns the warnings in the compiler's standard error, in order and with multiplicity, each as "LINE:COLUMN:
    MESSAGE", or just the message if it has no position. File paths and colors are left out."""
    warnings = []
    for line in ANSI_RE.sub('', stderr).splitlines():
        match = WARNING_RE.match(line)
        if match:
            line_number, column, message = match.groups()
            warnings.append(message if line_number is None else "{0}:{1}: {2}".format(line_number, column, message))
    return warnings


def check_warnings(name, stderr, warnings_path):
    """Checks that the warnings in stderr are exactly those listed, one per line, in the file at warnings_path (see
    parse_warnings for their format), if there is one. An empty file asserts that there are none."""
    if not os.path.exists(warnings_path):
        return
    expected = open(warnings_path, "r", encoding='utf-8').read().splitlines()
    actual = parse_warnings(stderr)
    if actual != expected:
        log("Expected warnings:\n" + "\n".join(expected) + "\nActual warnings:\n" + "\n".join(actual))
        fail_test(name + " (warnings)")


def compilation_test(name, optimize=True):
    source_path = test_paths(name, 'compilation')[0]
    # Each compilation has its own directory, as a test can be compiled several times at once.
    with tempfile.TemporaryDirectory() as directory:
        binary_path = os.path.join(directory, name)
        with source_lock(source_path):
            compiled = run([emojicodec, source_path, '-o', binary_path] + (['-O'] if optimize else []), check=True)
        check_warnings(name, compiled.stderr.decode('utf-8', 'replace'),
                       os.path.join(dist.source, "tests", "compilation", name + ".warnings"))
        check_output(name, binary_path)


def specialization_test(name):
    source_path = test_paths(name, 'compilation')[0]
    with tempfile.TemporaryDirectory() as directory:
        with source_lock(source_path):
            run([emojicodec, source_path, '--emit-llvm', '-o', os.path.join(directory, name)], check=True)
        ir = open(os.path.join(directory, name + ".ll"), "r", encoding='utf-8').read()
    check_specializations(name, ir, os.path.join(dist.source, "tests", "compilation", name + ".specializations"))


def check_specializations(name, ir, exp_path):
    """Checks that the specializations defined in the IR are those listed in the file at exp_path."""
    specializations = sorted(set(re.findall(r'^define internal [^@]*@"([^"]*\$s<[^"]*)"', ir, re.MULTILINE)))
    expected = open(exp_path, "r", encoding='utf-8').read().split()
    if specializations != expected:
        log("Missing specializations: " + ", ".join(sorted(set(expected) - set(specializations))))
        log("Unexpected specializations: " + ", ".join(sorted(set(specializations) - set(expected))))
        fail_test(name + " (specializations)")


def ir_test(name):
    source_path = test_paths(name, 'compilation')[0]
    with tempfile.TemporaryDirectory() as directory:
        with source_lock(source_path):
            run([emojicodec, source_path, '--emit-llvm', '-o', os.path.join(directory, name)], check=True)
        ir = open(os.path.join(directory, name + ".ll"), "r", encoding='utf-8').read()
    check_ir(name, ir, os.path.join(dist.source, "tests", "compilation", name + ".ir"))


def check_ir(name, ir, check_path):
    """Checks the IR against the file at check_path (see ir_tests)."""
    functions = {m.group(1): m.group(2) for m in
                 re.finditer(r'^define [^\n]*@"?([^"(\s]+)"?\([^\n]*\{\n(.*?)^\}', ir, re.S | re.M)}
    bodies = []
    failed = False
    for line in open(check_path, "r", encoding='utf-8').read().splitlines():
        if not line or line.startswith('#'):
            continue
        kind, pattern = line[0], line[2:]
        if kind == '!':
            for function_name in functions:
                if re.search(pattern, function_name):
                    log("{0}: unexpectedly defined".format(function_name))
                    failed = True
            continue
        if kind == '@':
            bodies = [(n, b) for n, b in functions.items() if re.search(pattern, n)]
            if not bodies:
                log("No function matches " + pattern)
                failed = True
            continue
        if kind == '=':
            count_text, pattern = pattern.split(' ', 1)
            count = int(count_text)
            if not bodies:
                log("{0} is not preceded by an @ line that selects a function".format(line))
                failed = True
            for function_name, body in bodies:
                actual = len(re.findall(pattern, body))
                if actual != count:
                    log("{0}: expected {1} matches of {2}, found {3}".format(function_name, count, pattern, actual))
                    failed = True
            continue
        if kind not in '+-':
            log("Unknown check line: " + line)
            failed = True
            continue
        if not bodies:
            log("{0} is not preceded by an @ line that selects a function".format(line))
            failed = True
        for function_name, body in bodies:
            if (re.search(pattern, body) is not None) != (kind == '+'):
                log("{0}: {1} {2}".format(function_name, "missing" if kind == '+' else "unexpected", pattern))
                failed = True
    if failed:
        fail_test(name + " (IR)")


def package_ir_test(name):
    """Checks the optimized IR of the standard package NAME (see tests/packageIR/NAME.ir, in the format of ir_tests),
    whose functions are only generated in the package and so cannot be checked through a program."""
    directory = os.path.join(dist.source, "tests", "packageIR")
    main_file = os.path.join(dist.source, name, name + ".🍇")
    with tempfile.TemporaryDirectory() as ir_directory:
        run([emojicodec, '-p', name, '-O', '--emit-llvm', '-i', os.path.join(ir_directory, name + ".emojii"),
             '-o', os.path.join(ir_directory, name), main_file], check=True)
        ir = open(os.path.join(ir_directory, name + ".ll"), "r", encoding='utf-8').read()
    check_ir(name, ir, os.path.join(directory, name + ".ir"))


def host_test(name):
    directory = os.path.join(dist.source, "tests", "host")
    source_path = os.path.join(directory, name + ".emojic")
    # Everything generated, including the interface file, is in a directory of its own, as host tests run at once.
    with tempfile.TemporaryDirectory() as build_directory:
        object_path = os.path.join(build_directory, name + ".o")
        host_object_path = os.path.join(build_directory, name + "_host.o")
        binary_path = os.path.join(build_directory, name)
        run([emojicodec, '-p', name, '-o', object_path, '-i', os.path.join(build_directory, name + ".emojii"),
             '-c', source_path, '-O'], check=True)
        run(shlex.split(os.environ.get("CC", "cc")) +
            ['-c', os.path.join(directory, name + ".c"), '-o', host_object_path], check=True)
        libraries = [os.path.abspath(path) for path in ["c/libc.a", "sockets/libsockets.a", "s/libs.a", "runtime/libruntime.a"]]
        run(shlex.split(os.environ.get("CXX", "c++")) + [host_object_path, object_path] + libraries +
            ['-lm', '-lpthread', '-o', binary_path], check=True)
        completed = run([binary_path], stdout=PIPE)
    output = completed.stdout.decode('utf-8', 'backslashreplace')
    if output != open(os.path.join(directory, name + ".txt"), "r", encoding='utf-8').read() or \
            completed.returncode != 0:
        log(output)
        fail_test(name)


def importing_test(name):
    directory = os.path.join(dist.source, "tests", "importing")
    package = name + "Package"
    package_directory = os.path.join(directory, "packages", package)
    os.makedirs(package_directory, exist_ok=True)
    run([emojicodec, '-p', package, '-o', os.path.join(package_directory, "lib" + package + ".a"),
         os.path.join(directory, package + ".🍇"), '-O'], check=True)
    run([emojicodec, '-S', os.path.join(directory, "packages"), os.path.join(directory, name + ".emojic"), '-O'],
        check=True)
    # The specializations of the package's functions that the program creates and the functions it defines, if listed.
    exp_path = os.path.join(directory, name + ".specializations")
    check_path = os.path.join(directory, name + ".ir")
    if os.path.exists(exp_path) or os.path.exists(check_path):
        with tempfile.TemporaryDirectory() as ir_directory:
            run([emojicodec, '-S', os.path.join(directory, "packages"), os.path.join(directory, name + ".emojic"),
                 '--emit-llvm', '-o', os.path.join(ir_directory, name)], check=True)
            ir = open(os.path.join(ir_directory, name + ".ll"), "r", encoding='utf-8').read()
        if os.path.exists(exp_path):
            check_specializations(name, ir, exp_path)
        if os.path.exists(check_path):
            check_ir(name, ir, check_path)
    completed = run([os.path.join(directory, name)], stdout=PIPE)
    output = completed.stdout.decode('utf-8', 'backslashreplace')
    if output != open(os.path.join(directory, name + ".txt"), "r", encoding='utf-8').read() or \
            completed.returncode != 0:
        log(output)
        fail_test(name)


def reject_test(filename):
    completed = run([emojicodec, '-S', test_packages, filename], stderr=PIPE)
    output = completed.stderr.decode('utf-8', 'backslashreplace')
    # NAME.txt holds text that the error must contain (see discover_reject_tests).
    expected = open(os.path.splitext(filename)[0] + ".txt", encoding='utf-8').read().strip()
    if completed.returncode != 1 or len(re.findall(r"🚨 error:", output)) != 1 or not expected or \
            expected not in output:
        log(output)
        fail_test(filename)
    check_warnings(filename, output, os.path.splitext(filename)[0] + ".warnings")


def command_line_test(_):
    """Usage errors and unwritable outputs make the compiler fail with a diagnostic, not succeed or abort in LLVM."""
    source = os.path.join(dist.source, "tests", "compilation", "class.emojic")
    with tempfile.TemporaryDirectory() as directory:
        missing = os.path.join(directory, "missing", "out")
        writable = os.path.join(directory, "out")
        cases = [
            (['--help'], 0, None),
            (['--bogus', source], 1, None),
            ([], 1, None),
            ([source, '-o'], 1, None),
            ([source, '--emit-llvm', '-o', writable, '-S', test_packages], 0, None),
            ([source, '--emit-llvm', '-o', missing, '-S', test_packages], 1, "Could not write"),
            ([source, '-c', '-o', missing, '-S', test_packages], 1, "Could not write"),
            ([source, '-c', '-o', directory, '-S', test_packages], 1, "Could not write"),
        ]
        for arguments, status, message in cases:
            completed = run([emojicodec] + arguments, stdout=PIPE, stderr=PIPE)
            output = (completed.stdout + completed.stderr).decode('utf-8', 'replace')
            if completed.returncode != status or "LLVM ERROR" in output or (message and message not in output):
                log("{0}: exit status {1}\n{2}".format(arguments, completed.returncode, output))
                fail_test("command line " + " ".join(arguments))


def parse_test(filename):
    completed = run([emojicodec, '--parse-only', '-S', test_packages, filename],
                    stderr=PIPE)
    if completed.returncode != 0:
        log(completed.stderr.decode('utf-8', 'backslashreplace'))
        fail_test(filename)


TEXT_TOKENS = ('MultilineComment\t', 'SinglelineComment\t', 'DocumentationComment\t',
               'Package Documentation Token\t', 'String\t', 'BeginInterpolation\t', 'MiddleInterpolation\t',
               'EndInterpolation\t')


def source_text_tokens(path):
    """Returns the comments, documentation and string tokens of the source at path, sorted and without their
    positions. Formatting normalises the code around them (e.g. it writes attributes that are implied and moves
    instance variables and destructors), but it must keep every one of them."""
    completed = run([emojicodec, '--dump-tokens', path], stdout=PIPE, check=True)
    tokens = [line.split('\t', 1)[1] for line in completed.stdout.decode('utf-8').splitlines()]
    return sorted(token for token in tokens if token.startswith(TEXT_TOKENS))


def formatted_test(name, formatted):
    """Formats the sources of the compilation tests in formatted, compiles the test name from them and checks its
    output. Formatting must keep all tokens (including comments and documentation) and formatting the result again
    must not change it. The sources are restored before the program runs."""
    source_path = test_paths(name, 'compilation')[0]
    paths = [test_paths(file, 'compilation')[0] for file in formatted]
    with tempfile.TemporaryDirectory() as directory:
        binary_path = os.path.join(directory, name)
        with source_lock(source_path):
            pristine = {path: open(path, 'rb').read() for path in paths}
            try:
                tokens = {path: source_text_tokens(path) for path in paths}
                run([emojicodec, '--format', paths[0]], check=True)
                once = {path: open(path, 'rb').read() for path in paths}
                for path in paths:
                    if source_text_tokens(path) != tokens[path]:
                        log("Formatting changed the comments, documentation or strings of " + path)
                        fail_test(name + " (formatted)")
                run([emojicodec, source_path, '-O', '-o', binary_path], check=True)
                run([emojicodec, '--format', paths[0]], check=True)
                for path in paths:
                    if open(path, 'rb').read() != once[path]:
                        log("Formatting " + path + " a second time changed it")
                        fail_test(name + " (formatted)")
            finally:
                for path in paths:
                    open(path, 'wb').write(pristine[path])
                    if os.path.exists(path + '_original'):
                        os.remove(path + '_original')
        check_output(name, binary_path)


def format_test(filename):
    """Formats a copy of filename and compares the result with the .formatted file next to it. The comments,
    documentation and strings must survive and formatting the result again must not change it."""
    expected = open(os.path.splitext(filename)[0] + ".formatted", encoding='utf-8').read()
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, os.path.basename(filename))
        shutil.copyfile(filename, path)
        run([emojicodec, '-S', test_packages, '--format', path], check=True)
        formatted = open(path, encoding='utf-8').read()
        if formatted != expected:
            log("Formatted source differs from the expected one:\n" + formatted)
            fail_test(filename)
        if source_text_tokens(path) != source_text_tokens(filename):
            log("Formatting changed the comments, documentation or strings of " + filename)
            fail_test(filename)
        run([emojicodec, '-S', test_packages, '--format', path], check=True)
        if open(path, encoding='utf-8').read() != formatted:
            log("Formatting the formatted source changed it")
            fail_test(filename)


def prettyprint_test(name):
    formatted_test(name, [name] + formatted_includes.get(name, []))


def fail_unreported(name):
    """Fails a test that raised, unless it already reported its failure before raising."""
    if not report.failed:
        fail_test(name)


def perform(name, function, *args):
    """Runs a test and returns its report. A command that fails, or any other error, fails the test."""
    report.lines = []
    report.stderr = []
    report.failed = False
    try:
        function(*args)
    except CalledProcessError as error:
        log("Command failed with exit code {0}: {1}".format(error.returncode, " ".join(map(str, error.cmd))))
        fail_unreported(name)
    except TimeoutExpired as error:
        log("Command timed out after {0} s: {1}".format(error.timeout, " ".join(map(str, error.cmd))))
        fail_unreported(name)
    except Exception:
        # E.g. a missing expected output file, which must not abort the other tests.
        log(traceback.format_exc())
        fail_unreported(name)
    return report.lines, report.stderr, report.failed


def print_report(lines, stderr, failed):
    # Warnings of the compiler are only of interest if the test failed.
    if failed:
        for text in stderr:
            print(text, end='' if text.endswith('\n') else '\n')
    for line in lines:
        print(line)


def run_all(tasks):
    """Runs the tests in tasks, each a tuple of a name, a test function and its arguments, at once, and prints the
    report of each as it finishes."""
    with ThreadPoolExecutor(max_workers=jobs) as executor:
        futures = [executor.submit(perform, *task) for task in tasks]
        for future in as_completed(futures):
            print_report(*future.result())


# Tests that take seconds to run, which start first so that they do not end up running alone at the end.
slow_tests = set(stress_tests) | {name for name, tokens in library_test_directives.items() if "slow" in tokens}


def test():
    # Tests write only to their own directories or files. Formatting rewrites a source, which is locked meanwhile (see
    # source_lock()), so all tests can run at once.
    tasks = [(test, compilation_test, test) for test in compilation_tests]
    if not quick:
        tasks += [(test + " (formatted)", prettyprint_test, test) for test in compilation_tests]
        tasks += [("includer (included formatted)", formatted_test, 'includer', ['included'])]
    tasks += [(test + " (unoptimized)", compilation_test, test, False) for test in unoptimized_tests]
    tasks += [(test + " (leak check, unoptimized)", compilation_test, test, False) for test in leak_check_tests]
    tasks += [(test, library_test, test) for test in library_tests]
    tasks += [(test, host_test, test) for test in host_tests]
    tasks += [(test, importing_test, test) for test in importing_tests]
    tasks += [(test + " (specializations)", specialization_test, test) for test in specialization_tests]
    tasks += [(test + " (IR)", ir_test, test) for test in ir_tests]
    tasks += [(test + " (package IR)", package_ir_test, test) for test in package_ir_tests]
    tasks += [(test, reject_test, test) for test in reject_tests]
    tasks += [(test, parse_test, test) for test in parse_tests]
    tasks += [(test, format_test, test) for test in format_tests]
    tasks += [("command line", command_line_test, None)]
    tasks.sort(key=lambda task: task[2] not in slow_tests)  # A stable sort, which keeps the order otherwise.
    run_all(tasks)

    if len(failed_tests) == 0:
        print("✅ ✅  All tests passed.")
        sys.exit(0)
    else:
        print("🛑 🛑  {0} tests failed: {1}".format(len(failed_tests),
                                                  ", ".join(failed_tests)))
        sys.exit(1)


def valgrind_test(name):
    source_path = test_paths(name, 'compilation')[0]
    with tempfile.TemporaryDirectory() as directory:
        binary_path = os.path.join(directory, name)
        run([emojicodec, source_path, '-O', '-o', binary_path], check=True)
        completed = run(['valgrind', '--error-exitcode=22', '--leak-check=full', binary_path], stdout=PIPE,
                        stderr=PIPE)
    if completed.returncode == 22:
        log(completed.stdout.decode('utf-8', 'backslashreplace'))
        log(completed.stderr.decode('utf-8', 'backslashreplace'))
        fail_test(name)


def run_valgrind():
    run_all([(test, valgrind_test, test) for test in compilation_tests])
    if failed_tests:
        sys.exit(1)

if __name__ == "__main__":
    if valgrind:
        run_valgrind()
    else:
        test()

from concurrent.futures import ThreadPoolExecutor, as_completed
from subprocess import PIPE, CalledProcessError
import glob
import os
import dist
import subprocess
import sys
import re
import tempfile
import threading

quick = len(sys.argv) > 1 and sys.argv[1] == 'quick'
valgrind = len(sys.argv) > 1 and sys.argv[1] == 'valgrind'

compilation_tests = [
    "hello",
    "endOfFile",
    "print",
    "intTest",
    "if",
    "vars",
    "repeatedPackageImport",
    "enum",
    "enumMethod",
    "enumTypeMethod",
    "unwrap",
    "imperativeInterrogative",
    "assignmentMethod",
    "assignmentByCall",
    "repeatWhile",
    "conditionalProduce",
    "stringConcat",
    "babyBottleInitializer",
    "classInheritance",
    "classOverride",
    "classSuper",
    "classSubInstanceVar",
    "overload",
    "optionalParameter",
    "returnInBlock",
    "returnInIf",
    "identityOperator",
    "typesAsValues",
    "class",
    "ivarAssign",
    "useAndAssign",
    "privateClassMethod",
    "assignmentByCallInstanceVariable",
    "valueType",
    "valueTypeSelf",
    "valueTypeMutate",
    "compareNoValue",
    "downcastClass",
    "castAny",
    "somethingParameters",
    "castGenericValueType",
    "castBindingRemote",
    "castGenericClass",
    "protocolClass",
    "protocolSubclass",
    "protocolValueType",
    "genericRecursion",
    "optionalGenericField",
    "remoteBoxRelease",
    "boxValueSemantics",
    "borrowedBoxes",
    "forInVariableReuse",
    "valueTypeIterator",
    "listIterator",
    "specialization",
    "typeSpecialization",
    "specializationFallback",
    "specializationScoping",
    "specializationMangling",
    "selfConstraint",
    "numericMatrix",
    "directCalls",
    "protocolValueTypeRemote",
    "protocolEnum",
    "protocolGenericLayerClass",
    "protocolGenericLayerValueType",
    "protocolMulti",
    "reboxToSomething",
    "castOwnership",
    "assignmentByCallProtocol",
    "commonType",
    "generics",
    "genericsValueType",
    "genericProtocol",
    "genericProtocolValueType",
    "genericTypeMethod",
    "genericLocalAsArgToGeneric",
    "genericToConstraintOptional",
    "genericsInferenceValueType",
    "genericsInferenceClass",
    "variableInitAndScoping",
    "varInitPath",
    "valueTypeRemoteAdditional",
    "closureBasic",
    "closureCapture",
    "closureCaptureThis",
    "closureCaptureValueType",
    "closureCaptureThisClass",
    "closureCaptureNonEscaping",
    "closureGenerics",
    "closureError",
    "callableBoxing",
    "errorUnwrap",
    "errorAvocado",
    "errorInitializer",
    "errorReraiseMem",
    "errorReraiseMem2",
    "errorHandlerDiscardMem",
    "valueTypeCopySelf",
    "valueTypeBoxCopySelf",
    "includer",
    "threads",
    "linkHints",
    "linkHintFlag",
    "linkHintSource",
    "ffiScalars",
    "ffiPointers",
    "ffiStructByValue",
    "ffiStructPointer",
    "ffiCallbacks",
    "unsafeBlockReturnRelease",
    "threadUnjoined",
    "mutexTryLock",
    "inferLiteralFromExpec",
    "sequenceTypeNames",
    "typeValues",
    "deinitializer",
    "rcOrder",
    "rcOrderVt",
    "rcTempOrder",
    "rcInstanceVariable",
    "rcOnlyReference",
    "rcIvarArgMut",
    "rcEscaping",
    "classEscapingParamOverride",
    "references",
    "identifierTest",
    "shortCircuit",
    "errorReraisePrefix",
    "weak",
    "superMemoryFlow",
    "interpolationDereference",
    "interpolationRelease",
    "genericDynDisableLiteralConstraint"
]

if not (quick or valgrind):
    compilation_tests.extend([
      "stressTest1",
      "stressTest2",
      "stressTest3",
      "stressTest4"
    ])

library_tests = [
    "primitives",
    "mathTest",
    "rangeTest",
    "stringTest",
    "dataTest",
    "systemTest",
    "listTest",
    "enumerator",
    "dictionaryTest",
    "jsonTest",
    "fileTest"
]
# Compilation tests that are also compiled and run without optimizations, which inline code that tests otherwise
# only test inlined.
unoptimized_tests = [
    "valueTypeIterator",
    "listIterator",
    "specialization",
    "typeSpecialization",
    "specializationFallback",
    "specializationScoping",
    "remoteBoxRelease",
    "boxValueSemantics",
    "borrowedBoxes",
]
# Compilation tests whose specializations, functions whose symbol contains $s<, are compared with the names in
# NAME.specializations. A function that is not specialized, but called generically, does not change what a program
# prints.
specialization_tests = [
    "specialization",
    "typeSpecialization",
    "specializationScoping",
    "specializationMangling",
    "genericRecursion",
    "selfConstraint",
    "numericMatrix",
]
# Programs whose unoptimized LLVM IR is checked against NAME.ir. In NAME.ir, a line "@ REGEX" selects the functions
# whose names match, and the lines "+ REGEX" and "- REGEX" after it must and must not match their bodies. Lines
# starting with # are comments.
ir_tests = [
    "directCalls",
]
# Emojicode packages whose C functions (🎍🌊) are called by a C program of the same name, which also provides main.
host_tests = [
    "ffiHostLib",
]
# Programs that import a package of the same name with "Package" appended, which is compiled first. They test code
# that is only generated in importers, like the bodies of inlined methods.
importing_tests = [
    "inlineClosure",
]
reject_tests = glob.glob(os.path.join(dist.source, "tests", "reject",
                                      "*.emojic"))
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


def run(args, check=False, **kwargs):
    """Runs a command like subprocess.run. Its standard error is kept for the report of a test that fails, unless
    the caller handles it."""
    keep_stderr = 'stderr' not in kwargs
    if keep_stderr:
        kwargs['stderr'] = PIPE
    completed = subprocess.run(args, **kwargs)
    if keep_stderr and completed.stderr:
        report.stderr.append(completed.stderr.decode('utf-8', 'replace'))
    if check and completed.returncode != 0:
        raise CalledProcessError(completed.returncode, args)
    return completed


def test_paths(name, kind):
    return (os.path.join(dist.source, "tests", kind, name + ".emojic"),
            os.path.join(dist.source, "tests", kind, name))


def library_test(name):
    source_path = test_paths(name, 's')[0]
    with tempfile.TemporaryDirectory() as directory:
        binary_path = os.path.join(directory, name)
        run([emojicodec, source_path, '-O', '-o', binary_path], check=True)
        # The tests read files relative to their directory.
        completed = run([binary_path], stdout=PIPE, cwd=os.path.join(dist.source, "tests", "s"))
    if completed.returncode != 0:
        fail_test(name)
        log(completed.stdout.decode('utf-8'))


def check_output(name, binary_path):
    """Runs the program of the compilation test name and checks its output."""
    completed = run([binary_path], stdout=PIPE)
    exp_path = os.path.join(dist.source, "tests", "compilation", name + ".txt")
    output = completed.stdout.decode('utf-8')
    if output != open(exp_path, "r", encoding='utf-8').read() or completed.returncode != 0:
        log(output)
        fail_test(name)


def compilation_test(name, optimize=True):
    source_path = test_paths(name, 'compilation')[0]
    # Each compilation has its own directory, as a test can be compiled several times at once.
    with tempfile.TemporaryDirectory() as directory:
        binary_path = os.path.join(directory, name)
        with source_lock(source_path):
            run([emojicodec, source_path, '-o', binary_path] + (['-O'] if optimize else []), check=True)
        check_output(name, binary_path)


def specialization_test(name):
    source_path = test_paths(name, 'compilation')[0]
    with tempfile.TemporaryDirectory() as directory:
        with source_lock(source_path):
            run([emojicodec, source_path, '--emit-llvm', '-o', os.path.join(directory, name)], check=True)
        ir = open(os.path.join(directory, name + ".ll"), "r", encoding='utf-8').read()
    specializations = sorted(set(re.findall(r'^define internal [^@]*@"([^"]*\$s<[^"]*)"', ir, re.MULTILINE)))
    exp_path = os.path.join(dist.source, "tests", "compilation", name + ".specializations")
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
    functions = {m.group(1): m.group(2) for m in
                 re.finditer(r'^define [^\n]*@"?([^"(\s]+)"?\([^\n]*\{\n(.*?)^\}', ir, re.S | re.M)}
    bodies = []
    failed = False
    check_path = os.path.join(dist.source, "tests", "compilation", name + ".ir")
    for line in open(check_path, "r", encoding='utf-8').read().splitlines():
        if not line or line.startswith('#'):
            continue
        kind, pattern = line[0], line[2:]
        if kind == '@':
            bodies = [(n, b) for n, b in functions.items() if re.search(pattern, n)]
            if not bodies:
                log("No function matches " + pattern)
                failed = True
            continue
        for function_name, body in bodies:
            if (re.search(pattern, body) is not None) != (kind == '+'):
                log("{0}: {1} {2}".format(function_name, "missing" if kind == '+' else "unexpected", pattern))
                failed = True
    if failed:
        fail_test(name + " (IR)")


def host_test(name):
    directory = os.path.join(dist.source, "tests", "host")
    source_path = os.path.join(directory, name + ".emojic")
    object_path = os.path.join(directory, name + ".o")
    host_object_path = os.path.join(directory, name + "_host.o")
    binary_path = os.path.join(directory, name)
    run([emojicodec, '-p', name, '-o', object_path, '-c', source_path, '-O'], check=True)
    run([os.environ.get("CC", "cc"), '-c', os.path.join(directory, name + ".c"), '-o', host_object_path],
        check=True)
    libraries = [os.path.abspath(path) for path in ["c/libc.a", "s/libs.a", "runtime/libruntime.a"]]
    run([os.environ.get("CXX", "c++"), host_object_path, object_path] + libraries +
        ['-lm', '-lpthread', '-o', binary_path], check=True)
    completed = run([binary_path], stdout=PIPE)
    output = completed.stdout.decode('utf-8')
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
    completed = run([os.path.join(directory, name)], stdout=PIPE)
    output = completed.stdout.decode('utf-8')
    if output != open(os.path.join(directory, name + ".txt"), "r", encoding='utf-8').read() or \
            completed.returncode != 0:
        log(output)
        fail_test(name)


def reject_test(filename):
    completed = run([emojicodec, filename], stderr=PIPE)
    output = completed.stderr.decode('utf-8')
    if completed.returncode != 1 or len(re.findall(r"🚨 error:", output)) != 1:
        log(output)
        fail_test(filename)


def parse_test(filename):
    completed = run([emojicodec, '--parse-only', '-S', test_packages, filename],
                    stderr=PIPE)
    if completed.returncode != 0:
        log(completed.stderr.decode('utf-8'))
        fail_test(filename)


def available_compilation_tests():
    paths = glob.glob(os.path.join(dist.source, "tests", "compilation",
                                   "*.emojic"))

    map_it = map(lambda f: os.path.splitext(os.path.basename(f))[0], paths)
    tests = list(map_it)
    tests.remove('included')
    return tests


avl_compilation_tests = available_compilation_tests()


# Formatting a file also formats the files it includes, which only it includes, so they are covered by its lock.
formatted_includes = {
    "includer": ["included"],
}


def formatted_test(name, formatted):
    """Formats the sources of the compilation tests in formatted, compiles the test name from them and checks its
    output. The sources are restored before the program runs."""
    source_path = test_paths(name, 'compilation')[0]
    paths = [test_paths(file, 'compilation')[0] for file in formatted]
    with tempfile.TemporaryDirectory() as directory:
        binary_path = os.path.join(directory, name)
        with source_lock(source_path):
            try:
                run([emojicodec, '--format', paths[0]], check=True)
                run([emojicodec, source_path, '-O', '-o', binary_path], check=True)
            finally:
                for path in paths:
                    if os.path.exists(path + '_original'):
                        os.replace(path + '_original', path)
        check_output(name, binary_path)


def prettyprint_test(name):
    formatted_test(name, [name] + formatted_includes.get(name, []))


def perform(name, function, *args):
    """Runs a test and returns its report. A command that fails fails the test."""
    report.lines = []
    report.stderr = []
    report.failed = False
    try:
        function(*args)
    except CalledProcessError as error:
        log("Command failed with exit code {0}: {1}".format(error.returncode, " ".join(map(str, error.cmd))))
        fail_test(name)
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
slow_tests = ["stressTest2", "stressTest3", "jsonTest", "stressTest1", "stressTest4"]


def test():
    for test in compilation_tests:
        avl_compilation_tests.remove(test)

    # Tests write only to their own directories or files. Formatting rewrites a source, which is locked meanwhile (see
    # source_lock()), so all tests can run at once.
    tasks = [(test, compilation_test, test) for test in compilation_tests]
    if not quick:
        tasks += [(test + " (formatted)", prettyprint_test, test) for test in compilation_tests]
        tasks += [("includer (included formatted)", formatted_test, 'includer', ['included'])]
    tasks += [(test + " (unoptimized)", compilation_test, test, False) for test in unoptimized_tests]
    tasks += [(test, library_test, test) for test in library_tests]
    tasks += [(test, host_test, test) for test in host_tests]
    tasks += [(test, importing_test, test) for test in importing_tests]
    tasks += [(test + " (specializations)", specialization_test, test) for test in specialization_tests]
    tasks += [(test + " (IR)", ir_test, test) for test in ir_tests]
    tasks += [(test, reject_test, test) for test in reject_tests]
    tasks += [(test, parse_test, test) for test in parse_tests]
    tasks.sort(key=lambda task: task[2] not in slow_tests)  # A stable sort, which keeps the order otherwise.
    run_all(tasks)

    for file in avl_compilation_tests:
        print("☢️  {0} is not in compilation test list.".format(file))

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
        log(completed.stdout.decode('utf-8'))
        log(completed.stderr.decode('utf-8'))
        fail_test(name)


def run_valgrind():
    run_all([(test, valgrind_test, test) for test in compilation_tests])
    if failed_tests:
        sys.exit(1)

if valgrind:
    run_valgrind()
else:
    test()

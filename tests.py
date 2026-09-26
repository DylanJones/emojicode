from subprocess import *
import glob
import os
import dist
import sys
import re
import tempfile

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
    "forInVariableReuse",
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
    "valueTypeIterator",
    "listIterator",
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
    "genericRecursion",
    "optionalGenericField",
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
    "remoteBoxRelease",
    "boxValueSemantics",
    "borrowedBoxes",
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

emojicodec = os.path.abspath("Compiler/emojicodec")
os.environ["EMOJICODE_PACKAGES_PATH"] = os.path.abspath(".")


def fail_test(name):
    global failed_tests
    print("🛑 {0} failed".format(name))
    failed_tests.append(name)


def test_paths(name, kind):
    return (os.path.join(dist.source, "tests", kind, name + ".emojic"),
            os.path.join(dist.source, "tests", kind, name))


def library_test(name):
    source_path, binary_path = test_paths(name, 's')

    run([emojicodec, source_path, '-O'], check=True)
    completed = run([binary_path], stdout=PIPE)
    if completed.returncode != 0:
        fail_test(name)
        print(completed.stdout.decode('utf-8'))


def compilation_test(name):
    source_path, binary_path = test_paths(name, 'compilation')
    run([emojicodec, source_path, '-O'], check=True)
    completed = run([binary_path], stdout=PIPE)
    exp_path = os.path.join(dist.source, "tests", "compilation", name + ".txt")
    output = completed.stdout.decode('utf-8')
    if output != open(exp_path, "r", encoding='utf-8').read() or completed.returncode != 0:
        print(output)
        fail_test(name)


def ir_test(name):
    source_path = test_paths(name, 'compilation')[0]
    with tempfile.TemporaryDirectory() as directory:
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
                print("No function matches " + pattern)
                failed = True
            continue
        for function_name, body in bodies:
            if (re.search(pattern, body) is not None) != (kind == '+'):
                print("{0}: {1} {2}".format(function_name, "missing" if kind == '+' else "unexpected", pattern))
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
        print(output)
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
        print(output)
        fail_test(name)


def reject_test(filename):
    completed = run([emojicodec, filename], stderr=PIPE)
    output = completed.stderr.decode('utf-8')
    if completed.returncode != 1 or len(re.findall(r"🚨 error:", output)) != 1:
        print(output)
        fail_test(filename)


def parse_test(filename):
    completed = run([emojicodec, '--parse-only', '-S', test_packages, filename],
                    stderr=PIPE)
    if completed.returncode != 0:
        print(completed.stderr.decode('utf-8'))
        fail_test(filename)


def available_compilation_tests():
    paths = glob.glob(os.path.join(dist.source, "tests", "compilation",
                                   "*.emojic"))

    map_it = map(lambda f: os.path.splitext(os.path.basename(f))[0], paths)
    tests = list(map_it)
    tests.remove('included')
    return tests


avl_compilation_tests = available_compilation_tests()


def prettyprint_test(name):
    source_path = test_paths(name, 'compilation')[0]
    run([emojicodec, '--format', source_path], check=True)
    try:
        compilation_test(name)
    except CalledProcessError:
        fail_test(name)
    os.rename(source_path + '_original', source_path)


def test():
    for test in compilation_tests:
        avl_compilation_tests.remove(test)
        compilation_test(test)

    if not quick:
        for test in compilation_tests:
            prettyprint_test(test)

        included = os.path.join(dist.source, "tests", "compilation", "included.emojic")
        os.rename(included + '_original', included)

        source_path = test_paths('included', 'compilation')[0]
        run([emojicodec, '--format', source_path], check=True)
        compilation_test('includer')
        os.rename(source_path + '_original', source_path)

    for test in ir_tests:
        ir_test(test)

    for test in host_tests:
        host_test(test)
    for test in importing_tests:
        importing_test(test)
    for test in reject_tests:
        reject_test(test)
    for test in parse_tests:
        parse_test(test)
    os.chdir(os.path.join(dist.source, "tests", "s"))
    os.environ["TEST_ENV_1"] = "The day starts like the rest I've seen"
    for test in library_tests:
        library_test(test)

    for file in avl_compilation_tests:
        print("☢️  {0} is not in compilation test list.".format(file))

    if len(failed_tests) == 0:
        print("✅ ✅  All tests passed.")
        sys.exit(0)
    else:
        print("🛑 🛑  {0} tests failed: {1}".format(len(failed_tests),
                                                  ", ".join(failed_tests)))
        sys.exit(1)


def run_valgrind():
    for test in compilation_tests:
        source_path, binary_path = test_paths(test, 'compilation')
        run([emojicodec, source_path, '-O'], check=True)
        completed = run(['valgrind', '--error-exitcode=22', '--leak-check=full', binary_path], stdout=PIPE, stderr=PIPE)
        if completed.returncode == 22:
            print(completed.stdout.decode('utf-8'))
            print(completed.stderr.decode('utf-8'))
            fail_test(test)

if valgrind:
    run_valgrind()
else:
    test()


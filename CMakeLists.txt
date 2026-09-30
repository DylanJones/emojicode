cmake_minimum_required(VERSION 3.20)
project(Emojicode)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-Ofast")

add_compile_options(-pedantic-errors)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-fcolor-diagnostics)
endif()

if(defaultPackagesDirectory)
  add_definitions(-DdefaultPackagesDirectory="${defaultPackagesDirectory}")
endif()

add_subdirectory(runtime)
add_subdirectory(s)
add_subdirectory(files)
add_subdirectory(Compiler)
add_subdirectory(LanguageServer)
add_subdirectory(sockets)
add_subdirectory(testtube)
add_subdirectory(json)
add_subdirectory(c)

add_custom_target(dist python3 ${PROJECT_SOURCE_DIR}/dist.py)
add_custom_target(tests python3 ${PROJECT_SOURCE_DIR}/tests.py)
# tests.py runs the compiler and the packages in the build directory, so they must be up to date.
add_dependencies(tests emojicodec runtime s files sockets testtube json c)
# Runs the compilation tests under valgrind, which finds over-releases that the leak counter does not. Needs valgrind.
add_custom_target(valgrindtests python3 ${PROJECT_SOURCE_DIR}/tests.py valgrind)
add_dependencies(valgrindtests emojicodec runtime s files sockets testtube json c)
# Regression tests for tests.py's own test discovery and scheduling, run against isolated fixtures.
add_custom_target(testspy python3 ${PROJECT_SOURCE_DIR}/test_tests.py -v)
add_custom_target(emojitablestest python3 ${PROJECT_SOURCE_DIR}/test_generate_emoji_tables.py -v)
add_custom_target(magicinstall python3 ${PROJECT_SOURCE_DIR}/dist.py install)
# The tree-sitter grammar that editors use is checked too if the tree-sitter CLI is installed.
find_program(TREE_SITTER tree-sitter)
if(TREE_SITTER)
  set(GRAMMAR_CHECK_TREE_SITTER --tree-sitter)
endif()
add_custom_target(grammar python3 ${PROJECT_SOURCE_DIR}/tools/grammar_check.py --build ${CMAKE_BINARY_DIR}
                  --mutations 5 --generate 1000 ${GRAMMAR_CHECK_TREE_SITTER})
add_dependencies(grammar emojicodec s files sockets testtube json c)
add_custom_target(lsptests python3 ${PROJECT_SOURCE_DIR}/LanguageServer/tests/lsp_test.py
                  ${CMAKE_BINARY_DIR}/LanguageServer/emojicode-lsp)
add_dependencies(lsptests emojicode-lsp s files sockets testtube json c)
# The corpus of the tree-sitter grammar. (grammar_check.py only checks that it parses what the compiler accepts.)
if(TREE_SITTER)
  add_custom_target(treesittertests COMMAND ${TREE_SITTER} generate COMMAND ${TREE_SITTER} test
                    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/editors/tree-sitter-emojicode)
endif()

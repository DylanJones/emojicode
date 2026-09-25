find_package(LLVM REQUIRED CONFIG)
if (LLVM_VERSION_MAJOR LESS 21)
  message(FATAL_ERROR "LLVM 21 or newer is required, found ${LLVM_PACKAGE_VERSION}")
endif()
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})

# Everything but the command line entry point is built as a library, so that other tools like the language server
# can use the compiler.
file(GLOB_RECURSE EMOJICODEC_SOURCES "*.cpp" "*.hpp" "*.h" "*.hxx")
list(REMOVE_ITEM EMOJICODEC_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CLI/main.cpp)
add_library(emojicodec_core STATIC ${EMOJICODEC_SOURCES})
target_include_directories(emojicodec_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(emojicodec_core SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
target_compile_definitions(emojicodec_core PUBLIC ${LLVM_DEFINITIONS_LIST})
target_compile_options(emojicodec_core PUBLIC -Wall -Wno-unused-result -Wno-missing-braces -pedantic)

llvm_map_components_to_libnames(LLVM_LIBS core codegen passes ${LLVM_TARGETS_TO_BUILD})
target_link_libraries(emojicodec_core PUBLIC z m ${LLVM_LIBS})

add_executable(emojicodec CLI/main.cpp)
target_include_directories(emojicodec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/CLI)
target_link_libraries(emojicodec PRIVATE emojicodec_core)

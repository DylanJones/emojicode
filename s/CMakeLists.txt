file(GLOB S_SOURCES "*.cpp" "*.c" "*.hpp" "*.h")
file(GLOB S_EXCLUDED "utf8proc_data.c")

list(REMOVE_ITEM S_SOURCES ${S_EXCLUDED})

get_filename_component(MAIN_FILE s.🍇 ABSOLUTE)
file(GLOB EMOJIC_DEPEND "*.🍇")

add_library(s STATIC ${S_SOURCES} s.o)
set_property(TARGET s PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_options(s PUBLIC -Wall -Wno-unused-result -Wno-missing-braces -pedantic)

# -Ofast (used for Release builds, see CMakeLists.txt) enables fast-math,
# which lets the compiler replace exact integral powers of ten in
# std::pow(10, n) with an inexact exp2(n * log2(10)) approximation. These two
# files convert between Real and String and rely on std::pow(10, n) being
# exact, so they must not be built with fast-math.
set_source_files_properties(Integer.cpp String.cpp PROPERTIES COMPILE_OPTIONS -fno-fast-math)
add_custom_command(OUTPUT s.o COMMAND emojicodec -p s -o s.o --color ${MAIN_FILE} -O -c
        DEPENDS emojicodec ${EMOJIC_DEPEND})

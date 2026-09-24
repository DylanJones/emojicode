file(GLOB EMOJIC_DEPEND "*.🍇")

get_filename_component(MAIN_FILE c.🍇 ABSOLUTE)
set(PACKAGE_FILE c.o)

add_library(c STATIC ${PACKAGE_FILE})
set_property(TARGET c PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET c PROPERTY LINKER_LANGUAGE CXX)
add_custom_command(OUTPUT ${PACKAGE_FILE} COMMAND emojicodec -p c -o ${PACKAGE_FILE} --color
-S ${CMAKE_BINARY_DIR} -c ${MAIN_FILE} -O DEPENDS emojicodec s ${EMOJIC_DEPEND})

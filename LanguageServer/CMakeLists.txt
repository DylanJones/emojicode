file(GLOB LANGUAGE_SERVER_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")
add_executable(emojicode-lsp ${LANGUAGE_SERVER_SOURCES})
target_link_libraries(emojicode-lsp PRIVATE emojicodec_core)

//
//  main.cpp
//  EmojicodeLanguageServer
//
//  The Emojicode language server. It speaks the Language Server Protocol over stdin and stdout.
//

#include "JsonRpc.hpp"
#include "Server.hpp"
#include <filesystem>
#include <iostream>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>
#include <unistd.h>

using namespace EmojicodeLanguageServer;

/// Returns the directory with the packages built next to this executable, if it was built in an Emojicode build
/// directory, so that a development build of the server finds a development build of the standard library.
static std::vector<std::string> buildDirectoryPackages(const char *argv0) {
    auto executable = llvm::sys::fs::getMainExecutable(argv0, reinterpret_cast<void *>(&buildDirectoryPackages));
    auto buildDirectory = std::filesystem::path(executable).parent_path().parent_path();
    std::error_code error;
    if (!executable.empty() && std::filesystem::exists(buildDirectory / "s" / "🏛", error)) {
        return {buildDirectory.string()};
    }
    return {};
}

int main(int argc, char *argv[]) {
    if (argc > 1 && std::string(argv[1]) != "--stdio") {
        std::cerr << "emojicode-lsp is the Emojicode language server. Editors start it and talk to it over stdin and "
                     "stdout using the Language Server Protocol." << std::endl;
        return std::string(argv[1]) == "--help" ? 0 : 64;
    }

    // Messages to the client go to a copy of stdout. stdout itself is redirected to stderr, which clients log, so
    // that anything the compiler prints cannot corrupt the protocol.
    int output = dup(STDOUT_FILENO);
    dup2(STDERR_FILENO, STDOUT_FILENO);

    Transport transport(STDIN_FILENO, output);
    Server server(&transport, buildDirectoryPackages(argv[0]));
    try {
        return server.run();
    }
    catch (std::exception &e) {
        std::cerr << "💣 The Emojicode language server crashed: " << e.what() << std::endl;
        return 70;
    }
}

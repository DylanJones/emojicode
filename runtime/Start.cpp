//
//  Start.cpp
//  EmojicodeRuntime
//
//  The entry point of Emojicode programs. It is kept in its own file, and therefore in its own member of the
//  runtime library, so that C and C++ programs that call into Emojicode can link the runtime and use their own
//  main function. Such programs must call ejcInit() before calling any Emojicode function.
//

#include "Runtime.h"

extern "C" runtime::Integer fn_1f3c1();

int main(int argc, char **argv) {
    runtime::ejcInit(argc, argv);
    return static_cast<int>(fn_1f3c1());
}

//
//  CTrampolineGenerator.hpp
//  EmojicodeCompiler
//

#ifndef CTrampolineGenerator_hpp
#define CTrampolineGenerator_hpp

#include <string>

namespace EmojicodeCompiler {

class Function;
class Package;
class Type;

/// Whether @p type is a C struct (🎍🌊 🕊) passed by value.
bool isCStructValue(const Type &type);

/// Whether calls to the 🎍🌊 function @p function go through a trampoline.
///
/// LLVM leaves it to the frontend to lower C structs passed by value to the target's calling convention. Instead of
/// implementing that for every target, a function that takes or returns a C struct by value is called through a
/// small C function, the trampoline, which takes the structs by pointer. The C compiler builds the trampolines, so
/// the structs are passed exactly as C passes them.
bool needsCTrampoline(Function *function);

/// The name of the trampoline for @p function. The Emojicode side calls this function with every struct passed by
/// pointer and, if the function returns a struct, a pointer to memory for the result as last argument.
std::string cTrampolineName(Function *function);

/// Returns the C source of the trampolines for all 🎍🌊 functions declared in @p package that need one, or an empty
/// string if no function needs one.
std::string generateCTrampolines(Package *package);

}  // namespace EmojicodeCompiler

#endif /* CTrampolineGenerator_hpp */

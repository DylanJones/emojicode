//
//  ValueWitnessBuilder.hpp
//  EmojicodeCompiler
//

#ifndef ValueWitnessBuilder_hpp
#define ValueWitnessBuilder_hpp

#include <llvm/IR/Constant.h>
#include <map>
#include <string>

namespace EmojicodeCompiler {

class CodeGenerator;
class Type;

/// Builds the value witnesses of types, which generic code uses to operate on values of a type it does not know in
/// memory, like the elements of a 🍨.
///
/// Memory written with 🐽🐚T🍆 holds each value of the type C that T stands for as a value of C (e.g. an i64 for
/// 🔢), in generic code as well as in specializations for C, so that both can operate on the same memory. Generic code
/// holds a value of T in a box, and finds the witness of C in the type description of T. A witness contains:
///  - the size of a value of C in memory,
///  - a function copying a value from memory into a box, which it retains,
///  - a function copying the value in a box into memory, which it retains,
///  - a function releasing a value in memory.
/// The boxes are those of a generic parameter constrained to ⚪, i.e. their first field is the box info of the value's
/// type (not a protocol conformance), or null if they contain no value.
class ValueWitnessBuilder {
public:
    explicit ValueWitnessBuilder(CodeGenerator *generator) : generator_(generator) {}

    /// Returns the witness for values of @p type, which must be concrete, as they are stored in memory. The witness is
    /// created in the module if necessary.
    llvm::Constant* witnessFor(const Type &type);

private:
    CodeGenerator *generator_;
    std::map<std::string, llvm::Constant*> witnesses_;

    llvm::Function* buildLoad(const Type &type, const std::string &name);
    llvm::Function* buildStore(const Type &type, const std::string &name);
    llvm::Function* buildRelease(const Type &type, const std::string &name);
};

}  // namespace EmojicodeCompiler

#endif /* ValueWitnessBuilder_hpp */

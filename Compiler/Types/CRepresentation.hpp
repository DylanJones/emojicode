//
//  CRepresentation.hpp
//  EmojicodeCompiler
//

#ifndef CRepresentation_hpp
#define CRepresentation_hpp

#include <optional>
#include <string>

namespace llvm {
class LLVMContext;
class Type;
}  // namespace llvm

namespace EmojicodeCompiler {

/// Describes how values of a primitive value type are represented in C. Primitive value types declared with
/// `📻 🔤C type🔤 🕊` have one, as do the s primitives 🔢, 💯, 💧 and 👌. Only types with a C representation (and
/// C structs, pointers and C callables) may appear in the signature of a 🎍🌊 function.
struct CRepresentation {
    enum class Kind { Integer, Float, Pointer };

    CRepresentation(Kind kind, unsigned bits, bool isSigned) : kind(kind), bits(bits), isSigned(isSigned) {}

    Kind kind;
    /// The size of the type in bits. For booleans, which are stored as i1, this is 1.
    unsigned bits;
    /// Whether an integer type is signed. Determines sign or zero extension.
    bool isSigned;

    bool isInteger() const { return kind == Kind::Integer; }
    bool isFloat() const { return kind == Kind::Float; }
    bool isPointer() const { return kind == Kind::Pointer; }

    llvm::Type* llvmType(llvm::LLVMContext &context) const;

    /// Returns the representation of the C type @p name, e.g. "int", "unsigned long" or "void *", as the host C
    /// compiler would represent it. Returns an empty optional if the name is not known.
    static std::optional<CRepresentation> forName(const std::string &name);
};

}  // namespace EmojicodeCompiler

#endif /* CRepresentation_hpp */

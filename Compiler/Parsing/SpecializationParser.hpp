//
//  SpecializationParser.hpp
//  EmojicodeCompiler
//

#ifndef SpecializationParser_hpp
#define SpecializationParser_hpp

#include "AbstractParser.hpp"

namespace EmojicodeCompiler {

class Function;

/// Parses the declaration and body of a generic method again, so that its specialization gets its own AST.
///
/// Analysis modifies the AST of a function, so a specialization cannot share it with the generic function. The
/// specialization binds the names of the generic parameters to concrete types (see Generic::bindVariable()), so the
/// same source is analysed as if the concrete types had been written in place of the generic parameters.
class SpecializationParser : AbstractParser {
public:
    /// Parses the source of @p generic, which must have been declared in a file whose content is still available.
    static void parse(Function *generic, Function *specialization);

private:
    SpecializationParser(Package *pkg, TokenStream &stream) : AbstractParser(pkg, stream) {}

    /// Consumes the tokens up to and including the name of the method declared at @p position.
    void skipToName(const SourcePosition &position);
    void parseFunction(Function *specialization);
};

}  // namespace EmojicodeCompiler

#endif /* SpecializationParser_hpp */

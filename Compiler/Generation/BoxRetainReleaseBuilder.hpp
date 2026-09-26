//
//  BoxRetainReleaseBuilder.hpp
//  runtime
//
//  Created by Theo Weidmann on 30.03.19.
//

#ifndef BoxRetainReleaseBuilder_hpp
#define BoxRetainReleaseBuilder_hpp

#include <llvm/IR/Module.h>

namespace EmojicodeCompiler {

class CodeGenerator;
class Type;
class ValueType;
class TypeDefinition;

std::pair<llvm::Function*, llvm::Function*> buildBoxRetainRelease(CodeGenerator *cg, const Type &type);
/// Builds a function that takes a pointer to a box of @p type and ensures that the box is the only one storing its
/// value, so that a mutation of the value does not change other copies. Returns nullptr unless @p type is stored
/// remotely, i.e. in an object on the heap that copies of a box share.
llvm::Function* buildBoxMakeUnique(CodeGenerator *cg, const Type &type);
void buildCopyRetain(CodeGenerator *cg, ValueType *typeDef);
void buildDestructor(CodeGenerator *cg, TypeDefinition *typeDef);
llvm::Function* createMemoryFunction(const std::string &str, CodeGenerator *cg, TypeDefinition *typeDef);

}

#endif /* BoxRetainReleaseBuilder_hpp */

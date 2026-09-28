//
//  ASTTypeAsValue.cpp
//  runtime
//
//  Created by Theo Weidmann on 07.03.19.
//

#include "Analysis/ExpressionAnalyser.hpp"
#include "ASTTypeAsValue.hpp"
#include "ASTType.hpp"
#include "Types/Class.hpp"
#include "Generation/FunctionCodeGenerator.hpp"
#include "Generation/TypeDescriptionGenerator.hpp"

namespace EmojicodeCompiler {

Type ASTTypeAsValue::analyse(ExpressionAnalyser *analyser) {
    auto &type = type_->analyseType(analyser->typeContext());
    ASTTypeValueType::checkTypeValue(tokenType_, type, analyser->typeContext(), position(), analyser->package());
    if (type.type() == TypeType::Class && type.klass()->storesGenericArgs()) {
        analyser->usesGenericArgumentsOf(type);  // generate() describes the class at run time.
    }
    return Type(MakeTypeAsValue, type);
}

Value* ASTTypeAsValue::generate(FunctionCodeGenerator *fg) const {
    if (type_->type().type() == TypeType::Class) {
        // A class type value describes the class with its generic arguments, as a subclass that overrides a type
        // method may need more of them than the type through which the method is called has.
        auto type = type_->type();
        if (!type.klass()->storesGenericArgs()) {
            // Nothing reads the generic arguments of a class that doesn't store them (🎍🛢), whose own code cannot
            // describe them at run time.
            for (size_t i = 0; i < type.genericArguments().size(); i++) {
                type.setGenericArgument(i, Type::something());
            }
        }
        return TypeDescriptionGenerator(fg, TypeDescriptionUser::TypeValue).generate(type);
    }
    return llvm::UndefValue::get(fg->typeHelper().llvmTypeFor(Type(MakeTypeAsValue, type_->type())));
}

ASTTypeAsValue::ASTTypeAsValue(std::unique_ptr<ASTType> type, TokenType tokenType, const SourcePosition &p)
    : ASTExpr(p), type_(std::move(type)), tokenType_(tokenType) {}
ASTTypeAsValue::~ASTTypeAsValue() = default;

}  // namespace EmojicodeCompiler

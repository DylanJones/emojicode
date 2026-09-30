//
// Created by Theo Weidmann on 04.06.18.
//

#include "ASTType.hpp"
#include "Analysis/AnalysisObserver.hpp"
#include "Compiler.hpp"
#include "Functions/Function.hpp"
#include "Lex/Token.hpp"
#include "Package/Package.hpp"
#include "Parsing/AbstractParser.hpp"
#include "Types/Class.hpp"
#include "Types/Type.hpp"
#include "Types/TypeContext.hpp"
#include "Types/TypeDefinition.hpp"

namespace EmojicodeCompiler {

Type& ASTType::analyseType(const TypeContext &typeContext, bool allowReference, bool allowGenericInference,
                           bool allowNoReturn) {
    if (!wasAnalysed()) {
        type_ = getType(typeContext, allowGenericInference).applyMinimalBoxing().optionalized(optional_);
        if (reference_) {
            if (!allowReference) {
                package()->compiler()->error(CompilerError(position(), "Reference not allowed here."));
            }
            if (!type_.isReferenceUseful() && type_.type() != TypeType::GenericVariable &&
                type_.type() != TypeType::LocalGenericVariable) {
                package()->compiler()->warn(position(), "Reference is not useful.");
            }
            type_.setReference();
        }
        if (type_.type() == TypeType::Optional && type_.isReference()) {
            package()->compiler()->error(CompilerError(position(), "Optional references are not supported."));
        }
        auto observer = package()->compiler()->analysisObserver();
        package_ = nullptr;
        if (observer != nullptr) {
            observer->analysedType(this);
        }
    }
    if (!allowNoReturn && type_.unoptionalized().type() == TypeType::NoReturn) {
        throw CompilerError(position(), "◼️ is only allowed as a return or error type.");
    }
    return type_;
}

bool ASTType::isStoredBoxedByGenericCode() const {
    if (dynamic_cast<const ASTGenericVariable *>(this) != nullptr) {
        return optional_;
    }
    return dynamic_cast<const ASTCallableType *>(this) != nullptr && mentionsGenericVariable();
}

bool ASTTypeId::mentionsGenericVariable() const {
    return std::any_of(genericArgs_.begin(), genericArgs_.end(), [](auto &arg) {
        return arg->mentionsGenericVariable();
    });
}

bool ASTCallableType::mentionsGenericVariable() const {
    return (return_ != nullptr && return_->mentionsGenericVariable()) ||
        (errorType_ != nullptr && errorType_->mentionsGenericVariable()) ||
        std::any_of(params_.begin(), params_.end(), [](auto &param) { return param->mentionsGenericVariable(); });
}

Type ASTTypeId::rawType() const {
    return package()->getRawType(TypeIdentifier(name_, namespace_, position()));
}

Type ASTTypeId::getType(const TypeContext &typeContext, bool allowGenericInference) const {
    auto type = rawType();

    auto typeDef = type.typeDefinition();
    if (auto klass = dynamic_cast<Class *>(typeDef); klass != nullptr && typeContext.deferredChecks() != nullptr) {
        // The class may be declared after the class whose super type is being analysed.
        klass->analyseSuperTypeWhenUsed(typeContext.deferredChecks());
    }

    auto args = typeDef->superGenericArguments();
    for (auto &arg : genericArgs_) {
        args.emplace_back(arg->analyseType(typeContext));
    }
    type.setGenericArguments(std::move(args));
    if (allowGenericInference && type.genericArguments().empty()) {
        return type;
    }
    auto check = [type, context = TypeContext(typeContext, nullptr), p = position()] {
        type.typeDefinition()->requestReificationAndCheck(context, TypeContext(type), type.genericArguments(), p);
    };
    if (auto checks = typeContext.deferredChecks()) {
        checks->emplace_back(std::move(check));
    }
    else {
        check();
    }
    return type;
}

Type ASTMultiProtocol::getType(const TypeContext &typeContext, bool allowGenericInference) const {
    std::vector<Type> protocols;
    protocols.reserve(protocols_.size());

    for (auto &protocol : protocols_) {
        auto protocolType = protocol->analyseType(typeContext).unboxed();
        if (protocolType.type() != TypeType::Protocol) {
            package()->compiler()->error(CompilerError(position(),
                                                       "🍱 may only consist of non-optional protocol types."));
            continue;
        }
        protocols.push_back(protocolType.unboxed());
    }

    if (protocols.empty()) {
        throw CompilerError(position(), "An empty 🍱 is invalid.");
    }

    return Type(std::move(protocols));
}

Type ASTTypeValueType::getType(const TypeContext &typeContext, bool allowGenericInference) const {
    auto type = type_->analyseType(typeContext);
    checkTypeValue(tokenType_, type, typeContext, position(), package());
    return Type(MakeTypeAsValue, type);
}

void ASTTypeValueType::checkTypeValue(TokenType tokenType, const Type &type, const TypeContext &typeContext,
                                      const SourcePosition &p, Package *package) {
    if (type.type() == TypeType::Class) {
        if (tokenType != TokenType::Class)
            throw CompilerError(p, "Class type must be prefixed with 🐇: 🐇", type.toString(typeContext, package));
    }
    else if (type.type() == TypeType::Protocol) {
        if (tokenType != TokenType::Protocol)
            throw CompilerError(p, "Protocol type must be prefixed with 🐊: 🐊", type.toString(typeContext, package));
    }
    else if (type.type() == TypeType::ValueType) {
        if (tokenType != TokenType::ValueType)
            throw CompilerError(p, "Value type must be prefixed with 🕊: 🕊", type.toString(typeContext, package));
    }
    else if (type.type() == TypeType::Enum) {
        if (tokenType != TokenType::Enumeration)
            throw CompilerError(p, "Enumeration type must be prefixed with 🔘: 🔘", type.toString(typeContext, package));
    }
    else {
        throw CompilerError(p, "Unexpected type.");
    }
}

std::string ASTTypeValueType::toString(TokenType tokenType) {
    switch (tokenType) {
        case TokenType::Class:
            return "🐇";
        case TokenType::ValueType:
            return "🕊";
        case TokenType::Enumeration:
            return "🔘";
        case TokenType::Protocol:
            return "🐊";
        default:
            throw std::logic_error("TokenType cannot produce Type Value");
    }
}

Type ASTGenericVariable::getType(const TypeContext &typeContext, bool allowGenericInference) const {
    Type type = Type::noReturn();
    // A closure can use the generic parameters of the functions it is written in.
    for (auto function = typeContext.function(); function != nullptr; function = function->enclosingFunction()) {
        if (function->fetchVariable(name_, &type)) {
            return type;
        }
    }

    auto callee = typeContext.calleeType().unboxed();
    if (callee.type() == TypeType::TypeAsValue) {
        callee = callee.typeOfTypeValue();
    }
    if (callee.canHaveGenericArguments() && callee.typeDefinition()->fetchVariable(name_, &type)) {
        return type;
    }

    throw CompilerError(position(), "No such generic type variable \"", utf8(name_), "\".");
}

Type ASTCallableType::getType(const TypeContext &typeContext, bool allowGenericInference) const {
    auto returnType = return_ == nullptr ? Type::noReturn() : return_->analyseType(typeContext, false, false, true);
    auto errorType = errorType_ == nullptr ? Type::noReturn() : errorType_->analyseType(typeContext, false, false, true);
    auto type = Type(returnType, transformTypeAstVector(params_, typeContext), errorType);
    if (c_) {
        type.setCCallable();
        if (!type.isCRepresentable()) {
            throw CompilerError(position(), "A C function pointer (🍇🎍🌊) can only take and return C types, and C "
                                "structs only by 📍, and cannot raise errors.");
        }
    }
    return type;
}

}  // namespace EmojicodeCompiler

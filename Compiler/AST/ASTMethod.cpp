//
//  ASTMethod.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 05/08/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "ASTMethod.hpp"
#include <algorithm>
#include "ASTVariables.hpp"
#include "Analysis/FunctionAnalyser.hpp"
#include "Analysis/SemanticAnalyser.hpp"
#include "Compiler.hpp"
#include "Emojis.h"
#include "Functions/Function.hpp"
#include "MemoryFlowAnalysis/MFFunctionAnalyser.hpp"
#include "Scoping/SemanticScoper.hpp"
#include "Types/Enum.hpp"
#include "Types/Protocol.hpp"
#include "Types/TypeExpectation.hpp"

namespace EmojicodeCompiler {

Type ASTMethodable::analyseMethodCall(ExpressionAnalyser *analyser, const std::u32string &name,
                                      std::shared_ptr<ASTExpr> &callee) {
    return analyseMethodCall(analyser, name, callee, analyser->analyse(callee));

}

Type ASTMethodable::analyseMethodCall(ExpressionAnalyser *analyser, const std::u32string &name,
                                      std::shared_ptr<ASTExpr> &callee, const Type &otype) {
    determineCalleeType(analyser, name, callee, otype);

    if (calleeType_.unboxedType() == TypeType::MultiProtocol) {
        return analyseMultiProtocolCall(analyser, name, callee);
    }
    if (calleeType_.type() == TypeType::TypeAsValue) {
        return analyseTypeMethodCall(analyser, name, callee);
    }

    determineCallType(analyser);

    method_ = calleeType_.typeDefinition()->methods().get(name, args_.mood(), &args_,
                                                          &calleeType_, analyser, position());

    if (calleeType_.type() == TypeType::Class && (method_->accessLevel() == AccessLevel::Private || calleeType_.isExact())) {
        callType_ = CallType::StaticDispatch;
    }

    checkMutation(analyser, callee);
    ensureErrorIsHandled(analyser);
    auto rt = analyser->analyseFunctionCall(&args_, calleeType_, method_, &method_);
    if (!analyser->storesGenericValuesUnboxed(method_->owner()) &&
        (method_->returnType()->type().is<TypeType::GenericVariable>() ||
         method_->returnType()->type().is<TypeType::LocalGenericVariable>() ||
         method_->returnType()->type().unoptionalized().is<TypeType::GenericVariable>() ||
         method_->returnType()->type().unoptionalized().is<TypeType::LocalGenericVariable>())) {  // i.e. not boxed
        castTo_ = rt;
    }
    return rt;
}

const Type& ASTMethodable::errorType() const {
    return method_->errorType()->type();
}

bool ASTMethodable::isErrorProne() const {
    return method_->errorProne();
}

void ASTMethodable::determineCalleeType(ExpressionAnalyser *analyser, const std::u32string &name,
                                        std::shared_ptr<ASTExpr> &callee, const Type &otype) {
    Type type = analyser->semanticAnalyser()->defaultLiteralType(otype.resolveOnSuperArgumentsAndConstraints(analyser->typeContext()));
    if (builtIn(analyser, type, name)) {
        calleeType_ = analyser->comply(TypeExpectation(false, false), &callee);
    }
    else {
        calleeType_ = analyser->comply(TypeExpectation(true, false),
                                       &callee).resolveOnSuperArgumentsAndConstraints(analyser->typeContext());
    }
}

void ASTMethodable::determineCallType(const ExpressionAnalyser *analyser) {
    if (calleeType_.type() == TypeType::ValueType) {
        callType_ = CallType::StaticDispatch;
    }
    else if (calleeType_.unboxedType() == TypeType::Protocol) {
        callType_ = CallType::DynamicProtocolDispatch;
    }
    else if (calleeType_.type() == TypeType::Enum) {
        callType_ = CallType::StaticDispatch;
    }
    else if (calleeType_.type() == TypeType::Class) {
        callType_ = CallType::DynamicDispatch;
    }
    else {
        throw CompilerError(position(), calleeType_.toString(analyser->typeContext()), " does not provide methods.");
    }
}

void ASTMethodable::checkMutation(ExpressionAnalyser *analyser, const std::shared_ptr<ASTExpr> &callee) const {
    // A 🖍 protocol method may be implemented by a 🖍 method of a value type.
    auto mutatesValue = calleeType_.type() == TypeType::ValueType || calleeType_.unboxedType() == TypeType::Protocol ||
        calleeType_.unboxedType() == TypeType::MultiProtocol;
    if (mutatesValue && method_->mutating()) {
        try {
            callee->mutateReference(analyser);
            if (!calleeType_.isMutable()) {
                analyser->compiler()->error(CompilerError(position(), utf8(method_->name()),
                                                          " was marked 🖍 but callee is not mutable."));
            }
        }
        catch (CompilerError &err) {
            analyser->compiler()->error(err);
        }
    }
}

void ASTMethod::mutateReference(ExpressionAnalyser *analyser) {
    callee_->mutateReference(analyser);
}

Type ASTMethodable::analyseTypeMethodCall(ExpressionAnalyser *analyser, const std::u32string &name,
                                          std::shared_ptr<ASTExpr> &callee) {
    calleeType_ = calleeType_.typeOfTypeValue();

    if (calleeType_.type() == TypeType::ValueType || calleeType_.type() == TypeType::Enum) {
        callType_ = CallType::StaticContextfreeDispatch;
    }
    else if (calleeType_.type() == TypeType::Class) {
        callType_ = CallType::DynamicDispatchOnType;
    }
    else {
        throw CompilerError(position(), calleeType_.toString(analyser->typeContext()), " does not provide methods.");
    }

    method_ = calleeType_.typeDefinition()->typeMethods().get(name, args_.mood(), &args_,
                                                              &calleeType_, analyser, position());

    if (calleeType_.type() == TypeType::Class && (method_->accessLevel() == AccessLevel::Private || calleeType_.isExact())) {
        callType_ = CallType::StaticDispatch;
    }
    ensureErrorIsHandled(analyser);
    return analyser->analyseFunctionCall(&args_, calleeType_, method_, &method_);
}

Type ASTMethodable::analyseMultiProtocolCall(ExpressionAnalyser *analyser, const std::u32string &name,
                                             const std::shared_ptr<ASTExpr> &callee) {
    std::vector<Type> argTypes;
    for (auto &arg : args_.args()) {
        argTypes.emplace_back(analyser->analyse(arg));
    }
    auto genericArgs = transformTypeAstVector(args_.genericArguments(), analyser->typeContext());
    // The generic parameters of a method, e.g. Element in 🍡🐚🔢🍆, are resolved on the protocol that declares it.
    for (multiprotocolN_ = 0; multiprotocolN_ < calleeType_.protocols().size(); multiprotocolN_++) {
        auto protocol = calleeType_.protocols()[multiprotocolN_];
        auto resolution = FunctionResolution<Function>(name, args_.mood(), argTypes, genericArgs, protocol,
                                                       analyser->typeContext(), analyser->semanticAnalyser(),
                                                       position());
        resolution.addResolver(&protocol.protocol()->methods());
        if ((method_ = resolution.resolveAndReificate(&args_, &protocol)) != nullptr) {
            builtIn_ = BuiltInType::Multiprotocol;
            callType_ = CallType::DynamicProtocolDispatch;
            checkMutation(analyser, callee);
            return analyser->analyseFunctionCall(&args_, protocol, method_);
        }
    }
    throw CompilerError(position(), "No type in ", calleeType_.toString(analyser->typeContext()),
                        " provides a method ", utf8(name), ".");
}

std::map<std::pair<TypeDefinition*, char32_t>, ASTMethodable::BuiltInType> ASTMethodable::kBuiltIns = {};

void ASTMethodable::prepareBuiltIns(Compiler *c) {
    if (!kBuiltIns.empty()) return;
    kBuiltIns = {
        {{c->sBoolean, E_NEGATIVE_SQUARED_CROSS_MARK}, BuiltInType::BooleanNegate},
        {{c->sInteger, E_HUNDRED_POINTS_SYMBOL}, BuiltInType::IntegerToDouble},
        {{c->sInteger, E_NEGATIVE_SQUARED_CROSS_MARK}, BuiltInType::IntegerNot},
        {{c->sInteger, E_BATTERY}, BuiltInType::IntegerInverse},
        {{c->sInteger, 0x1f4a7}, BuiltInType::IntegerToByte},
        {{c->sByte, E_NEGATIVE_SQUARED_CROSS_MARK}, BuiltInType::IntegerNot},
        {{c->sByte, E_BATTERY}, BuiltInType::IntegerInverse},
        {{c->sByte, 0x1f522}, BuiltInType::ByteToInteger},
        {{c->sReal, E_BATTERY}, BuiltInType::DoubleInverse},
        {{c->sReal, 0x1F3C2}, BuiltInType::Power},
        {{c->sReal, 0x1f6a3}, BuiltInType::Log2},
        {{c->sReal, 0x1f94f}, BuiltInType::Log10},
        {{c->sReal, 0x1f3c4}, BuiltInType::Ln},
        {{c->sReal, 0x1f6b5}, BuiltInType::Floor},
        {{c->sReal, 0x1f6b4}, BuiltInType::Ceil},
        {{c->sReal, 0x1f3c7}, BuiltInType::Round},
        {{c->sReal, 0x1f3e7}, BuiltInType::DoubleAbs},
        {{c->sReal, 0x1f522}, BuiltInType::DoubleToInteger},
        {{c->sMemory, E_RECYCLING_SYMBOL}, BuiltInType::Release},
        {{c->sMemory, 0x1F69C}, BuiltInType::MemoryMove},
        {{c->sMemory, 0x270D}, BuiltInType::MemorySet},
        {{c->sMemory, 0x1F43D}, BuiltInType::Load},
    };
}

bool ASTMethodable::builtIn(ExpressionAnalyser *analyser, const Type &btype, const std::u32string &name) {
    auto type = btype.unboxed();
    if (type.type() != TypeType::ValueType) {
        return false;
    }

    if (args_.mood() == Mood::Assignment && type.typeDefinition() == analyser->compiler()->sMemory
        && name.front() == 0x1F43D) {
        builtIn_ = BuiltInType::Store;
        return true;
    }

    if (type.valueType()->declaresCRepresentation() && builtInC(analyser, type, name)) {
        return true;
    }

    prepareBuiltIns(analyser->compiler());
    auto it = kBuiltIns.find(std::make_pair(type.valueType(), name.front()));
    if (it != kBuiltIns.end()) {
        builtIn_ = it->second;
        return true;
    }

    return false;
}

bool ASTMethodable::builtInC(ExpressionAnalyser *analyser, const Type &type, const std::u32string &name) {
    auto compiler = analyser->compiler();
    auto valueType = type.valueType();
    auto &representation = *valueType->cRepresentation();
    // Only the methods the type declares as built-ins are built-ins; methods with bodies are called normally.
    auto &methods = valueType->methods().list();
    if (name.size() != 1 || std::none_of(methods.begin(), methods.end(), [&](Function *method) {
        return method->name() == name && method->mood() == args_.mood() && method->externalName() == "ejcBuiltIn";
    })) {
        return false;
    }
    auto first = name.front();

    if (valueType == compiler->cPointer) {
        auto &pointee = type.genericArguments().front();
        auto accessesPointee = first == E_PIG_NOSE || first == E_NEXT_TRACK;
        if (accessesPointee && (pointee.is<TypeType::GenericVariable>() ||
                                pointee.is<TypeType::LocalGenericVariable>() || pointee.storageType() == StorageType::Box ||
                                pointee.unboxedType() == TypeType::Something ||
                                pointee.unboxedType() == TypeType::Protocol ||
                                pointee.unboxedType() == TypeType::MultiProtocol)) {
            throw CompilerError(position(), "Cannot access a ", type.toString(analyser->typeContext()),
                                " because its values are not stored as C values. Use a concrete pointee type.");
        }
        switch (first) {
            case E_PIG_NOSE:
                builtIn_ = args_.mood() == Mood::Assignment ? BuiltInType::CPointerStore : BuiltInType::CPointerLoad;
                return true;
            case E_NEXT_TRACK:
                builtIn_ = BuiltInType::CPointerAdvance;
                return true;
            case E_PERFORMING_ARTS:
            case E_HOLE:
                builtIn_ = BuiltInType::CReinterpret;
                return true;
            default:
                break;
        }
    }
    if (valueType == compiler->cVoidPointer) {
        switch (first) {
            case E_ROUND_PUSHPIN:
            case E_INBOX_TRAY:
                builtIn_ = BuiltInType::CReinterpret;
                return true;
            case E_EYES:
                builtIn_ = BuiltInType::CBorrowObject;
                return true;
            default:
                break;
        }
    }
    if (args_.mood() != Mood::Imperative || !args_.args().empty()) {
        return false;
    }
    switch (first) {
        case E_INPUT_SYMBOL_FOR_NUMBERS:
        case E_HUNDRED_POINTS_SYMBOL:
            builtIn_ = BuiltInType::CConvert;
            return true;
        case E_NEGATIVE_SQUARED_CROSS_MARK:
            if (representation.isInteger()) {
                builtIn_ = BuiltInType::IntegerNot;
                return true;
            }
            return false;
        case E_BATTERY:
            if (representation.isPointer()) {
                return false;
            }
            builtIn_ = representation.isFloat() ? BuiltInType::DoubleInverse : BuiltInType::IntegerInverse;
            return true;
        default:
            return false;
    }
}

Type ASTMethod::analyse(ExpressionAnalyser *analyser) {
    return analyseMethodCall(analyser, name_, callee_);
}

void ASTMethod::analyseMemoryFlow(MFFunctionAnalyser *analyser, MFFlowCategory type) {
    analyser->analyseFunctionCall(&args_, callee_.get(), method_);
}

}  // namespace EmojicodeCompiler

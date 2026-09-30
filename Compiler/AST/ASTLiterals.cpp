//
//  ASTLiterals.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 04/08/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "Generation/StringPool.hpp"
#include "Generation/FunctionCodeGenerator.hpp"
#include "Generation/RunTimeHelper.hpp"
#include "Generation/CallCodeGenerator.hpp"
#include "ASTInitialization.hpp"
#include "ASTLiterals.hpp"
#include "Analysis/FunctionAnalyser.hpp"
#include "Analysis/SemanticAnalyser.hpp"
#include "Compiler.hpp"
#include "MemoryFlowAnalysis/MFFunctionAnalyser.hpp"
#include "Package/Package.hpp"
#include "Parsing/AbstractParser.hpp"
#include "Scoping/SemanticScoper.hpp"
#include "Types/Class.hpp"
#include "Types/ValueType.hpp"
#include "Types/CommonTypeFinder.hpp"
#include "Types/TypeExpectation.hpp"
#include <optional>

namespace EmojicodeCompiler {

Type ASTStringLiteral::analyse(ExpressionAnalyser *analyser) {
    auto type = Type(analyser->compiler()->sString);
    type.setExact(true);
    return type;
}

Type ASTBooleanTrue::analyse(ExpressionAnalyser *analyser) {
    return analyser->boolean();
}

Type ASTBooleanFalse::analyse(ExpressionAnalyser *analyser) {
    return analyser->boolean();
}

Type ASTNumberLiteral::analyse(ExpressionAnalyser *analyser) {
    if (type_ == NumberType::Integer) {
        return Type::integerLiteral();
    }
    return Type::realLiteral(analyser->real());
}

Type ASTNumberLiteral::comply(ExpressionAnalyser *analyser, const TypeExpectation &expectation) {
    // The expectation may wrap the C type in an optional or a box (e.g. a generic argument); the analyser wraps the
    // complied value accordingly.
    auto target = expectation.unboxed();
    if (target.type() == TypeType::Optional) {
        target = target.optionalType();
    }
    if (target.type() == TypeType::ValueType && target.valueType()->declaresCRepresentation()) {
        auto &representation = *target.valueType()->cRepresentation();
        if (representation.isFloat() || (representation.isInteger() && type_ == NumberType::Integer)) {
            if (type_ == NumberType::Integer) {
                doubleValue_ = integerValue_;
                warnIfOutOfRange(analyser, representation);
            }
            type_ = NumberType::C;
            cType_ = target;
            cType_.setMutable(false);
            cType_.setReference(false);
            return cType_;
        }
    }
    if (type_ == NumberType::Double) {
        return analyser->real();
    }
    if (expectation == analyser->real()) {
        type_ = NumberType::Double;
        doubleValue_ = integerValue_;
        return analyser->real();
    }
    if (expectation == analyser->byte()) {
        if (integerValue_ > 255) {
            analyser->compiler()->warn(position(), "Literal implicitly is a byte integer literal but value does ",
                                       "not fit into byte type.");
        }
        type_ = NumberType::Byte;
        return analyser->byte();
    }
    return analyser->integer();
}

void ASTNumberLiteral::warnIfOutOfRange(ExpressionAnalyser *analyser, const CRepresentation &representation) const {
    auto bits = representation.bits;
    if (bits < 2 || bits >= 64) {
        return;
    }
    // Accept every value that fits the type either as a signed or as an unsigned integer, like C does silently.
    auto minimum = -(int64_t(1) << (bits - 1)), maximum = (int64_t(1) << bits) - 1;
    if (integerValue_ < minimum || integerValue_ > maximum) {
        analyser->compiler()->warn(position(), "Literal ", integerValue_, " does not fit into the C type and is ",
                                   "truncated.");
    }
}

Type ASTThis::analyse(ExpressionAnalyser *analyser) {
    analyser->checkThisUse(position());

    if (!isSelfAllowed(analyser->functionType())) {
        throw CompilerError(position(), "Illegal use of 👇.");
    }
    analyser->pathAnalyser().record(PathAnalyserIncident::UsedSelf);
    return analyser->typeContext().calleeType();
}

void ASTThis::analyseMemoryFlow(MFFunctionAnalyser *analyser, MFFlowCategory type) {
    analyser->recordThis(type);
}

Type ASTNoValue::analyse(ExpressionAnalyser *analyser) {
    return Type::noValueLiteral();
}

Type ASTNoValue::comply(ExpressionAnalyser *analyser, const TypeExpectation &expectation) {
    if (expectation.unboxedType() != TypeType::Optional && expectation.unboxedType() != TypeType::Something) {
        throw CompilerError(position(), "🤷‍ can only be used when an optional is expected.");
    }
    type_ = expectation.copyType();
    return type_;
}

ASTCollectionLiteral::ASTCollectionLiteral(const SourcePosition &p) : ASTExpr(p) {}

ASTCollectionLiteral::~ASTCollectionLiteral() = default;

/// @returns True if @p type is the type of an empty collection literal, or of one whose elements are all such literals,
/// which Type::compatibleTo accepts for a collection of any element type.
static bool isEmptyCollectionLiteral(const Type &type) {
    if (!type.is<TypeType::ListLiteral>() && !type.is<TypeType::DictionaryLiteral>()) {
        return false;
    }
    auto &element = type.genericArguments().front();
    return element.type() == TypeType::NoReturn || isEmptyCollectionLiteral(element);
}

/// Merges the types of two empty collection literals (see isEmptyCollectionLiteral()) into the type of one both fit.
/// @returns Nothing if there is none, as one is a list and the other a dictionary literal at the same depth.
static std::optional<Type> mergeEmptyCollectionLiterals(const Type &a, const Type &b) {
    if (a.type() == TypeType::NoReturn) {
        return b;
    }
    if (b.type() == TypeType::NoReturn) {
        return a;
    }
    if (a.type() != b.type()) {
        return std::nullopt;
    }
    auto element = mergeEmptyCollectionLiterals(a.genericArguments().front(), b.genericArguments().front());
    if (!element) {
        return std::nullopt;
    }
    return a.is<TypeType::ListLiteral>() ? Type::listLiteral(*element) : Type::dictionaryLiteral(*element);
}

Type ASTCollectionLiteral::analyse(ExpressionAnalyser *analyser) {
    finder_ = std::make_unique<CommonTypeFinder>(analyser->semanticAnalyser());

    std::vector<Type> types;
    if (pairs_) {
        for (auto it = values_.begin(); it != values_.end(); it++) {
            analyser->analyse(*it);
            if (++it == values_.end()) {
                throw CompilerError(position(), "A value must be provided for every key.");
            }
            types.emplace_back(analyser->analyse(*it));
        }
    }
    else {
        for (auto &valueNode : values_) {
            types.emplace_back(analyser->analyse(valueNode));
        }
    }

    auto element = commonElementType(types, analyser->typeContext());
    return pairs_ ? Type::dictionaryLiteral(element) : Type::listLiteral(element);
}

Type ASTCollectionLiteral::commonElementType(const std::vector<Type> &types, const TypeContext &typeContext) {
    if (types.empty()) {
        return Type::noReturn();  // Type::compatibleTo accepts an empty literal for any element type.
    }
    // An empty literal as element fits the element type of the others, and must not widen it to ⚪️.
    std::vector<Type> empty;
    for (auto &type : types) {
        if (isEmptyCollectionLiteral(type)) {
            empty.emplace_back(type);
        }
        else {
            finder_->addType(type, typeContext);
        }
    }
    if (empty.size() == types.size()) {
        std::optional<Type> merged = Type::noReturn();
        for (auto &type : empty) {
            if (merged) {
                merged = mergeEmptyCollectionLiterals(*merged, type);
            }
        }
        if (merged) {
            return *merged;
        }
    }
    for (auto &type : empty) {
        if (empty.size() == types.size() || !type.compatibleTo(finder_->getCommonType(), typeContext)) {
            finder_->addType(type, typeContext);
        }
    }
    return finder_->getCommonType();
}

/// Returns the collection type a collection literal is expected to be. Like with number literals, the expectation may
/// wrap it in an optional or a box, in which case the analyser wraps the complied value accordingly.
static Type collectionExpectation(const TypeExpectation &expectation) {
    auto target = expectation.unboxed();
    if (target.type() == TypeType::Optional) {
        target = target.optionalType();
    }
    target.setMutable(false);
    target.setReference(false);
    return target;
}

void ASTCollectionLiteral::lookupInitializer(ExpressionAnalyser *analyser, const std::vector<Type> &arguments) {
    initializer_ = type_.typeDefinition()->inits().lookup(U"🍪", Mood::Imperative, arguments, type_,
                                                          analyser->typeContext(), analyser->semanticAnalyser());
    initializer_->createUnspecificReification();
}

void ASTCollectionLiteral::adoptType(ExpressionAnalyser *analyser, const TypeExpectation &expectation) {
    auto target = collectionExpectation(expectation);
    if (target.type() == TypeType::ValueType && target.typeDefinition()->canInitFrom(expressionType())) {
        type_ = target;
    }
    else {
        finder_->issueWarning(position(), analyser->compiler());
        type_ = analyser->semanticAnalyser()->defaultLiteralType(expressionType());
    }
    finder_ = nullptr;
    type_.setExact(true);
}

Type ASTCollectionLiteral::complyPairs(ExpressionAnalyser *analyser, const TypeExpectation &expectation) {
    adoptType(analyser, expectation);
    setElementType(analyser->compiler()->sDictionary->typeForVariable(0), analyser);
    for (auto it = values_.begin(); it != values_.end(); it++) {
        complyElement(analyser, analyser->compiler()->sString->type(), &(*it));
        if (++it == values_.end()) {
            throw CompilerError(position(), "A value must be provided for every key.");
        }
        complyElement(analyser, elementType_, &(*it));
    }
    lookupInitializer(analyser, { analyser->compiler()->sMemory->type(), analyser->compiler()->sMemory->type(),
                                  analyser->integer() });
    return type_;
}

void ASTCollectionLiteral::setElementType(const Type &variable, ExpressionAnalyser *analyser) {
    auto boxed = variable.resolveOn(TypeContext(type_));
    auto unboxed = boxed.unboxedType();
    // The element type, like 🍨🐚T🍆, is described at run time.
    analyser->usesGenericArgumentsOf(boxed);
    if (unboxed == TypeType::GenericVariable || unboxed == TypeType::LocalGenericVariable) {
        elementType_ = boxed;  // Generic here, so stored with the witness of the type it stands for.
    }
    else {
        elementType_ = boxed.unboxed().withMinimalBoxing();
    }
}

void ASTCollectionLiteral::complyElement(ExpressionAnalyser *analyser, const Type &type,
                                         std::shared_ptr<ASTExpr> *node) const {
    auto elementType = analyser->comply(TypeExpectation(type), node);
    if (!elementType.compatibleTo(type, analyser->typeContext())) {
        throw CompilerError((*node)->position(), elementType.toString(analyser->typeContext()),
                            " is not compatible to ", type.toString(analyser->typeContext()), ".");
    }
}

void ASTCollectionLiteral::analyseMemoryFlow(MFFunctionAnalyser *analyser, MFFlowCategory type) {
    for (auto &valueNode : values_) {
        valueNode->analyseMemoryFlow(analyser, MFFlowCategory::Escaping);
    }
}

Type ASTCollectionLiteral::comply(ExpressionAnalyser *analyser, const TypeExpectation &expectation) {
    if (pairs_) return complyPairs(analyser, expectation);
    adoptType(analyser, expectation);
    setElementType(analyser->compiler()->sList->typeForVariable(0), analyser);
    for (auto &valueNode : values_) {
        complyElement(analyser, elementType_, &valueNode);
    }
    lookupInitializer(analyser, { analyser->compiler()->sMemory->type(), analyser->integer() });
    return type_;
}

Type ASTInterpolationLiteral::analyse(ExpressionAnalyser *analyser) {
    Type sb = analyser->package()->getRawType(TypeIdentifier(U"🔠", kDefaultNamespace, position()));
    init_ = sb.typeDefinition()->inits().lookup(U"🆕", Mood::Imperative, { Type(analyser->compiler()->sInteger) },
                                                Type(sb), analyser->typeContext(), analyser->semanticAnalyser());

    append_ = sb.typeDefinition()->methods().lookup(U"🐻", Mood::Imperative,
                                                    {Type(analyser->compiler()->sString)}, Type(sb),
                                                    analyser->typeContext(), analyser->semanticAnalyser());

    get_ = sb.typeDefinition()->methods().lookup(U"🔡", Mood::Imperative, {}, Type(sb),
                                                 analyser->typeContext(), analyser->semanticAnalyser());

    auto magnet = Type(analyser->compiler()->sInterpolateable).applyMinimalBoxing().referenced();
    toString_ = magnet.typeDefinition()->methods().lookup(U"🔡", Mood::Imperative, {}, magnet,
                                                          analyser->typeContext(), analyser->semanticAnalyser());

    for (auto &value : values_) {
        analyser->expectType(magnet, &value);
    }
    return analyser->compiler()->sString->type();
}

void ASTInterpolationLiteral::analyseMemoryFlow(MFFunctionAnalyser *analyser, MFFlowCategory type) {
    for (auto &valueNode : values_) {
        valueNode->analyseMemoryFlow(analyser, MFFlowCategory::Borrowing);
    }
}

}  // namespace EmojicodeCompiler

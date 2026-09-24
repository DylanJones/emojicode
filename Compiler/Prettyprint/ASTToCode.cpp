//
//  ASTToCode.cpp
//  EmojicodeCompiler
//
//  Created by Theo Weidmann on 22/08/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "AST/ASTBinaryOperator.hpp"
#include "AST/ASTCast.hpp"
#include "AST/ASTClosure.hpp"
#include "AST/ASTControlFlow.hpp"
#include "AST/ASTExpr.hpp"
#include "AST/ASTInitialization.hpp"
#include "AST/ASTLiterals.hpp"
#include "AST/ASTMethod.hpp"
#include "AST/ASTStatements.hpp"
#include "AST/ASTTypeExpr.hpp"
#include "AST/ASTUnary.hpp"
#include "AST/ASTUnsafeBlock.hpp"
#include "AST/ASTVariables.hpp"
#include "AST/ASTSuper.hpp"
#include "AST/ASTType.hpp"
#include "AST/ASTConditionalAssignment.hpp"
#include "AST/ASTTypeAsValue.hpp"
#include "Parsing/OperatorHelper.hpp"
#include "PrettyStream.hpp"
#include "Types/Type.hpp"
#include <sstream>

namespace EmojicodeCompiler {

void ASTArguments::genericArgsToCode(PrettyStream &pretty) const {
    if (!genericArguments_.empty()) {
        pretty << "🐚";
        for (auto &arg : genericArguments_) {
            pretty << arg;
        }
        pretty << "🍆";
    }
}

void ASTArguments::toCode(PrettyStream &pretty) const {
    if (!arguments_.empty()) {
        pretty << " ";
        for (auto &arg : arguments_) {
            pretty << arg;
            pretty.offerSpace();
        }
    }
    pretty.refuseOffer() << (mood_ == Mood::Imperative ? "❗️" : "❓️");
}

void ASTBlock::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    if (stmts_.empty()) {
        pretty << "🍇🍉\n";
        return;
    }
    pretty << "🍇";
    pretty.offerNewLine();
    innerToCode(pretty);
    pretty.indent() << "🍉\n";
}

void ASTBlock::innerToCode(PrettyStream &pretty) const {
    pretty.increaseIndent();
    for (auto &stmt : stmts_) {
        pretty << stmt;
        pretty.offerNewLine();
        if (stmt->paragraph()) {
            pretty << "\n";
        }
    }
    pretty.decreaseIndent();
}

void ASTRepeatWhile::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty.indent() << "🔁 " << condition_ << " " << block_;
}

void ASTForIn::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty.indent() << "🔂 " << varName_ << " " << iteratee_ << " " << block_;
}

void ASTUnsafeBlock::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty.indent() << "☣️ " << block_;
}

void printBranchSpeed(PrettyStream &pretty, ASTIf::BranchSpeed speed) {
    switch (speed) {
        case ASTIf::BranchSpeed::Fast:
            pretty << "🎍🏎";
            break;
        case ASTIf::BranchSpeed::Slow:
            pretty << "🎍🐌";
            break;
        default:
            break;
    }
}

void ASTIf::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty.indent() << "↪️ " << conditions_.front() << " ";
    printBranchSpeed(pretty, blocks_.front().speed);
    pretty << blocks_.front().block;
    for (size_t i = 1; i < conditions_.size(); i++) {
        pretty.indent() << "🙅↪️ " << conditions_[i] << " ";
        printBranchSpeed(pretty, blocks_[i].speed);
        pretty << blocks_[i].block;
    }
    if (hasElse()) {
        pretty.indent() << "🙅 " << blocks_.back().block;
    }
}

void ASTClosure::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty.printClosure(closure_.get(), isEscaping_);
}

void ASTErrorHandler::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty.indent() << "🆗 " << valueVarName_ << " " << value_ << " " << valueBlock_;
    pretty.indent() << "🙅‍♀️ " << errorVarName_ << " " << errorBlock_;
}

void ASTExprStatement::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty.indent() << expr_;
}

void ASTVariableDeclaration::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty.indent() << "🖍🆕 " << varName_ << " " << type_;
}

void ASTVariableAssignment::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty.indent() << expr_ << "➡️ 🖍" << name();
}

void ASTVariableDeclareAndAssign::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty.indent() << expr_ << "➡️ 🖍🆕 " << name();
}

void ASTConstantVariable::toCode(PrettyStream &pretty) const {
    pretty.indent() << expr_;
    pretty.printComments(position());
    pretty << " ➡️ " << name();
}

void ASTConditionalAssignment::toCode(PrettyStream &pretty) const {
    pretty << expr_ << " ➡️ " << varName_;
}

void ASTOperatorAssignment::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty.indent();
    auto binaryOperator = dynamic_cast<ASTBinaryOperator *>(expr_.get());
    pretty << name() << " ⬅️" << operatorName(binaryOperator->operatorType()) << " " << binaryOperator->right();
}

void ASTGetVariable::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << name();
}

void ASTIsOnlyReference::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << "🏮 " << name();
}

void ASTSuper::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << "⤴️" << name_;
    args_.genericArgsToCode(pretty);
    pretty << args_;
}

void ASTInitialization::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << "🆕" << typeExpr_;
    if (name_ != U"🆕") {
        pretty << "▶️" << name_;
    }
    args_.genericArgsToCode(pretty);
    pretty << args_;
}

void ASTThisType::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << "👇";
}

void ASTInferType::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << "⚫️";
}

void ASTStaticType::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << type_;
}

void ASTTypeFromExpr::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << "⬛️" << expr_;
}

void ASTTypeAsValue::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << ASTTypeValueType::toString(tokenType_) << type_;
}

void ASTSizeOf::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << "⚖️" << type_;
}

void ASTCallableCall::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << "⁉️" << callable_ << args_;
}

void ASTBooleanTrue::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << "👍";
}

void ASTBooleanFalse::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << "👎";
}

void ASTThis::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << "👇";
}

void ASTUnwrap::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << " 🍺" << expr_;
}

void ASTReraise::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << "🔺" << expr_;
}

void ASTNumberLiteral::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << string_;
    pretty.offerSpace();
}

void ASTNoValue::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << "🤷‍♀️";
}

void ASTStringLiteral::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << "🔤" << value_ << "🔤";
}

void ASTRaise::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty.indent() << "🚨" << value_;
}

void ASTReturn::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    if (value_ == nullptr) {
        pretty.indent() << "↩️↩️";
    }
    else {
        pretty.indent() << "↩️ " << value_;
    }
}

void ASTCast::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << "🔲" << expr_ << typeExpr_;
}

void ASTMethod::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    if (args_.mood() == Mood::Assignment) {
        pretty << args_.args().front() << " ➡️ " << name_;
        args_.genericArgsToCode(pretty);
        pretty << callee_;
        pretty.offerSpace();
        for (size_t i = 1; i < args_.args().size(); i++) {
            pretty << args_.args()[i];
        }
        pretty.refuseOffer() << "❗️";
    }
    else {
        pretty << name_;
        args_.genericArgsToCode(pretty);
        pretty << callee_ << args_;
    }
}

void ASTInterpolationLiteral::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    auto literalsIt = literals_.begin();
    pretty << "🔤" << *literalsIt++;
    for (auto &val : values_) {
        pretty << "🧲" << val << "🧲" << *literalsIt++;
    }
    pretty << "🔤";
}

void ASTCollectionLiteral::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    pretty << "🍿 ";
    if (pairs_) {
        for (auto it = values_.begin(); it != values_.end(); it++) {
            pretty << *it++ << " ➡️ " << *it;
        }
    } else {
        for (auto &val : values_) {
            pretty << val << " ";
        }
    }
    pretty << "🍆";
}

void ASTBinaryOperator::printBinaryOperand(int precedence, const std::shared_ptr<ASTExpr> &expr,
                                           PrettyStream &pretty) const {
    pretty.printComments(position());
    if (auto oper = dynamic_cast<ASTBinaryOperator *>(expr.get())) {
        if (operatorPrecedence(oper->operator_) < precedence) {
            pretty << "🤜" << expr << "🤛";
            return;
        }
    }
    pretty << expr;
}

void ASTBinaryOperator::toCode(PrettyStream &pretty) const {
    pretty.printComments(position());
    auto precedence = operatorPrecedence(operator_);
    printBinaryOperand(precedence, left_, pretty);
    pretty << " " << operatorName(operator_) << " ";
    printBinaryOperand(precedence, right_, pretty);
}

void ASTType::toCode(PrettyStream &pretty) const {
    if (optional_) {
        pretty << "🍬";
    }
    if (reference_) {
        pretty << "✴️";
    }
    toCodeType(pretty);
}

void ASTTypeId::toCodeType(PrettyStream &pretty) const {
    if (!namespace_.empty()) {
        pretty << "🔶" << namespace_;
    }
    pretty << name_;
    if (!genericArgs_.empty()) {
        pretty << "🐚";
        for (auto &arg : genericArgs_) {
            pretty << arg;
        }
        pretty.refuseOffer() << "🍆";
    }
}

void ASTCallableType::toCodeType(PrettyStream &pretty) const {
    pretty << (c_ ? "🍇🎍🌊" : "🍇");
    for (auto &type : params_) {
        pretty << type;
    }
    if (return_ != nullptr) {
        pretty << "➡️" << return_;
    }
    if (errorType_ != nullptr) {
        pretty << "🚧" << errorType_;
    }
    pretty << "🍉";
}

void ASTMultiProtocol::toCodeType(PrettyStream &pretty) const {
    pretty << "🍱";
    for (auto &type : protocols_) {
        pretty << type;
    }
    pretty << "🍱";
}

void ASTGenericVariable::toCodeType(PrettyStream &pretty) const {
    pretty << name_;
    pretty.offerSpace();
}

void ASTTypeValueType::toCodeType(PrettyStream &pretty) const {
    pretty << toString(tokenType_) << type_;
}

void ASTLiteralType::toCode(PrettyStream &pretty) const {
    pretty << type();
}

void ASTSelection::toCode(PrettyStream &pretty) const {
    pretty << "📣 " << expr_ << " " << typeExpr_;
}

} // namespace EmojicodeCompiler

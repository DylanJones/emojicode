//
//  PrettyStream.cpp
//  EmojicodeCompiler
//
//  Copyright © 2018 Theo Weidmann. All rights reserved.
//

#include "PrettyStream.hpp"
#include "AST/ASTType.hpp"
#include "CompilerError.hpp"
#include "Lex/SourceManager.hpp"
#include "PrettyPrinter.hpp"
#include "Types/Type.hpp"
#include "Utils/StringUtils.hpp"
#include <cerrno>
#include <cstring>

namespace EmojicodeCompiler {

void PrettyStream::printComments(const SourcePosition &p) {
    if (p.file == nullptr) {
        return;
    }
    p.file->findComments(lastCommentQuery_, p, [this, &p](const Token &comment) {
        if (whitespaceOffer_ == '\n') {
            if (comment.position().line >= p.line) {
                *stream_ << whitespaceOffer_;
                whitespaceOffer_ = 0;
                indent();
            }
            else {
                *stream_ << "  ";
            }
        }

        *this << (comment.type() == TokenType::MultilineComment ? "💭🔜" : "💭") << comment.value();
        if (comment.type() == TokenType::MultilineComment) *stream_ << "🔚💭";
        else offerNewLine();
    });
    lastCommentQuery_ = p;
}

void PrettyStream::setOutPath(const std::string &path) {
    path_ = path;
    stream_ = std::make_unique<std::fstream>(path, std::ios_base::out);
    if (!stream_->good()) {
        auto message = std::strerror(errno);
        stream_ = std::make_unique<std::ostringstream>();
        throw CompilerError(SourcePosition(), "Could not write ", path, ": ", message);
    }
}

void PrettyStream::finish() {
    errno = 0;
    stream_->flush();
    if (!stream_->good()) {
        throw CompilerError(SourcePosition(), "Could not write ", path_, ": ",
                            errno != 0 ? std::strerror(errno) : "I/O error");
    }
}

void PrettyStream::setOutString() {
    stream_ = std::make_unique<std::ostringstream>();
}

std::string PrettyStream::takeString() {
    auto string = static_cast<std::ostringstream *>(stream_.get())->str();
    stream_ = std::make_unique<std::ostringstream>();
    return string;
}

void PrettyStream::printClosure(Function *function, bool escaping) {
    prettyPrinter_->printClosure(function, escaping);
}

PrettyStream& PrettyStream::operator<<(ASTNode *node) {
    node->toCode(*this);
    return *this;
}

PrettyStream& PrettyStream::operator<<(const ASTNode &node) {
    node.toCode(*this);
    return *this;
}

PrettyStream& PrettyStream::operator<<(const std::u32string &str) {
    *this << utf8(str);
    return *this;
}

PrettyStream& PrettyStream::operator<<(const Type &type) {
    *this << type.toString(typeContext_, prettyPrinter_->package_);
    return *this;
}

PrettyStream& PrettyStream::operator<<(const std::string &rhs) {
    if (whitespaceOffer_ != 0) {
        *stream_ << whitespaceOffer_;
        whitespaceOffer_ = 0;
    }
    *stream_ << rhs;
    return *this;
}

void PrettyStream::setLastCommentQueryPlace(const SourcePosition &p) {
    lastCommentQuery_ = p;
}

void PrettyStream::withTypeContext(const TypeContext &context, std::function<void ()> fn) {
    typeContext_ = context;
    fn();
    typeContext_ = TypeContext();
}

}  // namespace EmojicodeCompiler

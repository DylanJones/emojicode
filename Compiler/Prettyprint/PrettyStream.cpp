//
//  PrettyStream.cpp
//  EmojicodeCompiler
//
//  Copyright © 2018 Theo Weidmann. All rights reserved.
//

#include "PrettyStream.hpp"
#include "AST/ASTType.hpp"
#include "Lex/SourceManager.hpp"
#include "PrettyPrinter.hpp"
#include "Types/Type.hpp"
#include "Utils/StringUtils.hpp"
#include <algorithm>
#include <limits>

namespace EmojicodeCompiler {

namespace {

bool isBefore(const SourcePosition &a, const SourcePosition &b) {
    return std::make_pair(a.line, a.character) < std::make_pair(b.line, b.character);
}

/// Returns true iff there is something other than whitespace in front of the comment on its line.
bool hasCodeBefore(const SourcePosition &position) {
    auto line = position.wholeLine();
    auto end = std::min<size_t>(position.character > 0 ? position.character - 1 : 0, line.size());
    return std::any_of(line.begin(), line.begin() + end, [](char32_t c) { return c != U' ' && c != U'\t'; });
}

}  // namespace

void PrettyStream::write(const std::string &string) {
    if (string.empty()) {
        return;
    }
    *stream_ << string;
    lastChar_ = string.back();
}

void PrettyStream::printComment(const Token &comment) {
    auto &position = comment.position();
    if (!printedComments_.emplace(position.file, position.line, position.character).second) {
        return;
    }
    auto multiline = comment.type() == TokenType::MultilineComment;
    auto trailing = hasCodeBefore(position) && lastChar_ != '\n';
    auto offeredNewLine = whitespaceOffer_ == '\n';
    whitespaceOffer_ = 0;
    // The comment has its own indentation, whatever it stands in front of still needs its indentation.
    auto wasIndentPending = indentPending_;
    indentPending_ = false;
    if (trailing) {
        write("  ");
    }
    else {
        // An offered new line after a finished line keeps the blank line that was intended.
        if ((lastChar_ != '\n' && lastChar_ != 0) || offeredNewLine) {
            write("\n");
        }
        write(std::string(indentation_ * 2, ' '));
    }
    write(multiline ? "💭🔜" : "💭");
    write(utf8(comment.value()));
    if (multiline) {
        write("🔚💭");
    }
    if (multiline && trailing) {
        offerSpace();
    }
    else {
        offerNewLine();
    }
    indentPending_ = wasIndentPending;
}

void PrettyStream::printComments(const SourcePosition &p) {
    if (p.file == nullptr) {
        return;
    }
    p.file->findComments(lastCommentQuery_, p, [this](const Token &comment) { printComment(comment); });
    if (isBefore(lastCommentQuery_, p)) {
        lastCommentQuery_ = p;
    }
}

bool PrettyStream::hasCommentsBefore(const SourcePosition &p) const {
    auto found = false;
    if (p.file != nullptr) {
        p.file->findComments(lastCommentQuery_, p, [&](const Token &comment) {
            auto &position = comment.position();
            found = found || printedComments_.count(std::make_tuple(position.file, position.line,
                                                                    position.character)) == 0;
        });
    }
    return found;
}

void PrettyStream::finishLine() {
    if (lastChar_ != '\n' && lastChar_ != 0) {
        write("\n");
    }
    whitespaceOffer_ = 0;
}

void PrettyStream::printRemainingComments(SourceFile *file) {
    if (file != nullptr) {
        printComments(SourcePosition(std::numeric_limits<unsigned int>::max(), 0, file));
    }
}

void PrettyStream::startFile() {
    lastCommentQuery_ = SourcePosition();
    printedComments_.clear();
}

void PrettyStream::ensureSpace() {
    if (whitespaceOffer_ == 0 && lastChar_ != ' ' && lastChar_ != '\n' && lastChar_ != 0) {
        offerSpace();
    }
}

void PrettyStream::setOutPath(const std::string &path) {
    stream_ = std::make_unique<std::fstream>(path, std::ios_base::out);
    lastChar_ = 0;
    whitespaceOffer_ = 0;
}

void PrettyStream::setOutString() {
    stream_ = std::make_unique<std::ostringstream>();
    lastChar_ = 0;
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
    if (rhs.empty()) {
        return *this;
    }
    if (whitespaceOffer_ != 0) {
        write(std::string(1, whitespaceOffer_));
        whitespaceOffer_ = 0;
    }
    if (indentPending_) {
        indentPending_ = false;
        write(std::string(indentation_ * 2, ' '));
    }
    write(rhs);
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

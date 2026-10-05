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
#include <algorithm>
#include <cerrno>
#include <cstring>
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

/// Returns true iff only whitespace lies between the one-character token at a and the position b (same line).
bool onlySpaceBetween(const SourcePosition &a, const SourcePosition &b) {
    auto line = a.wholeLine();
    auto end = std::min<size_t>(b.character > 0 ? b.character - 1 : 0, line.size());
    for (size_t i = a.character; i < end; i++) {
        if (line[i] != U' ' && line[i] != U'\t') return false;
    }
    return true;
}

}  // namespace

void PrettyStream::write(const std::string &string) {
    if (string.empty()) {
        return;
    }
    *stream_ << string;
    lastWasLineComment_ = false;
    lastChar_ = string.back();
}

void PrettyStream::printComment(const Token &comment, bool statementLevel) {
    auto &position = comment.position();
    if (!printedComments_.emplace(position.file, position.line, position.character).second) {
        return;
    }
    auto multiline = comment.type() == TokenType::MultilineComment;
    auto trailing = hasCodeBefore(position) && lastChar_ != '\n' && !lastWasLineComment_;
    auto offeredNewLine = whitespaceOffer_ == '\n';
    auto blankWritten = offeredNewLine && lastChar_ == '\n';
    whitespaceOffer_ = 0;
    // The comment has its own indentation, whatever it stands in front of still needs its indentation.
    auto wasIndentPending = indentPending_;
    indentPending_ = false;
    // An own-line comment in the middle of a statement is indented like the rest of the statement.
    auto midStatement = !trailing && !wasIndentPending && !statementLevel &&
                        (continuation_ || (!offeredNewLine && lastChar_ != 0 && lastChar_ != '\n'));
    if (trailing) {
        write("  ");
    }
    else {
        // An offered new line after a finished line keeps the blank line that was intended.
        if ((lastChar_ != '\n' && lastChar_ != 0) || offeredNewLine) {
            write("\n");
        }
        // Keep a blank line between two comments.
        if (lastCommentFile_ == position.file && !blankWritten && lastCommentEndLine_ != 0 && position.line > lastCommentEndLine_ + 1) {
            write("\n");
        }
        write(std::string((indentation_ + (midStatement ? 1 : 0)) * 2, ' '));
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
    lastWasLineComment_ = !multiline;
    // A line comment ends the line in the middle of an expression, unless the statement printer ends the line.
    continuation_ = (trailing && !multiline && !offeredNewLine) || midStatement;
    lastCommentFile_ = position.file;
    lastCommentEndLine_ = position.line + std::count(comment.value().begin(), comment.value().end(), U'\n');
}

void PrettyStream::printComments(const SourcePosition &p, bool statementLevel) {
    if (p.file == nullptr) {
        return;
    }
    p.file->findComments(lastCommentQuery_, p, [&](const Token &comment) { printComment(comment, statementLevel); });
    if (isBefore(lastCommentQuery_, p)) {
        lastCommentQuery_ = p;
    }
}

void PrettyStream::printTrailingComments(const SourcePosition &p, bool tokenEnd) {
    if (p.file == nullptr || lastChar_ == '\n' || lastChar_ == 0) {
        return;
    }
    auto endOfLine = SourcePosition(p.line, std::numeric_limits<unsigned int>::max(), p.file);
    auto printed = false;
    p.file->findComments(lastCommentQuery_, endOfLine, [&](const Token &comment) {
        auto &position = comment.position();
        if (position.line == p.line && hasCodeBefore(position) && (!tokenEnd || onlySpaceBetween(p, position))) {
            printComment(comment);
            printed = true;
        }
    });
    if (printed) {
        // The caller ends the line.
        whitespaceOffer_ = 0;
        continuation_ = false;
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
    continuation_ = false;
}

void PrettyStream::printRemainingComments(SourceFile *file) {
    if (file != nullptr) {
        printComments(SourcePosition(std::numeric_limits<unsigned int>::max(), 0, file));
    }
}

void PrettyStream::startFile() {
    lastCommentQuery_ = SourcePosition();
    printedComments_.clear();
    lastCommentEndLine_ = 0;
}

void PrettyStream::ensureSpace() {
    if (whitespaceOffer_ == 0 && lastChar_ != ' ' && lastChar_ != '\n' && lastChar_ != 0) {
        offerSpace();
    }
}

void PrettyStream::setOutPath(const std::string &path) {
    path_ = path;
    stream_ = std::make_unique<std::fstream>(path, std::ios_base::out);
    if (!stream_->good()) {
        auto message = std::strerror(errno);
        stream_ = std::make_unique<std::ostringstream>();
        throw CompilerError(SourcePosition(), "Could not write ", path, ": ", message);
    }
    lastChar_ = 0;
    whitespaceOffer_ = 0;
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
    if (rhs.find_first_not_of(" \n") != std::string::npos) {
        lastCommentEndLine_ = 0;
    }
    if (whitespaceOffer_ != 0) {
        write(std::string(1, whitespaceOffer_));
        whitespaceOffer_ = 0;
    }
    if (indentPending_) {
        indentPending_ = false;
        write(std::string(indentation_ * 2, ' '));
    }
    else if (continuation_ && lastChar_ == '\n') {
        write(std::string((indentation_ + 1) * 2, ' '));
    }
    continuation_ = false;
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

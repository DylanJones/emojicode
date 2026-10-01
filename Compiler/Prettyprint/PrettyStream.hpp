//
//  Prettyprinter.hpp
//  EmojicodeCompiler
//
//  Created by Theo Weidmann on 25/08/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#ifndef Prettyprinter_hpp
#define Prettyprinter_hpp

#include "Lex/SourcePosition.hpp"
#include "Types/TypeContext.hpp"
#include <fstream>
#include <sstream>
#include <functional>
#include <memory>
#include <set>
#include <tuple>

namespace EmojicodeCompiler {

class ASTNode;
class SourceFile;
class Token;
class PrettyPrinter;

/// PrettyStream manages the stream to which code is appended. PrettyStream can be appended to with <<.
///
/// PrettyStream features a concept of "whitespace offers". By a call to offerSpace() or offerNewLine() whitespace
/// is offered. The whitespace is then appended before the next object passed to << unless refuseOffer() is called
/// previously.
class PrettyStream {
public:
    PrettyStream(PrettyPrinter *prettyPrinter) : prettyPrinter_(prettyPrinter) {}

    /// Makes the stream write to the file at path.
    /// @throws CompilerError if the file cannot be opened.
    void setOutPath(const std::string &path);
    /// Flushes the file stream opened by setOutPath().
    /// @throws CompilerError if writing failed.
    void finish();
    /// Makes the stream write to a string, which takeString() returns.
    void setOutString();
    /// Returns what was written since setOutString() and clears it.
    std::string takeString();

    template <typename T>
    PrettyStream& operator<<(const std::unique_ptr<T> &node) {
        node->toCode(*this);
        return *this;
    }

    template <typename T>
    PrettyStream& operator<<(const std::shared_ptr<T> &node) {
        node->toCode(*this);
        return *this;
    }

    PrettyStream& operator<<(const std::string &rhs);
    PrettyStream& operator<<(const std::u32string &str);
    PrettyStream& operator<<(ASTNode *node);
    PrettyStream& operator<<(const ASTNode &node);
    PrettyStream& operator<<(const Type &type);

    void printClosure(Function *function, bool escaping);

    void setLastCommentQueryPlace(const SourcePosition &p);
    /// Prints the comments of the source file that are in front of @p p and were not printed yet.
    void printComments(const SourcePosition &p);
    /// Prints all comments of @p file that were not printed yet.
    void printRemainingComments(SourceFile *file);
    /// Prints the comments that follow the code on the source line of @p p after what is already written to the
    /// current line. Does nothing if the current line is empty. Must be called before the line is ended.
    /// @param tokenEnd If true, p is the position of a single-character token and only a comment directly
    /// following that token on its line (no other code in between) is printed.
    void printTrailingComments(const SourcePosition &p, bool tokenEnd = false);
    /// Returns true iff there is a comment that was not printed yet in front of @p p.
    bool hasCommentsBefore(const SourcePosition &p) const;
    /// Ends the current line if it is not empty, instead of offering a new line.
    void finishLine();
    /// Must be called when starting to print another source file.
    void startFile();
    /// Offers a space unless the output already ends with whitespace.
    void ensureSpace();

    void withTypeContext(const TypeContext &context, std::function<void ()> fn);

    /// Makes the next thing appended to the stream be preceded by the requested amount of indentation characters.
    /// The indentation is not written until then, so that comments can go in front of the line.
    PrettyStream& indent() { indentPending_ = true; return *this; }

    /// Like indent(), but only if the stream is at the start of a line or a new line is about to be written, e.g.
    /// after a comment.
    PrettyStream& indentAtLineStart() {
        indentPending_ = indentPending_ || whitespaceOffer_ == '\n' || lastChar_ == '\n';
        return *this;
    }

    void increaseIndent() { indentation_++; }
    void decreaseIndent() { indentation_--; }

    /// Refuses any available whitespace offer.
    /// @returns The instance.
    PrettyStream& refuseOffer() { whitespaceOffer_ = 0; return *this; }
    /// Offers a space character
    void offerSpace() { whitespaceOffer_ = ' '; }
    /// Offers a new line character unless the output already ends with one, which blocks do.
    void endLine() { if (lastChar_ != '\n') { offerNewLine(); } }
    /// Offers a new line character
    void offerNewLine() { whitespaceOffer_ = '\n'; }
    /// Calls offerSpace() unless collection returns true for empty()
    template<typename T>
    void offerNewLineUnlessEmpty(const T &collection) { if (!collection.empty()) { offerNewLine(); } }
    
private:
    std::unique_ptr<std::ostream> stream_;
    std::string path_;

    PrettyPrinter *prettyPrinter_;
    TypeContext typeContext_;
    char whitespaceOffer_ = 0;
    char lastChar_ = 0;
    bool indentPending_ = false;
    std::set<std::tuple<const SourceFile *, unsigned int, unsigned int>> printedComments_;

    void write(const std::string &string);
    void printComment(const Token &comment);
    unsigned int indentation_ = 0;
    SourcePosition lastCommentQuery_ = SourcePosition();
    /// The source line on which the comment that was printed last ends, if nothing but whitespace was written since.
    unsigned int lastCommentEndLine_ = 0;
    const SourceFile *lastCommentFile_ = nullptr;
};

}  // namespace EmojicodeCompiler


#endif /* Prettyprinter_hpp */

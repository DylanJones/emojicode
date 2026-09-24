//
//  Index.cpp
//  EmojicodeLanguageServer
//

#include "Index.hpp"
#include "AST/ASTExpr.hpp"
#include "AST/ASTStatements.hpp"
#include "AST/ASTType.hpp"
#include "Analysis/FunctionAnalyser.hpp"
#include "Functions/Function.hpp"
#include "Lex/SourceManager.hpp"
#include "Scoping/Scope.hpp"
#include "Scoping/SemanticScoper.hpp"
#include <algorithm>

namespace EmojicodeLanguageServer {

using namespace EmojicodeCompiler;

const std::string& Index::path(SourceFile *file) {
    auto it = paths_.find(file);
    if (it == paths_.end()) {
        it = paths_.emplace(file, canonicalPath(file->path())).first;
    }
    return it->second;
}

/// Returns the context without the function generic arguments, which point to memory that is freed after the call
/// was analysed.
static TypeContext storableContext(const TypeContext &context) {
    return TypeContext(context.calleeType(), context.function());
}

void Index::analysedExpression(ASTExpr *expr, ExpressionAnalyser *analyser) {
    auto &p = expr->position();
    if (p.isUnknown()) {
        return;
    }
    IndexedNode node{p.line, p.character};
    node.expr = expr;
    node.context = storableContext(analyser->typeContext());
    nodes_[path(p.file)].push_back(node);
}

void Index::analysedType(ASTType *type) {
    auto &p = type->position();
    if (p.isUnknown()) {
        return;
    }
    IndexedNode node{p.line, p.character};
    node.type = type;
    nodes_[path(p.file)].push_back(node);
}

void Index::leavingScope(const ASTBlock &block, const Scope &scope, FunctionAnalyser *analyser) {
    // A function's own block has no position, as its 🍇 is parsed with the function's signature. Its scope, which
    // contains the parameters, starts at the function.
    auto begin = block.position().isUnknown() ? analyser->function()->position() : block.position();
    auto &end = block.endPosition();
    if (begin.isUnknown() || end.isUnknown()) {
        return;
    }
    recordScope(Location{path(begin.file), begin.line, begin.character},
                Location{path(end.file), end.line, end.character}, scope, analyser);
}

void Index::analysisFailed(FunctionAnalyser *analyser) {
    // The scopes that were not left yet are known to start at the function at the latest. Where they end is
    // unknown, as the rest of the function was not analysed.
    auto &begin = analyser->function()->position();
    if (begin.isUnknown()) {
        return;
    }
    for (auto &scope : analyser->scoper().scopes()) {
        recordScope(Location{path(begin.file), begin.line, begin.character},
                    Location{path(begin.file), SIZE_MAX, SIZE_MAX}, scope, analyser);
    }
}

void Index::recordScope(const Location &begin, const Location &end, const Scope &scope, FunctionAnalyser *analyser) {
    auto context = storableContext(analyser->typeContext());
    scopes_.push_back(IndexedScope{begin, end, context});
    for (auto &pair : scope.map()) {
        auto &variable = pair.second;
        auto &p = variable.position();
        if (p.isUnknown()) {
            continue;
        }
        variables_.push_back(IndexedVariable{variable.name(), variable.type(), variable.constant(),
                                             Location{path(p.file), p.line, p.character}, begin, end, context});
    }
}

void Index::finish() {
    for (auto &pair : nodes_) {
        std::stable_sort(pair.second.begin(), pair.second.end(), [](const IndexedNode &a, const IndexedNode &b) {
            return std::make_pair(a.line, a.character) < std::make_pair(b.line, b.character);
        });
    }
}

const std::vector<IndexedNode>& Index::nodes(const std::string &path) const {
    static const std::vector<IndexedNode> none;
    auto it = nodes_.find(path);
    return it == nodes_.end() ? none : it->second;
}

std::vector<const IndexedNode*> Index::nodesAt(const std::string &path, size_t line, size_t character) const {
    auto &all = nodes(path);
    auto key = std::make_pair(line, character);
    auto it = std::lower_bound(all.begin(), all.end(), key, [](const IndexedNode &node, auto key) {
        return std::make_pair(node.line, node.character) < key;
    });
    std::vector<const IndexedNode*> result;
    for (; it != all.end() && it->line == line && it->character == character; ++it) {
        result.push_back(&*it);
    }
    return result;
}

}  // namespace EmojicodeLanguageServer

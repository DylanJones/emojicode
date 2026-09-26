//
//  Completion.cpp
//  EmojicodeLanguageServer
//

#include "Completion.hpp"
#include "EmojiNames.hpp"
#include "Index.hpp"
#include "Navigation.hpp"
#include "AST/ASTType.hpp"
#include "Functions/Function.hpp"
#include "Lex/EmojiTokenization.hpp"
#include "Package/RecordingPackage.hpp"
#include "Prettyprint/PrettyPrinter.hpp"
#include "Types/TypeDefinition.hpp"
#include <algorithm>
#include <cctype>
#include <set>
#include <unordered_map>

namespace EmojicodeLanguageServer {

using namespace EmojicodeCompiler;

namespace {

namespace Kind {
const int Text = 1, Method = 2, Field = 5, Variable = 6, Class = 7, Interface = 8, Enum = 13, Keyword = 14,
          Snippet = 15, Struct = 22, TypeParameter = 25;
}

/// Where a keyword can be written, as flags.
namespace Places {
/// At the start of a statement outside of all types and blocks.
const int TopLevel = 1;
/// At the start of a member of a class, value type or enumeration, e.g. a method.
const int Member = 2;
/// In the code of a method or block.
const int Code = 4;
/// After the start of a declaration, where types are written, e.g. the type of an instance variable.
const int Declaration = 8;
/// At the start of a member of a protocol, which only declares methods.
const int ProtocolMember = 16;
/// Where the grammar can't be followed, e.g. after an invalid token.
const int Anywhere = TopLevel | Member | Code | Declaration;
}

/// A keyword with the words it can be found by, a snippet that inserts it with what usually follows, and the
/// Places where it can be written.
struct Keyword {
    const char *words;
    const char *emoji;
    const char *description;
    const char *snippet;
    int places;
};

const Keyword kKeywords[] = {
    {"class", "🐇", "Defines a class.", "🐇 ${1:🐟} 🍇\n\t$0\n🍉", Places::TopLevel},
    {"value type struct", "🕊", "Defines a value type.", "🕊 ${1:🐟} 🍇\n\t$0\n🍉", Places::TopLevel},
    {"enumeration enum", "🔘", "Defines an enumeration.", "🔘 ${1:🚦} 🍇\n\t🆕▶️${2:🔴}\n🍉", Places::TopLevel},
    {"protocol interface", "🐊", "Defines a protocol.", "🐊 ${1:🐟} 🍇\n\t$0\n🍉", Places::TopLevel},
    {"conformance conform protocol interface", "🐊", "Declares that the type conforms to a protocol.", "🐊 ${1:🐟}",
     Places::Member},
    {"method function func def", "❗️", "Defines a method.", "❗️ ${1:🐽} ${2:value} ${3:🔢} 🍇\n\t$0\n🍉", Places::Member},
    {"method function func def", "❗️", "Declares a method that the types conforming to the protocol define.",
     "❗️ ${1:🐽} ${2:value} ${3:🔢}", Places::ProtocolMember},
    {"initializer init constructor", "🆕", "Defines an initializer.", "🆕 🍇\n\t$0\n🍉", Places::Member},
    {"start main", "🏁", "The code that runs when the program starts.", "🏁 🍇\n\t$0\n🍉", Places::TopLevel},
    {"if", "↪️", "Runs a block if a condition is true.", "↪️ ${1:condition} 🍇\n\t$0\n🍉", Places::Code},
    {"else", "🙅", "Runs a block if no condition before was true.", "🙅 🍇\n\t$0\n🍉", Places::Code},
    {"elseif", "🙅↪️", "Runs a block if no condition before was true and this one is.",
     "🙅↪️ ${1:condition} 🍇\n\t$0\n🍉", Places::Code},
    {"for foreach each loop iterate", "🔂", "Runs a block for each element of a collection.",
     "🔂 ${1:element} ${2:collection} 🍇\n\t$0\n🍉", Places::Code},
    {"while repeat loop", "🔁", "Runs a block while a condition is true.", "🔁 ${1:condition} 🍇\n\t$0\n🍉", Places::Code},
    {"return", "↩️", "Returns from a method.", "↩️ $0", Places::Code},
    {"variable var let mutable declare", "🖍🆕", "Declares a mutable variable.", "🖍🆕 ${1:name} ${2:🔢}",
     Places::Code | Places::Member},
    {"new create instance", "🆕", "Creates an instance.", "🆕${1:🐟}❗️", Places::Code},
    {"this self", "👇", "The instance on which the method was called.", nullptr, Places::Code},
    {"super parent", "⤴️", "Calls the method or initializer of the superclass.", nullptr, Places::Code},
    {"true yes", "👍", "True.", nullptr, Places::Code},
    {"false no", "👎", "False.", nullptr, Places::Code},
    {"nil null none nothing novalue", "🤷‍♀️", "No value.", nullptr, Places::Code},
    {"string text", "🔤", "A string literal.", "🔤$1🔤", Places::Code},
    {"interpolation interpolate", "🧲", "Inserts a value into a string.", "🧲$1🧲", Places::Code},
    {"comment", "💭", "A comment.", "💭 $0",
     Places::TopLevel | Places::Member | Places::ProtocolMember | Places::Code | Places::Declaration},
    {"documentation doc", "📗", "Documents the definition that follows.", "📗 $1 📗",
     Places::TopLevel | Places::Member | Places::ProtocolMember},
    {"print output log", "😀", "Prints a string.", "😀 🔤$1🔤❗️", Places::Code},
    {"unwrap force optional", "🍺", "Unwraps an optional, which must not be empty.", nullptr, Places::Code},
    {"try rethrow reraise", "🔺", "Raises the error of a call that raised one.", nullptr, Places::Code},
    {"cast as", "🔲", "Casts a value to a type.", "🔲 ${1:value} ${2:🔢}", Places::Code},
    {"error throw raise", "🚨", "Raises an error.", "🚨 $0", Places::Code},
    {"catch handle error", "🆗", "Handles the error a call raises.",
     "🆗 ${1:value} ${2:call} 🍇\n\t$0\n🍉 🙅 ${3:error} 🍇\n\t\n🍉", Places::Code},
    {"deinitializer deinit destructor", "♻️", "Defines code that runs when an instance is released.",
     "♻️ 🍇\n\t$0\n🍉", Places::Member},
    {"unsafe", "☣️", "Allows unsafe code in a block.", "☣️ 🍇\n\t$0\n🍉", Places::Code},
    {"import package", "📦", "Imports a package.", "📦 ${1:package} 🏠", Places::TopLevel},
    {"include file", "📜", "Includes another file of this package.", "📜 🔤$1🔤", Places::TopLevel},
    {"list array collection", "🍿", "A list or dictionary literal.", "🍿 $1 🍆", Places::Code},
    {"optional maybe", "🍬", "Makes a type optional.", nullptr, Places::Code | Places::Declaration},
    {"generic", "🐚", "Generic arguments or parameters.", "🐚$1🍆", Places::Code | Places::Declaration},
    {"export public", "🌍", "Exports a type from the package.", nullptr, Places::TopLevel},
    {"final sealed", "🔏", "Prevents overriding or subclassing.", nullptr, Places::TopLevel | Places::Member},
    {"override", "✒️", "Overrides a method of the superclass.", nullptr, Places::Member},
    {"public", "🔓", "Public access.", nullptr, Places::Member},
    {"private", "🔒", "Private access.", nullptr, Places::Member},
    {"protected", "🔐", "Protected access.", nullptr, Places::Member},
    {"required", "🔑", "Requires subclasses to implement the initializer.", nullptr, Places::Member},
    {"static typemethod", "🐇", "Makes a method a type method.", nullptr, Places::Member},
    {"and", "🤝", "Logical and.", nullptr, Places::Code},
    {"or", "👐", "Logical or.", nullptr, Places::Code},
    {"equal equals", "🙌", "Whether two values are equal.", nullptr, Places::Code},
    {"plus add", "➕", "Adds.", nullptr, Places::Code},
    {"minus subtract", "➖", "Subtracts.", nullptr, Places::Code},
    {"times multiply", "✖️", "Multiplies.", nullptr, Places::Code},
    {"divide", "➗", "Divides.", nullptr, Places::Code},
    {"remainder modulo mod", "🚮", "The remainder of a division.", nullptr, Places::Code},
    {"call closure", "⁉️", "Calls a callable.", nullptr, Places::Code},
    {"group parenthesis", "🤜", "Groups an expression.", "🤜$1🤛", Places::Code},
};

/// Emoji that are shown with U+FE0F in source code, as they default to text presentation.
std::string emojiText(const std::u32string &name) {
    auto text = utf8(name);
    if (name.size() == 1 && name[0] < 0x10000) {
        text += "️";
    }
    return text;
}

std::string lowercase(std::string string) {
    for (auto &c : string) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return string;
}

/// Returns 0 if @p text starts with @p word, 1 if a word in @p text does, and -1 otherwise.
int match(const std::string &word, const std::string &text) {
    if (word.empty()) {
        return 1;
    }
    auto lower = lowercase(text);
    if (lower.compare(0, word.size(), word) == 0) {
        return 0;
    }
    for (size_t i = 1; i + word.size() <= lower.size(); i++) {
        if (!isalnum(static_cast<unsigned char>(lower[i - 1])) && lower.compare(i, word.size(), word) == 0) {
            return 1;
        }
    }
    return -1;
}

const std::unordered_map<char32_t, const char *>& emojiNames() {
    static std::unordered_map<char32_t, const char *> names = [] {
        std::unordered_map<char32_t, const char *> names;
        for (auto &entry : kEmojiNames) names.emplace(entry.emoji, entry.name);
        return names;
    }();
    return names;
}

/// The Unicode names of the emoji in @p name, which can be searched for.
std::string namesOf(const std::u32string &name) {
    std::string names;
    for (auto c : name) {
        auto it = emojiNames().find(c);
        if (it != emojiNames().end()) {
            names += std::string(it->second) + " ";
        }
    }
    return names;
}

std::string firstParagraph(const std::u32string &documentation) {
    auto text = utf8(documentation);
    auto begin = text.find_first_not_of(" \n\t");
    if (begin == std::string::npos) {
        return "";
    }
    auto end = text.find("\n\n", begin);
    return text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

bool isOperator(const std::u32string &name) {
    static const std::u32string operators = U"➕➖➗✖👐🤝⭕💢❌👈👉🚮🙌😜◀▶";
    return !name.empty() && operators.find(name.front()) != std::u32string::npos;
}

/// Whether @p token is an attribute of a declaration, or ends a block before a statement on the same line, so that a
/// statement can still start after it.
bool precedesStatement(const TokenSpan &token) {
    static const std::u32string attributes = U"🌍🔏📻🥯⚠✒☣🖍🔑🔓🔒🔐";
    switch (token.type) {
        case TokenType::Decorator:
        case TokenType::BlockEnd:
        case TokenType::DocumentationComment:
        case TokenType::PackageDocumentationComment:
        case TokenType::MultilineComment:
            return true;
        default:
            return token.value.size() == 1 && attributes.find(token.value[0]) != std::u32string::npos;
    }
}

}  // namespace

Completer::Place Completer::place(size_t offset) const {
    auto &text = source_.text;
    auto &tokens = source_.tokens;
    auto lineStart = [&](size_t index) { return index - source_.lines.lineAndCharacter(index).second; };
    // Whether only tokens that can precede a statement come before token @p index on its line.
    auto headsLine = [&](size_t index) {
        auto start = lineStart(tokens[index].start);
        for (auto j = index; j > 0 && tokens[j - 1].start >= start; j--) {
            if (!precedesStatement(tokens[j - 1])) return false;
        }
        return true;
    };
    // What each open 🍇 contains. A 🍇 at the top level opens the body of a type if a type was declared since the
    // last body, maybe on an earlier line; in the generic arguments of the declaration, it is a callable type. All
    // other blocks contain code.
    std::vector<Place::Kind> blocks;
    auto declared = Place::Code;
    size_t generics = 0;  // The 🐚 at the top level that are not closed yet.
    size_t i = 0;
    for (; i < tokens.size() && tokens[i].end <= offset; i++) {
        auto &token = tokens[i];
        if (token.type == TokenType::BlockBegin) {
            blocks.push_back(blocks.empty() ? declared : Place::Code);
            if (blocks.size() == 1 && generics == 0) declared = Place::Code;
        }
        else if (token.type == TokenType::BlockEnd) {
            if (!blocks.empty()) blocks.pop_back();
        }
        else if (blocks.empty()) {
            switch (token.type) {
                case TokenType::Class:
                case TokenType::ValueType:
                case TokenType::Enumeration:
                    declared = Place::TypeBody;
                    generics = 0;
                    break;
                case TokenType::Protocol:
                    declared = Place::ProtocolBody;
                    generics = 0;
                    break;
                case TokenType::Generic:
                    generics++;
                    break;
                case TokenType::Identifier:
                    if (token.value == U"🍆") {
                        if (generics > 0) generics--;
                    }
                    // A statement like 🏁 that is no type declaration starts. Elsewhere, e.g. as the name of a type,
                    // these are types.
                    else if ((token.value == U"🏁" || token.value == U"📦" || token.value == U"📜" ||
                              token.value == U"🔗") && headsLine(i)) {
                        declared = Place::Code;
                        generics = 0;
                    }
                    break;
                default:
                    break;
            }
        }
    }
    // The lexer stops at an invalid token, e.g. an ASCII operator, so the grammar can't be followed after it.
    if (i == tokens.size()) {
        for (auto k = tokens.empty() ? 0 : tokens.back().end; k < std::min(offset, text.size()); k++) {
            if (!isSkipped(text[k]) && text[k] != 0xFE0F) return Place{Place::Unknown, false};
        }
    }

    Place place{blocks.empty() ? Place::TopLevel : blocks.back(), true};
    // The tokens before the offset on its line are the last ones that the loop went through.
    auto start = lineStart(offset);
    auto first = i;
    while (first > 0 && tokens[first - 1].start >= start) first--;
    for (auto j = first; j < i; j++) {
        auto &token = tokens[j];
        auto inTypeBody = place.kind == Place::TypeBody || place.kind == Place::ProtocolBody;
        if (inTypeBody && token.type == TokenType::LeftProductionOperator) {
            place.kind = Place::Code;  // The default value of an instance variable.
        }
        // In a type body, 🐇 is the attribute that makes the method after it a type method.
        if (!precedesStatement(token) && !(inTypeBody && token.type == TokenType::Class)) {
            place.statementStart = false;
        }
    }
    return place;
}

size_t mapOffset(const std::u32string &now, const std::u32string &before, size_t offset) {
    auto limit = std::min(now.size(), before.size());
    size_t prefix = 0;
    while (prefix < limit && now[prefix] == before[prefix]) {
        prefix++;
    }
    size_t suffix = 0;
    while (suffix < limit - prefix && now[now.size() - 1 - suffix] == before[before.size() - 1 - suffix]) {
        suffix++;
    }
    if (offset <= prefix) {
        return offset;
    }
    if (offset >= now.size() - suffix) {
        return offset - now.size() + before.size();
    }
    return prefix;  // In the changed part, which did not exist before.
}

size_t Completer::wordStart(size_t offset) const {
    auto &text = source_.text;
    auto start = std::min(offset, text.size());
    while (start > 0) {
        auto c = text[start - 1];
        if (isEmoji(c) || c == 0xFE0F || c == 0x200D || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            break;
        }
        start--;
    }
    return start;
}

bool Completer::canComplete(size_t offset) const {
    auto token = tokenAt(source_.tokens, offset);
    if (token == nullptr || offset <= token->start) {
        return true;
    }
    switch (token->type) {
        case TokenType::SinglelineComment:
            return false;  // Also at its end, which is the end of the line.
        case TokenType::String:
        case TokenType::BeginInterpolation:
        case TokenType::MiddleInterpolation:
        case TokenType::EndInterpolation:
        case TokenType::MultilineComment:
        case TokenType::DocumentationComment:
        case TokenType::PackageDocumentationComment:
            return !token->unterminated && offset >= token->end;
        default:
            return true;
    }
}

void Completer::addVariables(size_t offset, const std::string &word, std::vector<CompletionItem> *items) const {
    if (analysis_ == nullptr || analysis_->index == nullptr) {
        return;
    }
    // The analysis may be of the text before the latest changes, so the position is mapped to that text.
    auto analysed = analysis_->texts.find(path_);
    auto &text = analysed != analysis_->texts.end() ? analysed->second : source_.text;
    auto here = LineIndex(text).compilerPosition(mapOffset(source_.text, text, offset));
    auto contains = [&](const Location &begin, const Location &end) {
        return begin.path == path_ && std::make_pair(begin.line, begin.character) <= here &&
               here <= std::make_pair(end.line, end.character);
    };
    auto package = analysis_->compiler->mainPackage();

    std::set<std::u32string> seen;
    for (auto &variable : analysis_->index->variables()) {
        auto &d = variable.declaration;
        if (!contains(variable.scopeBegin, variable.scopeEnd) || std::make_pair(d.line, d.character) >= here ||
            !seen.insert(variable.name).second) {
            continue;
        }
        auto name = utf8(variable.name);
        auto quality = match(word, name);
        if (quality < 0) continue;
        items->push_back(CompletionItem{name, Kind::Variable,
                                        variable.type.unboxed().toString(variable.context, package), "", name,
                                        false, 0, quality});
    }

    // Instance variables of the type whose method contains the position.
    const IndexedScope *innermost = nullptr;
    for (auto &scope : analysis_->index->scopes()) {
        if (contains(scope.begin, scope.end) &&
            (innermost == nullptr || std::make_pair(scope.begin.line, scope.begin.character) >
                                     std::make_pair(innermost->begin.line, innermost->begin.character))) {
            innermost = &scope;
        }
    }
    if (innermost == nullptr) {
        return;
    }
    auto callee = innermost->context.calleeType().unboxed();
    switch (callee.unboxedType()) {
        case TypeType::Class:
        case TypeType::ValueType:
        case TypeType::Enum:
            break;
        default:
            return;
    }
    for (auto &pair : instanceVariables(callee.typeDefinition())) {
        auto &variable = *pair.first;
        auto name = utf8(variable.name);
        auto quality = match(word, name);
        if (quality < 0 || !seen.insert(variable.name).second) continue;
        auto type = variable.type->wasAnalysed()
            ? variable.type->type().toString(TypeContext(pair.second->type()), package) : "";
        items->push_back(CompletionItem{name, Kind::Field, type, "", name, false, 0, quality});
    }
}

void Completer::addKeywords(const std::string &word, int places, std::vector<CompletionItem> *items) const {
    for (auto &keyword : kKeywords) {
        if ((keyword.places & places) == 0) continue;
        auto quality = match(word, keyword.words);
        if (quality < 0) continue;
        CompletionItem item{std::string(keyword.emoji) + " " + keyword.words, Kind::Keyword, keyword.description,
                            "", keyword.emoji, false, 1, quality};
        if (snippets_ && keyword.snippet != nullptr) {
            item.kind = Kind::Snippet;
            item.insertText = keyword.snippet;
            item.isSnippet = true;
            item.documentation = "```emojicode\n" + std::string(keyword.snippet) + "\n```";
        }
        items->push_back(std::move(item));
    }
}

void Completer::addTypesAndMethods(const std::string &word, bool methods,
                                   std::vector<CompletionItem> *items) const {
    if (analysis_ == nullptr || analysis_->compiler == nullptr) {
        return;
    }
    auto package = analysis_->compiler->mainPackage();
    PrettyPrinter printer(package);
    std::set<TypeDefinition *> seen;
    for (auto &pair : package->types()) {
        auto &type = pair.second;
        int kind;
        const char *keyword;
        switch (type.type()) {
            case TypeType::Class: kind = Kind::Class; keyword = "class"; break;
            case TypeType::ValueType: kind = Kind::Struct; keyword = "value type"; break;
            case TypeType::Enum: kind = Kind::Enum; keyword = "enumeration"; break;
            case TypeType::Protocol: kind = Kind::Interface; keyword = "protocol"; break;
            default: continue;
        }
        auto definition = type.typeDefinition();
        auto name = definition->name();
        auto ns = pair.first.substr(0, pair.first.size() - name.size());
        auto label = (ns == U"🏠" ? U"" : U"🔶" + ns) + name;
        auto documentation = firstParagraph(definition->documentation());
        auto quality = match(word, namesOf(name) + keyword + " " + documentation);
        if (quality >= 0) {
            items->push_back(CompletionItem{utf8(label), kind, std::string(keyword) + " " + utf8(label),
                                            documentation, ns == U"🏠" ? emojiText(name) : utf8(label), false, 2,
                                            quality});
        }

        if (!methods || !seen.insert(definition).second) {
            continue;
        }
        auto addMethod = [&](Function *function) {
            if (function->isThunk() || isOperator(function->name()) ||
                (function->accessLevel() == AccessLevel::Private && function->package() != package)) {
                return;
            }
            auto documentation = firstParagraph(function->documentation());
            auto quality = match(word, namesOf(function->name()) + documentation);
            if (quality < 0) return;
            auto declaration = printer.declaration(function);
            declaration.erase(declaration.find_last_not_of(" \n") + 1);
            items->push_back(CompletionItem{utf8(function->name()) + "  " + utf8(name), Kind::Method, declaration,
                                            documentation, emojiText(function->name()), false, 3, quality});
        };
        for (auto function : definition->methods().list()) addMethod(function);
        for (auto function : definition->typeMethods().list()) addMethod(function);
    }
}

void Completer::addEmoji(const std::string &word, std::vector<CompletionItem> *items) const {
    if (word.size() < 2) {
        return;  // Most emoji would match a single letter.
    }
    for (auto &entry : kEmojiNames) {
        auto quality = match(word, entry.name);
        if (quality < 0) continue;
        auto emoji = emojiText(std::u32string(1, entry.emoji));
        items->push_back(CompletionItem{emoji + " " + entry.name, Kind::Text, "", "", emoji, false, 4, quality});
    }
}

std::vector<CompletionItem> Completer::complete(size_t start, size_t offset, size_t limit) const {
    auto word = lowercase(utf8(source_.text.substr(start, offset - start)));
    std::vector<CompletionItem> items;
    // Only what the grammar allows where the word is: e.g. at the start of a member of a type, a method or an
    // instance variable is declared, so no variable, type or method is used there.
    auto where = place(start);
    if (where.kind == Place::Code || where.kind == Place::Unknown) {
        addVariables(offset, word, &items);
        // In code, most words name a variable, type or method, so keywords are only offered for a typed word.
        if (!word.empty()) addKeywords(word, where.kind == Place::Code ? Places::Code : Places::Anywhere, &items);
        addTypesAndMethods(word, true, &items);
    }
    else if (where.statementStart) {
        addKeywords(word, where.kind == Place::TopLevel ? Places::TopLevel
                          : where.kind == Place::ProtocolBody ? Places::ProtocolMember : Places::Member, &items);
    }
    else {
        addKeywords(word, Places::Declaration, &items);
        addTypesAndMethods(word, false, &items);
    }
    addEmoji(word, &items);
    // Variables in scope are what is most likely meant, and there are few of them, so they come first and are never
    // cut off by the limit.
    auto key = [](const CompletionItem &item) {
        return std::make_tuple(item.rank == 0 ? 0 : 1, item.quality, item.rank, std::cref(item.label));
    };
    std::stable_sort(items.begin(), items.end(), [&](const CompletionItem &a, const CompletionItem &b) {
        return key(a) < key(b);
    });
    if (items.size() > limit) {
        items.resize(limit);
    }
    return items;
}

}  // namespace EmojicodeLanguageServer

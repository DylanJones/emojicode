//
//  Server.cpp
//  EmojicodeLanguageServer
//

#include "Server.hpp"
#include "Completion.hpp"
#include "SemanticTokens.hpp"
#include <algorithm>

namespace EmojicodeLanguageServer {

using rapidjson::Value;
using namespace std::chrono_literals;

/// How long after the last change a document is checked, so that it is not checked for every key press.
static const auto kCheckDelay = 250ms;

namespace ErrorCodes {
const int InvalidRequest = -32600;
const int MethodNotFound = -32601;
}  // namespace ErrorCodes

static std::string string(const Value &object, const char *key) {
    if (object.IsObject()) {
        auto member = object.FindMember(key);
        if (member != object.MemberEnd() && member->value.IsString()) {
            return std::string(member->value.GetString(), member->value.GetStringLength());
        }
    }
    return "";
}

static const Value& member(const Value &object, const char *key) {
    static const Value null;
    if (object.IsObject()) {
        auto member = object.FindMember(key);
        if (member != object.MemberEnd()) {
            return member->value;
        }
    }
    return null;
}

static Value jsonString(const std::string &string, rapidjson::Document::AllocatorType &allocator) {
    return Value(string.c_str(), static_cast<rapidjson::SizeType>(string.size()), allocator);
}

int Server::run() {
    while (true) {
        std::optional<rapidjson::Document> message;
        try {
            message = transport_->read(millisecondsToNextCheck());
        }
        catch (EndOfInput &) {
            return shutdown_ ? 0 : 1;
        }
        if (!message) {
            runChecks(false);
            continue;
        }
        if (string(*message, "method") == "exit") {
            return shutdown_ ? 0 : 1;
        }
        handle(*message);
    }
}

void Server::handle(const rapidjson::Document &message) {
    if (!message.IsObject()) {
        respondError(Value(), ErrorCodes::InvalidRequest, "The message is not a JSON object.");
        return;
    }
    auto method = string(message, "method");
    auto &params = member(message, "params");
    auto &id = member(message, "id");
    if (method.empty()) {
        return;  // A response to a request of the server, which does not send any.
    }
    if (id.IsNull()) {
        handleNotification(method, params);
    }
    else {
        handleRequest(method, id, params);
    }
}

void Server::handleRequest(const std::string &method, const Value &id, const Value &params) {
    if (method == "initialize") {
        initialize(id, params);
    }
    else if (method == "textDocument/hover") {
        hover(id, params);
    }
    else if (method == "textDocument/definition") {
        definition(id, params);
    }
    else if (method == "textDocument/semanticTokens/full") {
        semanticTokens(id, params);
    }
    else if (method == "textDocument/documentSymbol") {
        documentSymbols(id, params);
    }
    else if (method == "textDocument/completion") {
        completion(id, params);
    }
    else if (method == "shutdown") {
        shutdown_ = true;
        rapidjson::Document document;
        Value result;
        respond(id, result, document);
    }
    else {
        respondError(id, ErrorCodes::MethodNotFound, "Unsupported method " + method + ".");
    }
}

void Server::handleNotification(const std::string &method, const Value &params) {
    if (method == "textDocument/didOpen") {
        didOpen(params);
    }
    else if (method == "textDocument/didChange") {
        didChange(params);
    }
    else if (method == "textDocument/didSave") {
        didSave(params);
    }
    else if (method == "textDocument/didClose") {
        didClose(params);
    }
}

void Server::initialize(const Value &id, const Value &params) {
    auto &encodings = member(member(member(params, "capabilities"), "general"), "positionEncodings");
    if (encodings.IsArray()) {
        // The compiler counts code points, so UTF-32 needs no conversion. UTF-16 is the default all clients support.
        for (auto &encoding : encodings.GetArray()) {
            if (encoding.IsString() && std::string(encoding.GetString()) == "utf-32") {
                encoding_ = PositionEncoding::UTF32;
            }
        }
    }
    auto &snippets = member(member(member(member(member(params, "capabilities"), "textDocument"), "completion"),
                                   "completionItem"), "snippetSupport");
    snippetSupport_ = snippets.IsBool() && snippets.GetBool();
    auto &paths = member(member(params, "initializationOptions"), "packageSearchPaths");
    if (paths.IsArray()) {
        std::vector<std::string> clientPaths;
        for (auto &path : paths.GetArray()) {
            if (path.IsString()) {
                clientPaths.emplace_back(path.GetString());
            }
        }
        searchPaths_.insert(searchPaths_.begin(), clientPaths.begin(), clientPaths.end());
    }

    rapidjson::Document document;
    auto &allocator = document.GetAllocator();
    Value capabilities(rapidjson::kObjectType);
    capabilities.AddMember("positionEncoding", Value(encoding_ == PositionEncoding::UTF32 ? "utf-32" : "utf-16"),
                           allocator);
    Value sync(rapidjson::kObjectType);
    sync.AddMember("openClose", true, allocator);
    sync.AddMember("change", 1, allocator);  // Full: the whole document is sent with each change.
    sync.AddMember("save", Value(rapidjson::kObjectType), allocator);
    capabilities.AddMember("textDocumentSync", sync, allocator);
    capabilities.AddMember("hoverProvider", true, allocator);
    capabilities.AddMember("definitionProvider", true, allocator);
    capabilities.AddMember("documentSymbolProvider", true, allocator);
    capabilities.AddMember("completionProvider", Value(rapidjson::kObjectType), allocator);
    Value tokenTypes(rapidjson::kArrayType);
    for (auto type : kSemanticTokenTypes) tokenTypes.PushBack(Value(rapidjson::StringRef(type)), allocator);
    Value tokenModifiers(rapidjson::kArrayType);
    for (auto modifier : kSemanticTokenModifiers) {
        tokenModifiers.PushBack(Value(rapidjson::StringRef(modifier)), allocator);
    }
    Value legend(rapidjson::kObjectType);
    legend.AddMember("tokenTypes", tokenTypes, allocator);
    legend.AddMember("tokenModifiers", tokenModifiers, allocator);
    Value semanticTokens(rapidjson::kObjectType);
    semanticTokens.AddMember("legend", legend, allocator);
    semanticTokens.AddMember("full", true, allocator);
    capabilities.AddMember("semanticTokensProvider", semanticTokens, allocator);

    Value info(rapidjson::kObjectType);
    info.AddMember("name", "emojicode-lsp", allocator);
    Value result(rapidjson::kObjectType);
    result.AddMember("capabilities", capabilities, allocator);
    result.AddMember("serverInfo", info, allocator);
    respond(id, result, document);
}

void Server::didOpen(const Value &params) {
    auto &item = member(params, "textDocument");
    auto uri = string(item, "uri");
    auto path = uriToPath(uri);
    if (path.empty()) {
        return;
    }
    auto &version = member(item, "version");
    documents_[path] = Document{uri, path, version.IsInt() ? version.GetInt() : 0};
    overlays_[path] = utf32(string(item, "text"));
    roots_.erase(path);
    schedule(updateRoot(path), 0ms);
}

void Server::didChange(const Value &params) {
    auto path = uriToPath(string(member(params, "textDocument"), "uri"));
    auto &changes = member(params, "contentChanges");
    auto document = documents_.find(path);
    if (document == documents_.end() || !changes.IsArray() || changes.Empty()) {
        return;
    }
    // Full synchronization: the last change contains the whole document.
    overlays_[path] = utf32(string(changes[changes.Size() - 1], "text"));
    auto &version = member(member(params, "textDocument"), "version");
    if (version.IsInt()) {
        document->second.version = version.GetInt();
    }
    schedule(roots_[path], kCheckDelay);
}

void Server::didSave(const Value &params) {
    auto path = uriToPath(string(member(params, "textDocument"), "uri"));
    if (documents_.count(path) == 0) {
        return;
    }
    // An include may have been added or removed, which changes the root. Other packages may import this one.
    schedule(updateRoot(path), 0ms);
}

void Server::didClose(const Value &params) {
    auto path = uriToPath(string(member(params, "textDocument"), "uri"));
    auto root = roots_[path];
    documents_.erase(path);
    overlays_.erase(path);
    roots_.erase(path);

    bool rootStillOpen = std::any_of(roots_.begin(), roots_.end(), [&](auto &pair) { return pair.second == root; });
    if (rootStillOpen) {
        schedule(root, 0ms);  // The closed file is read from disk now, which may differ from what was open.
        return;
    }
    // Nothing of this package is open anymore: its diagnostics are removed.
    scheduled_.erase(root);
    analyses_.erase(root);
    parsedAnalyses_.erase(root);
    Analysis empty;
    empty.rootPath = root;
    publishDiagnostics(empty);
    published_.erase(root);
}

std::string Server::updateRoot(const std::string &path) {
    auto root = checker().rootFile(path);
    auto &current = roots_[path];
    if (!current.empty() && current != root) {
        schedule(current, 0ms);
    }
    current = root;
    return root;
}

Checker Server::checker() const {
    return Checker(searchPaths_, overlays_);
}

void Server::schedule(const std::string &root, std::chrono::milliseconds delay) {
    if (root.empty()) {
        return;
    }
    auto time = Clock::now() + delay;
    auto it = scheduled_.find(root);
    if (it == scheduled_.end() || delay == 0ms) {
        scheduled_[root] = time;
    }
    else {
        it->second = time;  // Postponed with every change until the user pauses typing.
    }
}

int Server::millisecondsToNextCheck() const {
    if (scheduled_.empty()) {
        return -1;
    }
    auto next = std::min_element(scheduled_.begin(), scheduled_.end(),
                                 [](auto &a, auto &b) { return a.second < b.second; })->second;
    auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(next - Clock::now()).count();
    return static_cast<int>(std::max<long long>(0, wait));
}

void Server::runChecks(bool all) {
    auto now = Clock::now();
    std::vector<std::string> due;
    for (auto &pair : scheduled_) {
        if (all || pair.second <= now) {
            due.push_back(pair.first);
        }
    }
    for (auto &root : due) {
        scheduled_.erase(root);
        check(root);
    }
}

void Server::check(const std::string &root) {
    auto analysis = checker().check(root);
    publishDiagnostics(analysis);
    if (analysis.analysed) {
        parsedAnalyses_.erase(root);  // The latest analysis is now the last one that parsed.
    }
    else if (analyses_.count(root) > 0 && analyses_[root].analysed) {
        parsedAnalyses_[root] = std::move(analyses_[root]);
    }
    analyses_[root] = std::move(analysis);
}

std::string Server::uriForPath(const std::string &path) const {
    auto document = documents_.find(path);
    return document != documents_.end() ? document->second.uri : pathToUri(path);
}

const SourceText& Server::sourceText(const std::string &path) {
    auto &text = sourceTexts_[path];
    if (text == nullptr) {
        text = std::make_unique<SourceText>(checker().read(path));
    }
    return *text;
}

rapidjson::Value Server::range(const Location &location, rapidjson::Document::AllocatorType &allocator) {
    auto &source = sourceText(location.path);
    auto &lines = source.lines;
    auto line = location.line > 0 ? location.line - 1 : 0;
    auto character = location.character > 0 ? location.character - 1 : 0;
    auto startOffset = lines.offset(line, character);
    auto token = tokenStartingAt(source.tokens, startOffset);
    auto endOffset = token != nullptr ? token->end : std::min(startOffset + 1, lines.offset(line, SIZE_MAX));
    if (endOffset == startOffset && startOffset > 0) {
        startOffset--;  // E.g. an unexpected end of file: the range covers the last character so that it is visible.
    }
    return range(source, startOffset, endOffset, allocator);
}

rapidjson::Value Server::range(const SourceText &source, size_t start, size_t end,
                               rapidjson::Document::AllocatorType &allocator) {
    auto toJson = [&](size_t offset) {
        auto position = source.lines.lineAndCharacter(offset);
        auto client = source.lines.toClient(position.first, position.second, encoding_);
        Value json(rapidjson::kObjectType);
        json.AddMember("line", static_cast<uint64_t>(client.line), allocator);
        json.AddMember("character", static_cast<uint64_t>(client.character), allocator);
        return json;
    };
    Value range(rapidjson::kObjectType);
    range.AddMember("start", toJson(start), allocator);
    range.AddMember("end", toJson(end), allocator);
    return range;
}

rapidjson::Value Server::locationJson(const Location &location, rapidjson::Document::AllocatorType &allocator) {
    Value json(rapidjson::kObjectType);
    json.AddMember("uri", jsonString(uriForPath(location.path), allocator), allocator);
    json.AddMember("range", range(location, allocator), allocator);
    return json;
}

std::optional<std::pair<std::string, size_t>> Server::documentPosition(const Value &params) {
    auto path = uriToPath(string(member(params, "textDocument"), "uri"));
    auto &position = member(params, "position");
    auto &line = member(position, "line");
    auto &character = member(position, "character");
    if (documents_.count(path) == 0 || !line.IsUint() || !character.IsUint()) {
        return std::nullopt;
    }
    analysisFor(path);  // Checks pending changes, so that the text and the analysis match.
    auto &source = sourceText(path);
    auto codePoint = source.lines.toCodePoint(ClientPosition{line.GetUint(), character.GetUint()}, encoding_);
    return std::make_pair(path, source.lines.offset(line.GetUint(), codePoint));
}

const Analysis* Server::analysisFor(const std::string &path) {
    auto root = roots_.find(path);
    if (root == roots_.end()) {
        return nullptr;
    }
    if (scheduled_.count(root->second) > 0) {
        scheduled_.erase(root->second);
        check(root->second);
    }
    auto analysis = analyses_.find(root->second);
    return analysis == analyses_.end() ? nullptr : &analysis->second;
}

Navigator Server::navigator(const Analysis &analysis, const std::string &path) {
    return Navigator(analysis, path, [this](const std::string &path) -> const SourceText& { return sourceText(path); });
}

void Server::hover(const Value &id, const Value &params) {
    rapidjson::Document document;
    auto &allocator = document.GetAllocator();
    Value result;
    auto position = documentPosition(params);
    auto analysis = position ? analysisFor(position->first) : nullptr;
    if (analysis != nullptr) {
        auto navigator = this->navigator(*analysis, position->first);
        if (auto symbol = navigator.symbolAt(position->second)) {
            result.SetObject();
            Value contents(rapidjson::kObjectType);
            contents.AddMember("kind", "markdown", allocator);
            contents.AddMember("value", jsonString(symbol->first.hover, allocator), allocator);
            result.AddMember("contents", contents, allocator);
            result.AddMember("range", range(navigator.source(), symbol->second->start, symbol->second->end, allocator),
                             allocator);
        }
    }
    respond(id, result, document);
}

void Server::definition(const Value &id, const Value &params) {
    rapidjson::Document document;
    auto &allocator = document.GetAllocator();
    Value result;
    auto position = documentPosition(params);
    auto analysis = position ? analysisFor(position->first) : nullptr;
    if (analysis != nullptr) {
        auto symbol = navigator(*analysis, position->first).symbolAt(position->second);
        if (symbol && symbol->first.declaration) {
            result = locationJson(*symbol->first.declaration, allocator);
        }
    }
    respond(id, result, document);
}

void Server::publishDiagnostics(const Analysis &analysis) {
    sourceTexts_.clear();  // Files may have changed since the last publication.
    std::map<std::string, std::vector<const Diagnostic *>> byPath;
    for (auto &diagnostic : analysis.diagnostics) {
        byPath[diagnostic.location.path].push_back(&diagnostic);
    }
    // Open files of the package and files that had diagnostics before get a list even if it is empty: it tells the
    // client that the file was checked and clears the old diagnostics.
    for (auto &pair : roots_) {
        if (pair.second == analysis.rootPath) {
            byPath[pair.first];
        }
    }
    auto &published = published_[analysis.rootPath];
    for (auto &path : published) {
        byPath[path];
    }
    published.clear();

    for (auto &pair : byPath) {
        rapidjson::Document document;
        auto &allocator = document.GetAllocator();
        Value diagnostics(rapidjson::kArrayType);
        for (auto diagnostic : pair.second) {
            Value json(rapidjson::kObjectType);
            json.AddMember("range", range(diagnostic->location, allocator), allocator);
            json.AddMember("severity", static_cast<int>(diagnostic->severity), allocator);
            json.AddMember("source", "emojicode", allocator);
            json.AddMember("message", jsonString(diagnostic->message, allocator), allocator);
            if (!diagnostic->notes.empty()) {
                Value related(rapidjson::kArrayType);
                for (auto &note : diagnostic->notes) {
                    Value location(rapidjson::kObjectType);
                    location.AddMember("uri", jsonString(uriForPath(note.first.path), allocator), allocator);
                    location.AddMember("range", range(note.first, allocator), allocator);
                    Value information(rapidjson::kObjectType);
                    information.AddMember("location", location, allocator);
                    information.AddMember("message", jsonString(note.second, allocator), allocator);
                    related.PushBack(information, allocator);
                }
                json.AddMember("relatedInformation", related, allocator);
            }
            diagnostics.PushBack(json, allocator);
        }
        if (!pair.second.empty()) {
            published.insert(pair.first);
        }

        Value params(rapidjson::kObjectType);
        params.AddMember("uri", jsonString(uriForPath(pair.first), allocator), allocator);
        auto open = documents_.find(pair.first);
        if (open != documents_.end()) {
            params.AddMember("version", open->second.version, allocator);
        }
        params.AddMember("diagnostics", diagnostics, allocator);
        notify("textDocument/publishDiagnostics", params, document);
    }
}

void Server::semanticTokens(const Value &id, const Value &params) {
    rapidjson::Document document;
    auto &allocator = document.GetAllocator();
    Value result(rapidjson::kObjectType);
    Value data(rapidjson::kArrayType);
    auto path = uriToPath(string(member(params, "textDocument"), "uri"));
    auto analysis = documents_.count(path) > 0 ? analysisFor(path) : nullptr;
    if (analysis != nullptr) {
        for (auto value : EmojicodeLanguageServer::semanticTokens(navigator(*analysis, path), encoding_)) {
            data.PushBack(value, allocator);
        }
    }
    result.AddMember("data", data, allocator);
    respond(id, result, document);
}

/// Returns the LSP SymbolKind for @p kind.
static int symbolKind(Symbol::Kind kind) {
    switch (kind) {
        case Symbol::Kind::Class: return 5;
        case Symbol::Kind::Method: return 6;
        case Symbol::Kind::TypeMethod: return 6;
        case Symbol::Kind::InstanceVariable: return 7;
        case Symbol::Kind::Initializer: return 9;
        case Symbol::Kind::Enum: return 10;
        case Symbol::Kind::Protocol: return 11;
        case Symbol::Kind::Variable: return 13;
        case Symbol::Kind::ValueType: return 23;
        case Symbol::Kind::OtherType: return 26;
        case Symbol::Kind::Expression: return 13;
    }
    return 13;
}

void Server::documentSymbols(const Value &id, const Value &params) {
    rapidjson::Document document;
    auto &allocator = document.GetAllocator();
    Value result(rapidjson::kArrayType);
    auto path = uriToPath(string(member(params, "textDocument"), "uri"));
    auto analysis = documents_.count(path) > 0 ? analysisFor(path) : nullptr;
    if (analysis != nullptr) {
        std::function<Value (const OutlineEntry &)> toJson = [&](const OutlineEntry &entry) {
            Value json(rapidjson::kObjectType);
            json.AddMember("name", jsonString(utf8(entry.name), allocator), allocator);
            json.AddMember("kind", symbolKind(entry.kind), allocator);
            json.AddMember("range", range(entry.location, allocator), allocator);
            json.AddMember("selectionRange", range(entry.location, allocator), allocator);
            Value children(rapidjson::kArrayType);
            for (auto &child : entry.children) {
                children.PushBack(toJson(child), allocator);
            }
            json.AddMember("children", children, allocator);
            return json;
        };
        auto navigator = this->navigator(*analysis, path);
        for (auto &entry : navigator.outline()) {
            result.PushBack(toJson(entry), allocator);
        }
    }
    respond(id, result, document);
}

void Server::completion(const Value &id, const Value &params) {
    rapidjson::Document document;
    auto &allocator = document.GetAllocator();
    Value items(rapidjson::kArrayType);
    auto position = documentPosition(params);
    if (position) {
        auto &path = position->first;
        auto analysis = analysisFor(path);
        if (analysis != nullptr && !analysis->analysed) {
            auto parsed = parsedAnalyses_.find(roots_[path]);
            if (parsed != parsedAnalyses_.end()) {
                analysis = &parsed->second;
            }
        }
        auto &source = sourceText(path);
        Completer completer(analysis, path, source, snippetSupport_);
        if (completer.canComplete(position->second)) {
            auto start = completer.wordStart(position->second);
            auto word = utf8(std::u32string_view(source.text).substr(start, position->second - start));
            size_t index = 0;
            for (auto &item : completer.complete(start, position->second, 200)) {
                Value json(rapidjson::kObjectType);
                json.AddMember("label", jsonString(item.label, allocator), allocator);
                json.AddMember("kind", item.kind, allocator);
                if (!item.detail.empty()) {
                    json.AddMember("detail", jsonString(item.detail, allocator), allocator);
                }
                if (!item.documentation.empty()) {
                    Value documentation(rapidjson::kObjectType);
                    documentation.AddMember("kind", "markdown", allocator);
                    documentation.AddMember("value", jsonString(item.documentation, allocator), allocator);
                    json.AddMember("documentation", documentation, allocator);
                }
                // The items are already filtered and sorted: the typed word describes the item, it does not start it.
                json.AddMember("filterText", jsonString(word, allocator), allocator);
                char sortText[16];
                snprintf(sortText, sizeof(sortText), "%05zu", index++);
                json.AddMember("sortText", Value(sortText, allocator), allocator);
                Value edit(rapidjson::kObjectType);
                edit.AddMember("range", range(source, start, position->second, allocator), allocator);
                edit.AddMember("newText", jsonString(item.insertText, allocator), allocator);
                json.AddMember("textEdit", edit, allocator);
                if (item.isSnippet) {
                    json.AddMember("insertTextFormat", 2, allocator);
                }
                items.PushBack(json, allocator);
            }
        }
    }
    Value result(rapidjson::kObjectType);
    // Incomplete, so that the client asks again as the word grows: the server matches it in ways the client cannot.
    result.AddMember("isIncomplete", true, allocator);
    result.AddMember("items", items, allocator);
    respond(id, result, document);
}

void Server::respond(const Value &id, Value &result, rapidjson::Document &document) {
    auto &allocator = document.GetAllocator();
    Value message(rapidjson::kObjectType);
    message.AddMember("jsonrpc", "2.0", allocator);
    message.AddMember("id", Value(id, allocator), allocator);
    message.AddMember("result", result, allocator);
    transport_->write(message);
}

void Server::respondError(const Value &id, int code, const std::string &text) {
    rapidjson::Document document;
    auto &allocator = document.GetAllocator();
    Value error(rapidjson::kObjectType);
    error.AddMember("code", code, allocator);
    error.AddMember("message", jsonString(text, allocator), allocator);
    Value message(rapidjson::kObjectType);
    message.AddMember("jsonrpc", "2.0", allocator);
    message.AddMember("id", Value(id, allocator), allocator);
    message.AddMember("error", error, allocator);
    transport_->write(message);
}

void Server::notify(const std::string &method, Value &params, rapidjson::Document &document) {
    auto &allocator = document.GetAllocator();
    Value message(rapidjson::kObjectType);
    message.AddMember("jsonrpc", "2.0", allocator);
    message.AddMember("method", jsonString(method, allocator), allocator);
    message.AddMember("params", params, allocator);
    transport_->write(message);
}

}  // namespace EmojicodeLanguageServer

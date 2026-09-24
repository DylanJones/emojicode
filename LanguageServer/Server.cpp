//
//  Server.cpp
//  EmojicodeLanguageServer
//

#include "Server.hpp"
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
    auto end = lines.lineAndCharacter(endOffset);

    auto toJson = [&](ClientPosition position) {
        Value json(rapidjson::kObjectType);
        json.AddMember("line", static_cast<uint64_t>(position.line), allocator);
        json.AddMember("character", static_cast<uint64_t>(position.character), allocator);
        return json;
    };
    Value range(rapidjson::kObjectType);
    range.AddMember("start", toJson(lines.toClient(line, character, encoding_)), allocator);
    range.AddMember("end", toJson(lines.toClient(end.first, end.second, encoding_)), allocator);
    return range;
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

//
//  Server.hpp
//  EmojicodeLanguageServer
//

#ifndef Server_hpp
#define Server_hpp

#include "Checker.hpp"
#include "JsonRpc.hpp"
#include "Navigation.hpp"
#include "Positions.hpp"
#include "Tokens.hpp"
#include "Utils/rapidjson/document.h"
#include <chrono>
#include <optional>
#include <map>
#include <set>
#include <string>

namespace EmojicodeLanguageServer {

using Clock = std::chrono::steady_clock;

/// A file open in the editor.
struct Document {
    std::string uri;
    std::string path;
    int version = 0;
};

/// The language server: answers the client's requests and publishes diagnostics.
class Server {
public:
    /// @param searchPaths Package search paths to use before those the client provides.
    Server(Transport *transport, std::vector<std::string> searchPaths)
        : transport_(transport), searchPaths_(std::move(searchPaths)) {}

    /// Handles messages until the client sends exit or closes the connection.
    /// @returns The exit code of the server.
    int run();

private:
    void handle(const rapidjson::Document &message);
    void handleRequest(const std::string &method, const rapidjson::Value &id, const rapidjson::Value &params);
    void handleNotification(const std::string &method, const rapidjson::Value &params);

    void initialize(const rapidjson::Value &id, const rapidjson::Value &params);
    void didOpen(const rapidjson::Value &params);
    void didChange(const rapidjson::Value &params);
    void didSave(const rapidjson::Value &params);
    void didClose(const rapidjson::Value &params);
    void hover(const rapidjson::Value &id, const rapidjson::Value &params);
    void definition(const rapidjson::Value &id, const rapidjson::Value &params);
    void semanticTokens(const rapidjson::Value &id, const rapidjson::Value &params);
    void documentSymbols(const rapidjson::Value &id, const rapidjson::Value &params);
    void completion(const rapidjson::Value &id, const rapidjson::Value &params);

    /// Returns the canonical path of the document in @p params and the code point offset of the position in it.
    /// Checks the document's package first if it has pending changes.
    std::optional<std::pair<std::string, size_t>> documentPosition(const rapidjson::Value &params);
    Navigator navigator(const Analysis &analysis, const std::string &path);
    /// Returns the latest analysis of the package that contains the open document at @p path, or nullptr.
    const Analysis* analysisFor(const std::string &path);

    void respond(const rapidjson::Value &id, rapidjson::Value &result, rapidjson::Document &document);
    void respondError(const rapidjson::Value &id, int code, const std::string &message);
    void notify(const std::string &method, rapidjson::Value &params, rapidjson::Document &document);

    /// Checks the package with the main file @p root in @p delay, or earlier if it was already scheduled.
    void schedule(const std::string &root, std::chrono::milliseconds delay);
    /// Checks the packages whose time has come, or all scheduled packages if @p all is true.
    void runChecks(bool all);
    /// Returns how long until the next scheduled check, or -1 if none is scheduled.
    int millisecondsToNextCheck() const;
    void check(const std::string &root);
    void publishDiagnostics(const Analysis &analysis);

    /// Returns the root file of the open document at @p path and schedules a check of it if the root changed.
    std::string updateRoot(const std::string &path);

    /// Returns the text of the file at @p path. The result is cached until the next call of clearSourceTexts().
    const SourceText& sourceText(const std::string &path);
    /// Returns the LSP range of the token at @p location.
    rapidjson::Value range(const Location &location, rapidjson::Document::AllocatorType &allocator);
    /// Returns the LSP range from code point offset @p start to @p end.
    rapidjson::Value range(const SourceText &source, size_t start, size_t end,
                           rapidjson::Document::AllocatorType &allocator);
    rapidjson::Value locationJson(const Location &location, rapidjson::Document::AllocatorType &allocator);
    std::string uriForPath(const std::string &path) const;

    Checker checker() const;

    Transport *transport_;
    std::vector<std::string> searchPaths_;
    PositionEncoding encoding_ = PositionEncoding::UTF16;
    bool shutdown_ = false;
    bool snippetSupport_ = false;

    /// Open documents by canonical path.
    std::map<std::string, Document> documents_;
    /// The content of the open documents by canonical path.
    std::map<std::string, std::u32string> overlays_;
    /// The root file of each open document.
    std::map<std::string, std::string> roots_;
    /// The last analysis of each root file.
    std::map<std::string, Analysis> analyses_;
    /// The last analysis of each root file that parsed, which completion uses while the code being typed does not.
    std::map<std::string, Analysis> parsedAnalyses_;
    /// The files for which diagnostics were published from each root file.
    std::map<std::string, std::set<std::string>> published_;
    /// When each root file with pending changes is to be checked.
    std::map<std::string, Clock::time_point> scheduled_;
    std::map<std::string, std::unique_ptr<SourceText>> sourceTexts_;
};

}  // namespace EmojicodeLanguageServer

#endif /* Server_hpp */

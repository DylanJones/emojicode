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
    int version = 0;
    /// The files the document includes with 📜, which determine the root files of the open documents.
    std::vector<std::string> includes;
};

/// A request that is answered once the package it is about was checked.
struct DeferredRequest {
    std::string method;
    std::unique_ptr<rapidjson::Document> id;
    std::unique_ptr<rapidjson::Document> params;
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
    /// Answers emojicode/docs, a request with the parameters of a hover: where the package documentation describes
    /// the symbol at the position, as {path, range}, or null.
    void docs(const rapidjson::Value &id, const rapidjson::Value &params);
    void semanticTokens(const rapidjson::Value &id, const rapidjson::Value &params);
    void documentSymbols(const rapidjson::Value &id, const rapidjson::Value &params);
    void completion(const rapidjson::Value &id, const rapidjson::Value &params);

    /// Returns the canonical path of the document in @p params and the code point offset of the position in it.
    /// If @p checkPending is true, the document's package is checked first if it has pending changes.
    std::optional<std::pair<std::string, size_t>> documentPosition(const rapidjson::Value &params,
                                                                   bool checkPending = true);
    Navigator navigator(const Analysis &analysis, const std::string &path, bool describe = true);
    /// Returns the latest analysis of the package that contains the open document at @p path, or nullptr. If
    /// @p checkPending is true, the package is checked first if it has pending changes.
    const Analysis* analysisFor(const std::string &path, bool checkPending = true);
    /// Returns the last analysis of the package that contains the open document at @p path that analysed the function
    /// bodies, if @p analysis, its latest analysis, did not. Otherwise returns nullptr.
    const Analysis* previousAnalysis(const std::string &path, const Analysis *analysis);

    void respond(const rapidjson::Value &id, rapidjson::Value &result, rapidjson::Document &document);
    void respondError(const rapidjson::Value &id, int code, const std::string &message);
    void notify(const std::string &method, rapidjson::Value &params, rapidjson::Document &document);

    /// Checks the package with the main file @p root in @p delay, or earlier if it was already scheduled.
    void schedule(const std::string &root, std::chrono::milliseconds delay);
    /// Checks the packages whose time has come.
    void runChecks();
    /// Returns how long until the next scheduled check, or -1 if none is scheduled.
    int millisecondsToNextCheck() const;
    void check(const std::string &root);
    /// Publishes the diagnostics of the files that the latest analysis of @p root has or had diagnostics for, and of
    /// its open files.
    void publishDiagnostics(const std::string &root);
    /// Publishes the diagnostics of each file in @p paths. Those of an open file are those of the analysis of its
    /// root; the client keeps one list per file, so the analysis of another root that includes it must not replace
    /// them. Those of other files are those of all analyses.
    void publishDiagnostics(const std::set<std::string> &paths);

    /// Finds the root file of every open document again, e.g. after an include was added. Roots that changed are
    /// checked, and roots that no open document belongs to anymore are dropped.
    void updateRoots();
    /// Schedules the check of each root file whose package contains the file at @p path in @p delay.
    void scheduleRootsContaining(const std::string &path, std::chrono::milliseconds delay);
    bool isRootOpen(const std::string &root) const;
    /// Forgets the analyses of @p root and removes its diagnostics.
    void dropRoot(const std::string &root);

    /// If the package of the document in @p params has a pending check, stores the request to be answered after
    /// the check and returns true.
    bool defer(const std::string &method, const rapidjson::Value &id, const rapidjson::Value &params);
    /// Answers the requests deferred until @p root was checked.
    void answerDeferred(const std::string &root);

    /// Returns the text of the file at @p path. The result is cached until the file changes in the editor or
    /// diagnostics are published, which reads files from disk again.
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
    /// The last analysis of each root file that analysed the function bodies, if the latest one did not. Completion and
    /// semantic tokens use it while the code being typed has errors that stop the analysis.
    std::map<std::string, Analysis> parsedAnalyses_;
    /// The files for which the latest analysis of each root file has diagnostics.
    std::map<std::string, std::set<std::string>> published_;
    /// When each root file with pending changes is to be checked.
    std::map<std::string, Clock::time_point> scheduled_;
    std::map<std::string, std::unique_ptr<SourceText>> sourceTexts_;
    /// Requests waiting for the check of a root file.
    std::map<std::string, std::vector<DeferredRequest>> deferred_;
};

}  // namespace EmojicodeLanguageServer

#endif /* Server_hpp */

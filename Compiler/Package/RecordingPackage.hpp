//
//  RecordingPackage.hpp
//  EmojicodeCompiler
//
//  Created by Theo Weidmann on 26/08/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#ifndef RecordingPackage_hpp
#define RecordingPackage_hpp

#include "Lex/SourcePosition.hpp"
#include "Package.hpp"
#include <memory>

namespace EmojicodeCompiler {

/// The RecordingPackage class records all packages that it’s imported as well as all types offered that were
/// not offered as a result of a package import.
class RecordingPackage : public Package {
    using Package::Package;
public:
    class Recording {
    public:
        explicit Recording(const SourcePosition &p = SourcePosition()) : position_(p) {}
        virtual ~Recording() = default;
        /// Where the recorded declaration starts in its source file. Unknown if it has no position.
        const SourcePosition& position() const { return position_; }
    private:
        SourcePosition position_;
    };

    class Import : public Recording {
    public:
        Import(std::string pkg, std::u32string ns, const SourcePosition &p)
        : Recording(p), package(std::move(pkg)), destNamespace(std::move(ns)) {}
        std::string package;
        std::u32string destNamespace;
    };

    class Include : public Recording {
    public:
        Include(std::string path, const SourcePosition &p) : Recording(p), path_(std::move(path)) {}
        std::string path_;
    };

    class RecordedType : public Recording {
    public:
        explicit RecordedType(Type type) : type_(std::move(type)) {}
        Type type_;
    };

    class StartFlagFunctionRecording : public Recording {};

    class LinkHintsRecording : public Recording {
    public:
        using Recording::Recording;
    };

    class DocumentationRecording : public Recording {
    public:
        DocumentationRecording(std::u32string documentation, const SourcePosition &p)
        : Recording(p), documentation_(std::move(documentation)) {}
        std::u32string documentation_;
    };

    struct File {
        explicit File(std::string path) : path_(std::move(path)) {}
        std::string path_;
        std::vector<std::unique_ptr<Recording>> recordings_;
    };

    const std::vector<File>& files() const { return files_; }

    void importPackage(const std::string &name, const std::u32string &ns, const SourcePosition &p) override;
    void offerType(Type t, const std::u32string &name, const std::u32string &ns, bool exportFromPkg,
                           const SourcePosition &p) override;
    void includeDocument(const std::string &path, const std::string &relativePath,
                         const SourcePosition &p) override;
    void setDocumentation(const std::u32string &doc, const SourcePosition &p) override;
    void setStartFlagFunction(Function *function) override;
    void setLinkHints(std::vector<std::string> hints, const SourcePosition &p) override;
private:
    std::vector<File> files_;
    size_t currentFile_ = 0;
};

}  // namespace EmojicodeCompiler

#endif /* RecordingPackage_hpp */

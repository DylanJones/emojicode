//
//  RecordingPackage.cpp
//  EmojicodeCompiler
//
//  Created by Theo Weidmann on 26/08/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "RecordingPackage.hpp"
#include "Types/TypeDefinition.hpp"

namespace EmojicodeCompiler {

void RecordingPackage::importPackage(const std::string &name, const std::u32string &ns, const SourcePosition &p) {
    if (name != "s" || ns != kDefaultNamespace) {
        files_[currentFile_].recordings_.emplace_back(std::make_unique<Import>(name, ns, p));
    }
    Package::importPackage(name, ns, p);
}

void RecordingPackage::offerType(Type t, const std::u32string &name, const std::u32string &ns, bool exportFromPkg,
                                 const SourcePosition &p) {
    if (exportFromPkg || t.typeDefinition()->package() == this) {
        files_[currentFile_].recordings_.emplace_back(std::make_unique<RecordedType>(t));
    }
    Package::offerType(std::move(t), name, ns, exportFromPkg, p);
}

void RecordingPackage::includeDocument(const std::string &path, const std::string &relativePath,
                                       const SourcePosition &p) {
    auto temp = currentFile_;
    if (!files_.empty()) {
        files_[currentFile_].recordings_.emplace_back(std::make_unique<Include>(relativePath, p));
    }
    currentFile_ = files_.size();
    files_.emplace_back(path);
    Package::includeDocument(path, relativePath, p);
    currentFile_ = temp;
}

void RecordingPackage::setDocumentation(const std::u32string &doc, const SourcePosition &p) {
    files_[currentFile_].recordings_.emplace_back(std::make_unique<DocumentationRecording>(doc, p));
    Package::setDocumentation(doc, p);
}

void RecordingPackage::setStartFlagFunction(Function *function) {
    files_[currentFile_].recordings_.emplace_back(std::make_unique<StartFlagFunctionRecording>());
    Package::setStartFlagFunction(function);
}

void RecordingPackage::setLinkHints(std::vector<std::string> hints, const SourcePosition &p) {
    files_[currentFile_].recordings_.emplace_back(std::make_unique<LinkHintsRecording>(p));
    Package::setLinkHints(std::move(hints), p);
}

}  // namespace EmojicodeCompiler

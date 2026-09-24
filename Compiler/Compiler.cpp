//
//  Compiler.cpp
//  EmojicodeCompiler
//
//  Created by Theo Weidmann on 24/08/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "CompilerError.hpp"
#include "Analysis/SemanticAnalyser.hpp"
#include "Compiler.hpp"
#include "Generation/CodeGenerator.hpp"
#include "Package/RecordingPackage.hpp"
#include "Parsing/AbstractParser.hpp"
#include "Prettyprint/PrettyPrinter.hpp"
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/StringSaver.h>
#include "MemoryFlowAnalysis/MFAnalyser.hpp"
#include "Types/ValueType.hpp"
#include "Functions/Function.hpp"
#include <utility>

namespace EmojicodeCompiler {

Compiler::Compiler(std::string mainPackage, std::string mainFile, std::vector<std::string> pkgSearchPaths,
                   std::unique_ptr<CompilerDelegate> delegate)
        : mainFile_(std::move(mainFile)), packageSearchPaths_(std::move(pkgSearchPaths)),
          delegate_(std::move(delegate)),
          mainPackage_(std::make_unique<RecordingPackage>(mainPackage, mainFile_, this, false)) {}

Compiler::~Compiler() = default;

bool Compiler::compile() {
    delegate_->begin();
    try {
        for (auto &phase : phases_) {
            phase->perform(this);
            if (hasError_) {
                break;
            }
        }
    }
    catch (CompilerError &ce) {
        error(ce);
    }
    delegate_->finish();
    return !hasError_;
}

void Compiler::ParsePhase::perform(Compiler *compiler) {
    compiler->mainPackage_->parse(compiler->mainFile_);
}

void Compiler::AnalysisPhase::perform(Compiler *compiler) {
    SemanticAnalyser(compiler->mainPackage(), false).analyse(standalone_);
    if (compiler->hasError_) return;
    MFAnalyser(compiler->mainPackage()).analyse();
}

void Compiler::PrintInterfacePhase::perform(Compiler *compiler) {
    PrettyPrinter(compiler->mainPackage()).printInterface(path_);
}

void Compiler::GenerationPhase::perform(Compiler *compiler) {
    assert(compiler->generator_ == nullptr);
    compiler->generator_ = std::make_unique<CodeGenerator>(compiler, optimize_);
    compiler->generator_->generate();
}

void Compiler::ObjectFileEmissionPhase::perform(Compiler *compiler) {
    assert(compiler->generator_ != nullptr && "ObjectFileEmissionPhase must be run after GenerationPhase");
    compiler->generator_->emit(false, path_);
}

void Compiler::LLVMIREmissionPhase::perform(Compiler *compiler) {
    assert(compiler->generator_ != nullptr && "LLVMIREmissionPhase must be run after GenerationPhase");
    compiler->generator_->emit(true, path_);
}

/// Runs @p tool with @p arguments and waits for it to finish.
/// @param tool The program to run. As it is usually taken from an environment variable like $CXX, it may contain
///             arguments of its own, e.g. "ccache c++".
/// @throws CompilerError if the tool could not be run or did not exit successfully.
static void runTool(const std::string &tool, const std::vector<std::string> &arguments) {
    llvm::BumpPtrAllocator allocator;
    llvm::StringSaver saver(allocator);
    llvm::SmallVector<const char *, 4> toolArgs;
    llvm::cl::TokenizeGNUCommandLine(tool, saver, toolArgs);
    if (toolArgs.empty()) {
        throw CompilerError(SourcePosition(), "No program to run was specified.");
    }

    std::vector<llvm::StringRef> args(toolArgs.begin(), toolArgs.end());
    args.insert(args.end(), arguments.begin(), arguments.end());

    auto program = llvm::sys::findProgramByName(args.front());
    if (!program) {
        throw CompilerError(SourcePosition(), "Could not find ", args.front().str(), ": ",
                            program.getError().message(), ".");
    }
    std::string errorMessage;
    auto status = llvm::sys::ExecuteAndWait(*program, args, std::nullopt, {}, 0, 0, &errorMessage);
    if (status < 0) {
        throw CompilerError(SourcePosition(), "Could not run ", tool, ": ", errorMessage, ".");
    }
    if (status > 0) {
        throw CompilerError(SourcePosition(), tool, " failed with exit code ", status, ".");
    }
}

void Compiler::LinkPhase::perform(Compiler *compiler) {
    std::vector<std::string> args { objectFilePath_ };

    for (auto &hint : compiler->mainPackage()->linkHints()) {
        args.emplace_back("-l" + hint);
    }

    for (auto it = compiler->packageImportOrder_.rbegin(); it != compiler->packageImportOrder_.rend(); it++) {
        auto package = *it;
        args.emplace_back(compiler->findBinaryPathPackage(package->path(), package->name()));
        for (auto &hint : package->linkHints()) {
            args.emplace_back("-l" + hint);
        }
    }

    auto runtimeLib = compiler->findBinaryPathPackage(compiler->searchPackage("runtime", SourcePosition()), "runtime");
    args.insert(args.end(), { runtimeLib, "-o", outPath_ });

    runTool(linker_, args);
}

void Compiler::ArchivePhase::perform(Compiler *compiler) {
    runTool(ar_, { "cr", outPath_, objectFilePath_ });
}

std::string Compiler::searchPackage(const std::string &name, const SourcePosition &p) {
    for (auto &path : packageSearchPaths_) {
        auto full = path + "/";
        full += name;
        if (llvm::sys::fs::is_directory(full)) {
            return full;
        }
    }

    auto ce = CompilerError(p, "Could not find package ", name, ".");
    std::string str = "Searched in:";
    for (auto &path : packageSearchPaths_) {
        str.append("\n").append(path);
    }
    ce.addNotes(SourcePosition(), str);
    throw ce;
}

std::string Compiler::findBinaryPathPackage(const std::string &packagePath, const std::string &packageName) {
    return packagePath + "/lib" + packageName + ".a";
}

Package *Compiler::findPackage(const std::string &name) const {
    auto it = packages_.find(name);
    return it != packages_.end() ? it->second.get() : nullptr;
}

Package *Compiler::loadPackage(const std::string &name, const SourcePosition &p, Package *requestor) {
    if (auto package = findPackage(name)) {
        if (!package->finishedLoading()) {
            throw CompilerError(p, "Circular dependency detected: ", requestor->name(), " and ", name,
                                " depend on each other.");
        }
        return package;
    }

    auto package = std::make_unique<Package>(name, searchPackage(name, p), this, true);
    auto rawPtr = package.get();
    packageImportOrder_.emplace_back(rawPtr);
    packages_.emplace(name, std::move(package));
    parseInterface(rawPtr, p);

    SemanticAnalyser(rawPtr, true).analyse(false);
    if (!hasError_) {
        MFAnalyser(rawPtr).analyse();
    }
    return rawPtr;
}

void Compiler::parseInterface(Package *pkg, const SourcePosition &p) {
    std::string emojiPath = pkg->path() + "/🏛", textPath = pkg->path() + "/interface.emojii";
    bool emojiExists = llvm::sys::fs::exists(emojiPath), textExists = llvm::sys::fs::exists(textPath);
    if (emojiExists && textExists) {
        throw CompilerError(p, "Package ", pkg->name(), " contains both a 🏛 file and interface.emojii.");
    }
    pkg->parse(textExists ? textPath : emojiPath);
}

void Compiler::error(const CompilerError &ce) {
    hasError_ = true;
    delegate_->error(this, ce);
}

void Compiler::warn(const SourcePosition &p, const std::string &warning) {
    delegate_->warn(this, warning, p);
}

Class *getStandardClass(const std::u32string &name, Package *_) {
    Type type = Type::noReturn();
    _->lookupRawType(TypeIdentifier(name, kDefaultNamespace, SourcePosition()), &type);
    if (type.type() != TypeType::Class) {
        throw CompilerError(SourcePosition(), "s package class ", utf8(name), " is missing.");
    }
    return type.klass();
}

Protocol *getStandardProtocol(const std::u32string &name, Package *_) {
    Type type = Type::noReturn();
    _->lookupRawType(TypeIdentifier(name, kDefaultNamespace,  SourcePosition()), &type);
    if (type.unboxedType() != TypeType::Protocol) {
        throw CompilerError(SourcePosition(), "s package protocol ", utf8(name), " is missing.");
    }
    return type.protocol();
}

ValueType *getStandardValueType(const std::u32string &name, Package *_) {
    Type type = Type::noReturn();
    _->lookupRawType(TypeIdentifier(name, kDefaultNamespace,  SourcePosition()), &type);
    if (type.type() != TypeType::ValueType) {
        throw CompilerError( SourcePosition(), "s package value type ", utf8(name), " is missing.");
    }
    return type.valueType();
}

void Compiler::assignSTypes(Package *s) {
    sBoolean = getStandardValueType(U"👌", s);
    sInteger = getStandardValueType(U"🔢", s);
    sInteger->constructibleFrom_ = TypeType::IntegerLiteral;
    sReal = getStandardValueType(std::u32string(1, E_HUNDRED_POINTS_SYMBOL), s);
    sReal->constructibleFrom_ = TypeType::IntegerLiteral;
    sMemory = getStandardValueType(U"🧠", s);
    sByte = getStandardValueType(U"💧", s);
    sByte->constructibleFrom_ = TypeType::IntegerLiteral;
    sWeak = getStandardValueType(U"📶", s);
    sString = getStandardClass(U"🔡", s);
    sError = getStandardClass(U"🚧", s);
    sList = getStandardValueType(U"🍨", s);
    sList->constructibleFrom_ = TypeType::ListLiteral;
    sDictionary = getStandardValueType(U"🍯", s);
    sDictionary->constructibleFrom_ = TypeType::DictionaryLiteral;

    sInterpolateable = getStandardProtocol(U"↘🔸🔡", s);
    sEnumerable = getStandardProtocol(
            std::u32string(1, E_CLOCKWISE_RIGHTWARDS_AND_LEFTWARDS_OPEN_CIRCLE_ARROWS_WITH_CIRCLED_ONE_OVERLAY), s);
}

} // namespace EmojicodeCompiler

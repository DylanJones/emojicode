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
#include "Generation/CTrampolineGenerator.hpp"
#include "Package/RecordingPackage.hpp"
#include "Parsing/AbstractParser.hpp"
#include "Prettyprint/PrettyPrinter.hpp"
#include <llvm/Support/CommandLine.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/StringSaver.h>
#include <llvm/Support/raw_ostream.h>
#include "MemoryFlowAnalysis/MFAnalyser.hpp"
#include "Types/Class.hpp"
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
/// @param tool The command to run, usually taken from an environment variable like $CXX. It is interpreted by the
///             shell, so it may contain arguments of its own (e.g. "ccache c++") and shell expansions. The arguments
///             are passed to it unchanged.
/// @throws CompilerError if the tool could not be run or did not exit successfully.
static void runTool(const std::string &tool, const std::vector<std::string> &arguments) {
    // sh -c 'TOOL "$@"' sh ARGUMENTS... : the arguments become positional parameters and are never re-split.
    std::string script = tool + " \"$@\"";
    std::vector<llvm::StringRef> args { "sh", "-c", script, "sh" };
    args.insert(args.end(), arguments.begin(), arguments.end());

    std::string errorMessage;
    auto status = llvm::sys::ExecuteAndWait("/bin/sh", args, std::nullopt, {}, 0, 0, &errorMessage);
    if (status == -1) {
        throw CompilerError(SourcePosition(), "Could not run ", tool, ": ", errorMessage, ".");
    }
    if (status < 0) {
        throw CompilerError(SourcePosition(), tool, " crashed: ", errorMessage, ".");
    }
    if (status > 0) {
        throw CompilerError(SourcePosition(), tool, " failed with exit code ", status, ".");
    }
}

/// Appends the linker arguments for a link hint. A hint is split at whitespace like a command line, so that hints
/// such as "ssl -lcrypto", which used to be split by the shell, keep working. The first word names a library
/// unless it starts with "-", so that flags like "-framework Foundation" or "-L/opt/lib" can be passed as well.
/// Native source hints are skipped: NativeCompilationPhase compiles them.
static void appendLinkHint(std::vector<std::string> &args, const std::string &hint) {
    if (Package::isNativeSourceHint(hint)) {
        return;
    }
    llvm::BumpPtrAllocator allocator;
    llvm::StringSaver saver(allocator);
    llvm::SmallVector<const char *, 4> tokens;
    llvm::cl::TokenizeGNUCommandLine(hint, saver, tokens);
    for (size_t i = 0; i < tokens.size(); i++) {
        if (i == 0 && tokens[i][0] != '-') {
            args.emplace_back(std::string("-l") + tokens[i]);
        }
        else {
            args.emplace_back(tokens[i]);
        }
    }
}

void Compiler::NativeCompilationPhase::perform(Compiler *compiler) {
    auto package = compiler->mainPackage();
    llvm::StringRef base = objectFilePath_;
    base.consume_back(".o");

    auto trampolines = generateCTrampolines(package);
    if (!trampolines.empty()) {
        auto source = (base + "_trampolines.c").str();
        auto object = (base + "_trampolines.o").str();
        std::error_code error;
        llvm::raw_fd_ostream stream(source, error);
        if (error) {
            throw CompilerError(SourcePosition(), "Could not write ", source, ": ", error.message());
        }
        stream << trampolines;
        stream.close();
        runTool(cc_, { "-c", "-O2", "-w", source, "-o", object });
        compiler->nativeObjects_.emplace_back(object);
    }

    size_t sourceIndex = 0;
    for (auto &hint : package->linkHints()) {
        if (!Package::isNativeSourceHint(hint)) {
            continue;
        }
        llvm::SmallString<128> source;
        if (!llvm::sys::path::is_absolute(hint)) {
            source = package->linkHintsDirectory();
        }
        llvm::sys::path::append(source, hint);
        if (!llvm::sys::fs::exists(source)) {
            throw CompilerError(SourcePosition(), "Native source ", std::string(source), " does not exist.");
        }

        auto extension = llvm::sys::path::extension(source);
        if (extension == ".o") {
            compiler->nativeObjects_.emplace_back(source.str());
            continue;
        }
        // The index keeps sources with the same name in different directories (or x.c and x.cpp) apart.
        auto object = (base + "_" + std::to_string(sourceIndex++) + "_" + llvm::sys::path::stem(source) + ".o").str();
        auto tool = extension == ".c" || extension == ".m" ? cc_ : cxx_;
        runTool(tool, { "-c", "-O2", std::string(source), "-o", object });
        compiler->nativeObjects_.emplace_back(object);
    }

    // Only LinkPhase and ArchivePhase add the native objects. Otherwise the requested object file must contain them.
    if (mergeIntoObject_ && !compiler->nativeObjects_.empty()) {
        auto merged = (base + "_merged.o").str();
        std::vector<std::string> args { "-r", "-nostdlib", "-o", merged, objectFilePath_ };
        args.insert(args.end(), compiler->nativeObjects_.begin(), compiler->nativeObjects_.end());
        runTool(cxx_, args);
        if (auto error = llvm::sys::fs::rename(merged, objectFilePath_)) {
            throw CompilerError(SourcePosition(), "Could not write ", objectFilePath_, ": ", error.message());
        }
        compiler->nativeObjects_.clear();
    }
}

void Compiler::LinkPhase::perform(Compiler *compiler) {
    std::vector<std::string> args { objectFilePath_ };
    args.insert(args.end(), compiler->nativeObjects_.begin(), compiler->nativeObjects_.end());

    for (auto &hint : compiler->mainPackage()->linkHints()) {
        appendLinkHint(args, hint);
    }

    for (auto it = compiler->packageImportOrder_.rbegin(); it != compiler->packageImportOrder_.rend(); it++) {
        auto package = *it;
        args.emplace_back(compiler->findBinaryPathPackage(package->path(), package->name()));
        for (auto &hint : package->linkHints()) {
            appendLinkHint(args, hint);
        }
    }

    auto runtimeLib = compiler->findBinaryPathPackage(compiler->searchPackage("runtime", SourcePosition()), "runtime");
    args.insert(args.end(), { runtimeLib, "-o", outPath_ });

    runTool(linker_, args);
}

void Compiler::ArchivePhase::perform(Compiler *compiler) {
    std::vector<std::string> args { "cr", outPath_, objectFilePath_ };
    args.insert(args.end(), compiler->nativeObjects_.begin(), compiler->nativeObjects_.end());
    runTool(ar_, args);
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
    if (trapsErrors_) {
        throw TrappedError();
    }
    hasError_ = true;
    delegate_->error(this, ce);
}

void Compiler::warn(const SourcePosition &p, const std::string &warning) {
    if (trapsErrors_) {
        return;
    }
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
    sInteger->setCRepresentation(CRepresentation(CRepresentation::Kind::Integer, 64, true));
    sReal->setCRepresentation(CRepresentation(CRepresentation::Kind::Float, 64, true));
    sByte->setCRepresentation(CRepresentation(CRepresentation::Kind::Integer, 8, true));
    sBoolean->setCRepresentation(CRepresentation(CRepresentation::Kind::Integer, 1, false));
    sWeak = getStandardValueType(U"📶", s);
    sString = getStandardClass(U"🔡", s);
    sError = getStandardClass(U"🚧", s);
    sList = getStandardValueType(U"🍨", s);
    sList->constructibleFrom_ = TypeType::ListLiteral;
    sDictionary = getStandardValueType(U"🍯", s);
    sDictionary->constructibleFrom_ = TypeType::DictionaryLiteral;

    // 🤯 aborts the program, so code after a call to it is never executed.
    for (auto function : getStandardClass(U"💻", s)->typeMethods().list()) {
        if (function->name() == U"🤯") {
            function->setNeverReturns();
        }
    }

    sInterpolateable = getStandardProtocol(U"↘🔸🔡", s);
    sEnumerable = getStandardProtocol(
            std::u32string(1, E_CLOCKWISE_RIGHTWARDS_AND_LEFTWARDS_OPEN_CIRCLE_ARROWS_WITH_CIRCLED_ONE_OVERLAY), s);
}

void Compiler::assignCTypes(Package *c) {
    Type type = Type::noReturn();
    if (c->lookupRawType(TypeIdentifier(U"📍", U"🌊", SourcePosition()), &type) &&
        type.type() == TypeType::ValueType) {
        cPointer = type.valueType();
    }
    if (c->lookupRawType(TypeIdentifier(U"🕳", U"🌊", SourcePosition()), &type) &&
        type.type() == TypeType::ValueType) {
        cVoidPointer = type.valueType();
    }
}

} // namespace EmojicodeCompiler

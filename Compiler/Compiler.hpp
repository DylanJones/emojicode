//
//  Compiler.hpp
//  EmojicodeCompiler
//
//  Created by Theo Weidmann on 24/08/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#ifndef Compiler_hpp
#define Compiler_hpp

#include "Utils/StringUtils.hpp"
#include "Lex/SourceManager.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

/// The main namespace of the Emojicode Compiler. It contains everything releated to compilation.
namespace EmojicodeCompiler {

class CompilerError;
class RecordingPackage;
class Package;
class Function;
struct SourcePosition;
class Class;
class Protocol;
class ValueType;
class Compiler;
class CodeGenerator;
class AnalysisObserver;

/// CompilerDelegate is an interface class, which is used by Compiler to notify about certain events, like
/// compiler errors.
class CompilerDelegate {
public:
    /// Called when the compilation begins, i.e. when Compiler::compile was called.
    virtual void begin() = 0;
    /// A compiler error occured.
    /// @param p The location at which the error occurred.
    /// @param message A string message describing the error.
    virtual void error(Compiler *compiler, const CompilerError &ce) = 0;
    /// A compiler warning has been issued.
    /// @param p The location at which the warning was issued.
    /// @param message A string message describing the warning.
    virtual void warn(Compiler *compiler, const std::string &message, const SourcePosition &p) = 0;
    /// Called when the compilation stops, i.e. just before Compiler::compile returns.
    virtual void finish() = 0;

    virtual ~CompilerDelegate() = default;
};

/// The Compiler manages the compilation.
///
/// The Compiler class is highly configurable and its steps are described by instance of Phase. To compile a package
/// an instance of Compiler must be created and appropriate phases must be added. Normally, each instance requires at
/// least ParsePhase.
class Compiler final {
public:
    /// Abstract class representing compiler phase.
    class Phase {
    public:
        virtual void perform(Compiler *compiler) = 0;
        virtual ~Phase() = default;
    };

    /// Parses the main package.
    class ParsePhase final : public Phase {
        void perform(Compiler *compiler) override;
    };

    /// Analyses the main package. Must be preceded by ParsePhase.
    class AnalysisPhase final : public Phase {
    public:
        /// @param standalone If the package is a standalone package a start flag block is required.
        AnalysisPhase(bool standalone) : standalone_(standalone) {}
        void perform(Compiler *compiler) override;
    private:
        bool standalone_;
    };

    /// Prints the interface. Must be preceded by AnalysisPhase.
    class PrintInterfacePhase final : public Phase {
    public:
        /// @param path The path at which an interface file for the main package shall be created.
        PrintInterfacePhase(std::string path) : path_(std::move(path)) {}
        void perform(Compiler *compiler) override;
    private:
        std::string path_;
    };

    /// Generates code. Must be preceded by AnalysisPhase. Must only be added once per Compiler.
    class GenerationPhase final : public Phase {
    public:
        /// @param optimize Whether optimizations should be run.
        GenerationPhase(bool optimize) : optimize_(optimize) {}
        void perform(Compiler *compiler) override;
    private:
        bool optimize_;
    };

    /// Emits the generated code to an object file. Must be preceded by GenerationPhase.
    class ObjectFileEmissionPhase final : public Phase {
    public:
        ObjectFileEmissionPhase(std::string path) : path_(std::move(path)) {}
        void perform(Compiler *compiler) override;
    private:
        std::string path_;
    };

    /// Emits the generated code to an object file. Must be preceded by GenerationPhase.
    class LLVMIREmissionPhase final : public Phase {
    public:
        LLVMIREmissionPhase(std::string path) : path_(std::move(path)) {}
        void perform(Compiler *compiler) override;
    private:
        std::string path_;
    };

    /// Compiles the native sources that the main package lists in its link hints, and any C code the compiler
    /// generated, to object files next to the main object file. LinkPhase and ArchivePhase include them.
    class NativeCompilationPhase final : public Phase {
    public:
        /// @param objectFilePath Where the object file of the main package is located.
        /// @param cc Name of or path to the C compiler to use.
        /// @param cxx Name of or path to the C++ compiler to use.
        /// @param mergeIntoObject Whether to merge the native objects into the main object file, because the object
        ///                        file is the result and neither LinkPhase nor ArchivePhase will run.
        NativeCompilationPhase(std::string objectFilePath, std::string cc, std::string cxx, bool mergeIntoObject)
            : objectFilePath_(std::move(objectFilePath)), cc_(std::move(cc)), cxx_(std::move(cxx)),
              mergeIntoObject_(mergeIntoObject) {}
        void perform(Compiler *compiler) override;
    private:
        std::string objectFilePath_;
        std::string cc_;
        std::string cxx_;
        bool mergeIntoObject_;
    };

    /// Links an object file with the archives of the imported packages of the Compiler.
    class LinkPhase final : public Phase {
    public:
        /// @param objectFilePath Where the object file of the main package is located.
        /// @param outPath Where the linked binary shall be placed.
        /// @param linker Name of or path to the linker to use.
        LinkPhase(std::string objectFilePath, std::string outPath, std::string linker)
            : objectFilePath_(std::move(objectFilePath)), outPath_(std::move(outPath)), linker_(std::move(linker)) {}
        void perform(Compiler *compiler) override;
    private:
        std::string objectFilePath_;
        std::string outPath_;
        std::string linker_;
    };

    class ArchivePhase final : public Phase {
    public:
        /// @param objectFilePath Where the object file of the main package is located.
        /// @param outPath Where the archive shall be placed.
        /// @param ar Name of or path to the archiver to use.
        ArchivePhase(std::string objectFilePath, std::string outPath, std::string ar)
            : objectFilePath_(std::move(objectFilePath)), outPath_(std::move(outPath)), ar_(std::move(ar)) {}
        void perform(Compiler *compiler) override;
    private:
        std::string objectFilePath_;
        std::string outPath_;
        std::string ar_;
    };

    /// Constructs an Compiler instance.
    /// @param mainPackage The name of the main package. This is the package for which the compiler can produce
    ///                    an object file, interface, executable and/or archive.
    /// @param mainFile The main package’s main file. (See Package::Package.)
    /// @param pkgSearchPaths The paths the compiler will search for a requested package.
    Compiler(std::string mainPackage, std::string mainFile, std::vector<std::string> pkgSearchPaths,
             std::unique_ptr<CompilerDelegate> delegate);

    template <typename Phase, typename... Args>
    void add(Args&&... args) { phases_.emplace_back(std::make_unique<Phase>(std::forward<Args>(args)...)); }

    /// Compiles the application by running all phases added with add().
    /// @return True iff the compilation completed without error.
    bool compile();

    RecordingPackage* mainPackage() const { return mainPackage_.get(); }

    std::vector<Package *> importedPackages() const { return packageImportOrder_; }

    /// Object files produced by NativeCompilationPhase that must be linked or archived with the main object file.
    const std::vector<std::string>& nativeObjects() const { return nativeObjects_; }

    SourceManager &sourceManager() { return sourceManager_; }

    /// Sets an observer that is told about the results of the semantic analysis. It is not owned by the compiler.
    void setAnalysisObserver(AnalysisObserver *observer) { analysisObserver_ = observer; }
    AnalysisObserver* analysisObserver() const { return trapsErrors_ ? nullptr : analysisObserver_; }

    /// Thrown by error() while errors are trapped.
    struct TrappedError {};
    /// While errors are trapped, error() throws TrappedError instead of reporting the error, warnings are dropped and
    /// no AnalysisObserver is notified. Specializations are analysed like this, as their code was already reported on
    /// in its generic form, and a specialization that does not compile is not used.
    bool trapsErrors() const { return trapsErrors_; }
    void setTrapsErrors(bool traps) { trapsErrors_ = traps; }

    /// Issues a compiler warning. The compilation is continued normally.
    /// @param args All arguments will be concatenated.
    template<typename... Args>
    void warn(const SourcePosition &p, Args... args) {
        std::stringstream stream;
        appendToStream(stream, args...);
        warn(p, stream.str());
    }

    /// Issues a compiler warning. The compilation is continued normally.
    void warn(const SourcePosition &p, const std::string &warning);
    /// Issues a compiler error. The compilation can continue, but no code will be generated.
    void error(const CompilerError &ce);

    /// Loads the package with the given name. If the package has already been loaded it is returned immediately.
    /// @param requestor The package that caused the call to this method.
    /// @see findPackage()
    Package *loadPackage(const std::string &name, const SourcePosition &p, Package *requestor);

    void assignSTypes(Package *s);
    /// Looks up the pointer types 📍 and 🕳 of the c package, which are defined in the namespace 🌊.
    void assignCTypes(Package *c);

    Class *sString = nullptr;
    Class *sError = nullptr;
    ValueType *sList = nullptr;
    ValueType *sDictionary = nullptr;
    Protocol *sEnumerable = nullptr;
    Protocol *sInterpolateable = nullptr;
    ValueType *sBoolean = nullptr;
    ValueType *sInteger = nullptr;
    ValueType *sReal = nullptr;
    ValueType *sMemory = nullptr;
    ValueType *sByte = nullptr;
    ValueType *sWeak = nullptr;
    /// 📍🐚T🍆 of the c package, a C pointer to T. nullptr if the c package is not loaded.
    ValueType *cPointer = nullptr;
    /// 🕳 of the c package, a C void pointer. nullptr if the c package is not loaded.
    ValueType *cVoidPointer = nullptr;

    ~Compiler();

private:
    std::vector<std::unique_ptr<Phase>> phases_;
    std::string searchPackage(const std::string &name, const SourcePosition &p);
    void parseInterface(Package *pkg, const SourcePosition &p);
    std::string findBinaryPathPackage(const std::string &packagePath, const std::string &packageName);

    /// Searches the loaded packages for the package with the given name.
    /// If the package has not been loaded yet @c nullptr is returned.
    Package *findPackage(const std::string &name) const;

    std::map<std::string, std::unique_ptr<Package>> packages_;
    std::vector<Package *> packageImportOrder_;
    std::vector<std::string> nativeObjects_;

    bool hasError_ = false;
    std::string mainFile_;
    const std::vector<std::string> packageSearchPaths_;
    const std::unique_ptr<CompilerDelegate> delegate_;
    std::unique_ptr<CodeGenerator> generator_;
    std::unique_ptr<RecordingPackage> mainPackage_;
    SourceManager sourceManager_;
    AnalysisObserver *analysisObserver_ = nullptr;
    bool trapsErrors_ = false;
};

}  // namespace EmojicodeCompiler


#endif /* Compiler_hpp */

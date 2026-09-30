//
//  PrettyPrinter.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 23.06.18.
//

#include "PrettyPrinter.hpp"
#include "AST/ASTStatements.hpp"
#include "Emojis.h"
#include "Functions/Function.hpp"
#include "Functions/Initializer.hpp"
#include "Compiler.hpp"
#include "Package/Package.hpp"
#include "Parsing/OperatorHelper.hpp"
#include "Types/Class.hpp"
#include "Types/Enum.hpp"
#include "Types/Protocol.hpp"
#include "Types/Type.hpp"
#include "Lex/SourceManager.hpp"
#include "Scoping/Scope.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>

namespace EmojicodeCompiler {

void PrettyPrinter::printRecordings(const std::vector<std::unique_ptr<RecordingPackage::Recording>> &recordings) {
    for (auto &recording : recordings) {
        print(recording.get());
    }
}

void PrettyPrinter::print() {
    for (auto &file : package_->files()) {
        auto sourceFile = package_->compiler()->sourceManager().read(file.path_);
        // The source is moved aside before it is rewritten; a failed rewrite must not leave the user without it.
        auto backup = file.path_ + "_original";
        if (std::rename(file.path_.c_str(), backup.c_str()) != 0) {
            throw CompilerError(SourcePosition(), "Could not write ", file.path_, ": ", std::strerror(errno));
        }
        try {
            prettyStream_.setOutPath(file.path_);
            prettyStream_.startFile();

            printRecordings(file.recordings_);
            prettyStream_.printRemainingComments(sourceFile);
            prettyStream_.finishLine();
            prettyStream_.finish();
        }
        catch (...) {
            std::rename(backup.c_str(), file.path_.c_str());
            throw;
        }
    }
}

void PrettyPrinter::printInterface(const std::string &out) {
    interface_ = true;
    prettyStream_.setOutPath(out);

    prettyStream_ << kABIVersionPrefix + std::to_string(kABIVersion) + "\n";
    if (!package_->documentation().empty()) {
        prettyStream_.indent() << "📘" << package_->documentation() << "📘\n";
    }
    prettyStream_.offerNewLine();

    printRecordings(package_->files().front().recordings_);
    printLinkHints();
    prettyStream_.finish();
}

void PrettyPrinter::printLinkHints() {
    std::vector<std::string> hints;
    for (auto &hint : package_->linkHints()) {
        // Native sources are compiled into the package's archive, so importers do not need them.
        if (!interface_ || !Package::isNativeSourceHint(hint)) {
            hints.emplace_back(hint);
        }
    }
    if (!hints.empty()) {
        prettyStream_.indent() << "🔗 ";
        for (auto &hint : hints) {
            prettyStream_ << "🔤" << hint << "🔤 ";
        }
        prettyStream_ << "🔗\n";
    }
}

void PrettyPrinter::printComments(const SourcePosition &p) {
    // Interfaces do not keep comments.
    if (!interface_) {
        prettyStream_.printComments(p);
    }
}

void PrettyPrinter::print(RecordingPackage::Recording *recording) {
    if (recording->position().file != nullptr) {
        printComments(recording->position());
        // The recordings are printed without offering whitespace.
        if (!interface_) {
            prettyStream_.finishLine();
        }
    }
    if (auto documentation = dynamic_cast<RecordingPackage::DocumentationRecording *>(recording)) {
        // Interfaces print the documentation at their beginning, see printInterface().
        if (!interface_) {
            prettyStream_.refuseOffer() << "📘" << documentation->documentation_ << "📘\n";
            prettyStream_.offerNewLine();
        }
    }
    if (auto import = dynamic_cast<RecordingPackage::Import *>(recording)) {
        prettyStream_.refuseOffer() << "📦 " << import->package << " " << import->destNamespace << "\n";
        prettyStream_.offerNewLine();
    }
    if (auto type = dynamic_cast<RecordingPackage::RecordedType *>(recording)) {
        printTypeDef(type->type_);
    }
    if (auto include = dynamic_cast<RecordingPackage::Include *>(recording)) {
        if (interface_) {
            printRecordings(package_->files()[interfaceFileIndex++].recordings_);
        }
        else {
            prettyStream_.refuseOffer() << "📜 🔤" << include->path_ << "🔤\n";
            prettyStream_.offerNewLine();
        }
    }
    if (dynamic_cast<RecordingPackage::LinkHintsRecording *>(recording) && !interface_) {
        // Interfaces list the link hints at their end, see printInterface().
        printLinkHints();
        prettyStream_.offerNewLine();
    }
    if (dynamic_cast<RecordingPackage::StartFlagFunctionRecording *>(recording)) {
        prettyStream_.printComments(package_->startFlagFunction()->position());
        prettyStream_ << "🏁 ";
        if (package_->startFlagFunction()->returnType() != nullptr) {
            printReturnType(package_->startFlagFunction());
            prettyStream_ << " ";
        }
        prettyStream_.setLastCommentQueryPlace(package_->startFlagFunction()->position());
        package_->startFlagFunction()->ast()->toCode(prettyStream_);
    }
}

std::string PrettyPrinter::declaration(Function *function) {
    if (function->owner() == nullptr) {
        return "";
    }
    prettyStream_.setOutString();
    auto initializer = dynamic_cast<Initializer *>(function) != nullptr;
    print(initializer ? "🆕" : moodEmoji(function->mood()), function, false,
          initializer || isTypeMethod(function), false);
    return prettyStream_.takeString();
}

void PrettyPrinter::printArguments(Function *function) {
    if (auto initializer = dynamic_cast<Initializer *>(function)) {
        auto it = initializer->argumentsToVariables().begin();
        for (auto &arg : function->parameters()) {
            if (it != initializer->argumentsToVariables().end() && arg.name == *it) {
                it++;
                prettyStream_ << "🍼 ";
            }
            if (arg.memoryFlowType.isEscaping()) {
                prettyStream_ << "🎍🥡 ";
            }
            prettyStream_ << arg.name << " " << arg.type << " ";
        }
        return;
    }
    for (auto &arg : function->parameters()) {
        if (arg.memoryFlowType.isEscaping()) {
            prettyStream_ << "🎍🥡 ";
        }
        prettyStream_ << arg.name << " " << arg.type << " ";
    }
}

void PrettyPrinter::printClosure(Function *function, bool escaping) {
    prettyStream_ << "🍇";
    if (escaping) prettyStream_ << "🎍🥡 ";
    if (function->isC()) prettyStream_ << "🎍🌊 ";
    printArguments(function);
    printReturnType(function);
    printErrorType(function);
    prettyStream_ << "\n";
    function->ast()->innerToCode(prettyStream_);
    prettyStream_ << "🍉\n";
}

void PrettyPrinter::printReturnType(Function *function) {
    auto type = function->returnType();
    if (type == nullptr) {
        return;
    }
    prettyStream_ << "➡️ " << type;
}

void PrettyPrinter::printDocumentation(const std::u32string &doc) {
    if (!doc.empty()) {
        prettyStream_.indent() << "📗" << doc << "📗\n";
    }
}

void PrettyPrinter::printTypeDef(const Type &type) {
    auto typeDef = type.typeDefinition();

    printComments(typeDef->position());

    printDocumentation(typeDef->documentation());

    if (typeDef->exported()) {
        prettyStream_ << "🌍 ";
    }
    if (typeDef->isGenericDynamismDisabled()) {
        prettyStream_ << "🎍🛢 ";
    }
    if (auto klass = type.klass()) {
        if (klass->final()) {
            prettyStream_ << "🔏 ";
        }
        if (klass->foreign()) {
            prettyStream_ << "📻 ";
        }
    }
    if (auto valueType = type.valueType()) {
        if (valueType->isCStruct()) {
            prettyStream_ << "🎍🌊 ";
        }
        if (valueType->isPrimitive() && type.type() != TypeType::Enum) {
            prettyStream_ << "📻 ";
            if (valueType->declaresCRepresentation()) {
                prettyStream_ << "🔤" << valueType->cRepresentationName() << "🔤 ";
            }
        }
    }

    printTypeDefName(type);

    prettyStream_.increaseIndent();
    prettyStream_ << "🍇\n";

    if (auto protocol = type.protocol()) {
        std::vector<Member> methods;
        for (auto method : protocol->methods().list()) {
            methods.push_back({moodEmoji(method->mood()), method, false});
        }
        for (auto &member : sortedMembers(std::move(methods))) {
            print(member.key, member.function, false, member.noMutate);
        }
        printTypeEnd(typeDef);
        prettyStream_ << "🍉\n\n";
        return;
    }
    if (auto enumeration = type.enumeration()) {
        printEnumValues(enumeration);
    }

    auto context = TypeContext(type);
    printProtocolConformances(typeDef, context);
    printInstanceVariables(typeDef, context);
    printMethodsAndInitializers(typeDef);

    if (auto klass = type.klass()) {
        if (klass->deinitializer() != nullptr) {
            printComments(klass->deinitializer()->position());
            prettyStream_.indent() << "♻️";
            printBody(klass->deinitializer());
        }
    }

    printTypeEnd(typeDef);
    prettyStream_.refuseOffer() << "🍉\n\n";
}

void PrettyPrinter::printTypeEnd(TypeDefinition *typeDef) {
    // Comments in front of the 🍉 belong to the type.
    printComments(typeDef->endPosition());
    if (!interface_) {
        prettyStream_.finishLine();
    }
    prettyStream_.decreaseIndent();
}

void PrettyPrinter::printTypeDefName(const Type &type) {
    auto typeDef = type.typeDefinition();

    switch (type.unboxedType()) {
        case TypeType::Class:
            prettyStream_ << "🐇 ";
            break;
        case TypeType::ValueType:
            prettyStream_ << "🕊 ";
            break;
        case TypeType::Enum:
            prettyStream_ << "🔘 ";
            break;
        case TypeType::Protocol:
            prettyStream_ << "🐊 ";
            break;
        default:
            break;
    }

    prettyStream_ << type.namespaceAccessor(package_) << typeDef->name();
    prettyStream_.offerSpace();
    printGenericParameters(typeDef);

    if (auto klass = type.klass()) {
        if (klass->superType() != nullptr) {
            prettyStream_ << klass->superType() << " ";
        }
    }
}

void PrettyPrinter::printMethodsAndInitializers(TypeDefinition *typeDef) {
    std::vector<Member> members;
    for (auto init : typeDef->inits().list()) {
        members.push_back({"🆕", init, true});
    }
    for (auto method : typeDef->methods().list()) {
        members.push_back({moodEmoji(method->mood()), method, false});
    }
    for (auto method : typeDef->typeMethods().list()) {
        members.push_back({moodEmoji(method->mood()), method, true});
    }
    for (auto &member : sortedMembers(std::move(members))) {
        print(member.key, member.function, true, member.noMutate);
    }
}

std::vector<PrettyPrinter::Member> PrettyPrinter::sortedMembers(std::vector<Member> members) {
    if (!interface_) {
        // Keep the declarations in the order of the source so that comments stay with what they belong to.
        std::stable_sort(members.begin(), members.end(), [](const Member &a, const Member &b) {
            auto key = [](const Member &m) {
                auto &p = m.function->position();
                return std::make_tuple(p.file == nullptr, p.line, p.character);
            };
            return key(a) < key(b);
        });
    }
    return members;
}

void PrettyPrinter::printProtocolConformances(TypeDefinition *typeDef, const TypeContext &typeContext) {
    for (auto &protocol : typeDef->protocols()) {
        printComments(protocol.position);
        prettyStream_.indent() << "🐊 " << protocol.type << "\n";
    }
    prettyStream_.offerNewLineUnlessEmpty(typeDef->protocols());
}

void PrettyPrinter::printInstanceVariables(TypeDefinition *typeDef, const TypeContext &typeContext) {
    for (auto &ivar : typeDef->instanceVariables()) {
        if (interface_ && typeDef->instanceScope().getLocalVariable(ivar.name).inherited()) {
            continue;
        }
        printComments(ivar.position);
        prettyStream_.indent() << "🖍🆕 " << ivar.name << " " << ivar.type;
        if (ivar.expr != nullptr) {
            prettyStream_ << " ⬅️ " << ivar.expr;
        }
        prettyStream_ << "\n";
    }
    prettyStream_.offerNewLineUnlessEmpty(typeDef->instanceVariables());
}

void PrettyPrinter::printEnumValues(Enum *enumeration) {
    auto values = std::vector<std::pair<std::u32string, EnumValue>>();
    std::transform(enumeration->values().begin(), enumeration->values().end(), std::back_inserter(values),
                   [](auto pair){ return std::make_pair(pair.first, pair.second); });
    std::sort(values.begin(), values.end(), [](auto &a, auto &b) { return a.second.value < b.second.value; });
    for (auto &value : values) {
        printComments(value.second.position);
        printDocumentation(value.second.documentation);
        prettyStream_.indent() << "🆕▶️" << value.first << "\n";
    }
    prettyStream_.offerNewLineUnlessEmpty(values);
}

void PrettyPrinter::printFunctionAttributes(Function *function, bool noMutate) {
    // A source file only keeps the attribute if it was written, small functions are inlined automatically.
    if (interface_ ? function->isInline() : function->isExplicitlyInline()) {
        prettyStream_ << "🥯 ";
    }
    if (function->deprecated()) {
        prettyStream_ << "⚠️ ";
    }
    if (function->final()) {
        prettyStream_ << "🔏 ";
    }
    if (function->overriding()) {
        prettyStream_ << "✒️ ";
    }
    if (function->isC()) {
        prettyStream_ << "🎍🌊 ";
    }
    if (function->functionType() == FunctionType::ClassMethod ||
        (function->functionType() == FunctionType::Function &&
         dynamic_cast<ValueType*>(function->owner()) != nullptr)) {
        prettyStream_ << "🐇 ";
    }
    if (function->unsafe()) {
        prettyStream_ << "☣️ ";
    }

    if ((function->owner()->type().type() == TypeType::ValueType ||
         function->owner()->type().type() == TypeType::Protocol) && function->mutating() && !noMutate) {
        prettyStream_ << "🖍 ";
    }
    if (auto initializer = dynamic_cast<Initializer *>(function)) {
        if (initializer->required()) {
            prettyStream_ << "🔑 ";
        }
    }
    if (!function->memoryFlowTypeForThis().isUnknown() && function->memoryFlowTypeForThis().isEscaping()) {
        prettyStream_ << "🎍🥡 ";
    }
}

void PrettyPrinter::printFunctionAccessLevel(Function *function) {
    switch (function->accessLevel()) {
        case AccessLevel::Private:
            prettyStream_ << "🔒";
            break;
        case AccessLevel::Protected:
            prettyStream_ << "🔐";
            break;
        case AccessLevel::Public:
            prettyStream_ << "🔓";
            break;
        case AccessLevel::Default:
            break;
    }
}

void PrettyPrinter::printErrorType(Function *function) {
    if (function->errorType() != nullptr && !(function->errorType()->wasAnalysed() &&
                                              function->errorType()->type().type() == TypeType::NoReturn)) {
        prettyStream_ << "🚧" << function->errorType() << " ";
    }
}

void PrettyPrinter::print(const char *key, Function *function, bool body, bool noMutate, bool documentation) {
    if (function->isThunk() || function->functionType() == FunctionType::Deinitializer) {
        return;
    }
    if (documentation) {
        printComments(function->position());
        printDocumentation(function->documentation());
    }

    auto initializer = dynamic_cast<Initializer *>(function);
    prettyStream_.withTypeContext(function->typeContext(), [&]() {
        prettyStream_.indent();
        printFunctionAttributes(function, noMutate);
        printFunctionAccessLevel(function);

        if (initializer != nullptr) {
            prettyStream_ << key;
            if (initializer->name().front() != E_NEW_SIGN) {
                prettyStream_ << " ▶️" << function->name() << " ";
            }
        }
        else {
            if (!hasInstanceScope(function->functionType()) || operatorType(function->name()) == OperatorType::Invalid) {
                prettyStream_ << key << " ";
            }

            prettyStream_ << function->name() << " ";
        }

        printGenericParameters(function);
        printArguments(function);
        if (initializer == nullptr) {
            printReturnType(function);
        }
        printErrorType(function);

        if (body) {
            printBody(function);
        }
        prettyStream_ << "\n";
    });
}

void PrettyPrinter::printBody(Function *function) {
    if (!function->externalName().empty()) {
        prettyStream_.ensureSpace();
        prettyStream_ << "📻 🔤" << function->externalName() << "🔤";
    }
    // Importers call a function exported to C like any other C function, so interfaces only need its name.
    if (function->externalName().empty() || (function->isExported() && !interface_)) {
        if (interface_) {
            if (function->isInline()) {
                auto str = function->position().file->file();
                auto code = str.substr(function->ast()->beginIndex(),
                                       function->ast()->endIndex() - function->ast()->beginIndex() + 1);
                prettyStream_ << " 🍇\n";
                prettyStream_.increaseIndent();
                prettyStream_.indent() << code;
                prettyStream_.decreaseIndent();
            }
        }
        else {
            prettyStream_.ensureSpace();
            function->ast()->toCode(prettyStream_);
        }
    }
}

}  // namespace EmojicodeCompiler

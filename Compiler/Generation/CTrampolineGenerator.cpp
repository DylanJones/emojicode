//
//  CTrampolineGenerator.cpp
//  EmojicodeCompiler
//

#include "CTrampolineGenerator.hpp"
#include "Functions/Function.hpp"
#include "Package/Package.hpp"
#include "Types/CRepresentation.hpp"
#include "Types/Type.hpp"
#include "Types/ValueType.hpp"
#include <map>
#include <sstream>

namespace EmojicodeCompiler {

bool isCStructValue(const Type &type) {
    return type.type() == TypeType::ValueType && !type.isReference() && type.valueType()->isCStruct();
}

bool needsCTrampoline(Function *function) {
    if (!function->isC() || function->externalName().empty()) {
        return false;
    }
    if (isCStructValue(function->returnType()->type())) {
        return true;
    }
    for (auto &param : function->parameters()) {
        if (isCStructValue(param.type->type())) {
            return true;
        }
    }
    return false;
}

static std::string cIdentifier(const std::string &string) {
    std::stringstream stream;
    for (unsigned char c : string) {
        if (std::isalnum(c) || c == '_') {
            stream << c;
        }
        else {
            stream << "_" << std::hex << static_cast<int>(c);
        }
    }
    return stream.str();
}

std::string cTrampolineName(Function *function) {
    return "__ejc_tramp_" + cIdentifier(function->package()->name()) + "_" + function->externalName();
}

namespace {

/// Writes the C spelling of types and the definitions of the structs they need.
class CDeclarationWriter {
public:
    std::string typeName(const Type &type) {
        if (type.type() == TypeType::NoReturn) {
            return "void";
        }
        if (isCStructValue(type)) {
            return structName(type.valueType());
        }
        if (type.type() == TypeType::ValueType && type.valueType()->cRepresentation()) {
            auto &representation = *type.valueType()->cRepresentation();
            switch (representation.kind) {
                case CRepresentation::Kind::Pointer:
                    return "void *";
                case CRepresentation::Kind::Float:
                    return representation.bits == 32 ? "float" : "double";
                case CRepresentation::Kind::Integer:
                    if (representation.bits == 1) {
                        return "_Bool";
                    }
                    return std::string(representation.isSigned ? "int" : "uint") +
                        std::to_string(representation.bits) + "_t";
            }
        }
        // Optional pointers and C callables are pointers.
        return "void *";
    }

    std::string definitions() const { return definitions_.str(); }

private:
    std::map<ValueType *, std::string> structs_;
    std::stringstream definitions_;

    std::string structName(ValueType *valueType) {
        auto it = structs_.find(valueType);
        if (it != structs_.end()) {
            return it->second;
        }
        auto name = "struct ejc_struct_" + std::to_string(structs_.size());
        std::stringstream fields;
        size_t i = 0;
        for (auto &ivar : valueType->instanceVariables()) {
            fields << "    " << typeName(ivar.type->type()) << " f" << i++ << ";\n";
        }
        structs_.emplace(valueType, name);
        definitions_ << name << " {\n" << fields.str() << "};\n\n";
        return name;
    }
};

void writeTrampoline(Function *function, CDeclarationWriter &writer, std::stringstream &code) {
    auto &returnType = function->returnType()->type();
    auto returnsStruct = isCStructValue(returnType);

    std::stringstream prototype, parameters, arguments;
    prototype << writer.typeName(returnType) << " " << function->externalName() << "(";
    for (size_t i = 0; i < function->parameters().size(); i++) {
        auto &type = function->parameters()[i].type->type();
        auto separator = i > 0 ? ", " : "";
        prototype << separator << writer.typeName(type);
        parameters << separator << writer.typeName(type) << (isCStructValue(type) ? " *" : " ") << "a" << i;
        arguments << separator << (isCStructValue(type) ? "*a" : "a") << i;
    }
    if (function->parameters().empty()) {
        prototype << "void";
    }
    prototype << ")";
    if (returnsStruct) {
        parameters << (function->parameters().empty() ? "" : ", ") << writer.typeName(returnType) << " *result";
    }
    if (function->parameters().empty() && !returnsStruct) {
        parameters << "void";
    }

    code << "extern " << prototype.str() << ";\n\n";
    code << (returnsStruct ? "void" : writer.typeName(returnType)) << " " << cTrampolineName(function) << "("
         << parameters.str() << ") {\n    ";
    if (returnsStruct) {
        code << "*result = ";
    }
    else if (returnType.type() != TypeType::NoReturn) {
        code << "return ";
    }
    code << function->externalName() << "(" << arguments.str() << ");\n}\n\n";
}

}  // namespace

std::string generateCTrampolines(Package *package) {
    CDeclarationWriter writer;
    std::stringstream code;
    for (auto &valueType : package->valueTypes()) {
        for (auto function : valueType->typeMethods().list()) {
            if (needsCTrampoline(function)) {
                writeTrampoline(function, writer, code);
            }
        }
    }
    if (code.str().empty()) {
        return "";
    }
    return "// Generated by emojicodec. Calls C functions that take or return structs by value.\n\n"
           "#include <stdint.h>\n\n" + writer.definitions() + code.str();
}

}  // namespace EmojicodeCompiler

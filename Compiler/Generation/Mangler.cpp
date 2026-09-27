//
//  Mangler.cpp
//  EmojicodeCompiler
//
//  Created by Theo Weidmann on 05/09/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "Mangler.hpp"
#include "Functions/Function.hpp"
#include "Functions/Initializer.hpp"
#include "Package/Package.hpp"
#include "Types/Class.hpp"
#include "Types/Type.hpp"
#include "Types/ValueType.hpp"
#include "Types/Protocol.hpp"
#include <sstream>
#include <stdexcept>

namespace EmojicodeCompiler {

void mangleIdentifier(std::stringstream &stream, const std::u32string &string) {
    bool first = true;
    for (auto ch : string) {
        if (first) {
            first = false;
        }
        else {
            stream << "_";
        }
        stream << std::hex << ch;
    }
}

void mangleTypeName(std::stringstream &stream, const Type &typeb) {
    auto type = typeb.unboxed();
    stream << type.typePackage() << ".";
    switch (type.type()) {
        case TypeType::ValueType:
            stream << "vt_";
            break;
        case TypeType::Class:
            stream << "class_";
            break;
        case TypeType::Enum:
            stream << "enum_";
            break;
        case TypeType::Protocol:
            stream << "protocol_";
            break;
        case TypeType::Callable:
            stream << (type.isCCallable() ? "ccallable_" : "callable_");
            for (auto it = type.parameters(); it < type.parametersEnd(); it++) {
                mangleTypeName(stream, *it);
            }
            stream << "__";
            mangleTypeName(stream, type.returnType());
            return;
        case TypeType::NoReturn:
            stream << "no_return";
            return;
        case TypeType::LocalGenericVariable:
            stream << "l_" << type.genericVariableIndex();
            return;
        case TypeType::GenericVariable:
            stream << "t_" << type.genericVariableIndex();
            return;
        case TypeType::Optional:
            stream << "op_";
            mangleTypeName(stream, type.unoptionalized());
            return;
        case TypeType::TypeAsValue:
            stream << "tv_";
            mangleTypeName(stream, type.typeOfTypeValue());
            return;
        case TypeType::MultiProtocol:
            stream << "mp_";
            for (auto &proto : type.protocols()) {
                mangleTypeName(stream, proto);
            }
            return;
        case TypeType::Something:
            stream << "something";
            return;
        case TypeType::Someobject:
            stream << "someobject";
            return;
        case TypeType::Invalid:
        case TypeType::Box:  // Removed by unboxed() above.
        case TypeType::StorageExpectation:
        case TypeType::IntegerLiteral:
        case TypeType::RealLiteral:
        case TypeType::ListLiteral:
        case TypeType::DictionaryLiteral:
        case TypeType::NoValueLiteral:
            throw std::logic_error("Cannot mangle compile-time type.");
    }
    mangleIdentifier(stream, type.typeDefinition()->name());
}

void mangleGenericArguments(std::stringstream &stream, const std::map<size_t, Type> &genericArgs) {
    for (auto &pair : genericArgs) {
        stream << '$' << pair.first << '_';
        mangleTypeName(stream, pair.second);
    }
}

std::string mangleClassInfoName(Class *klass) {
    std::stringstream stream;
    stream << klass->package()->name() << "_class_info_";
    mangleIdentifier(stream, klass->name());
    return stream.str();
}

std::string mangleFunction(Function *function, const std::map<size_t, Type> &genericArgs) {
    std::stringstream stream;
    if (function->owner() != nullptr) {
        mangleTypeName(stream, function->owner()->type());
        if (function->functionType() == FunctionType::Deinitializer) {
            stream << ".deinit";
            return stream.str();
        }
        if (function->functionType() == FunctionType::CopyRetainer) {
            stream << ".copy";
            return stream.str();
        }
        if (isFullyInitializedCheckRequired(function->functionType())) {
            stream << ".init";
        }
        else if (function->functionType() == FunctionType::ClassMethod ||
                function->functionType() == FunctionType::Function) {
            stream << ".type";
        }
        stream << ".";
    }
    else {
        stream << "fn_";
    }

    mangleIdentifier(stream, function->name());
    if (function->mood() == Mood::Interogative) {
        stream << "_intrg";
    }
    else if (function->mood() == Mood::Assignment) {
        stream << "_assign";
    }
    mangleGenericArguments(stream, genericArgs);
    for (auto &param : function->parameters()) {
        stream << '-';
        mangleTypeName(stream, param.type->type());
    }
    return stream.str();
}

std::string mangleBoxRetain(const Type &type) {
    return mangleTypeName(type) + ".boxRetain";
}

std::string mangleBoxRelease(const Type &type) {
    return mangleTypeName(type) + ".boxRelease";
}

std::string mangleBoxMakeUnique(const Type &type) {
    return mangleTypeName(type) + ".boxMakeUnique";
}

std::string mangleBoxInfoName(const Type &type) {
    return mangleTypeName(type) + ".boxInfo";
}

std::string mangleCopyRetain(const Type &type) {
    return mangleTypeName(type) + ".copyRetain";
}

std::string mangleDestructor(const Type &type) {
    return mangleTypeName(type) + ".destructor";
}

std::string mangleTypeName(const Type &type) {
    std::stringstream stream;
    mangleTypeName(stream, type);
    return stream.str();
}

std::string mangleProtocolConformance(const Type &type, const Type &protocol) {
    std::stringstream stream;
    mangleTypeName(stream, type);
    stream << ".conformances.";
    mangleTypeName(stream, protocol);
    return stream.str();
}

std::string mangleProtocolRunTimeTypeInfo(Protocol *protocol) {
    std::stringstream stream;
    stream << protocol->package()->name() << ".";
    mangleIdentifier(stream, protocol->name());
    stream << "_rtti";
    return stream.str();
}

std::string mangleMultiprotocolConformance(const Type &multi, const Type &conformer) {
    std::stringstream stream;
    mangleTypeName(stream, conformer);
    stream << "_multi";
    for (auto &protocol : multi.protocols()) {
        mangleTypeName(stream, protocol);
    }
    return stream.str();
}

}  // namespace EmojicodeCompiler

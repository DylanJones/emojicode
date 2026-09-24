//
//  ValueType.hpp
//  Emojicode
//
//  Created by Theo Weidmann on 12/06/16.
//  Copyright © 2016 Theo Weidmann. All rights reserved.
//

#ifndef ValueType_hpp
#define ValueType_hpp

#include "CRepresentation.hpp"
#include "TypeDefinition.hpp"
#include <utility>

namespace EmojicodeCompiler {

class ValueType : public TypeDefinition {
public:
    ValueType(std::u32string name, Package *p, SourcePosition pos, const std::u32string &documentation, bool exported,
              bool primitive);

    Type type() override { return Type(this); }

    bool canResolve(TypeDefinition *resolutionConstraint) const override {
        return resolutionConstraint == this;
    }

    void addInstanceVariable(const InstanceVariableDeclaration &declaration) override {
        if (primitive_) {
            throw CompilerError(position(), "A value type marked with 📻 cannot have instance variables.");
        }
        TypeDefinition::addInstanceVariable(declaration);
    }

    bool isPrimitive() const { return primitive_; }

    /// The C representation of this primitive value type, if it has one.
    const std::optional<CRepresentation>& cRepresentation() const { return cRepresentation_; }
    void setCRepresentation(CRepresentation representation) { cRepresentation_ = representation; }
    /// True if the C representation was declared in source with `📻 🔤C type🔤 🕊`. Such types get built-in
    /// conversions and operators.
    bool declaresCRepresentation() const { return !cRepresentationName_.empty(); }
    /// The C type name given in the declaration, e.g. "unsigned int".
    const std::string& cRepresentationName() const { return cRepresentationName_; }
    void setCRepresentationName(std::string name) { cRepresentationName_ = std::move(name); }

    /// Whether this is a C struct, declared with 🎍🌊.
    bool isCStruct() const { return cStruct_; }
    void setCStruct() { cStruct_ = true; }

    /// Whether this Value Type has a deinitializer and a copy retainer that must be called to deinitialize 
    bool isManaged();

    void setCopyRetain(llvm::Function *function) { copyRetain_ = function; }
    llvm::Function* copyRetain() { return copyRetain_; }

    void setBoxInfo(llvm::GlobalVariable *boxInfo) { boxInfo_ = boxInfo; }
    llvm::GlobalVariable* boxInfo() { return boxInfo_; }

    bool storesGenericArgs() const override;

    bool canInitFrom(const Type &literal) const override { return literal.type() == constructibleFrom_; }

    TypeType constructibleFrom_ = TypeType::NoReturn;

    virtual ~ValueType();

private:
    enum class Managed { Unknown, Yes, No };
    bool primitive_;
    bool cStruct_ = false;
    std::string cRepresentationName_;
    std::optional<CRepresentation> cRepresentation_;
    Managed managed_ = Managed::Unknown;
    llvm::Function *copyRetain_ = nullptr;
    llvm::GlobalVariable *boxInfo_ = nullptr;
};

}  // namespace EmojicodeCompiler

#endif /* ValueType_hpp */

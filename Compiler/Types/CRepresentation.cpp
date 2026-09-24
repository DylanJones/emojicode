//
//  CRepresentation.cpp
//  EmojicodeCompiler
//

#include "CRepresentation.hpp"
#include <climits>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <limits>
#include <map>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Type.h>
#include <sys/types.h>

namespace EmojicodeCompiler {

llvm::Type* CRepresentation::llvmType(llvm::LLVMContext &context) const {
    switch (kind) {
        case Kind::Integer:
            return llvm::Type::getIntNTy(context, bits);
        case Kind::Float:
            return bits == 32 ? llvm::Type::getFloatTy(context) : llvm::Type::getDoubleTy(context);
        case Kind::Pointer:
            return llvm::PointerType::get(context, 0);
    }
}

template <typename T>
static std::pair<std::string, CRepresentation> integer(const char *name) {
    return { name, CRepresentation(CRepresentation::Kind::Integer, sizeof(T) * CHAR_BIT,
                                   std::numeric_limits<T>::is_signed) };
}

std::optional<CRepresentation> CRepresentation::forName(const std::string &name) {
    // Emojicode compiles for the host, so the host compiler's sizes are the target's sizes.
    static const std::map<std::string, CRepresentation> kTypes = {
        integer<char>("char"),
        integer<signed char>("signed char"),
        integer<unsigned char>("unsigned char"),
        integer<short>("short"),
        integer<unsigned short>("unsigned short"),
        integer<int>("int"),
        integer<unsigned int>("unsigned int"),
        integer<long>("long"),
        integer<unsigned long>("unsigned long"),
        integer<long long>("long long"),
        integer<unsigned long long>("unsigned long long"),
        integer<size_t>("size_t"),
        integer<ssize_t>("ssize_t"),
        integer<ptrdiff_t>("ptrdiff_t"),
        integer<intptr_t>("intptr_t"),
        integer<uintptr_t>("uintptr_t"),
        integer<time_t>("time_t"),
        integer<int8_t>("int8_t"),
        integer<int16_t>("int16_t"),
        integer<int32_t>("int32_t"),
        integer<int64_t>("int64_t"),
        integer<uint8_t>("uint8_t"),
        integer<uint16_t>("uint16_t"),
        integer<uint32_t>("uint32_t"),
        integer<uint64_t>("uint64_t"),
        { "bool", CRepresentation(Kind::Integer, 1, false) },
        { "float", CRepresentation(Kind::Float, 32, true) },
        { "double", CRepresentation(Kind::Float, 64, true) },
        { "void *", CRepresentation(Kind::Pointer, sizeof(void *) * CHAR_BIT, false) },
    };
    auto it = kTypes.find(name);
    if (it == kTypes.end()) {
        return std::nullopt;
    }
    return it->second;
}

}  // namespace EmojicodeCompiler

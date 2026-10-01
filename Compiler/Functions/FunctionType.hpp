//
//  FunctionType.hpp
//  Emojicode
//
//  Created by Theo Weidmann on 29/07/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#ifndef FunctionType_hpp
#define FunctionType_hpp

namespace EmojicodeCompiler {

class Function;

enum class FunctionType {
    ObjectMethod,
    ObjectInitializer,
    /** A function with a context. (e.g. a value type method) */
    ValueTypeMethod,
    ValueTypeInitializer,
    /** A type method. */
    ClassMethod,
    /** A plain function without a context. (🏁) */
    Function,
    Deinitializer,
    CopyRetainer,
};

bool isSuperconstructorRequired(FunctionType);
bool isFullyInitializedCheckRequired(FunctionType);
bool isSelfAllowed(FunctionType);
bool hasInstanceScope(FunctionType);
bool isReturnForbidden(FunctionType);
bool hasThisArgument(Function *function);
bool isTypeMethod(Function *function);
/// Whether @p function takes the generic arguments of its type as a parameter, which a type method of a value type or
/// enum that stores them does. A closure in such a type method captures them instead.
bool takesTypeGenericArgs(Function *function);

/// Whether an initializer takes the generic arguments of the type it initializes as its last argument, directly after
/// the parameters. Classes pass them as a {ptr, i1} struct, value types as a pointer.
bool takesInitializerGenericArgs(Function *function);
/// Whether @p function, a type method of a class that stores generic arguments, reads them from its this, the class
/// type value, which describes the class it is called on. All type methods of classes are called the same way, so an
/// override in a generic subclass of a class without generic parameters, or with fewer, gets all the arguments of its
/// class. A closure in such a type method captures them instead.
bool readsTypeGenericArgsFromThis(Function *function);

}  // namespace EmojicodeCompiler

#endif /* FunctionType_hpp */

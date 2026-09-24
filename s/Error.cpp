//
//  Error.cpp
//  EmojicodeCompiler
//
//  Created by Theo Weidmann on 11/09/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "Error.h"
#include <cerrno>
#include <cstring>

s::Error::Error(const char *message) : message(s::String::init(message)) {

}

s::IOError::IOError(const char *message) : message(s::String::init(message)) {

}

s::IOError::IOError() : message(s::String::init(std::strerror(errno))) {

}

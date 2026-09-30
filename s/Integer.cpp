//
//  Integer.cpp
//  EmojicodeCompiler
//
//  Created by Theo Weidmann on 09/09/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "../runtime/Runtime.h"
#include "String.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using s::String;

extern "C" runtime::Integer sIntAbsolute(runtime::Integer *integer) {
    return std::abs(*integer);
}

extern "C" s::String* sIntToString(runtime::Integer *nptr, runtime::Integer base) {
    auto n = *nptr;
    auto a = std::abs(n);
    bool negative = n < 0;

    unsigned int d = negative ? 2 : 1;
    while ((n /= base) != 0) {
        d++;
    }

    auto string = String::init();
    string->count = d;
    string->characters = runtime::allocate<char>(d);

    auto *characters = string->characters.get() + d;
    do {
        *--characters =  "0123456789abcdefghijklmnopqrstuvxyz"[a % base % 35];
    } while ((a /= base) > 0);

    if (negative) {
        characters[-1] = '-';
    }
    return string;
}

namespace {

/// Returns the decimal digits of @p value, which must be non-negative and integral.
std::string integerDigits(double value) {
    char buffer[400];  // DBL_MAX has 309 digits.
    std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    return buffer;
}

/// Writes the first @p precision decimal digits of @p fractional, which must be in [0, 1), to @p characters.
void writeFractionalDigits(char *characters, runtime::Integer precision, double fractional) {
    std::fill(characters, characters + precision, '0');
    if (precision <= 18) {
        // Scale and truncate. The product fits in a long long, and its rounding keeps 0.3 to 1 digit from giving 0.2.
        auto f = static_cast<long long>(std::pow(10, precision) * fractional);
        for (auto i = precision - 1; i >= 0 && f > 0; i--, f /= 10) {
            characters[i] = static_cast<char>('0' + f % 10);
        }
        return;
    }
    if (!(fractional > 0)) {
        return;
    }

    // Beyond 18 digits, use the fewest significant digits that identify the double (at most 17), so 0.99 gives
    // 0.99000…, and truncate those. Scaling instead would overflow an integer, and 10^precision is inexact beyond
    // 10^22, so even 0.5 would get spurious digits.
    char buffer[32];
    for (int digits = 0; digits <= 16; digits++) {
        std::snprintf(buffer, sizeof(buffer), "%.*e", digits, fractional);
        if (std::strtod(buffer, nullptr) == fractional) {
            break;
        }
    }
    auto exponentStart = std::strchr(buffer, 'e');
    auto exponent = std::atoi(exponentStart + 1);
    if (exponent >= 0) {
        // Digits that read back as a value below 1 have a negative exponent; do not print a wrong digit if not.
        std::fill(characters, characters + precision, '9');
        return;
    }
    // The first significant digit is the (-exponent)th digit after the decimal separator.
    auto position = static_cast<runtime::Integer>(-exponent) - 1;
    for (auto c = buffer; c < exponentStart && position < precision; c++) {
        if (*c >= '0' && *c <= '9') {
            characters[position++] = *c;
        }
    }
}

s::String* makeString(const std::string &string) {
    auto result = String::init();
    result->count = static_cast<runtime::Integer>(string.size());
    result->characters = runtime::allocate<char>(result->count);
    std::copy(string.begin(), string.end(), result->characters.get());
    return result;
}

}  // namespace

extern "C" s::String* sRealToString(runtime::Real *real, runtime::Integer precision) {
    double integral;
    double fractional = std::abs(std::modf(*real, &integral));

    if (precision <= 0) {
        return makeString((integral < 0 ? "-" : "") + integerDigits(std::abs(integral)));
    }

    auto prefix = (*real < 0 ? "-" : "") + integerDigits(std::abs(integral)) + ".";
    if (precision > INT64_MAX - static_cast<runtime::Integer>(prefix.size())) {
        ejcPanic("Invalid allocation size");
    }

    auto string = String::init();
    string->count = static_cast<runtime::Integer>(prefix.size()) + precision;
    string->characters = runtime::allocate<char>(string->count);
    auto *characters = std::copy(prefix.begin(), prefix.end(), string->characters.get());
    writeFractionalDigits(characters, precision, fractional);
    return string;
}

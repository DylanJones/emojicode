//
// Created by Theo Weidmann on 19.03.18.
//

#include "../runtime/Runtime.h"
#include "ByteSearch.h"
#include "Data.h"
#include "String.h"
#include "utf8proc.h"
#include <algorithm>

namespace s {

extern "C" runtime::SimpleOptional<runtime::Integer> sDataFindFromIndex(Data *data, Data *search,
                                                                        runtime::Integer offset) {
    return findBytesFromOffset(data->data.get(), data->count, search->data.get(), search->count, offset);
}

extern "C" runtime::SimpleOptional<String *> sDataAsString(Data *data) {
    auto bytes = reinterpret_cast<const utf8proc_uint8_t *>(data->data.get());
    for (size_t off = 0; off < static_cast<size_t>(data->count);) {
        utf8proc_int32_t codepoint;
        auto state = utf8proc_iterate(bytes + off, data->count - off, &codepoint);
        if (state < 0) {
            return runtime::NoValue;
        }
        off += state;
    }

    auto *string = String::init();
    string->count = data->count;
    string->characters = data->data;
    data->data.retain();
    return string;
}

}  // namespace s

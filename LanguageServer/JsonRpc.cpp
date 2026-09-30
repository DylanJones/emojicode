//
//  JsonRpc.cpp
//  EmojicodeLanguageServer
//

#include "JsonRpc.hpp"
#include "Utils/rapidjson/stringbuffer.h"
#include "Utils/rapidjson/writer.h"
#include <cerrno>
#include <cstdlib>
#include <poll.h>
#include <unistd.h>

namespace EmojicodeLanguageServer {

/// Returns whether the JSON text nests arrays and objects deeper than Transport::kMaximumDepth. Brackets inside
/// strings do not count. Needs no recursion, so it can reject input before it is parsed.
static bool exceedsDepth(const std::string &text) {
    size_t depth = 0;
    bool inString = false;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (inString) {
            if (c == '\\') {
                i++;
            }
            else if (c == '"') {
                inString = false;
            }
        }
        else if (c == '"') {
            inString = true;
        }
        else if ((c == '[' || c == '{') && ++depth > Transport::kMaximumDepth) {
            return true;
        }
        else if ((c == ']' || c == '}') && depth > 0) {
            depth--;
        }
    }
    return false;
}

Transport::Framing Transport::takeMessage(std::string *content) {
    if (resynchronizing_) {
        // The body of the message with the invalid header is still in the buffer. Skip to the next Content-Length.
        std::string lower = buffer_;
        for (auto &c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        const std::string field = "content-length:";
        auto next = lower.find(field);
        if (next == std::string::npos) {
            buffer_.erase(0, buffer_.size() >= field.size() ? buffer_.size() - field.size() + 1 : 0);
            return Framing::Incomplete;
        }
        buffer_.erase(0, next);
        resynchronizing_ = false;
    }
    if (skippingHeader_) {
        auto end = buffer_.find("\r\n\r\n");
        if (end == std::string::npos) {
            buffer_.erase(0, buffer_.size() > 3 ? buffer_.size() - 3 : 0);
            return Framing::Incomplete;
        }
        buffer_.erase(0, end + 4);
        skippingHeader_ = false;
    }
    auto headerEnd = buffer_.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        if (buffer_.size() > kMaximumHeaderSize) {
            // Not a header. Skip to the next blank line, where the next message may start.
            buffer_.erase(0, buffer_.size() - 3);
            skippingHeader_ = true;
            return Framing::Invalid;
        }
        return Framing::Incomplete;
    }
    bool found = false;
    bool valid = true;
    size_t length = 0;
    size_t lineStart = 0;
    while (lineStart < headerEnd) {
        auto lineEnd = buffer_.find("\r\n", lineStart);
        auto line = buffer_.substr(lineStart, lineEnd - lineStart);
        const std::string field = "content-length:";
        if (line.size() >= field.size()) {
            std::string name = line.substr(0, field.size());
            for (auto &c : name) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            if (name == field) {
                // Only digits: strtoul would accept signs and trailing garbage, and overflows silently.
                auto digits = line.find_first_not_of(" \t", field.size());
                auto end = line.find_last_not_of(" \t");
                valid = valid && !found && digits != std::string::npos;
                found = true;
                size_t value = 0;
                for (size_t i = digits; valid && i <= end; i++) {
                    valid = line[i] >= '0' && line[i] <= '9';
                    value = valid ? value * 10 + static_cast<size_t>(line[i] - '0') : 0;
                    valid = valid && value <= kMaximumMessageSize;
                }
                length = value;
            }
        }
        lineStart = lineEnd + 2;
    }
    auto bodyStart = headerEnd + 4;
    if (!found || !valid) {
        buffer_.erase(0, bodyStart);
        resynchronizing_ = true;
        return Framing::Invalid;
    }
    if (buffer_.size() - bodyStart < length) {
        return Framing::Incomplete;
    }
    *content = buffer_.substr(bodyStart, length);
    buffer_.erase(0, bodyStart + length);
    return Framing::Complete;
}

std::optional<rapidjson::Document> Transport::read(int timeoutMilliseconds, bool *parseError) {
    std::string content;
    Framing framing;
    while ((framing = takeMessage(&content)) == Framing::Incomplete) {
        pollfd fd{input_, POLLIN, 0};
        auto ready = poll(&fd, 1, timeoutMilliseconds);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready == 0) {
            return std::nullopt;
        }
        char chunk[65536];
        auto count = ::read(input_, chunk, sizeof(chunk));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            throw EndOfInput();
        }
        buffer_.append(chunk, static_cast<size_t>(count));
    }
    rapidjson::Document document;
    if (framing == Framing::Invalid || exceedsDepth(content)) {
        *parseError = true;
        return document;
    }
    document.Parse<rapidjson::kParseIterativeFlag>(content.c_str(), content.size());
    *parseError = document.HasParseError();
    return document;
}

void Transport::write(const rapidjson::Value &message) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    message.Accept(writer);
    std::string data = "Content-Length: " + std::to_string(buffer.GetSize()) + "\r\n\r\n";
    data.append(buffer.GetString(), buffer.GetSize());

    size_t written = 0;
    while (written < data.size()) {
        auto count = ::write(output_, data.data() + written, data.size() - written);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            throw EndOfInput();
        }
        written += static_cast<size_t>(count);
    }
}

}  // namespace EmojicodeLanguageServer

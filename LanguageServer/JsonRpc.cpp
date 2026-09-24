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

bool Transport::takeMessage(std::string *content) {
    auto headerEnd = buffer_.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return false;
    }
    size_t length = 0;
    size_t lineStart = 0;
    while (lineStart < headerEnd) {
        auto lineEnd = buffer_.find("\r\n", lineStart);
        auto line = buffer_.substr(lineStart, lineEnd - lineStart);
        const std::string field = "content-length:";
        if (line.size() > field.size()) {
            std::string name = line.substr(0, field.size());
            for (auto &c : name) c = static_cast<char>(tolower(c));
            if (name == field) {
                length = std::strtoul(line.c_str() + field.size(), nullptr, 10);
            }
        }
        lineStart = lineEnd + 2;
    }
    auto bodyStart = headerEnd + 4;
    if (buffer_.size() < bodyStart + length) {
        return false;
    }
    *content = buffer_.substr(bodyStart, length);
    buffer_.erase(0, bodyStart + length);
    return true;
}

std::optional<rapidjson::Document> Transport::read(int timeoutMilliseconds, bool *parseError) {
    std::string content;
    while (!takeMessage(&content)) {
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
    document.Parse(content.c_str(), content.size());
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

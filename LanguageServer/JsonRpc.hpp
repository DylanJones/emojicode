//
//  JsonRpc.hpp
//  EmojicodeLanguageServer
//

#ifndef JsonRpc_hpp
#define JsonRpc_hpp

#include "Utils/rapidjson/document.h"
#include <optional>
#include <stdexcept>
#include <string>

namespace EmojicodeLanguageServer {

/// Thrown by Transport::read when the client closed the input.
class EndOfInput : public std::runtime_error {
public:
    EndOfInput() : std::runtime_error("The client closed the connection.") {}
};

/// Reads and writes JSON-RPC messages framed with Content-Length headers, as the Language Server Protocol does.
class Transport {
public:
    Transport(int input, int output) : input_(input), output_(output) {}

    /// Waits at most @p timeoutMilliseconds (forever if negative) for the next message and returns it, or returns
    /// std::nullopt if none arrived in time. A message that is not valid JSON is returned as a document that is not
    /// an object.
    /// @throws EndOfInput if the client closed the input.
    std::optional<rapidjson::Document> read(int timeoutMilliseconds);
    void write(const rapidjson::Value &message);

private:
    /// Removes the first complete message from buffer_ and stores its content in @p content.
    bool takeMessage(std::string *content);

    int input_;
    int output_;
    std::string buffer_;
};

}  // namespace EmojicodeLanguageServer

#endif /* JsonRpc_hpp */

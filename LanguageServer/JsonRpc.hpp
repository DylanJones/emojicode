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
    /// The deepest nesting of arrays and objects a message may have. Parsing, copying and writing a DOM recurse.
    static constexpr size_t kMaximumDepth = 256;
    /// The largest Content-Length accepted.
    static constexpr size_t kMaximumMessageSize = 256 * 1024 * 1024;
    /// How much input without the end of the headers is buffered before it is discarded.
    static constexpr size_t kMaximumHeaderSize = 64 * 1024;

    Transport(int input, int output) : input_(input), output_(output) {}

    /// Waits at most @p timeoutMilliseconds (forever if negative) for the next message and returns it, or returns
    /// std::nullopt if none arrived in time. If the message is not valid JSON, @p parseError is set to true.
    /// @throws EndOfInput if the client closed the input.
    /// This is also set if the headers of the message are invalid (no or a malformed Content-Length) or if it nests
    /// arrays and objects deeper than kMaximumDepth.
    std::optional<rapidjson::Document> read(int timeoutMilliseconds, bool *parseError);
    void write(const rapidjson::Value &message);

private:
    enum class Framing { Complete, Incomplete, Invalid };
    /// Removes the first complete message from buffer_ and stores its content in @p content. Invalid headers are
    /// removed from buffer_ too.
    Framing takeMessage(std::string *content);

    int input_;
    int output_;
    std::string buffer_;
    /// Whether the input is discarded up to the next blank line because a header was too long.
    bool skippingHeader_ = false;
    /// Whether the input is discarded up to the next Content-Length header because the last one was invalid and the
    /// length of its body is unknown.
    bool resynchronizing_ = false;
};

}  // namespace EmojicodeLanguageServer

#endif /* JsonRpc_hpp */

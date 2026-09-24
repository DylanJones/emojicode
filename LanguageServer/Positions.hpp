//
//  Positions.hpp
//  EmojicodeLanguageServer
//

#ifndef Positions_hpp
#define Positions_hpp

#include <string>
#include <vector>

namespace EmojicodeLanguageServer {

/// How a client counts the characters of a line: LSP positions count UTF-16 code units by default, clients may
/// agree on UTF-8 code units or code points (UTF-32) instead. The compiler always counts code points.
enum class PositionEncoding { UTF8, UTF16, UTF32 };

/// A position as the client sees it: both values start at 0, character is counted in the negotiated encoding.
struct ClientPosition {
    size_t line = 0;
    size_t character = 0;
};

/// Converts between the code point positions of the compiler and client positions for one text.
class LineIndex {
public:
    explicit LineIndex(const std::u32string &text);

    /// Returns the client position for code point @p character on @p line (0-based). Out of range values are
    /// clamped to the end of the line or text.
    ClientPosition toClient(size_t line, size_t character, PositionEncoding encoding) const;
    /// Returns the code point index on the line of @p position.
    size_t toCodePoint(ClientPosition position, PositionEncoding encoding) const;

    /// Returns the index in the text of code point @p character on @p line.
    size_t offset(size_t line, size_t character) const;
    /// Returns the line and code point on that line of the index @p offset in the text.
    std::pair<size_t, size_t> lineAndCharacter(size_t offset) const;

    size_t lineCount() const { return lineStarts_.size(); }
    /// The code points of @p line without the line break.
    std::u32string_view line(size_t line) const;

private:
    const std::u32string &text_;
    std::vector<size_t> lineStarts_;
};

/// Returns the absolute, canonical file system path for a file:// URI, or an empty string for other URIs.
std::string uriToPath(const std::string &uri);
/// Returns a file:// URI for an absolute path.
std::string pathToUri(const std::string &path);
std::u32string utf32(const std::string &utf8);
std::string utf8(std::u32string_view utf32);

}  // namespace EmojicodeLanguageServer

#endif /* Positions_hpp */

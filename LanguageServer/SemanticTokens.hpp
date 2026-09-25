//
//  SemanticTokens.hpp
//  EmojicodeLanguageServer
//

#ifndef SemanticTokens_hpp
#define SemanticTokens_hpp

#include "Navigation.hpp"
#include "Positions.hpp"
#include <cstdint>
#include <vector>

namespace EmojicodeLanguageServer {

/// The token types the server uses, in the order of the legend it sends to the client.
extern const std::vector<const char *> kSemanticTokenTypes;
/// The token modifiers the server uses, in the order of the legend it sends to the client.
extern const std::vector<const char *> kSemanticTokenModifiers;

/// Returns the semantic tokens of the file of @p navigator in the LSP encoding: five integers per token (line and
/// start relative to the previous token, length, type and modifiers).
/// @param previous Navigates the version of the file of the last analysis that analysed the function bodies, if the
///                 analysis of @p navigator did not. A token that @p navigator cannot classify is classified like the
///                 same token in that version, if it is on a line before or after the lines that changed since.
std::vector<uint32_t> semanticTokens(const Navigator &navigator, PositionEncoding encoding,
                                     const Navigator *previous = nullptr);

}  // namespace EmojicodeLanguageServer

#endif /* SemanticTokens_hpp */

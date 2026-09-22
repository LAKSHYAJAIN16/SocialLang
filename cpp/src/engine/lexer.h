#pragma once

#include <string>
#include <vector>

namespace sl {

enum class TK { Number, String, Keyword, Ident, Symbol, End };

struct Token {
  TK kind;
  std::string value;
  int line;
};

// Throws ParseError on an unterminated string or an unexpected character.
std::vector<Token> tokenize(const std::string& source);

}  // namespace sl

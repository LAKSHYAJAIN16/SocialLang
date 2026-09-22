#pragma once

#include <memory>
#include <string>

#include "engine/ast.h"

namespace sl {

// Lexes, parses, and links a whole `sim Name { ... }` program. Throws
// ParseError with a "line N: ..." message on any syntax error.
std::shared_ptr<Program> parseProgram(const std::string& source);

}  // namespace sl

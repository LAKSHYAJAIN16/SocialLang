// SocialLang in the script editor: syntax highlighting, autocomplete, parse
// errors, and typo fixes for the three kinds of .sl file (environment,
// behavior, game).
#pragma once

#include <string>
#include <vector>

#include "textedit/TextEditor.h"

namespace app {

// Highlighting rules shared by every .sl file.
const TextEditor::Language* slLanguage();
TextEditor::Palette slPalette(bool dark);

// Words that belong in a file of this kind, for autocomplete and typo checks.
const std::vector<std::string>& slVocabulary(const std::string& kind);

// Autocomplete: vocabulary + words already in the file, best matches first.
std::vector<std::string> slSuggestions(const std::string& kind, const std::string& text, const std::string& prefix);

// A likely misspelling of a known word, and its fix. Line and column are
// zero-based (column in characters).
struct SlTypo {
  size_t line = 0, col = 0, len = 0;
  std::string word, fix;
};
std::vector<SlTypo> findTypos(const std::string& text, const std::string& kind);

// Parses the text as its kind; returns false with a message and the 1-based
// line (0 if unknown) on a syntax error.
bool checkSource(const std::string& text, const std::string& kind, std::string& message, int& line);

}  // namespace app

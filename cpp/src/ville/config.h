// Declarative SocialLang files -- environments and behaviors -- share one
// block syntax, on the same lexer as .sl games:
//
//   environment OakHill {             // a node: a word, arguments, a body
//     start_hour: 6                   // a field: key, colon, value
//     building "Hobbs Cafe" {         // a nested node
//       type: cafe
//       room "cafe" { objects: ["piano", "cafe customer seating"] }
//     }
//   }
//
//   routine student {
//     9..12 "attending class" at classroom      // a line entry: the tokens
//   }                                           // of one line, in order
//
// Values are strings, numbers, bare words, lists [a, b], and ranges 9..12.
//
// Imports: a file can start from a library and change only what differs.
//
//   import smallville                 // lib/smallville.env.sl (or .behavior.sl)
//   environment MyTown {
//     days: 1                         // fields replace the library's
//     building "Hobbs Cafe" {         // a node with the same kind and name
//       floor: "#E8C07A"              //   merges into the library's node
//     }
//     remove building "Johnson Park"  // drops one of the library's nodes
//   }
#pragma once

#include <string>
#include <vector>

namespace ville {

struct CValue {
  enum Kind { Str, Num, Word, List, Range, Symbol } kind = Str;
  std::string s;  // Str / Word / Symbol
  double n = 0, n2 = 0;  // Num / Range
  std::vector<CValue> items;  // List
  int line = 0;

  std::string text() const;  // Str / Word as text; Num formatted
  double number(double fallback = 0) const { return kind == Num ? n : fallback; }
};

struct CNode {
  std::string kind;  // "environment", "building", "room", ...
  std::vector<CValue> args;
  std::vector<std::pair<std::string, CValue>> fields;
  std::vector<CNode> children;
  std::vector<std::vector<CValue>> lines;  // line entries
  std::vector<std::string> imports;        // root only: `import name` lines
  int line = 0;

  const CValue* field(const std::string& key) const;
  std::string str(const std::string& key, const std::string& fallback = "") const;
  double num(const std::string& key, double fallback) const;
  std::string arg(size_t i, const std::string& fallback = "") const;
};

// Parses one top-level node (throws sl::ParseError "line N: ...").
CNode parseConfig(const std::string& source);

// `over` on top of `base`: fields replace, same-named nodes merge, new nodes
// are added, `remove kind "name"` lines drop nodes. Routine and activity
// bodies replace wholesale; other line entries merge by their first value.
void mergeConfig(CNode& base, const CNode& over);
// What `full` changes relative to `base` -- the inverse of mergeConfig.
CNode diffConfig(const CNode& base, const CNode& full);
// Canonical text for a node tree (imports first).
std::string writeConfig(const CNode& n);
// A node's identity for merging: kind plus its arguments.
std::string nodeKey(const CNode& n);

// Writing: quoted string, and a number without a trailing ".0".
std::string quote(const std::string& s);
std::string fmtNum(double n);

}  // namespace ville

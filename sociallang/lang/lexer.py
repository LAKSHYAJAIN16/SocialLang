from __future__ import annotations

from dataclasses import dataclass

KEYWORDS = {
    "sim", "agents", "memory", "role", "fn", "phase", "win_condition", "loop",
    "let", "if", "else", "while", "for", "in", "return", "run", "break",
    "true", "false", "null", "and", "or", "not",
}

# Longest-match-first so e.g. ".." isn't tokenized as two "." tokens.
SYMBOLS = [
    "..", "==", "!=", "<=", ">=",
    "{", "}", "(", ")", "[", "]", ",", ":", ".", "+", "-", "*", "/", "%",
    "<", ">", "=",
]


@dataclass
class Token:
    kind: str  # IDENT | NUMBER | STRING | KEYWORD | SYMBOL | EOF
    value: str
    line: int


class LexError(Exception):
    pass


def tokenize(source: str) -> list[Token]:
    tokens: list[Token] = []
    i = 0
    line = 1
    n = len(source)

    while i < n:
        ch = source[i]

        if ch == "\n":
            line += 1
            i += 1
            continue
        if ch in " \t\r":
            i += 1
            continue
        if ch == "/" and i + 1 < n and source[i + 1] == "/":
            while i < n and source[i] != "\n":
                i += 1
            continue

        if ch == '"':
            j = i + 1
            buf = []
            while j < n and source[j] != '"':
                if source[j] == "\\" and j + 1 < n:
                    esc = source[j + 1]
                    buf.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(esc, esc))
                    j += 2
                    continue
                buf.append(source[j])
                j += 1
            if j >= n:
                raise LexError(f"unterminated string starting at line {line}")
            tokens.append(Token("STRING", "".join(buf), line))
            i = j + 1
            continue

        if ch.isdigit():
            j = i
            while j < n and source[j].isdigit():
                j += 1
            # A single '.' followed by a digit is a decimal point; '..' (the range
            # operator, e.g. "agents: 6..10") must NOT be swallowed into the number.
            if j < n and source[j] == "." and j + 1 < n and source[j + 1].isdigit():
                j += 1
                while j < n and source[j].isdigit():
                    j += 1
            tokens.append(Token("NUMBER", source[i:j], line))
            i = j
            continue

        if ch.isalpha() or ch == "_":
            j = i
            while j < n and (source[j].isalnum() or source[j] == "_"):
                j += 1
            word = source[i:j]
            tokens.append(Token("KEYWORD" if word in KEYWORDS else "IDENT", word, line))
            i = j
            continue

        matched = next((s for s in SYMBOLS if source.startswith(s, i)), None)
        if matched:
            tokens.append(Token("SYMBOL", matched, line))
            i += len(matched)
            continue

        raise LexError(f"unexpected character {ch!r} at line {line}")

    tokens.append(Token("EOF", "", line))
    return tokens
